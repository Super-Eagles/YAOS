#ifndef YAOS_CHANNELS_QQCHANNEL_H
#define YAOS_CHANNELS_QQCHANNEL_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include "../bus/MessageBus.h"
#include "../config/Config.h"
#include "../platform/network/FastNetWebSocketTransport.h"
#include "Channel.h"

namespace yaos::channels {

class QQChannel : public QObject, public Channel {
    Q_OBJECT
public:
    QQChannel(const config::QQConfig &config,
              bus::MessageBus &bus,
              QObject *parent = nullptr);
    ~QQChannel() override;

    QString name() const override;
    bool start() override;
    void stop() override;
    void send(const bus::OutboundMessage &msg) override;

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString &message);
    void onError(const QString &error);
    void reconnect();
    void sendHeartbeat();

private:
    struct RouteTarget {
        QString kind;
        QString id;
    };

    bool isAllowed(const QString &senderId) const;
    bool ensureAccessToken();
    QString fetchGatewayUrl();
    QJsonObject postJsonSync(const QUrl &url,
                             const QJsonObject &payload,
                             bool *ok = nullptr,
                             int timeoutMs = 15000);
    QJsonObject getJsonSync(const QUrl &url,
                            bool *ok = nullptr,
                            int timeoutMs = 15000);
    QMap<QByteArray, QByteArray> authorizedHeaders() const;
    QString accessTokenHeaderValue() const;
    RouteTarget routeForOutbound(const bus::OutboundMessage &msg) const;
    QString normalizedContent(QString content) const;
    QStringList extractMediaUrls(const QJsonArray &attachments) const;
    void publishInboundMessage(const QString &routeKind,
                               const QString &targetId,
                               const QString &senderId,
                               QString content,
                               const QJsonObject &payload,
                               const QJsonArray &attachments,
                               const QString &messageId);
    void handleDispatchEvent(const QString &eventType, const QJsonObject &data);
    void identify();
    void resume();

    config::QQConfig _config;
    bus::MessageBus &_bus;
    platform::network::FastNetWebSocketTransport _webSocket;
    QTimer _reconnectTimer;
    QTimer _heartbeatTimer;
    bool _running = false;
    QString _accessToken;
    QDateTime _accessTokenExpiresAt;
    QString _sessionId;
    qint64 _lastSeq = -1;
};

} // namespace yaos::channels

#endif // YAOS_CHANNELS_QQCHANNEL_H
