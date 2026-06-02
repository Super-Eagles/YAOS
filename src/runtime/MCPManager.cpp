#include "MCPManager.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QUrl>

#include <FastNet/FastNet.h>
#include <FastNet/HttpClient.h>

#include "platform/network/FastNetHttpTransport.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

Q_LOGGING_CATEGORY(lcMcp, "yaos.mcp")

namespace yaos::runtime {

namespace {

struct SseEvent {
    QString event;
    QString data;
};

QString transportFor(const config::MCPServerConfig &cfg) {
    QString transport = cfg.type.trimmed();
    if (!transport.isEmpty()) {
        return transport;
    }
    if (!cfg.command.trimmed().isEmpty()) {
        return "stdio";
    }
    if (!cfg.url.trimmed().isEmpty()) {
        return cfg.url.trimmed().endsWith("/sse", Qt::CaseInsensitive) ? "sse" : "streamableHttp";
    }
    return QString();
}

QJsonObject parseJsonObject(const QByteArray &payload) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    return doc.object();
}

QString extractTextResult(const QJsonObject &result) {
    const QJsonArray content = result.value("content").toArray();
    QStringList parts;
    for (const QJsonValue &value : content) {
        const QJsonObject block = value.toObject();
        if (block.value("type").toString() == "text") {
            parts.append(block.value("text").toString());
        }
    }
    if (!parts.isEmpty()) {
        return parts.join("\n");
    }
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QVector<MCPRemoteTool> parseToolList(const QString &serverName, const QJsonObject &result) {
    QVector<MCPRemoteTool> tools;
    const QJsonArray items = result.value("tools").toArray();
    tools.reserve(items.size());

    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString toolName = item.value("name").toString().trimmed();
        if (toolName.isEmpty()) {
            continue;
        }

        MCPRemoteTool tool;
        tool.serverName = serverName;
        tool.name = toolName;
        tool.description = item.value("description").toString();
        tool.inputSchema = item.value("inputSchema").toObject();
        if (tool.inputSchema.isEmpty()) {
            tool.inputSchema = item.value("input_schema").toObject();
        }
        tools.append(tool);
    }

    return tools;
}

QVector<SseEvent> takeSseEvents(QByteArray *buffer) {
    QVector<SseEvent> events;
    if (!buffer) {
        return events;
    }

    while (true) {
        int splitLf = buffer->indexOf("\n\n");
        int splitCrLf = buffer->indexOf("\r\n\r\n");
        int split = -1;
        int delimiterSize = 0;

        if (splitLf >= 0 && (splitCrLf < 0 || splitLf < splitCrLf)) {
            split = splitLf;
            delimiterSize = 2;
        } else if (splitCrLf >= 0) {
            split = splitCrLf;
            delimiterSize = 4;
        }

        if (split < 0) {
            break;
        }

        QByteArray raw = buffer->left(split);
        buffer->remove(0, split + delimiterSize);
        raw.replace("\r\n", "\n");

        SseEvent event;
        QStringList dataLines;
        const QList<QByteArray> lines = raw.split('\n');
        for (const QByteArray &line : lines) {
            if (line.startsWith("event:")) {
                event.event = QString::fromUtf8(line.mid(6)).trimmed();
                continue;
            }
            if (line.startsWith("data:")) {
                dataLines.append(QString::fromUtf8(line.mid(5)).trimmed());
            }
        }
        event.data = dataLines.join("\n").trimmed();
        if (!event.event.isEmpty() || !event.data.isEmpty()) {
            events.append(event);
        }
    }

    return events;
}

QUrl resolveEventUrl(const QUrl &baseUrl, const QString &raw) {
    const QUrl value(raw);
    if (value.isValid() && !value.scheme().isEmpty()) {
        return value;
    }
    return baseUrl.resolved(QUrl(raw));
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

FastNet::RequestHeaders makeSseHeaders(const QHash<QString, QString> &headers) {
    FastNet::RequestHeaders result;
    result["Accept"] = "text/event-stream";
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        result[toStdString(it.key())] = toStdString(it.value());
    }
    return result;
}

class SseStream {
public:
    ~SseStream() {
        close();
    }

