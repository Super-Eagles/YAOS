#include "QQChannel.h"

#include "ChannelHttp.h"

#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QUrl>

Q_LOGGING_CATEGORY(lcQQ, "yaos.channels.qq")

namespace yaos::channels {

namespace {

constexpr qint64 kIntentGuilds = 1LL << 0;
constexpr qint64 kIntentGuildMessages = 1LL << 9;
constexpr qint64 kIntentDirectMessages = 1LL << 12;
constexpr qint64 kIntentGroupAndC2c = 1LL << 25;

QString qqPlatformName() {
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QString qqOpenApiBase() {
    return QStringLiteral("https://api.sgroup.qq.com");
}

QUrl qqApiUrl(const QString &path) {
    return QUrl(qqOpenApiBase() + path);
}

QString encodedSegment(const QString &value) {
    return QString::fromUtf8(QUrl::toPercentEncoding(value.trimmed()));
}

QString qqErrorMessage(const QByteArray &payload, const QString &fallback) {
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        const QString message = obj.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) {
            return message;
        }
        const QString description = obj.value(QStringLiteral("description")).toString().trimmed();
        if (!description.isEmpty()) {
            return description;
        }
    }

    const QString text = QString::fromUtf8(payload).trimmed();
    return text.isEmpty() ? fallback : text;
}

QString firstNonEmpty(const QStringList &candidates) {
    for (const QString &candidate : candidates) {
        if (!candidate.trimmed().isEmpty()) {
            return candidate.trimmed();
        }
    }
    return QString();
}

QString senderIdFromPayload(const QJsonObject &payload) {
    const QJsonObject author = payload.value(QStringLiteral("author")).toObject();
    return firstNonEmpty({
        author.value(QStringLiteral("member_openid")).toString(),
        author.value(QStringLiteral("user_openid")).toString(),
        author.value(QStringLiteral("union_openid")).toString(),
        author.value(QStringLiteral("id")).toString(),
        payload.value(QStringLiteral("author_id")).toString(),
        payload.value(QStringLiteral("openid")).toString()
    });
}

QString senderDisplayNameFromPayload(const QJsonObject &payload) {
    const QJsonObject author = payload.value(QStringLiteral("author")).toObject();
    return firstNonEmpty({
        author.value(QStringLiteral("username")).toString(),
        author.value(QStringLiteral("member_nick")).toString(),
        author.value(QStringLiteral("nick")).toString(),
        author.value(QStringLiteral("nickname")).toString()
    });
}

QJsonObject responseObject(const QJsonObject &root) {
    const QJsonValue data = root.value(QStringLiteral("data"));
    return data.isObject() ? data.toObject() : root;
}

bool responseOk(const QJsonObject &root) {
    return !root.contains(QStringLiteral("code")) || root.value(QStringLiteral("code")).toInt() == 0;
}

QString attachmentUrl(const QJsonObject &attachment) {
    QString url = firstNonEmpty({
        attachment.value(QStringLiteral("url")).toString(),
        attachment.value(QStringLiteral("proxy_url")).toString()
    });
    if (url.startsWith(QStringLiteral("//"))) {
        url.prepend(QStringLiteral("https:"));
    } else if (!url.contains(QStringLiteral("://")) && !url.isEmpty()) {
        url.prepend(QStringLiteral("https://"));
    }
    return url.trimmed();
}

qint64 messageSeqValue(const QJsonObject &metadata) {
    const qint64 seq = static_cast<qint64>(metadata.value(QStringLiteral("msg_seq")).toDouble(1));
    return seq > 0 ? seq : 1;
}

QStringList attachmentLines(const QStringList &urls) {
    QStringList lines;
    if (urls.isEmpty()) {
        return lines;
    }
    lines.append(QStringLiteral("[QQ attachments]"));
    for (const QString &url : urls) {
        lines.append(QStringLiteral("- %1").arg(url));
    }
    return lines;
}

} // namespace

