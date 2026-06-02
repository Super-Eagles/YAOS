#ifndef YAOS_CHANNELS_FEISHUCHANNEL_H
#define YAOS_CHANNELS_FEISHUCHANNEL_H

#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include "../bus/MessageBus.h"
#include "../config/Config.h"
#include "../platform/network/FastNetWebSocketTransport.h"
#include "Channel.h"

namespace yaos::channels {

class FeishuChannel : public QObject, public Channel {
    Q_OBJECT
public:
    FeishuChannel(const config::FeishuConfig &config, bus::MessageBus &bus,
                  QObject *parent = nullptr);
    ~FeishuChannel() override;

    QString name() const override { return "feishu"; }
    bool start() override;
    void stop() override;
    void send(const bus::OutboundMessage &msg) override;

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString &message);
    void onError(const QString &error);
    void reconnect();

private:
    void fetchAccessToken();
    void fetchWebSocketUrl();
    void handleEvent(const QJsonObject &event);
    void sendResponse(const QString &requestId, const QJsonObject &data);

    config::FeishuConfig _config;
    bus::MessageBus &_bus;
    platform::network::FastNetWebSocketTransport _webSocket;
    QTimer _reconnectTimer;
    
    QString _accessToken;
    bool _running = false;
    int _retryCount = 0;
};

} // namespace yaos::channels

#endif // YAOS_CHANNELS_FEISHUCHANNEL_H