    bool open(const QUrl &url,
              const QHash<QString, QString> &headers,
              int timeoutMs,
              QString *error) {
        close();
        if (!ensureFastNetInitialized(error)) {
            return false;
        }

        state_ = std::make_shared<State>();
        client_ = std::make_shared<FastNet::HttpClient>(FastNet::getGlobalIoService());

        const uint32_t timeout = timeoutMs > 0 ? static_cast<uint32_t>(timeoutMs) : 8000U;
        client_->setConnectTimeout(timeout);
        client_->setRequestTimeout(timeout);
        client_->setReadTimeout(300000U);
        client_->setFollowRedirects(true);

        auto state = state_;
        auto client = client_;
        const std::string urlText = toStdString(url.toString(QUrl::FullyEncoded));
        const FastNet::RequestHeaders streamHeaders = makeSseHeaders(headers);

        const bool connectStarted = client->connect(urlText,
            [client, state, streamHeaders](bool success, const std::string &message) {
                if (!success) {
                    failState(state, QString::fromStdString(message.empty()
                                  ? std::string("MCP SSE connection failed.")
                                  : message));
                    return;
                }

                const bool requestStarted = client->streamGet(
                    "",
                    streamHeaders,
                    [state](const FastNet::HttpResponse &response) {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        if (response.statusCode >= 200 && response.statusCode < 300) {
                            state->started = true;
                        } else {
                            state->failed = true;
                            state->error = response.statusMessage.empty()
                                ? QStringLiteral("MCP SSE HTTP %1.").arg(response.statusCode)
                                : QString::fromStdString(response.statusMessage);
                        }
                        state->condition.notify_all();
                    },
                    [state](std::string_view chunk) {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        if (state->cancelled || state->failed) {
                            return false;
                        }

                        state->buffer.append(chunk.data(), static_cast<int>(chunk.size()));
                        const QVector<SseEvent> events = takeSseEvents(&state->buffer);
                        for (const SseEvent &event : events) {
                            state->events.append(event);
                        }
                        state->condition.notify_all();
                        return !state->cancelled && !state->failed;
                    },
                    [state](const FastNet::HttpResponse &response) {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        if (!state->cancelled && !state->failed) {
                            if (response.statusCode == 0) {
                                state->failed = true;
                                state->error = QString::fromStdString(response.statusMessage.empty()
                                                   ? std::string("MCP SSE stream closed.")
                                                   : response.statusMessage);
                            } else if (response.statusCode < 200 || response.statusCode >= 300) {
                                state->failed = true;
                                state->error = response.statusMessage.empty()
                                    ? QStringLiteral("MCP SSE HTTP %1.").arg(response.statusCode)
                                    : QString::fromStdString(response.statusMessage);
                            }
                        }
                        state->finished = true;
                        state->condition.notify_all();
                    });

                if (!requestStarted) {
                    const FastNet::Error fastError = client->getLastError();
                    failState(state, QString::fromStdString(fastError.isFailure()
                                  ? fastError.toString()
                                  : std::string("MCP SSE request failed to start.")));
                    client->disconnect();
                }
            });

        if (!connectStarted) {
            const FastNet::Error fastError = client_->getLastError();
            if (error) {
                *error = QString::fromStdString(fastError.isFailure()
                             ? fastError.toString()
                             : std::string("MCP SSE connection failed to start."));
            }
            close();
            return false;
        }

        std::unique_lock<std::mutex> lock(state_->mutex);
        const bool ready = state_->condition.wait_for(
            lock,
            std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 8000),
            [state]() {
                return state->started || state->failed || state->finished;
            });
        if (!ready) {
            if (error) {
                *error = QStringLiteral("Timed out opening MCP SSE stream.");
            }
            lock.unlock();
            close();
            return false;
        }
        if (!state_->started) {
            if (error) {
                *error = state_->error.isEmpty()
                    ? QStringLiteral("Failed to open MCP SSE stream.")
                    : state_->error;
            }
            lock.unlock();
            close();
            return false;
        }
        return true;
    }

