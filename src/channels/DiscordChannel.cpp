#include "DiscordChannel.h"

#include "ChannelHttp.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QUrl>

namespace yaos::channels {

namespace {

QString discordPlatformName() {
#if defined(Q_OS_WIN)
    return "windows";
#elif defined(Q_OS_MACOS)
    return "macos";
#else
    return "linux";
#endif
}

} // namespace

DiscordChannel::DiscordChannel(const config::DiscordConfig &config, bus::MessageBus &bus,
                               QObject *parent)
    : QObject(parent), _config(config), _bus(bus) {
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::connected, this, &DiscordChannel::onConnected);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::disconnected, this, &DiscordChannel::onDisconnected);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::textMessageReceived, this, &DiscordChannel::onTextMessageReceived);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::errorMessage, this, &DiscordChannel::onError);

    _reconnectTimer.setSingleShot(true);
    connect(&_reconnectTimer, &QTimer::timeout, this, &DiscordChannel::reconnect);

    connect(&_heartbeatTimer, &QTimer::timeout, this, &DiscordChannel::sendHeartbeat);
}

DiscordChannel::~DiscordChannel() {
    stop();
}

bool DiscordChannel::start() {
    if (!_config.enabled) return false;
    if (_config.token.isEmpty()) {
        qWarning() << "Discord token is empty";
        return false;
    }
    _running = true;
    reconnect();
    return true;
}

void DiscordChannel::stop() {
    _running = false;
    _heartbeatTimer.stop();
    _webSocket.close();
}

void DiscordChannel::reconnect() {
    if (!_running) return;
    
    QString gatewayUrl = "wss://gateway.discord.gg/?v=10&encoding=json"; // Default
    qInfo() << "Connecting to Discord gateway:" << gatewayUrl;
    _webSocket.open(QUrl(gatewayUrl));
}

void DiscordChannel::onConnected() {
    qInfo() << "Discord gateway connected";
}

void DiscordChannel::onDisconnected() {
    qWarning() << "Discord gateway disconnected";
    _heartbeatTimer.stop();
    if (_running) {
        _reconnectTimer.start(5000);
    }
}

void DiscordChannel::onTextMessageReceived(const QString &message) {
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();
    int op = obj.value("op").toInt();
    int s = obj.value("s").toInt(-1);
    if (s != -1) _lastSeq = s;

    QJsonObject d = obj.value("d").toObject();

    if (op == 10) { // HELLO
        int interval = d.value("heartbeat_interval").toInt();
        _heartbeatTimer.start(interval);
        identify();
    } else if (op == 0) { // DISPATCH
        QString t = obj.value("t").toString();
        handleEvent(t, d);
    } else if (op == 1) { // Heartbeat request
        sendHeartbeat();
    } else if (op == 7 || op == 9) { // Reconnect or Invalid Session
        _webSocket.close();
    }
}

void DiscordChannel::identify() {
    QJsonObject identify;
    identify["op"] = 2;
    
    QJsonObject d;
    d["token"] = _config.token;
    d["intents"] = 33280; // Default intents (GUILD_MESSAGES | DIRECT_MESSAGES | MESSAGE_CONTENT)
    
    QJsonObject properties;
    properties["$os"] = discordPlatformName();
    properties["$browser"] = "yaos";
    properties["$device"] = "yaos";
    d["properties"] = properties;
    
    identify["d"] = d;

    _webSocket.sendTextMessage(QString::fromUtf8(
        QJsonDocument(identify).toJson(QJsonDocument::Compact)
    ));
}

void DiscordChannel::sendHeartbeat() {
    QJsonObject heartbeat;
    heartbeat["op"] = 1;
    if (_lastSeq != -1) heartbeat["d"] = _lastSeq;
    else heartbeat["d"] = QJsonValue::Null;
    
    _webSocket.sendTextMessage(QString::fromUtf8(
        QJsonDocument(heartbeat).toJson(QJsonDocument::Compact)
    ));
}

void DiscordChannel::handleEvent(const QString &eventType, const QJsonObject &data) {
    if (eventType == "READY") {
        _botUserId = data.value("user").toObject().value("id").toString();
        qInfo() << "Discord bot READY as" << _botUserId;
    } else if (eventType == "MESSAGE_CREATE") {
        QJsonObject author = data.value("author").toObject();
        if (author.value("bot").toBool()) return;

        QString senderId = author.value("id").toString();
        QString channelId = data.value("channel_id").toString();
        QString content = data.value("content").toString();

        if (!_config.allowFrom.isEmpty() && !_config.allowFrom.contains(senderId)) {
            return;
        }

        bus::InboundMessage inMsg;
        inMsg.channel = name();
        inMsg.chatId = channelId;
        inMsg.senderId = senderId;
        inMsg.content = content;
        inMsg.metadata = QJsonObject{{"raw", data}};

        _bus.publishInbound(inMsg);
    }
}

void DiscordChannel::onError(const QString &error) {
    qWarning() << "Discord gateway socket error:" << error;
}

void DiscordChannel::send(const bus::OutboundMessage &msg) {
    // Discord send is via REST API
    QUrl url(QString("https://discord.com/api/v10/channels/%1/messages").arg(msg.chatId));
    QJsonObject body;
    body["content"] = msg.content;

    QMap<QByteArray, QByteArray> headers;
    headers.insert("Content-Type", "application/json; charset=utf-8");
    headers.insert("Authorization", ("Bot " + _config.token).toUtf8());
    http::sendDetached(http::makeRequest(QStringLiteral("POST"),
                                         url.toString(QUrl::FullyEncoded),
                                         headers,
                                         QJsonDocument(body).toJson(QJsonDocument::Compact),
                                         15000),
                       QStringLiteral("Failed to send Discord message:"));
}

} // namespace yaos::channels
