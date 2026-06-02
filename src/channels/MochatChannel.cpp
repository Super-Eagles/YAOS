#include "MochatChannel.h"

#include "ChannelHttp.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QUrl>

namespace yaos::channels {

MochatChannel::MochatChannel(const config::MochatConfig &config, bus::MessageBus &bus,
                             QObject *parent)
    : QObject(parent), _config(config), _bus(bus) {
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::connected, this, &MochatChannel::onConnected);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::disconnected, this, &MochatChannel::onDisconnected);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::textMessageReceived, this, &MochatChannel::onTextMessageReceived);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::errorMessage, this, &MochatChannel::onError);

    _reconnectTimer.setSingleShot(true);
    connect(&_reconnectTimer, &QTimer::timeout, this, &MochatChannel::reconnect);

    _pingTimer.setInterval(25000); // Typical Engine.io ping interval
    connect(&_pingTimer, &QTimer::timeout, this, &MochatChannel::sendPing);
}

MochatChannel::~MochatChannel() {
    stop();
}

bool MochatChannel::start() {
    if (!_config.enabled) return false;
    if (_config.clawToken.isEmpty()) {
        qWarning() << "Mochat clawToken is empty";
        return false;
    }
    _running = true;
    reconnect();
    return true;
}

void MochatChannel::stop() {
    _running = false;
    _ready = false;
    _pingTimer.stop();
    _webSocket.close();
}

void MochatChannel::reconnect() {
    if (!_running) return;
    
    QString socketUrl = _config.baseUrl;
    if (socketUrl.endsWith("/")) socketUrl.chop(1);
    
    QUrl url(socketUrl + "/socket.io/?EIO=4&transport=websocket");
    qInfo() << "Connecting to Mochat WebSocket:" << url;
    _webSocket.open(url);
}

void MochatChannel::onConnected() {
    qInfo() << "Mochat Engine.io connected, sending Socket.io connect...";
    // Engine.io 'open' is handled automatically by Socket.io logic if we were using a lib,
    // but here we wait for '0' message from server.
}

void MochatChannel::onDisconnected() {
    qWarning() << "Mochat WebSocket disconnected";
    _ready = false;
    _pingTimer.stop();
    if (_running) {
        _reconnectTimer.start(5000);
    }
}

void MochatChannel::onTextMessageReceived(const QString &message) {
    if (message.isEmpty()) return;

    // Handle Socket.io / Engine.io packet types
    QChar type = message.at(0);
    QString payload = message.mid(1);

    if (type == '0') {
        // Engine.io open
        qDebug() << "Engine.io Open:" << payload;
        // Send Socket.io connect with auth
        QJsonObject auth;
        auth["token"] = _config.clawToken;
        _webSocket.sendTextMessage("40" + QString::fromUtf8(
            QJsonDocument(auth).toJson(QJsonDocument::Compact)
        ));
    } else if (type == '2') {
        // Engine.io ping -> pong
        _webSocket.sendTextMessage("3" + payload);
    } else if (type == '4') {
        // Socket.io packet
        if (payload.startsWith("0")) {
            // Socket.io connect ack
            qInfo() << "Mochat Socket.io authenticated";
            _ready = true;
            _pingTimer.start();
            subscribeAll();
        } else if (payload.startsWith("2")) {
            // Socket.io event
            handleSocketIoMessage(payload.mid(1));
        }
    }
}

void MochatChannel::handleSocketIoMessage(const QString &message) {
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isArray()) return;

    QJsonArray arr = doc.array();
    if (arr.size() < 2) return;

    QString eventName = arr.at(0).toString();
    QJsonObject data = arr.at(1).toObject();

    handleEvent(eventName, data);
}

void MochatChannel::handleEvent(const QString &eventName, const QJsonObject &data) {
    if (eventName != "claw.session.events" && eventName != "claw.panel.events") return;

    QJsonArray events = data.value("events").toArray();
    QString sessionId = data.value("sessionId").toString();

    for (const QJsonValue &v : events) {
        QJsonObject evt = v.toObject();
        if (evt.value("type").toString() != "message.add") continue;

        QJsonObject payload = evt.value("payload").toObject();
        QString author = payload.value("author").toString();
        
        // Skip self
        if (!_config.agentUserId.isEmpty() && author == _config.agentUserId) continue;

        QString messageId = payload.value("messageId").toString();
        if (_seenMessageIds.contains(messageId)) continue;
        _seenMessageIds.insert(messageId);
        if (_seenMessageIds.size() > 1000) _seenMessageIds.clear(); // Simple dedup reset

        QString text = payload.value("content").toString();
        
        bus::InboundMessage inMsg;
        inMsg.channel = name();
        inMsg.chatId = sessionId; // Or panelId
        inMsg.senderId = author;
        inMsg.content = text;
        inMsg.metadata = QJsonObject{
            {"message_id", messageId},
            {"raw", evt}
        };

        _bus.publishInbound(inMsg);
    }
}

void MochatChannel::subscribeAll() {
    // Minimal subscription: auto-discover is usually handled by refresh loop,
    // but we can send a raw subscribe if we have IDs.
    // For now, assume it's like a bridge.
}

void MochatChannel::sendPing() {
    if (_webSocket.isValid()) {
        _webSocket.sendTextMessage("2"); // Engine.io ping
    }
}

void MochatChannel::onError(const QString &error) {
    qWarning() << "Mochat WebSocket error:" << error;
}

void MochatChannel::send(const bus::OutboundMessage &msg) {
    if (!_ready) return;

    // Call /api/claw/sessions/send or similar
    // Actually we can send via Socket.io if the API supports it, but the Python version uses HTTP for 'send'.
    // Let's use HTTP for send to match Python behavior exactly.
    
    QUrl url(_config.baseUrl + "/api/claw/sessions/send");

    QJsonObject body;
    body["sessionId"] = msg.chatId;
    body["content"] = msg.content;

    QMap<QByteArray, QByteArray> headers;
    headers.insert("Content-Type", "application/json; charset=utf-8");
    headers.insert("x-claw-token", _config.clawToken.toUtf8());
    http::sendDetached(http::makeRequest(QStringLiteral("POST"),
                                         url.toString(QUrl::FullyEncoded),
                                         headers,
                                         QJsonDocument(body).toJson(QJsonDocument::Compact),
                                         15000),
                       QStringLiteral("Failed to send Mochat message:"));
}

} // namespace yaos::channels