    QUrl waitForEndpoint(const QUrl &baseUrl, int timeoutMs, QString *error) {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 8000);

        while (std::chrono::steady_clock::now() < deadline) {
            const QVector<SseEvent> events = takeAvailableEvents(deadline);
            for (const SseEvent &event : events) {
                if (event.data.isEmpty()) {
                    continue;
                }
                if (event.event == "endpoint" ||
                    event.data.startsWith("http", Qt::CaseInsensitive) ||
                    event.data.startsWith('/')) {
                    return resolveEventUrl(baseUrl, event.data);
                }
            }

            if (isFailedOrClosed(error, QStringLiteral("SSE stream closed before endpoint discovery."))) {
                return QUrl();
            }
        }

        if (error) {
            *error = QStringLiteral("Timed out waiting for SSE endpoint.");
        }
        return QUrl();
    }

    QJsonObject waitForJsonRpc(int requestId, int timeoutMs, QString *error) {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 8000);

        while (std::chrono::steady_clock::now() < deadline) {
            const QVector<SseEvent> events = takeAvailableEvents(deadline);
            for (const SseEvent &event : events) {
                if (event.data.isEmpty()) {
                    continue;
                }
                const QJsonObject obj = parseJsonObject(event.data.toUtf8());
                if (obj.isEmpty()) {
                    continue;
                }
                if (obj.value("id").toInt(-1) == requestId) {
                    return obj;
                }
            }

            if (isFailedOrClosed(error, QStringLiteral("SSE stream closed before JSON-RPC response."))) {
                return {};
            }
        }

        if (error) {
            *error = QStringLiteral("Timed out waiting for SSE JSON-RPC response.");
        }
        return {};
    }

    void close() {
        std::shared_ptr<FastNet::HttpClient> client;
        {
            if (!state_) {
                client = client_;
            } else {
                std::lock_guard<std::mutex> lock(state_->mutex);
                state_->cancelled = true;
                state_->condition.notify_all();
                client = client_;
            }
        }

        if (client) {
            client->cancelRequest();
            client->disconnect();
        }
        client_.reset();
        state_.reset();
    }

private:
    struct State {
        std::mutex mutex;
        std::condition_variable condition;
        bool started = false;
        bool failed = false;
        bool finished = false;
        bool cancelled = false;
        QString error;
        QByteArray buffer;
        QVector<SseEvent> events;
    };

    static void failState(const std::shared_ptr<State> &state, const QString &error) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->failed = true;
        state->error = error;
        state->condition.notify_all();
    }

    QVector<SseEvent> takeAvailableEvents(const std::chrono::steady_clock::time_point &deadline) {
        if (!state_) {
            return {};
        }

        std::unique_lock<std::mutex> lock(state_->mutex);
        if (state_->events.isEmpty() && !state_->failed && !state_->finished) {
            state_->condition.wait_until(lock, deadline, [this]() {
                return !state_->events.isEmpty() || state_->failed || state_->finished;
            });
        }

        QVector<SseEvent> events = state_->events;
        state_->events.clear();
        return events;
    }

    bool isFailedOrClosed(QString *error, const QString &closedMessage) const {
        if (!state_) {
            if (error) {
                *error = QStringLiteral("SSE stream is not open.");
            }
            return true;
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->failed) {
            if (error) {
                *error = state_->error.isEmpty() ? closedMessage : state_->error;
            }
            return true;
        }
        if (state_->finished && state_->events.isEmpty()) {
            if (error) {
                *error = closedMessage;
            }
            return true;
        }
        return false;
    }

    std::shared_ptr<State> state_;
    std::shared_ptr<FastNet::HttpClient> client_;
};

