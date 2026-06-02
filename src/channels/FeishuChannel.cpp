#include "FeishuChannel.h"

#include "ChannelHttp.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QUrl>

namespace yaos::channels {

FeishuChannel::FeishuChannel(const config::FeishuConfig &config, bus::MessageBus &bus,
                             QObject *parent)
    : QObject(parent), _config(config), _bus(bus) {
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::connected, this, &FeishuChannel::onConnected);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::disconnected, this, &FeishuChannel::onDisconnected);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::textMessageReceived, this, &FeishuChannel::onTextMessageReceived);
    connect(&_webSocket, &platform::network::FastNetWebSocketTransport::errorMessage, this, &FeishuChannel::onError);

    _reconnectTimer.setSingleShot(true);
    connect(&_reconnectTimer, &QTimer::timeout, this, &FeishuChannel::reconnect);
}

FeishuChannel::~FeishuChannel() {
    stop();
}

bool FeishuChannel::start() {
    if (!_config.enabled) return false;
    if (_config.appId.isEmpty() || _config.appSecret.isEmpty()) {
        qWarning() << "Feishu appId or appSecret is empty";
        return false;
    }

    _running = true;
    reconnect();
    return true;
}

void FeishuChannel::stop() {
    _running = false;
    _webSocket.close();
}

void FeishuChannel::reconnect() {
    if (!_running) return;

    fetchAccessToken();
    if (_accessToken.isEmpty()) {
        _reconnectTimer.start(5000);
        return;
    }

    fetchWebSocketUrl();
}

void FeishuChannel::fetchAccessToken() {
    QJsonObject body;
    body["app_id"] = _config.appId;
    body["app_secret"] = _config.appSecret;

    const platform::network::HttpResponse response =
        http::sendJson(QStringLiteral("POST"),
                       QStringLiteral("https://open-apis.feishu.cn/open-apis/auth/v3/app_access_token/internal"),
                       body,
                       {},
                       15000);
    bool parsed = false;
    const QJsonObject res = http::parseJsonObject(response.body, &parsed);
    if (response.ok() && parsed) {
        _accessToken = res.value("app_access_token").toString();
    } else {
        qWarning() << "Failed to fetch Feishu access token:" << response.error;
        _accessToken.clear();
    }
}

void FeishuChannel::fetchWebSocketUrl() {
    QMap<QByteArray, QByteArray> headers;
    headers.insert("Content-Type", "application/json; charset=utf-8");
    headers.insert("Authorization", ("Bearer " + _accessToken).toUtf8());
    const platform::network::HttpResponse response =
        http::sendBody(QStringLiteral("POST"),
                       QStringLiteral("https://open-apis.feishu.cn/open-apis/websockets/endpoint"),
                       QByteArray(),
                       headers,
                       15000);
    bool parsed = false;
    const QJsonObject res = http::parseJsonObject(response.body, &parsed);
    if (response.ok() && parsed) {
        QString url = res.value("url").toString();
        if (!url.isEmpty()) {
            qInfo() << "Connecting to Feishu WebSocket:" << url;
            _webSocket.open(QUrl(url));
        } else {
            qWarning() << "Feishu WebSocket URL is empty";
            _reconnectTimer.start(5000);
        }
    } else {
        qWarning() << "Failed to fetch Feishu WebSocket URL:" << response.error;
        _reconnectTimer.start(5000);
    }
}

void FeishuChannel::onConnected() {
    qInfo() << "Feishu WebSocket connected";
    _retryCount = 0;
}

void FeishuChannel::onDisconnected() {
    qWarning() << "Feishu WebSocket disconnected";
    if (_running) {
        _reconnectTimer.start(5000);
    }
}

void FeishuChannel::onTextMessageReceived(const QString &message) {
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();
    QString type = obj.value("type").toString();
    QString requestId = obj.value("request_id").toString();

    if (type == "hello") {
        // Hello message, no specific action needed but good to log
        qDebug() << "Received Feishu hello";
    } else if (type == "ping") {
        // Respond to ping
        QJsonObject pong;
        pong["type"] = "pong";
        _webSocket.sendTextMessage(QString::fromUtf8(QJsonDocument(pong).toJson()));
    } else if (type == "event") {
        handleEvent(obj.value("event").toObject());
        // Acknowledge the event
        sendResponse(requestId, QJsonObject());
    }
}

void FeishuChannel::handleEvent(const QJsonObject &event) {
    QString eventType = event.value("type").toString();
    if (eventType != "im.message.receive_v1") return;

    QJsonObject msgObj = event.value("message").toObject();
    QJsonObject senderObj = event.value("sender").toObject();

    QString messageId = msgObj.value("message_id").toString();
    QString chatId = msgObj.value("chat_id").toString();
    QString senderId = senderObj.value("sender_id").toObject().value("open_id").toString();
    QString contentStr = msgObj.value("content").toString();
    
    // Check allow list
    if (!_config.allowFrom.isEmpty() && !_config.allowFrom.contains(senderId)) {
        return;
    }

    QJsonObject content = QJsonDocument::fromJson(contentStr.toUtf8()).object();
    QString text = content.value("text").toString();

    // Remove @mentions if possible (simple version)
    text = text.trimmed();

    bus::InboundMessage inMsg;
    inMsg.channel = name();
    inMsg.chatId = chatId;
    inMsg.senderId = senderId;
    inMsg.content = text;
    inMsg.metadata = QJsonObject{
        {"message_id", messageId},
        {"raw", event}
    };

    _bus.publishInbound(inMsg);
}

void FeishuChannel::onError(const QString &error) {
    qWarning() << "Feishu WebSocket error:" << error;
}

void FeishuChannel::sendResponse(const QString &requestId, const QJsonObject &data) {
    QJsonObject res;
    res["request_id"] = requestId;
    res["data"] = data;
    _webSocket.sendTextMessage(QString::fromUtf8(QJsonDocument(res).toJson()));
}

void FeishuChannel::send(const bus::OutboundMessage &msg) {
    if (_accessToken.isEmpty()) return;

    QString receiveIdType = msg.chatId.startsWith("oc_") ? "chat_id" : "open_id";
    QUrl url(QString("https://open-apis.feishu.cn/open-apis/im/v1/messages?receive_id_type=%1").arg(receiveIdType));
    
    QJsonObject body;
    body["receive_id"] = msg.chatId;
    body["msg_type"] = "text";
    
    QJsonObject content;
    content["text"] = msg.content;
    body["content"] = QString::fromUtf8(
        QJsonDocument(content).toJson(QJsonDocument::Compact)
    );

    QMap<QByteArray, QByteArray> headers;
    headers.insert("Content-Type", "application/json; charset=utf-8");
    headers.insert("Authorization", ("Bearer " + _accessToken).toUtf8());
    http::sendDetached(http::makeRequest(QStringLiteral("POST"),
                                         url.toString(QUrl::FullyEncoded),
                                         headers,
                                         QJsonDocument(body).toJson(QJsonDocument::Compact),
                                         15000),
                       QStringLiteral("Failed to send Feishu message:"));
}

} // namespace yaos::channels
