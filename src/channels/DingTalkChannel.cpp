#include "DingTalkChannel.h"

#include "ChannelHttp.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace yaos::channels {

DingTalkChannel::DingTalkChannel(const config::DingTalkConfig &config, bus::MessageBus &bus,
                                 QObject *parent)
    : QObject(parent), _config(config), _bus(bus) {
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::connected, this, &DingTalkChannel::onConnected);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::disconnected, this, &DingTalkChannel::onDisconnected);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::textMessageReceived, this, &DingTalkChannel::onTextMessageReceived);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::errorMessage, this, &DingTalkChannel::onError);

    _reconnectTimer.setSingleShot(true);
    connect(&_reconnectTimer, &QTimer::timeout, this, &DingTalkChannel::reconnect);
}

DingTalkChannel::~DingTalkChannel() {
    stop();
}

bool DingTalkChannel::start() {
    if (!_config.enabled) return false;
    _running = true;
    reconnect();
    return true;
}

void DingTalkChannel::stop() {
    _running = false;
    _webSocket.close();
}

void DingTalkChannel::reconnect() {
    if (!_running) return;

    fetchAccessToken();
    if (_accessToken.isEmpty()) {
        _reconnectTimer.start(5000);
        return;
    }

    fetchWebSocketUrl();
}

void DingTalkChannel::fetchAccessToken() {
    QJsonObject body;
    body["appKey"] = _config.clientId;
    body["appSecret"] = _config.clientSecret;

    const platform::network::HttpResponse response =
        http::sendJson(QStringLiteral("POST"),
                       QStringLiteral("https://api.dingtalk.com/v1.0/oauth2/accessToken"),
                       body,
                       {},
                       15000);
    bool parsed = false;
    const QJsonObject res = http::parseJsonObject(response.body, &parsed);
    if (response.ok() && parsed) {
        _accessToken = res.value("accessToken").toString();
    } else {
        qWarning() << "Failed to fetch DingTalk access token:" << response.error;
        _accessToken.clear();
    }
}

void DingTalkChannel::fetchWebSocketUrl() {
    // DingTalk Stream gateway connection URL
    QJsonObject body;
    body["clientId"] = _config.clientId;
    body["clientSecret"] = _config.clientSecret;
    // body["subscriptions"] ... usually handled by SDK, but we might need topic registration

    QMap<QByteArray, QByteArray> headers;
    headers.insert("x-acs-dingtalk-access-token", _accessToken.toUtf8());
    const platform::network::HttpResponse response =
        http::sendJson(QStringLiteral("POST"),
                       QStringLiteral("https://api.dingtalk.com/v1.0/gateway/connections/open"),
                       body,
                       headers,
                       15000);
    bool parsed = false;
    const QJsonObject res = http::parseJsonObject(response.body, &parsed);
    if (response.ok() && parsed) {
        QString url = res.value("endpoint").toString();
        if (!url.isEmpty()) {
            qInfo() << "Connecting to DingTalk Stream:" << url;
            _webSocket.open(QUrl(url));
        } else {
            qWarning() << "DingTalk endpoint is empty";
            _reconnectTimer.start(5000);
        }
    } else {
        qWarning() << "Failed to fetch DingTalk endpoint:" << response.error;
        _reconnectTimer.start(5000);
    }
}

void DingTalkChannel::onConnected() {
    qInfo() << "DingTalk Stream connected";
}

void DingTalkChannel::onDisconnected() {
    qWarning() << "DingTalk Stream disconnected";
    if (_running) {
        _reconnectTimer.start(5000);
    }
}

void DingTalkChannel::onTextMessageReceived(const QString &message) {
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();
    QString type = obj.value("type").toString();
    
    if (type == "SYSTEM") {
        // System message, e.g., connect successfully
    } else if (type == "EVENT" || type == "CALLBACK") {
        handleEvent(obj);
        sendAck(obj.value("messageId").toString());
    }
}

void DingTalkChannel::handleEvent(const QJsonObject &event) {
    QJsonObject data = event.value("data").toObject();
    if (data.isEmpty() && event.value("data").isString()) {
        data = QJsonDocument::fromJson(event.value("data").toString().toUtf8()).object();
    }
    
    // We mainly care about chatbot messages
    QString text = data.value("text").toObject().value("content").toString().trimmed();
    QString senderId = data.value("senderStaffId").toString();
    if (senderId.isEmpty()) senderId = data.value("senderId").toString();
    
    QString conversationId = data.value("openConversationId").toString();
    if (conversationId.isEmpty()) conversationId = data.value("conversationId").toString();
    
    bool isGroup = data.value("conversationType").toString() == "2";
    QString chatId = isGroup ? ("group:" + conversationId) : senderId;

    if (!_config.allowFrom.isEmpty() && !_config.allowFrom.contains(senderId)) {
        return;
    }

    bus::InboundMessage inMsg;
    inMsg.channel = name();
    inMsg.chatId = chatId;
    inMsg.senderId = senderId;
    inMsg.content = text;
    inMsg.metadata = QJsonObject{
        {"is_group", isGroup},
        {"raw", event}
    };

    _bus.publishInbound(inMsg);
}

void DingTalkChannel::onError(const QString &error) {
    qWarning() << "DingTalk Stream socket error:" << error;
}

void DingTalkChannel::sendAck(const QString &messageId) {
    QJsonObject ack;
    ack["type"] = "ACK";
    ack["messageId"] = messageId;
    _webSocket.sendTextMessage(QString::fromUtf8(
        QJsonDocument(ack).toJson(QJsonDocument::Compact)
    ));
}

void DingTalkChannel::send(const bus::OutboundMessage &msg) {
    if (_accessToken.isEmpty()) return;

    QString url;
    QJsonObject body;
    
    if (msg.chatId.startsWith("group:")) {
        url = "https://api.dingtalk.com/v1.0/robot/groupMessages/send";
        body["openConversationId"] = msg.chatId.mid(6);
    } else {
        url = "https://api.dingtalk.com/v1.0/robot/oToMessages/batchSend";
        QJsonArray userIds;
        userIds.append(msg.chatId);
        body["userIds"] = userIds;
    }

    body["robotCode"] = _config.clientId;
    body["msgKey"] = "sampleMarkdown";
    
    QJsonObject msgParam;
    msgParam["title"] = "YAOS Reply";
    msgParam["text"] = msg.content;
    body["msgParam"] = QString::fromUtf8(
        QJsonDocument(msgParam).toJson(QJsonDocument::Compact)
    );

    QMap<QByteArray, QByteArray> headers;
    headers.insert("Content-Type", "application/json; charset=utf-8");
    headers.insert("x-acs-dingtalk-access-token", _accessToken.toUtf8());
    http::sendDetached(http::makeRequest(QStringLiteral("POST"),
                                         url,
                                         headers,
                                         QJsonDocument(body).toJson(QJsonDocument::Compact),
                                         15000),
                       QStringLiteral("Failed to send DingTalk message:"));
}

} // namespace yaos::channels