bool postJsonRpc(const QUrl &url,
                 const QHash<QString, QString> &headers,
                 const QJsonObject &payload,
                 int timeoutMs,
                 QByteArray *responseBody,
                 QString *error) {
    platform::network::HttpRequest request;
    request.method = QStringLiteral("POST");
    request.url = url.toString(QUrl::FullyEncoded);
    request.headers.insert("Content-Type", "application/json");
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        request.headers.insert(it.key().toUtf8(), it.value().toUtf8());
    }
    request.body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    request.timeoutMs = timeoutMs;

    const platform::network::HttpResponse response = platform::network::FastNetHttpTransport::send(request);
    if (!response.ok()) {
        if (error) {
            *error = response.error.isEmpty()
                ? QStringLiteral("MCP JSON-RPC HTTP %1.").arg(response.statusCode)
                : response.error;
        }
        return false;
    }

    if (responseBody) {
        *responseBody = response.body;
    }
    return true;
}

} // namespace

MCPManager::MCPManager(const QHash<QString, config::MCPServerConfig> &servers, QObject *parent)
    : QObject(parent),
      _servers(servers) {}

MCPManager::~MCPManager() {
    QMutexLocker lock(&_procMutex);
    for (auto &sp : _processes) {
        if (sp.process) {
            sp.process->terminate();
            sp.process->waitForFinished(2000);
            delete sp.process;
            sp.process = nullptr;
        }
    }
}

QStringList MCPManager::servers() const {
    return _servers.keys();
}

bool MCPManager::hasServer(const QString &name) const {
    return _servers.contains(name);
}

QVector<MCPRemoteTool> MCPManager::listTools(const QString &serverName) {
    if (!_servers.contains(serverName)) {
        return {};
    }

    const config::MCPServerConfig cfg = _servers.value(serverName);
    const QString transport = transportFor(cfg);
    if (transport == "stdio") {
        return listToolsStdio(serverName);
    }
    if (transport == "streamableHttp") {
        return listToolsHttp(serverName);
    }
    if (transport == "sse") {
        return listToolsSse(serverName);
    }
    return {};
}

QVector<MCPRemoteTool> MCPManager::listAllTools() {
    QVector<MCPRemoteTool> out;
    for (auto it = _servers.begin(); it != _servers.end(); ++it) {
        const QVector<MCPRemoteTool> tools = listTools(it.key());
        for (const MCPRemoteTool &tool : tools) {
            out.append(tool);
        }
    }
    return out;
}

MCPManager::CallResult MCPManager::call(
    const QString &serverName,
    const QString &toolName,
    const QJsonObject &arguments)
{
    if (!_servers.contains(serverName)) {
        return {false, "MCP server not found: " + serverName};
    }
    const config::MCPServerConfig cfg = _servers.value(serverName);
    const QString transport = transportFor(cfg);

    if (transport == "stdio") {
        return callStdio(serverName, toolName, arguments);
    }
    if (transport == "streamableHttp") {
        return callHttp(serverName, toolName, arguments);
    }
    if (transport == "sse") {
        return callSse(serverName, toolName, arguments);
    }
    return {false, "Unknown MCP transport: " + transport};
}

MCPManager::ServerProcess *MCPManager::getOrStartProcess(const QString &serverName) {
    {
        QMutexLocker lock(&_procMutex);
        if (_processes.contains(serverName)) {
            ServerProcess &sp = _processes[serverName];
            if (sp.process && sp.process->state() == QProcess::Running) {
                return &_processes[serverName];
            }
            qWarning(lcMcp) << "MCP server" << serverName << "died, restarting";
            if (sp.process) {
                delete sp.process;
                sp.process = nullptr;
            }
            _processes.remove(serverName);
        }
    }

    const config::MCPServerConfig cfg = _servers.value(serverName);

    auto *proc = new QProcess();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = cfg.env.begin(); it != cfg.env.end(); ++it) {
        env.insert(it.key(), it.value());
    }
    proc->setProcessEnvironment(env);
    proc->start(cfg.command, cfg.args);

    if (!proc->waitForStarted(5000)) {
        qWarning(lcMcp) << "Failed to start MCP server:" << serverName;
        delete proc;
        return nullptr;
    }

    ServerProcess sp;
    sp.process = proc;
    sp.initialized = false;
    sp.nextId = 1;

    if (!performInitHandshake(serverName, sp)) {
        proc->terminate();
        proc->waitForFinished(2000);
        delete proc;
        return nullptr;
    }

    QMutexLocker lock(&_procMutex);
    if (_processes.contains(serverName)) {
        proc->terminate();
        proc->waitForFinished(2000);
        delete proc;
        return &_processes[serverName];
    }
    _processes.insert(serverName, sp);
    qDebug(lcMcp) << "MCP server" << serverName << "started and initialized";
    return &_processes[serverName];
}

