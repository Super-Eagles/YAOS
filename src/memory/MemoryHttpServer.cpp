#include "MemoryHttpServer.h"

#include <FastNet/FastNet.h>
#include <FastNet/HttpServer.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <utility>

#include <mutex>
#include <string>

#include "../runtime/StructuredLog.h"
#include "RemoteMemoryProtocol.h"

Q_LOGGING_CATEGORY(lcMemoryHttpServer, "yaos.memory.http")

namespace yaos::memory {

namespace {

QString reasonPhraseFor(int statusCode) {
    switch (statusCode) {
    case 200: return QStringLiteral("OK");
    case 400: return QStringLiteral("Bad Request");
    case 401: return QStringLiteral("Unauthorized");
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

bool authorized(const QString &configuredApiKey, const FastNet::HttpRequest &request) {
    if (configuredApiKey.trimmed().isEmpty()) {
        return true;
    }
    return headerValue(request, QStringLiteral("authorization")) ==
           QStringLiteral("Bearer %1").arg(configuredApiKey.trimmed());
}

} // namespace

MemoryHttpServer::MemoryHttpServer(MemoryServiceCore &core,
                                   QString apiKey,
                                   QObject *parent)
    : QObject(parent),
      _core(core),
      _apiKey(std::move(apiKey)) {}

MemoryHttpServer::~MemoryHttpServer() {
    stop();
}

bool MemoryHttpServer::start(const QString &host, quint16 port, QString *error) {
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

void MemoryHttpServer::stop() {
    if (_server) {
        _server->stop();
        _server.reset();
    }
    _listenAddress.clear();
    _listenPort = 0;
}

QString MemoryHttpServer::listenAddress() const {
    return _listenAddress;
}

quint16 MemoryHttpServer::listenPort() const {
    return _listenPort;
}

void MemoryHttpServer::handleRequest(const FastNet::HttpRequest &request,
                                     FastNet::HttpResponse &response) {
    QMutexLocker requestLock(&_requestMutex);

    const QString httpMethod = requestMethod(request);
    const QString path = requestPath(request);
    const QByteArray body = bodyBytes(request);
    const QString traceId =
        runtime::StructuredLog::ensureTraceId(headerValue(request, QStringLiteral("x-yaos-trace-id")));
    const runtime::ScopedTraceContext traceScope(traceId);
    const QString peerAddress = peerAddressText(request);

    runtime::StructuredLog::log(QStringLiteral("info"),
                                QStringLiteral("http.server"),
                                QStringLiteral("Inbound memory HTTP request"),
                                QJsonObject{
                                    {QStringLiteral("component"), QStringLiteral("memory")},
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
        runtime::StructuredLog::log(statusCode >= 500 ? QStringLiteral("error")
                                                      : (statusCode >= 400 ? QStringLiteral("warning")
                                                                           : QStringLiteral("info")),
                                    QStringLiteral("http.server"),
                                    message,
                                    QJsonObject{
                                        {QStringLiteral("component"), QStringLiteral("memory")},
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

    if (!authorized(_apiKey, request)) {
        respondError(401, QStringLiteral("Unauthorized."));
        return;
    }

    if (!_core.isReady()) {
        respondJson(503, _core.health(), QStringLiteral("Memory service unavailable"));
        return;
    }

    if (httpMethod == QStringLiteral("GET") &&
        (path == QStringLiteral("/health") || path == QStringLiteral("/v1/memory/health"))) {
        respondJson(200, _core.health(), QStringLiteral("Memory health request completed"));
        return;
    }

    if (httpMethod != QStringLiteral("POST")) {
        respondError(405, QStringLiteral("Only POST is supported for this endpoint."));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        respondError(400, QStringLiteral("Invalid JSON request body."));
        return;
    }

    const QJsonObject payload = doc.object();
    QString error;

    if (path == QStringLiteral("/v1/memory/conversations/append")) {
        const QString sessionKey = payload.value("sessionKey").toString(payload.value("session_key").toString());
        if (sessionKey.trimmed().isEmpty()) {
            respondError(400, QStringLiteral("sessionKey is required."));
            return;
        }
        const QList<ConversationMessage> messages = jsonToConversationMessages(payload.value("messages"));
        if (!_core.appendConversation(sessionKey, messages, &error)) {
            respondError(500, error);
            return;
        }
        respondJson(200, QJsonObject{{"ok", true}}, QStringLiteral("Memory conversation append request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/memory/conversations/recent")) {
        const QString sessionKey = payload.value("sessionKey").toString(payload.value("session_key").toString());
        if (sessionKey.trimmed().isEmpty()) {
            respondError(400, QStringLiteral("sessionKey is required."));
            return;
        }
        const int limit = payload.value("limit").toInt(32);
        const QList<ConversationMessage> messages = _core.recentMessages(sessionKey, limit, &error);
        if (!error.isEmpty()) {
            respondError(500, error);
            return;
        }
        respondJson(200, QJsonObject{
            {"ok", true},
            {"messages", conversationMessagesToJson(messages)}
        }, QStringLiteral("Memory conversation recent request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/memory/facts/upsert")) {
        const QString workspaceId = payload.value("workspaceId").toString(payload.value("workspace_id").toString());
        const QList<MemoryFact> facts = jsonToMemoryFacts(payload.value("facts"));
        if (!_core.upsertFacts(workspaceId, facts, &error)) {
            respondError(500, error);
            return;
        }
        respondJson(200, QJsonObject{{"ok", true}}, QStringLiteral("Memory fact upsert request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/memory/facts/find")) {
        MemoryQuery query = jsonToMemoryQuery(payload);
        const QList<MemoryFact> facts = _core.findFacts(query, &error);
        if (!error.isEmpty()) {
            respondError(500, error);
            return;
        }
        respondJson(200, QJsonObject{
            {"ok", true},
            {"facts", memoryFactsToJson(facts)}
        }, QStringLiteral("Memory fact find request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/memory/recall")) {
        MemoryQuery query = jsonToMemoryQuery(payload);
        const QList<MemoryRecallItem> items = _core.recall(query, &error);
        if (!error.isEmpty()) {
            respondError(500, error);
            return;
        }
        respondJson(200, QJsonObject{
            {"ok", true},
            {"items", memoryRecallItemsToJson(items)}
        }, QStringLiteral("Memory recall request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/memory/ingest/turn")) {
        const QString workspaceId = payload.value("workspaceId").toString(payload.value("workspace_id").toString());
        const QString sessionKey = payload.value("sessionKey").toString(payload.value("session_key").toString());
        if (sessionKey.trimmed().isEmpty()) {
            respondError(400, QStringLiteral("sessionKey is required."));
            return;
        }
        const QList<ConversationMessage> messages = jsonToConversationMessages(payload.value("messages"));
        if (!_core.ingestTurn(workspaceId, sessionKey, messages, &error)) {
            respondError(500, error);
            return;
        }
        respondJson(200, QJsonObject{{"ok", true}}, QStringLiteral("Memory ingest turn request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/memory/ingest/daily-summary")) {
        const QString workspaceId = payload.value("workspaceId").toString(payload.value("workspace_id").toString());
        const QString dateText = payload.value("date").toString();
        const QDate date = QDate::fromString(dateText, QStringLiteral("yyyy-MM-dd"));
        if (!date.isValid()) {
            respondError(400, QStringLiteral("date must be yyyy-MM-dd."));
            return;
        }
        if (!_core.buildDailySummary(workspaceId, date, &error)) {
            respondError(500, error);
            return;
        }
        respondJson(200, QJsonObject{{"ok", true}}, QStringLiteral("Memory daily summary request completed"));
        return;
    }

    respondError(404, QStringLiteral("Unknown memory endpoint."));
}

void MemoryHttpServer::writeJsonResponse(FastNet::HttpResponse &response,
                                         int statusCode,
                                         const QJsonObject &payload) const {
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    response.statusCode = statusCode;
    response.statusMessage = toStdString(reasonPhraseFor(statusCode));
    response.headers["Content-Type"] = "application/json; charset=utf-8";
    response.headers["Connection"] = "close";
    response.body.assign(body.constData(), static_cast<size_t>(body.size()));
}

void MemoryHttpServer::writeError(FastNet::HttpResponse &response,
                                  int statusCode,
                                  const QString &message) const {
    writeJsonResponse(response, statusCode, QJsonObject{
        {"ok", false},
        {"error", message}
    });
}

} // namespace yaos::memory