QQChannel::QQChannel(const config::QQConfig &config,
                     bus::MessageBus &bus,
                     QObject *parent)
    : QObject(parent),
      _config(config),
      _bus(bus) {
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::connected, this, &QQChannel::onConnected);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::disconnected, this, &QQChannel::onDisconnected);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::textMessageReceived, this, &QQChannel::onTextMessageReceived);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::errorMessage, this, &QQChannel::onError);

    _reconnectTimer.setSingleShot(true);
    _reconnectTimer.setInterval(5000);
    connect(&_reconnectTimer, &QTimer::timeout, this, &QQChannel::reconnect);

    _heartbeatTimer.setSingleShot(false);
    connect(&_heartbeatTimer, &QTimer::timeout, this, &QQChannel::sendHeartbeat);
}

QQChannel::~QQChannel() {
    stop();
}

QString QQChannel::name() const {
    return QStringLiteral("qq");
}

bool QQChannel::start() {
    if (_running) {
        return true;
    }
    if (!_config.enabled) {
        return false;
    }
    if (_config.appId.trimmed().isEmpty() || _config.secret.trimmed().isEmpty()) {
        qWarning(lcQQ) << "QQ channel requires appId and secret";
        return false;
    }

    _running = true;
    reconnect();
    return true;
}

void QQChannel::stop() {
    _running = false;
    _reconnectTimer.stop();
    _heartbeatTimer.stop();
    _webSocket.close();
}

bool QQChannel::isAllowed(const QString &senderId) const {
    if (_config.allowFrom.isEmpty() || _config.allowFrom.contains(QStringLiteral("*"))) {
        return true;
    }
    if (_config.allowFrom.contains(senderId)) {
        return true;
    }

    const QStringList aliases = senderId.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (const QString &alias : aliases) {
        if (_config.allowFrom.contains(alias.trimmed())) {
            return true;
        }
    }
    return false;
}