bool MCPManager::performInitHandshake(const QString &serverName, ServerProcess &sp) {
    const int timeoutMs = 10000;

    QJsonObject initReq;
    initReq["jsonrpc"] = "2.0";
    initReq["id"] = sp.nextId++;
    initReq["method"] = "initialize";
    initReq["params"] = QJsonObject{
        {"protocolVersion", "2024-11-05"},
        {"capabilities", QJsonObject{}},
        {"clientInfo", QJsonObject{{"name", "yaos"}, {"version", "1.0"}}}
    };

    const QJsonObject initResp = sendRequest(sp, initReq, timeoutMs);
    if (initResp.isEmpty() || initResp.contains("error")) {
        qWarning(lcMcp) << "MCP server" << serverName
                        << "initialize failed:" << initResp.value("error").toObject().value("message").toString();
        return false;
    }

    QJsonObject notif;
    notif["jsonrpc"] = "2.0";
    notif["method"] = "notifications/initialized";
    const QByteArray notifLine = QJsonDocument(notif).toJson(QJsonDocument::Compact) + "\n";
    sp.process->write(notifLine);
    sp.process->waitForBytesWritten(3000);

    sp.initialized = true;
    return true;
}

QJsonObject MCPManager::sendRequest(ServerProcess &sp, const QJsonObject &request, int timeoutMs) {
    const QByteArray line = QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n";
    sp.process->write(line);
    if (!sp.process->waitForBytesWritten(3000)) {
        qWarning(lcMcp) << "Failed to write to MCP server";
        return {};
    }
    return readResponseLine(sp.process, timeoutMs);
}

QJsonObject MCPManager::readResponseLine(QProcess *proc, int timeoutMs) {
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;

    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (!proc->canReadLine()) {
            const int remaining = static_cast<int>(deadline - QDateTime::currentMSecsSinceEpoch());
            if (remaining <= 0) {
                break;
            }
            proc->waitForReadyRead(qMin(remaining, 500));
            continue;
        }

        const QByteArray rawLine = proc->readLine().trimmed();
        if (rawLine.isEmpty()) {
            continue;
        }

        const QJsonObject obj = parseJsonObject(rawLine);
        if (obj.isEmpty()) {
            qWarning(lcMcp) << "Malformed JSON-RPC line:" << rawLine.left(200);
            continue;
        }
        if (obj.contains("method") && !obj.contains("id")) {
            continue;
        }
        return obj;
    }

    qWarning(lcMcp) << "Timed out waiting for MCP response";
    return {};
}

MCPManager::CallResult MCPManager::callStdio(
    const QString &serverName,
    const QString &toolName,
    const QJsonObject &arguments)
{
    ServerProcess *sp = getOrStartProcess(serverName);
    if (!sp) {
        return {false, QString("Failed to start MCP server '%1'").arg(serverName)};
    }

    const config::MCPServerConfig cfg = _servers.value(serverName);
    const int timeoutMs = qMax(1, cfg.toolTimeout) * 1000;

    QMutexLocker lock(&_procMutex);
    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["id"] = sp->nextId++;
    req["method"] = "tools/call";
    req["params"] = QJsonObject{
        {"name", toolName},
        {"arguments", arguments}
    };

    const QJsonObject resp = sendRequest(*sp, req, timeoutMs);
    if (resp.isEmpty()) {
        return {false, QString("MCP server '%1' timed out").arg(serverName)};
    }
    if (resp.contains("error")) {
        const QString msg = resp.value("error").toObject().value("message").toString("unknown error");
        qWarning(lcMcp) << "MCP tool call error from" << serverName << ":" << msg;
        return {false, msg};
    }

    return {true, extractTextResult(resp.value("result").toObject())};
}

