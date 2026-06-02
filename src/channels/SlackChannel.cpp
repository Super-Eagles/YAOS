#include "SlackChannel.h"

#include "ChannelHttp.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace yaos::channels {

SlackChannel::SlackChannel(const config::SlackConfig &config, bus::MessageBus &bus, QObject *parent)
    : QObject(parent),
      _config(config),
      _bus(bus) {
    connect(&_socket, &platform::network::FastNetWebSocketTransport::connected, this, &SlackChannel::onSocketConnected);
    connect(&_socket, &platform::network::FastNetWebSocketTransport::disconnected, this, &SlackChannel::onSocketDisconnected);
    connect(&_socket, &platform::network::FastNetWebSocketTransport::textMessageReceived, this, &SlackChannel::onSocketTextMessage);
}

QString SlackChannel::name() const {
    return "slack";
}

QJsonObject SlackChannel::apiPost(
    const QString &endpoint,
    const QString &token,
    const QJsonObject &payload,
    bool *ok,
    int timeoutMs
) {
    QMap<QByteArray, QByteArray> headers;
    headers.insert("Authorization", ("Bearer " + token).toUtf8());
    const platform::network::HttpResponse response =
        http::sendJson(QStringLiteral("POST"),
                       QStringLiteral("https://slack.com/api/%1").arg(endpoint),
                       payload,
                       headers,
                       timeoutMs);

    bool parsed = false;
    const QJsonObject obj = http::parseJsonObject(response.body, &parsed);
    if (ok) *ok = response.ok() && parsed && obj.value("ok").toBool(false);
    return obj;
}

QJsonObject SlackChannel::apiFormPost(
    const QString &endpoint,
    const QString &token,
    const QUrlQuery &payload,
    bool *ok,
    int timeoutMs
) {
    QMap<QByteArray, QByteArray> headers;
    headers.insert("Authorization", ("Bearer " + token).toUtf8());
    const platform::network::HttpResponse response =
        http::sendForm(QStringLiteral("https://slack.com/api/%1").arg(endpoint),
                       payload.query(QUrl::FullyEncoded).toUtf8(),
                       headers,
                       timeoutMs);

    bool parsed = false;
    const QJsonObject obj = http::parseJsonObject(response.body, &parsed);
    if (ok) *ok = response.ok() && parsed && obj.value("ok").toBool(false);
    return obj;
}

bool SlackChannel::postRawBytes(const QUrl &url, const QByteArray &data, int timeoutMs) {
    QMap<QByteArray, QByteArray> headers;
    headers.insert("Content-Type", "application/octet-stream");
    const platform::network::HttpResponse response =
        http::sendBody(QStringLiteral("POST"),
                       url.toString(QUrl::FullyEncoded),
                       data,
                       headers,
                       timeoutMs);
    return response.ok();
}

bool SlackChannel::uploadFiles(const bus::OutboundMessage &msg, const QString &threadTs, bool useThread) {
    QJsonArray uploadedFiles;
    for (const QString &path : msg.media) {
        const QString cleanPath = path.trimmed();
        if (cleanPath.isEmpty()) {
            continue;
        }

        QFile file(cleanPath);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }

        const QFileInfo info(file.fileName());
        QUrlQuery preparePayload;
        preparePayload.addQueryItem("filename", info.fileName());
        preparePayload.addQueryItem("length", QString::number(file.size()));

        bool prepareOk = false;
        const QJsonObject prepared = apiFormPost(
            "files.getUploadURLExternal",
            _config.botToken,
            preparePayload,
            &prepareOk
        );
        if (!prepareOk) {
            continue;
        }

        const QString uploadUrl = prepared.value("upload_url").toString();
        const QString fileId = prepared.value("file_id").toString();
        if (uploadUrl.isEmpty() || fileId.isEmpty()) {
            continue;
        }

        if (!postRawBytes(QUrl(uploadUrl), file.readAll())) {
            continue;
        }

        uploadedFiles.append(QJsonObject{
            {"id", fileId},
            {"title", info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName()}
        });
    }

    if (uploadedFiles.isEmpty()) {
        return false;
    }

    QJsonObject completePayload;
    completePayload["files"] = uploadedFiles;
    completePayload["channel_id"] = msg.chatId;
    if (useThread) {
        completePayload["thread_ts"] = threadTs;
    }
    if (!msg.content.trimmed().isEmpty()) {
        completePayload["initial_comment"] = msg.content;
    }

    bool completeOk = false;
    apiPost("files.completeUploadExternal", _config.botToken, completePayload, &completeOk, 30000);
    return completeOk;
}

bool SlackChannel::openSocketConnection() {
    bool ok = false;
    const QJsonObject opened = apiPost("apps.connections.open", _config.appToken, QJsonObject(), &ok);
    if (!ok) {
        return false;
    }
    const QString wsUrl = opened.value("url").toString();
    if (wsUrl.isEmpty()) {
        return false;
    }

    _socket.open(QUrl(wsUrl));
    return true;
}

bool SlackChannel::start() {
    if (_running) {
        return true;
    }
    if (_config.botToken.trimmed().isEmpty() || _config.appToken.trimmed().isEmpty()) {
        return false;
    }
    if (_config.mode != "socket") {
        return false;
    }

    _running = true;
    bool ok = false;
    const QJsonObject auth = apiPost("auth.test", _config.botToken, QJsonObject(), &ok);
    if (ok) {
        _botUserId = auth.value("user_id").toString();
    }
    return openSocketConnection();
}

