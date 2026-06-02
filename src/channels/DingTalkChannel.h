#ifndef YAOS_CHANNELS_DINGTALKCHANNEL_H
#define YAOS_CHANNELS_DINGTALKCHANNEL_H

#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include "../bus/MessageBus.h"
#include "../config/Config.h"
#include "../platform/network/FastNetWebSocketTransport.h"
#include "Channel.h"

namespace yaos::channels {

class DingTalkChannel : public QObject, public Channel {
    Q_OBJECT
public:
    DingTalkChannel(const config::DingTalkConfig &config, bus::MessageBus &bus,
                    QObject *parent = nullptr);
    ~DingTalkChannel() override;

    QString name() const override { return "dingtalk"; }
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
    void sendAck(const QString &messageId);

    config::DingTalkConfig _config;
    bus::MessageBus &_bus;
    platform::network::FastNetWebSocketTransport _webSocket;
    QTimer _reconnectTimer;
    
    QString _accessToken;
    bool _running = false;
};

} // namespace yaos::channels

#endif // YAOS_CHANNELS_DINGTALKCHANNEL_H