MCPManager::CallResult MCPManager::callHttp(
    const QString &serverName,
    const QString &toolName,
    const QJsonObject &arguments)
{
    const config::MCPServerConfig cfg = _servers.value(serverName);
    const QUrl url(cfg.url);
    if (!url.isValid()) {
        return {false, QString("MCP server '%1' has invalid URL").arg(serverName)};
    }

    QJsonObject reqBody;
    reqBody["jsonrpc"] = "2.0";
    reqBody["id"] = 1;
    reqBody["method"] = "tools/call";
    reqBody["params"] = QJsonObject{
        {"name", toolName},
        {"arguments", arguments}
    };

    QByteArray responseBody;
    QString error;
    if (!postJsonRpc(url, cfg.headers, reqBody, qMax(1, cfg.toolTimeout) * 1000, &responseBody, &error)) {
        return {false, QString("MCP server '%1' HTTP error: %2").arg(serverName, error)};
    }

    const QJsonObject root = parseJsonObject(responseBody);
    if (root.isEmpty()) {
        return {false, "Invalid JSON-RPC response from HTTP MCP server"};
    }
    if (root.contains("error")) {
        return {false, root.value("error").toObject().value("message").toString("unknown error")};
    }

    return {true, extractTextResult(root.value("result").toObject())};
}

MCPManager::CallResult MCPManager::callSse(
    const QString &serverName,
    const QString &toolName,
    const QJsonObject &arguments)
{
    const config::MCPServerConfig cfg = _servers.value(serverName);
    const QUrl url(cfg.url);
    if (!url.isValid()) {
        return {false, QString("MCP server '%1' has invalid SSE URL").arg(serverName)};
    }

    SseStream stream;
    QString error;
    if (!stream.open(url, cfg.headers, 8000, &error)) {
        return {false, error.isEmpty() ? QStringLiteral("Failed to open MCP SSE stream.") : error};
    }

    const QUrl endpoint = stream.waitForEndpoint(url, 8000, &error);
    if (!endpoint.isValid()) {
        return {false, error.isEmpty() ? QStringLiteral("Failed to discover SSE message endpoint.") : error};
    }

    QJsonObject initReq{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", QJsonObject{
            {"protocolVersion", "2024-11-05"},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "yaos"}, {"version", "1.0"}}}
        }}
    };
    if (!postJsonRpc(endpoint, cfg.headers, initReq, 5000, nullptr, &error)) {
        return {false, error};
    }

    const QJsonObject initResp = stream.waitForJsonRpc(1, 8000, &error);
    if (initResp.isEmpty() || initResp.contains("error")) {
        return {false, error.isEmpty() ? QStringLiteral("MCP SSE initialize failed.") : error};
    }

    QJsonObject initializedNotif{
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"}
    };
    if (!postJsonRpc(endpoint, cfg.headers, initializedNotif, 5000, nullptr, &error)) {
        return {false, error};
    }

    QJsonObject request{
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/call"},
        {"params", QJsonObject{
            {"name", toolName},
            {"arguments", arguments}
        }}
    };
    if (!postJsonRpc(endpoint, cfg.headers, request, 5000, nullptr, &error)) {
        return {false, error};
    }

    const QJsonObject response = stream.waitForJsonRpc(2, qMax(1, cfg.toolTimeout) * 1000, &error);

    if (response.isEmpty()) {
        return {false, error.isEmpty() ? QStringLiteral("Timed out waiting for MCP SSE tool response.") : error};
    }
    if (response.contains("error")) {
        return {false, response.value("error").toObject().value("message").toString("unknown error")};
    }
    return {true, extractTextResult(response.value("result").toObject())};
}