void SlackChannel::stop() {
    _running = false;
    if (_socket.isOpenOrConnecting()) {
        _socket.close();
    }
}

bool SlackChannel::isAllowed(const QString &senderId, const QString &chatId, const QString &channelType) const {
    if (channelType == "im") {
        if (!_config.dm.enabled) {
            return false;
        }
        if (_config.dm.policy == "allowlist") {
            return _config.dm.allowFrom.contains(senderId);
        }
        return true;
    }
    if (_config.groupPolicy == "allowlist") {
        return _config.groupAllowFrom.contains(chatId);
    }
    return true;
}

bool SlackChannel::shouldRespondInChannel(const QString &eventType, const QString &text, const QString &chatId) const {
    if (_config.groupPolicy == "open") {
        return true;
    }
    if (_config.groupPolicy == "mention") {
        if (eventType == "app_mention") {
            return true;
        }
        return !_botUserId.isEmpty() && text.contains("<@" + _botUserId + ">");
    }
    if (_config.groupPolicy == "allowlist") {
        return _config.groupAllowFrom.contains(chatId);
    }
    return false;
}

QString SlackChannel::stripBotMention(const QString &text) const {
    if (_botUserId.isEmpty()) {
        return text;
    }
    QString clean = text;
    clean.replace(QRegularExpression("<@" + QRegularExpression::escape(_botUserId) + ">\\s*"), "");
    return clean.trimmed();
}

void SlackChannel::send(const bus::OutboundMessage &msg) {
    if (_config.botToken.trimmed().isEmpty()) {
        return;
    }
    if (msg.chatId.trimmed().isEmpty()) {
        return;
    }

    const QJsonObject slackMeta = msg.metadata.value("slack").toObject();
    const QString threadTs = slackMeta.value("thread_ts").toString();
    const QString channelType = slackMeta.value("channel_type").toString();
    const bool useThread = _config.replyInThread && channelType != "im" && !threadTs.isEmpty();

    if (!msg.media.isEmpty() && uploadFiles(msg, threadTs, useThread)) {
        return;
    }

    if (msg.content.trimmed().isEmpty()) {
        return;
    }

    QJsonObject payload;
    payload["channel"] = msg.chatId;
    payload["text"] = msg.content.isEmpty() ? " " : msg.content;
    if (useThread) {
        payload["thread_ts"] = threadTs;
    }

    bool ok = false;
    apiPost("chat.postMessage", _config.botToken, payload, &ok);
    Q_UNUSED(ok);
}

void SlackChannel::onSocketConnected() {
}

void SlackChannel::onSocketDisconnected() {
    if (_running) {
        QTimer::singleShot(1500, this, [this]() { openSocketConnection(); });
    }
}

void SlackChannel::onSocketTextMessage(const QString &message) {
    if (!_running) {
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }
    const QJsonObject root = doc.object();

    const QString envelopeId = root.value("envelope_id").toString();
    if (!envelopeId.isEmpty()) {
        _socket.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
            {"envelope_id", envelopeId}
        }).toJson(QJsonDocument::Compact)));
    }

    const QJsonObject payload = root.value("payload").toObject();
    const QJsonObject event = payload.value("event").toObject();
    const QString eventType = event.value("type").toString();
    if (eventType != "message" && eventType != "app_mention") {
        return;
    }
    if (!event.value("subtype").toString().isEmpty()) {
        return;
    }

    const QString senderId = event.value("user").toString();
    const QString chatId = event.value("channel").toString();
    const QString channelType = event.value("channel_type").toString();
    QString text = event.value("text").toString();
    if (senderId.isEmpty() || chatId.isEmpty()) {
        return;
    }
    if (!_botUserId.isEmpty() && senderId == _botUserId) {
        return;
    }
    if (!isAllowed(senderId, chatId, channelType)) {
        return;
    }
    if (channelType != "im" && !shouldRespondInChannel(eventType, text, chatId)) {
        return;
    }

    text = stripBotMention(text);
    if (text.trimmed().isEmpty()) {
        return;
    }

    QString threadTs = event.value("thread_ts").toString();
    if (_config.replyInThread && threadTs.isEmpty()) {
        threadTs = event.value("ts").toString();
    }

    if (!_config.reactEmoji.trimmed().isEmpty() && !event.value("ts").toString().isEmpty()) {
        QJsonObject reactPayload;
        reactPayload["channel"] = chatId;
        reactPayload["name"] = _config.reactEmoji;
        reactPayload["timestamp"] = event.value("ts").toString();
        bool reactOk = false;
        apiPost("reactions.add", _config.botToken, reactPayload, &reactOk, 5000);
        Q_UNUSED(reactOk);
    }

    bus::InboundMessage inbound;
    inbound.channel = "slack";
    inbound.senderId = senderId;
    inbound.chatId = chatId;
    inbound.content = text;
    inbound.metadata = QJsonObject{
        {"slack", QJsonObject{
            {"thread_ts", threadTs},
            {"channel_type", channelType}
        }}
    };
    if (!threadTs.isEmpty() && channelType != "im") {
        inbound.sessionKeyOverride = QString("slack:%1:%2").arg(chatId, threadTs);
    }
    _bus.publishInbound(inbound);
}

} // namespace yaos::channels