bool QQChannel::ensureAccessToken() {
    if (!_accessToken.trimmed().isEmpty() &&
        _accessTokenExpiresAt.isValid() &&
        _accessTokenExpiresAt > QDateTime::currentDateTimeUtc().addSecs(60)) {
        return true;
    }

    const QJsonObject payload{
        {QStringLiteral("appId"), _config.appId.trimmed()},
        {QStringLiteral("clientSecret"), _config.secret.trimmed()}
    };

    QMap<QByteArray, QByteArray> headers;
    headers.insert("Accept", "application/json");
    const platform::network::HttpResponse httpResponse =
        http::sendJson(QStringLiteral("POST"),
                       QStringLiteral("https://bots.qq.com/app/getAppAccessToken"),
                       payload,
                       headers,
                       15000);

    const QByteArray response = httpResponse.body;
    if (!httpResponse.ok()) {
        qWarning(lcQQ) << "QQ access token request failed:"
                       << qqErrorMessage(response, httpResponse.error);
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(response);
    if (!doc.isObject()) {
        qWarning(lcQQ) << "QQ access token response was not JSON";
        return false;
    }

    const QJsonObject root = doc.object();
    if (!responseOk(root)) {
        qWarning(lcQQ) << "QQ access token request returned error:"
                       << root.value(QStringLiteral("message")).toString();
        return false;
    }

    const QJsonObject data = responseObject(root);
    _accessToken = firstNonEmpty({
        data.value(QStringLiteral("access_token")).toString(),
        root.value(QStringLiteral("access_token")).toString()
    });
    const int expiresIn = data.value(QStringLiteral("expires_in")).toInt(
        root.value(QStringLiteral("expires_in")).toInt(7200));
    if (_accessToken.trimmed().isEmpty()) {
        qWarning(lcQQ) << "QQ access token response did not include access_token";
        return false;
    }

    _accessTokenExpiresAt = QDateTime::currentDateTimeUtc().addSecs(qMax(60, expiresIn - 60));
    return true;
}

QString QQChannel::fetchGatewayUrl() {
    if (!ensureAccessToken()) {
        return QString();
    }

    const QList<QUrl> candidates = {
        qqApiUrl(QStringLiteral("/gateway")),
        qqApiUrl(QStringLiteral("/gateway/bot"))
    };
    for (const QUrl &url : candidates) {
        bool ok = false;
        const QJsonObject root = getJsonSync(url, &ok);
        if (!ok) {
            continue;
        }
        const QJsonObject data = responseObject(root);
        const QString gatewayUrl = firstNonEmpty({
            data.value(QStringLiteral("url")).toString(),
            root.value(QStringLiteral("url")).toString()
        });
        if (!gatewayUrl.trimmed().isEmpty()) {
            return gatewayUrl.trimmed();
        }
    }

    qWarning(lcQQ) << "Failed to resolve QQ gateway URL";
    return QString();
}

QJsonObject QQChannel::postJsonSync(const QUrl &url,
                                    const QJsonObject &payload,
                                    bool *ok,
                                    int timeoutMs) {
    const platform::network::HttpResponse httpResponse =
        http::sendJson(QStringLiteral("POST"),
                       url.toString(QUrl::FullyEncoded),
                       payload,
                       authorizedHeaders(),
                       timeoutMs);

    const QByteArray response = httpResponse.body;
    if (!httpResponse.ok()) {
        qWarning(lcQQ) << "QQ POST request failed:" << qqErrorMessage(response, httpResponse.error)
                       << url;
        if (ok) {
            *ok = false;
        }
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(response);
    if (!doc.isObject()) {
        if (ok) {
            *ok = false;
        }
        return {};
    }

    const QJsonObject root = doc.object();
    const bool success = responseOk(root);
    if (!success) {
        qWarning(lcQQ) << "QQ POST request returned API error:"
                       << root.value(QStringLiteral("message")).toString()
                       << url;
    }
    if (ok) {
        *ok = success;
    }
    return root;
}

QJsonObject QQChannel::getJsonSync(const QUrl &url,
                                   bool *ok,
                                   int timeoutMs) {
    const platform::network::HttpResponse httpResponse =
        http::sendGet(url.toString(QUrl::FullyEncoded),
                      authorizedHeaders(),
                      timeoutMs);

    const QByteArray response = httpResponse.body;
    if (!httpResponse.ok()) {
        qWarning(lcQQ) << "QQ GET request failed:" << qqErrorMessage(response, httpResponse.error)
                       << url;
        if (ok) {
            *ok = false;
        }
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(response);
    if (!doc.isObject()) {
        if (ok) {
            *ok = false;
        }
        return {};
    }

    const QJsonObject root = doc.object();
    const bool success = responseOk(root);
    if (ok) {
        *ok = success;
    }
    return root;
}

QMap<QByteArray, QByteArray> QQChannel::authorizedHeaders() const {
    QMap<QByteArray, QByteArray> headers;
    headers.insert("Accept", "application/json");
    if (!_accessToken.trimmed().isEmpty()) {
        headers.insert("Authorization", accessTokenHeaderValue().toUtf8());
    }
    if (!_config.appId.trimmed().isEmpty()) {
        headers.insert("X-Union-Appid", _config.appId.trimmed().toUtf8());
    }
    return headers;
}

QString QQChannel::accessTokenHeaderValue() const {
    if (_config.appId.trimmed().isEmpty() || _accessToken.trimmed().isEmpty()) {
        return QString();
    }
    return QStringLiteral("QQBot %1.%2").arg(_config.appId.trimmed(), _accessToken.trimmed());
}

QQChannel::RouteTarget QQChannel::routeForOutbound(const bus::OutboundMessage &msg) const {
    RouteTarget target;
    const QString explicitKind = msg.metadata.value(QStringLiteral("qq_route_kind")).toString().trimmed();
    const QString chatId = msg.chatId.trimmed();

    auto assign = [&target](const QString &kind, const QString &id) {
        target.kind = kind.trimmed();
        target.id = id.trimmed();
    };

    if (!explicitKind.isEmpty()) {
        QString id = chatId;
        const QString prefix = explicitKind + QStringLiteral(":");
        if (id.startsWith(prefix)) {
            id = id.mid(prefix.size());
        }
        assign(explicitKind, id);
    } else if (chatId.startsWith(QStringLiteral("channel:"))) {
        assign(QStringLiteral("channel"), chatId.mid(QStringLiteral("channel:").size()));
    } else if (chatId.startsWith(QStringLiteral("dm:"))) {
        assign(QStringLiteral("dm"), chatId.mid(QStringLiteral("dm:").size()));
    } else if (chatId.startsWith(QStringLiteral("group:"))) {
        assign(QStringLiteral("group"), chatId.mid(QStringLiteral("group:").size()));
    } else if (chatId.startsWith(QStringLiteral("c2c:"))) {
        assign(QStringLiteral("c2c"), chatId.mid(QStringLiteral("c2c:").size()));
    }

    if (target.kind == QStringLiteral("guild")) {
        target.kind = QStringLiteral("channel");
    }
    return target;
}

QString QQChannel::normalizedContent(QString content) const {
    content.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    content.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    if (!_config.appId.trimmed().isEmpty()) {
        content.remove(QRegularExpression(QStringLiteral("<@!?%1>").arg(
            QRegularExpression::escape(_config.appId.trimmed()))));
    }
    content.remove(QRegularExpression(QStringLiteral("^\\s*<@!?\\d+>\\s*")));
    content.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return content.trimmed();
}

QStringList QQChannel::extractMediaUrls(const QJsonArray &attachments) const {
    QStringList mediaUrls;
    for (const QJsonValue &value : attachments) {
        if (!value.isObject()) {
            continue;
        }
        const QString url = attachmentUrl(value.toObject());
        if (!url.isEmpty()) {
            mediaUrls.append(url);
        }
    }
    mediaUrls.removeDuplicates();
    return mediaUrls;
}

void QQChannel::publishInboundMessage(const QString &routeKind,
                                      const QString &targetId,
                                      const QString &senderId,
                                      QString content,
                                      const QJsonObject &payload,
                                      const QJsonArray &attachments,
                                      const QString &messageId) {
    if (routeKind.trimmed().isEmpty() || targetId.trimmed().isEmpty() || senderId.trimmed().isEmpty()) {
        return;
    }
    if (!isAllowed(senderId)) {
        return;
    }

    const QStringList mediaUrls = extractMediaUrls(attachments);
    content = normalizedContent(content);
    if (!mediaUrls.isEmpty()) {
        QStringList lines;
        if (!content.isEmpty()) {
            lines.append(content);
            lines.append(QString());
        }
        lines.append(attachmentLines(mediaUrls));
        content = lines.join(QLatin1Char('\n')).trimmed();
    }
    if (content.isEmpty()) {
        return;
    }

    bus::InboundMessage inbound;
    inbound.channel = name();
    inbound.chatId = QStringLiteral("%1:%2").arg(routeKind, targetId);
    inbound.senderId = senderId;
    inbound.content = content;
    inbound.media = mediaUrls;
    inbound.metadata = QJsonObject{
        {QStringLiteral("qq_route_kind"), routeKind},
        {QStringLiteral("message_id"), messageId},
        {QStringLiteral("raw"), payload}
    };
    if (!attachments.isEmpty()) {
        inbound.metadata.insert(QStringLiteral("attachments"), attachments);
    }
    const QString senderName = senderDisplayNameFromPayload(payload);
    if (!senderName.isEmpty()) {
        inbound.metadata.insert(QStringLiteral("sender_name"), senderName);
    }
    const QString guildId = payload.value(QStringLiteral("guild_id")).toString().trimmed();
    if (!guildId.isEmpty()) {
        inbound.metadata.insert(QStringLiteral("guild_id"), guildId);
    }
    const QString channelId = payload.value(QStringLiteral("channel_id")).toString().trimmed();
    if (!channelId.isEmpty()) {
        inbound.metadata.insert(QStringLiteral("channel_id"), channelId);
    }
    const QString groupOpenId = payload.value(QStringLiteral("group_openid")).toString().trimmed();
    if (!groupOpenId.isEmpty()) {
        inbound.metadata.insert(QStringLiteral("group_openid"), groupOpenId);
    }
    const QString eventId = payload.value(QStringLiteral("event_id")).toString().trimmed();
    if (!eventId.isEmpty()) {
        inbound.metadata.insert(QStringLiteral("event_id"), eventId);
    }

    _bus.publishInbound(inbound);
}

void QQChannel::handleDispatchEvent(const QString &eventType, const QJsonObject &data) {
    if (eventType == QStringLiteral("READY")) {
        _sessionId = data.value(QStringLiteral("session_id")).toString().trimmed();
        qInfo(lcQQ) << "QQ gateway READY";
        return;
    }
    if (eventType == QStringLiteral("RESUMED")) {
        qInfo(lcQQ) << "QQ gateway session resumed";
        return;
    }

    const QString messageId = firstNonEmpty({
        data.value(QStringLiteral("id")).toString(),
        data.value(QStringLiteral("message_id")).toString()
    });
    const QJsonArray attachments = data.value(QStringLiteral("attachments")).toArray();
    const QString senderId = senderIdFromPayload(data);
    const QString content = firstNonEmpty({
        data.value(QStringLiteral("content")).toString(),
        data.value(QStringLiteral("text")).toString()
    });

    if (eventType == QStringLiteral("AT_MESSAGE_CREATE")) {
        publishInboundMessage(QStringLiteral("channel"),
                              data.value(QStringLiteral("channel_id")).toString(),
                              senderId,
                              content,
                              data,
                              attachments,
                              messageId);
        return;
    }
    if (eventType == QStringLiteral("DIRECT_MESSAGE_CREATE")) {
        publishInboundMessage(QStringLiteral("dm"),
                              firstNonEmpty({
                                  data.value(QStringLiteral("guild_id")).toString(),
                                  data.value(QStringLiteral("channel_id")).toString()
                              }),
                              senderId,
                              content,
                              data,
                              attachments,
                              messageId);
        return;
    }
    if (eventType == QStringLiteral("GROUP_AT_MESSAGE_CREATE")) {
        publishInboundMessage(QStringLiteral("group"),
                              data.value(QStringLiteral("group_openid")).toString(),
                              senderId,
                              content,
                              data,
                              attachments,
                              messageId);
        return;
    }
    if (eventType == QStringLiteral("C2C_MESSAGE_CREATE")) {
        const QString openId = firstNonEmpty({
            data.value(QStringLiteral("openid")).toString(),
            senderId
        });
        publishInboundMessage(QStringLiteral("c2c"),
                              openId,
                              senderId,
                              content,
                              data,
                              attachments,
                              messageId);
    }
}

void QQChannel::identify() {
    if (!_running || !_accessToken.size()) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("op"), 2);

    QJsonObject data;
    data.insert(QStringLiteral("token"), accessTokenHeaderValue());
    data.insert(QStringLiteral("intents"),
                static_cast<double>(kIntentGuilds |
                                    kIntentGuildMessages |
                                    kIntentDirectMessages |
                                    kIntentGroupAndC2c));
    data.insert(QStringLiteral("shard"), QJsonArray{0, 1});
    data.insert(QStringLiteral("properties"), QJsonObject{
        {QStringLiteral("$os"), qqPlatformName()},
        {QStringLiteral("$browser"), QStringLiteral("yaos")},
        {QStringLiteral("$device"), QStringLiteral("yaos")}
    });

    payload.insert(QStringLiteral("d"), data);
    _webSocket.sendTextMessage(QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void QQChannel::resume() {
    if (!_running || _sessionId.trimmed().isEmpty() || _lastSeq < 0) {
        identify();
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("op"), 6);

    QJsonObject data;
    data.insert(QStringLiteral("token"), accessTokenHeaderValue());
    data.insert(QStringLiteral("session_id"), _sessionId);
    data.insert(QStringLiteral("seq"), static_cast<double>(_lastSeq));

    payload.insert(QStringLiteral("d"), data);
    _webSocket.sendTextMessage(QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void QQChannel::onConnected() {
    qInfo(lcQQ) << "QQ gateway connected";
}

void QQChannel::onDisconnected() {
    qWarning(lcQQ) << "QQ gateway disconnected";
    _heartbeatTimer.stop();
    if (_running) {
        _reconnectTimer.start();
    }
}

void QQChannel::onTextMessageReceived(const QString &message) {
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        return;
    }

    const QJsonObject root = doc.object();
    const qint64 seq = static_cast<qint64>(root.value(QStringLiteral("s")).toDouble(-1));
    if (seq >= 0) {
        _lastSeq = seq;
    }

    const int op = root.value(QStringLiteral("op")).toInt(-1);
    const QJsonObject data = root.value(QStringLiteral("d")).toObject();

    if (op == 10) {
        const int heartbeatIntervalMs = data.value(QStringLiteral("heartbeat_interval")).toInt(30000);
        _heartbeatTimer.start(qMax(5000, heartbeatIntervalMs));
        if (_sessionId.trimmed().isEmpty() || _lastSeq < 0) {
            identify();
        } else {
            resume();
        }
        return;
    }
    if (op == 11) {
        return;
    }
    if (op == 1) {
        sendHeartbeat();
        return;
    }
    if (op == 7) {
        _webSocket.close();
        return;
    }
    if (op == 9) {
        _sessionId.clear();
        _lastSeq = -1;
        _webSocket.close();
        return;
    }
    if (op == 0) {
        handleDispatchEvent(root.value(QStringLiteral("t")).toString(), data);
    }
}

void QQChannel::onError(const QString &error) {
    qWarning(lcQQ) << "QQ gateway socket error:" << error;
}

void QQChannel::reconnect() {
    if (!_running) {
        return;
    }

    _heartbeatTimer.stop();
    if (!ensureAccessToken()) {
        _reconnectTimer.start();
        return;
    }

    const QString gatewayUrl = fetchGatewayUrl();
    if (gatewayUrl.trimmed().isEmpty()) {
        _reconnectTimer.start();
        return;
    }

    if (_webSocket.isOpenOrConnecting()) {
        _webSocket.close();
    }
    _webSocket.open(QUrl(gatewayUrl));
}

void QQChannel::sendHeartbeat() {
    if (!_webSocket.isValid()) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("op"), 1);
    if (_lastSeq >= 0) {
        payload.insert(QStringLiteral("d"), static_cast<double>(_lastSeq));
    } else {
        payload.insert(QStringLiteral("d"), QJsonValue::Null);
    }
    _webSocket.sendTextMessage(QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void QQChannel::send(const bus::OutboundMessage &msg) {
    if (!_config.enabled || msg.chatId.trimmed().isEmpty()) {
        return;
    }
    if (!ensureAccessToken()) {
        qWarning(lcQQ) << "Skipping QQ outbound send because access token acquisition failed";
        return;
    }

    const RouteTarget target = routeForOutbound(msg);
    if (target.kind.isEmpty() || target.id.isEmpty()) {
        qWarning(lcQQ) << "Skipping QQ outbound send with unknown route target:" << msg.chatId;
        return;
    }

    QString text = normalizedContent(msg.content);
    if (!msg.media.isEmpty()) {
        QStringList lines;
        if (!text.isEmpty()) {
            lines.append(text);
            lines.append(QString());
        }
        lines.append(QStringLiteral("[attachments]"));
        for (const QString &item : msg.media) {
            if (!item.trimmed().isEmpty()) {
                lines.append(QStringLiteral("- %1").arg(item.trimmed()));
            }
        }
        text = lines.join(QLatin1Char('\n')).trimmed();
    }
    if (text.isEmpty()) {
        return;
    }

    QUrl url;
    if (target.kind == QStringLiteral("channel")) {
        url = qqApiUrl(QStringLiteral("/channels/%1/messages").arg(encodedSegment(target.id)));
    } else if (target.kind == QStringLiteral("dm")) {
        url = qqApiUrl(QStringLiteral("/dms/%1/messages").arg(encodedSegment(target.id)));
    } else if (target.kind == QStringLiteral("group")) {
        url = qqApiUrl(QStringLiteral("/v2/groups/%1/messages").arg(encodedSegment(target.id)));
    } else if (target.kind == QStringLiteral("c2c")) {
        url = qqApiUrl(QStringLiteral("/v2/users/%1/messages").arg(encodedSegment(target.id)));
    } else {
        qWarning(lcQQ) << "Skipping QQ outbound send with unsupported route kind:" << target.kind;
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("content"), text);

    const QString messageId = msg.metadata.value(QStringLiteral("message_id")).toString().trimmed();
    const QString eventId = msg.metadata.value(QStringLiteral("event_id")).toString().trimmed();
    if (!messageId.isEmpty()) {
        payload.insert(QStringLiteral("msg_id"), messageId);
    }
    if (!eventId.isEmpty()) {
        payload.insert(QStringLiteral("event_id"), eventId);
    }
    if (target.kind == QStringLiteral("group") || target.kind == QStringLiteral("c2c")) {
        payload.insert(QStringLiteral("msg_type"), 0);
        payload.insert(QStringLiteral("msg_seq"),
                       static_cast<double>(messageSeqValue(msg.metadata)));
    }

    bool ok = false;
    postJsonSync(url, payload, &ok, 15000);
    if (!ok) {
        qWarning(lcQQ) << "Failed to send QQ message to" << msg.chatId;
    }
}

} // namespace yaos::channels
