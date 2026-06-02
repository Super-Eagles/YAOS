#include "WhatsAppChannel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace yaos::channels {

WhatsAppChannel::WhatsAppChannel(const config::WhatsAppConfig &config, bus::MessageBus &bus,
                                 QObject *parent)
    : QObject(parent), _config(config), _bus(bus) {
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::connected, this, &WhatsAppChannel::onConnected);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::disconnected, this, &WhatsAppChannel::onDisconnected);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::textMessageReceived, this, &WhatsAppChannel::onTextMessageReceived);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::errorMessage, this, &WhatsAppChannel::onError);

    _reconnectTimer.setSingleShot(true);
    connect(&_reconnectTimer, &QTimer::timeout, this, &WhatsAppChannel::reconnect);
}

WhatsAppChannel::~WhatsAppChannel() {
    stop();
}

bool WhatsAppChannel::start() {
    if (!_config.enabled) return false;
    _running = true;
    reconnect();
    return true;
}

void WhatsAppChannel::stop() {
    _running = false;
    _webSocket.close();
}

void WhatsAppChannel::reconnect() {
    if (!_running) return;
    qInfo() << "Connecting to WhatsApp bridge at" << _config.bridgeUrl;
    _webSocket.open(QUrl(_config.bridgeUrl));
}

void WhatsAppChannel::onConnected() {
    _connected = true;
    qInfo() << "Connected to WhatsApp bridge";
    if (!_config.bridgeToken.isEmpty()) {
        QJsonObject auth;
        auth["type"] = "auth";
        auth["token"] = _config.bridgeToken;
        _webSocket.sendTextMessage(QString::fromUtf8(
            QJsonDocument(auth).toJson(QJsonDocument::Compact)
        ));
    }
}

void WhatsAppChannel::onDisconnected() {
    _connected = false;
    qWarning() << "Disconnected from WhatsApp bridge";
    if (_running) {
        _reconnectTimer.start(5000);
    }
}

void WhatsAppChannel::onTextMessageReceived(const QString &message) {
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isObject()) {
        handleBridgeMessage(doc.object());
    }
}

void WhatsAppChannel::handleBridgeMessage(const QJsonObject &data) {
    QString type = data.value("type").toString();
    
    if (type == "message") {
        QString sender = data.value("sender").toString();
        QString content = data.value("content").toString();
        QString id = data.value("id").toString();
        QString senderId = sender.contains("@") ? sender.split("@").at(0) : sender;

        if (!_config.allowFrom.isEmpty() &&
            !_config.allowFrom.contains("*") &&
            !_config.allowFrom.contains(senderId) &&
            !_config.allowFrom.contains(sender)) {
            return;
        }

        bus::InboundMessage inMsg;
        inMsg.channel = name();
        inMsg.chatId = sender; // Full ID for replies
        inMsg.senderId = senderId;
        inMsg.content = content;
        inMsg.metadata = QJsonObject{
            {"message_id", id},
            {"raw", data}
        };

        _bus.publishInbound(inMsg);
    } else if (type == "status") {
        QString status = data.value("status").toString();
        qInfo() << "WhatsApp status:" << status;
        _connected = (status == "connected");
    } else if (type == "qr") {
        qInfo() << "WhatsApp QR code received. Please scan via bridge terminal.";
    } else if (type == "error") {
        qWarning() << "WhatsApp bridge error:" << data.value("error").toString();
    }
}

void WhatsAppChannel::onError(const QString &error) {
    qWarning() << "WhatsApp bridge socket error:" << error;
}

void WhatsAppChannel::send(const bus::OutboundMessage &msg) {
    if (!_connected || !_webSocket.isValid()) return;

    QJsonObject payload;
    payload["type"] = "send";
    payload["to"] = msg.chatId;
    payload["text"] = msg.content;

    _webSocket.sendTextMessage(QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact)
    ));
}

} // namespace yaos::channels
