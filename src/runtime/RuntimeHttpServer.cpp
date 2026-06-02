#include "RuntimeHttpServer.h"

#include <FastNet/FastNet.h>
#include <FastNet/HttpServer.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>

#include <mutex>
#include <string>

#include "StructuredLog.h"

namespace yaos::runtime {

namespace {

QString reasonPhraseFor(int statusCode) {
    switch (statusCode) {
    case 200: return QStringLiteral("OK");
    case 400: return QStringLiteral("Bad Request");
    case 404: return QStringLiteral("Not Found");
    case 405: return QStringLiteral("Method Not Allowed");
    case 500: return QStringLiteral("Internal Server Error");
    case 503: return QStringLiteral("Service Unavailable");
    default: return QStringLiteral("Error");
    }
}

bool ensureFastNetInitialized(QString *error) {
    static std::once_flag once;
    static FastNet::ErrorCode result = FastNet::ErrorCode::UnknownError;
    std::call_once(once, []() {
        result = FastNet::initialize(2);
    });

    if (result == FastNet::ErrorCode::Success ||
        (result == FastNet::ErrorCode::AlreadyRunning && FastNet::isInitialized())) {
        return true;
    }

    if (error) {
        *error = QStringLiteral("FastNet initialization failed.");
    }
    return false;
}

std::string toStdString(const QString &value) {
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

QString bindHostFor(QString host) {
    host = host.trimmed();
    if (host.isEmpty() ||
        host == QStringLiteral("*") ||
        host == QStringLiteral("0.0.0.0")) {
        return QStringLiteral("0.0.0.0");
    }
    if (host == QStringLiteral("::")) {
        return QStringLiteral("::");
    }
    if (host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1")) {
        return QStringLiteral("127.0.0.1");
    }
    if (host == QStringLiteral("::1")) {
        return QStringLiteral("::1");
    }
    return host;
}

QString listenHostFromFastNet(const FastNet::Address &address,
                              const QString &fallback) {
    const QString host = QString::fromStdString(address.normalizedHost()).trimmed();
    return host.isEmpty() ? fallback : host;
}

QString headerValue(const FastNet::HttpRequest &request, const QString &key) {
    for (const auto &header : request.headers) {
        if (QString::fromStdString(header.first).compare(key, Qt::CaseInsensitive) == 0) {
            return QString::fromStdString(header.second).trimmed();
        }
    }
    return {};
}

QString peerAddressText(const FastNet::HttpRequest &request) {
    return QString::fromStdString(request.clientAddress.normalizedHost());
}

QByteArray bodyBytes(const FastNet::HttpRequest &request) {
    return QByteArray(request.body.data(), static_cast<int>(request.body.size()));
}

QString requestPath(const FastNet::HttpRequest &request) {
    const QString path = QString::fromStdString(request.path).trimmed();
    return path.isEmpty() ? QStringLiteral("/") : path;
}

QString requestMethod(const FastNet::HttpRequest &request) {
    return QString::fromStdString(request.methodName).trimmed().toUpper();
}

} // namespace

RuntimeHttpServer::RuntimeHttpServer(distributed::IRuntimeClient &client,
                                     QObject *parent)
    : QObject(parent),
      _client(client) {}

RuntimeHttpServer::~RuntimeHttpServer() {
    stop();
}

bool RuntimeHttpServer::start(const QString &host, quint16 port, QString *error) {
    stop();
    if (!ensureFastNetInitialized(error)) {
        return false;
    }

    _server = std::make_unique<FastNet::HttpServer>(FastNet::getGlobalIoService());
    _server->setMaxRequestSize(16 * 1024 * 1024);
    _server->setRequestHandler([this](const FastNet::HttpRequest &request,
                                      FastNet::HttpResponse &response) {
        handleRequest(request, response);
    });

    const QString bindHost = bindHostFor(host);
    const FastNet::Error startError = _server->start(port, toStdString(bindHost));
    if (startError.isFailure()) {
        if (error) {
            *error = QString::fromStdString(startError.toString());
        }
        _server.reset();
        return false;
    }

    const FastNet::Address actual = _server->getListenAddress();
    _listenAddress = listenHostFromFastNet(actual, bindHost);
    _listenPort = actual.port != 0 ? actual.port : port;
    return true;
}

void RuntimeHttpServer::stop() {
    if (_server) {
        _server->stop();
        _server.reset();
    }
    _listenAddress.clear();
    _listenPort = 0;
}

QString RuntimeHttpServer::listenAddress() const {
    return _listenAddress;
}

quint16 RuntimeHttpServer::listenPort() const {
    return _listenPort;
}

void RuntimeHttpServer::handleRequest(const FastNet::HttpRequest &request,
                                      FastNet::HttpResponse &response) {
    QMutexLocker requestLock(&_requestMutex);

    const QString httpMethod = requestMethod(request);
    const QString path = requestPath(request);
    const QByteArray body = bodyBytes(request);
    const QString traceId =
        StructuredLog::ensureTraceId(headerValue(request, QStringLiteral("x-yaos-trace-id")));
    const ScopedTraceContext traceScope(traceId);
    const QString peerAddress = peerAddressText(request);

    StructuredLog::log(QStringLiteral("info"),
                       QStringLiteral("http.server"),
                       QStringLiteral("Inbound runtime HTTP request"),
                       QJsonObject{
                           {QStringLiteral("component"), QStringLiteral("runtime")},
                           {QStringLiteral("direction"), QStringLiteral("inbound")},
                           {QStringLiteral("method"), httpMethod},
                           {QStringLiteral("path"), path},
                           {QStringLiteral("peerAddress"), peerAddress},
                           {QStringLiteral("bodyBytes"), body.size()}
                       });

    const auto respondJson = [&](int statusCode,
                                 QJsonObject payload,
                                 const QString &message) {
        payload.insert(QStringLiteral("traceId"), traceId);
        StructuredLog::log(statusCode >= 500 ? QStringLiteral("error")
                                             : (statusCode >= 400 ? QStringLiteral("warning")
                                                                  : QStringLiteral("info")),
                           QStringLiteral("http.server"),
                           message,
                           QJsonObject{
                               {QStringLiteral("component"), QStringLiteral("runtime")},
                               {QStringLiteral("direction"), QStringLiteral("outbound")},
                               {QStringLiteral("method"), httpMethod},
                               {QStringLiteral("path"), path},
                               {QStringLiteral("peerAddress"), peerAddress},
                               {QStringLiteral("statusCode"), statusCode},
                               {QStringLiteral("ok"), payload.value(QStringLiteral("ok")).toBool(statusCode < 400)}
                           });
        writeJsonResponse(response, statusCode, payload);
    };

    const auto respondError = [&](int statusCode, const QString &message) {
        respondJson(statusCode,
                    QJsonObject{
                        {QStringLiteral("ok"), false},
                        {QStringLiteral("error"), message}
                    },
                    message);
    };

    if (httpMethod == QStringLiteral("GET") &&
        (path == QStringLiteral("/health") || path == QStringLiteral("/v1/runtime/health"))) {
        const QString listenHost = _listenAddress;
        const QJsonObject serviceHealth = _client.invoke(QStringLiteral("serviceHealth"), QJsonObject{});
        const bool serviceOk = serviceHealth.value(QStringLiteral("ok")).toBool(false);
        QJsonObject status{
            {QStringLiteral("runtimeMode"), QStringLiteral("remote")},
            {QStringLiteral("listening"), true},
            {QStringLiteral("endpoint"),
             QStringLiteral("http://%1:%2")
                 .arg(listenHost.isEmpty() ? QStringLiteral("127.0.0.1") : listenHost)
                 .arg(listenPort())},
            {QStringLiteral("initialized"), serviceHealth.value(QStringLiteral("initialized")).toBool(false)},
            {QStringLiteral("requestedProvider"), serviceHealth.value(QStringLiteral("requestedProvider")).toString()},
            {QStringLiteral("actualBackend"), serviceHealth.value(QStringLiteral("actualBackend")).toString()},
            {QStringLiteral("backendFallback"), serviceHealth.value(QStringLiteral("backendFallback")).toBool(false)}
        };
        QJsonObject payload{
            {QStringLiteral("ok"), serviceOk},
            {QStringLiteral("service"), QStringLiteral("yaos-runtime")},
            {QStringLiteral("mode"), QStringLiteral("http")},
            {QStringLiteral("status"), status}
        };
        const QString errorText = serviceHealth.value(QStringLiteral("error")).toString().trimmed();
        if (!errorText.isEmpty()) {
            payload.insert(QStringLiteral("error"), errorText);
        }
        respondJson(serviceOk ? 200 : 503,
                    payload,
                    serviceOk
                        ? QStringLiteral("Runtime health request completed")
                        : QStringLiteral("Runtime health request failed"));
        return;
    }

    if (httpMethod != QStringLiteral("POST")) {
        respondError(405, QStringLiteral("Only POST is supported for this endpoint."));
        return;
    }

    if (path != QStringLiteral("/v1/runtime/invoke")) {
        respondError(404, QStringLiteral("Unknown runtime endpoint."));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        respondError(400, QStringLiteral("Invalid JSON request body."));
        return;
    }

    const QJsonObject invokeRequest = document.object();
    const QString method = invokeRequest.value(QStringLiteral("method")).toString().trimmed();
    if (method.isEmpty()) {
        respondError(400, QStringLiteral("method is required."));
        return;
    }

    const QJsonObject payload = invokeRequest.value(QStringLiteral("payload")).toObject();
    respondJson(200, _client.invoke(method, payload), QStringLiteral("Runtime invoke request completed"));
}

void RuntimeHttpServer::writeJsonResponse(FastNet::HttpResponse &response,
                                          int statusCode,
                                          const QJsonObject &payload) const {
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    response.statusCode = statusCode;
    response.statusMessage = toStdString(reasonPhraseFor(statusCode));
    response.headers["Content-Type"] = "application/json; charset=utf-8";
    response.headers["Connection"] = "close";
    response.body.assign(body.constData(), static_cast<size_t>(body.size()));
}

void RuntimeHttpServer::writeError(FastNet::HttpResponse &response,
                                   int statusCode,
                                   const QString &message) const {
    writeJsonResponse(response, statusCode, QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), message}
    });
}

} // namespace yaos::runtime
