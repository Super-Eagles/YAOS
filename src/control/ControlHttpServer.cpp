#include "ControlHttpServer.h"

#include <FastNet/FastNet.h>
#include <FastNet/HttpServer.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutexLocker>

#include <mutex>
#include <string>

#include "../distributed/ContractsJson.h"
#include "../runtime/StructuredLog.h"

Q_LOGGING_CATEGORY(lcControlHttpServer, "yaos.control.http")

namespace yaos::control {

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

QStringList stringListFromJson(const QJsonValue &value) {
    QStringList out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        out.append(item.toString());
    }
    return out;
}

} // namespace

ControlHttpServer::ControlHttpServer(ControlServiceCore &core,
                                     QObject *parent)
    : QObject(parent),
      _core(core) {}

ControlHttpServer::~ControlHttpServer() {
    stop();
}

bool ControlHttpServer::start(const QString &host, quint16 port, QString *error) {
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

void ControlHttpServer::stop() {
    if (_server) {
        _server->stop();
        _server.reset();
    }
    _listenAddress.clear();
    _listenPort = 0;
}

QString ControlHttpServer::listenAddress() const {
    return _listenAddress;
}

quint16 ControlHttpServer::listenPort() const {
    return _listenPort;
}

void ControlHttpServer::handleRequest(const FastNet::HttpRequest &request,
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
                                QStringLiteral("Inbound control HTTP request"),
                                QJsonObject{
                                    {QStringLiteral("component"), QStringLiteral("control")},
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
                                        {QStringLiteral("component"), QStringLiteral("control")},
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

    if (!_core.isReady()) {
        respondJson(503, _core.health(), QStringLiteral("Control service unavailable"));
        return;
    }

    if (httpMethod == QStringLiteral("GET") &&
        (path == QStringLiteral("/health") || path == QStringLiteral("/v1/control/health"))) {
        respondJson(200, _core.health(), QStringLiteral("Control health request completed"));
        return;
    }

    if (httpMethod != QStringLiteral("POST")) {
        respondError(405, QStringLiteral("Only POST is supported for this endpoint."));
        return;
    }

    QJsonObject payload;
    if (!body.trimmed().isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            respondError(400, QStringLiteral("Invalid JSON request body."));
            return;
        }
        payload = doc.object();
    }

    QString error;

    if (path == QStringLiteral("/v1/control/nodes/publish") ||
        path == QStringLiteral("/v1/control/nodes/register") ||
        path == QStringLiteral("/v1/control/nodes/heartbeat")) {
        const QJsonObject nodeObj = payload.value("node").toObject();
        if (nodeObj.isEmpty()) {
            respondError(400, QStringLiteral("node is required."));
            return;
        }
        const distributed::NodeDescriptor node = distributed::json::nodeDescriptorFromJson(nodeObj);
        if (!_core.publishPresence(node, &error)) {
            respondError(500, error);
            return;
        }
        respondJson(200, QJsonObject{{"ok", true}}, QStringLiteral("Control publish node request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/control/nodes/list")) {
        const bool onlineOnly = payload.value("onlineOnly").toBool(payload.value("online_only").toBool(false));
        const QString clusterId = payload.value("clusterId").toString(payload.value("cluster_id").toString());
        const int limit = payload.value("limit").toInt(256);
        const QList<distributed::NodeDescriptor> nodes = _core.listNodes(onlineOnly, clusterId, limit);
        respondJson(200, QJsonObject{
            {"ok", true},
            {"nodes", distributed::json::toJson(nodes)}
        }, QStringLiteral("Control node list request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/control/nodes/refresh-health")) {
        const bool force = payload.value("force").toBool(false);
        const int limit = payload.value("limit").toInt(64);
        const int refreshed = _core.refreshNodeHealth(force, limit);
        QJsonObject response = _core.health();
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("refreshedNodeCount"), refreshed);
        response.insert(QStringLiteral("force"), force);
        response.insert(QStringLiteral("limit"), limit);
        respondJson(200, response, QStringLiteral("Control node health refresh request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/control/nodes/resolve")) {
        const QString clusterId = payload.value("clusterId").toString(payload.value("cluster_id").toString());
        const QString role = payload.value("role").toString();
        const QStringList tags = stringListFromJson(payload.value("tags"));
        const QString tool = payload.value("tool").toString();
        const QString channel = payload.value("channel").toString();
        const QString memoryBackend = payload.value("memoryBackend").toString(payload.value("memory_backend").toString());
        const int limit = payload.value("limit").toInt(8);
        const QList<distributed::NodeDescriptor> nodes =
            _core.resolveNodes(clusterId, role, tags, tool, channel, memoryBackend, limit);
        respondJson(200, QJsonObject{
            {"ok", true},
            {"nodes", distributed::json::toJson(nodes)},
            {"node", nodes.isEmpty() ? QJsonObject() : distributed::json::toJson(nodes.first())}
        }, QStringLiteral("Control node resolve request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/control/tasks/submit")) {
        const QJsonObject taskObj = payload.value("task").toObject();
        if (taskObj.isEmpty()) {
            respondError(400, QStringLiteral("task is required."));
            return;
        }
        const distributed::TaskEnvelope task = distributed::json::taskEnvelopeFromJson(taskObj);
        if (!_core.submitTask(task, &error)) {
            respondError(500, error);
            return;
        }
        respondJson(200, QJsonObject{{"ok", true}}, QStringLiteral("Control task submit request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/control/tasks/pending")) {
        const QString targetNode = payload.value("targetNode").toString(payload.value("target_node").toString());
        const QString targetRole = payload.value("targetRole").toString(payload.value("target_role").toString());
        const int limit = payload.value("limit").toInt(100);
        const QList<distributed::TaskEnvelope> tasks = _core.pendingTasks(targetNode, targetRole, limit);
        QJsonArray array;
        for (const distributed::TaskEnvelope &task : tasks) {
            array.append(distributed::json::toJson(task));
        }
        respondJson(200, QJsonObject{
            {"ok", true},
            {"tasks", array}
        }, QStringLiteral("Control pending task request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/control/tasks/claim")) {
        const QString taskId = payload.value("taskId").toString(payload.value("task_id").toString());
        if (taskId.trimmed().isEmpty()) {
            respondError(400, QStringLiteral("taskId is required."));
            return;
        }
        const QString consumerNode = payload.value("consumerNode").toString(payload.value("consumer_node").toString());
        if (!_core.claimTask(taskId, consumerNode, &error)) {
            respondError(500, error);
            return;
        }
        respondJson(200, QJsonObject{{"ok", true}}, QStringLiteral("Control task claim request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/control/tasks/result")) {
        const QJsonObject resultObj = payload.value("result").toObject();
        if (resultObj.isEmpty()) {
            respondError(400, QStringLiteral("result is required."));
            return;
        }
        const distributed::TaskResultEnvelope result = distributed::json::taskResultEnvelopeFromJson(resultObj);
        if (!_core.publishResult(result, &error)) {
            respondError(500, error);
            return;
        }
        respondJson(200, QJsonObject{{"ok", true}}, QStringLiteral("Control task result request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/control/tasks/results")) {
        const QString taskId = payload.value("taskId").toString(payload.value("task_id").toString());
        const QString traceId = payload.value("traceId").toString(payload.value("trace_id").toString());
        const int limit = payload.value("limit").toInt(100);
        const QList<distributed::TaskResultEnvelope> results = _core.recentResults(taskId, traceId, limit);
        QJsonArray array;
        for (const distributed::TaskResultEnvelope &result : results) {
            array.append(distributed::json::toJson(result));
        }
        respondJson(200, QJsonObject{
            {"ok", true},
            {"results", array}
        }, QStringLiteral("Control task result list request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/control/tasks/cancel")) {
        const QString taskId = payload.value("taskId").toString(payload.value("task_id").toString());
        if (taskId.trimmed().isEmpty()) {
            respondError(400, QStringLiteral("taskId is required."));
            return;
        }
        if (!_core.cancelTask(taskId, &error)) {
            respondError(500, error);
            return;
        }
        respondJson(200, QJsonObject{{"ok", true}}, QStringLiteral("Control task cancel request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/control/delegation-templates/list")) {
        const int limit = payload.value("limit").toInt(512);
        const QList<config::DelegationTemplateConfig> records = _core.listDelegationTemplates(limit);
        respondJson(200, QJsonObject{
            {"ok", true},
            {"count", records.size()},
            {"templates", config::delegationTemplateExchangeArray(records)},
            {"envelope", config::delegationTemplateExchangeEnvelope(records)}
        }, QStringLiteral("Control template list request completed"));
        return;
    }

    if (path == QStringLiteral("/v1/control/delegation-templates/sync")) {
        QJsonDocument exchangeDocument;
        if (payload.value("envelope").isObject()) {
            exchangeDocument = QJsonDocument(payload.value("envelope").toObject());
        } else if (payload.value("templates").isArray()) {
            exchangeDocument = QJsonDocument(QJsonObject{
                {QStringLiteral("templates"), payload.value("templates").toArray()}
            });
        } else {
            exchangeDocument = QJsonDocument(payload);
        }

        QList<config::DelegationTemplateConfig> records;
        if (!config::parseDelegationTemplateExchangeDocument(exchangeDocument, &records, &error)) {
            respondError(400, error.isEmpty() ? QStringLiteral("templates are required.") : error);
            return;
        }

        const bool replaceExisting =
            payload.value("replace").toBool(payload.value("replaceExisting").toBool(false));
        if (!_core.syncDelegationTemplates(records, replaceExisting, &error)) {
            respondError(500, error);
            return;
        }

        const QList<config::DelegationTemplateConfig> saved = _core.listDelegationTemplates(4096);
        respondJson(200, QJsonObject{
            {"ok", true},
            {"replace", replaceExisting},
            {"importedCount", records.size()},
            {"totalTemplates", saved.size()},
            {"templates", config::delegationTemplateExchangeArray(saved)},
            {"envelope", config::delegationTemplateExchangeEnvelope(saved)}
        }, QStringLiteral("Control template sync request completed"));
        return;
    }

    respondError(404, QStringLiteral("Unknown control endpoint."));
}

void ControlHttpServer::writeJsonResponse(FastNet::HttpResponse &response,
                                          int statusCode,
                                          const QJsonObject &payload) const {
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    response.statusCode = statusCode;
    response.statusMessage = toStdString(reasonPhraseFor(statusCode));
    response.headers["Content-Type"] = "application/json; charset=utf-8";
    response.headers["Connection"] = "close";
    response.body.assign(body.constData(), static_cast<size_t>(body.size()));
}

void ControlHttpServer::writeError(FastNet::HttpResponse &response,
                                   int statusCode,
                                   const QString &message) const {
    writeJsonResponse(response, statusCode, QJsonObject{
        {"ok", false},
        {"error", message}
    });
}

} // namespace yaos::control