QVector<MCPRemoteTool> MCPManager::listToolsStdio(const QString &serverName) {
    if (!_servers.contains(serverName)) {
        return {};
    }

    const config::MCPServerConfig cfg = _servers.value(serverName);
    auto *proc = new QProcess();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = cfg.env.begin(); it != cfg.env.end(); ++it) {
        env.insert(it.key(), it.value());
    }
    proc->setProcessEnvironment(env);
    proc->start(cfg.command, cfg.args);

    if (!proc->waitForStarted(5000)) {
        qWarning(lcMcp) << "Failed to start MCP server for tool discovery:" << serverName;
        delete proc;
        return {};
    }

    // Tool discovery happens while AgentLoop is still on the bootstrap thread.
    // Keep this process ephemeral so later tool execution can start its own
    // long-lived stdio server inside the agent thread without cross-thread QProcess reuse.
    ServerProcess sp;
    sp.process = proc;
    sp.initialized = false;
    sp.nextId = 1;
    if (!performInitHandshake(serverName, sp)) {
        proc->terminate();
        proc->waitForFinished(2000);
        delete proc;
        return {};
    }

    QJsonObject req{
        {"jsonrpc", "2.0"},
        {"id", sp.nextId++},
        {"method", "tools/list"},
        {"params", QJsonObject{}}
    };
    const QJsonObject resp = sendRequest(sp, req, 10000);
    proc->terminate();
    proc->waitForFinished(2000);
    delete proc;
    if (resp.isEmpty() || resp.contains("error")) {
        return {};
    }
    return parseToolList(serverName, resp.value("result").toObject());
}

QVector<MCPRemoteTool> MCPManager::listToolsHttp(const QString &serverName) {
    const config::MCPServerConfig cfg = _servers.value(serverName);
    const QUrl url(cfg.url);
    if (!url.isValid()) {
        return {};
    }

    QJsonObject reqBody{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "tools/list"},
        {"params", QJsonObject{}}
    };

    QByteArray responseBody;
    QString error;
    if (!postJsonRpc(url, cfg.headers, reqBody, 10000, &responseBody, &error)) {
        return {};
    }
    const QJsonObject root = parseJsonObject(responseBody);
    if (root.isEmpty() || root.contains("error")) {
        return {};
    }
    return parseToolList(serverName, root.value("result").toObject());
}

QVector<MCPRemoteTool> MCPManager::listToolsSse(const QString &serverName) {
    const config::MCPServerConfig cfg = _servers.value(serverName);
    const QUrl url(cfg.url);
    if (!url.isValid()) {
        return {};
    }

    SseStream stream;
    QString error;
    if (!stream.open(url, cfg.headers, 8000, &error)) {
        return {};
    }

    const QUrl endpoint = stream.waitForEndpoint(url, 8000, &error);
    if (!endpoint.isValid()) {
        return {};
    }

    QJsonObject initReq{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", QJsonObject{
            {"protocolVersion", "2024-11-05"},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "yaos"}, {"version", "1.0"}}}
        }}
    };
    if (!postJsonRpc(endpoint, cfg.headers, initReq, 5000, nullptr, &error)) {
        return {};
    }
    const QJsonObject initResp = stream.waitForJsonRpc(1, 8000, &error);
    if (initResp.isEmpty() || initResp.contains("error")) {
        return {};
    }

    QJsonObject initializedNotif{
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"}
    };
    if (!postJsonRpc(endpoint, cfg.headers, initializedNotif, 5000, nullptr, &error)) {
        return {};
    }

    QJsonObject req{
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/list"},
        {"params", QJsonObject{}}
    };
    if (!postJsonRpc(endpoint, cfg.headers, req, 5000, nullptr, &error)) {
        return {};
    }

    const QJsonObject resp = stream.waitForJsonRpc(2, 10000, &error);
    if (resp.isEmpty() || resp.contains("error")) {
        return {};
    }
    return parseToolList(serverName, resp.value("result").toObject());
}

} // namespace yaos::runtime
