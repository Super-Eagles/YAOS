#ifndef YAOS_CHANNELS_DISCORDCHANNEL_H
#define YAOS_CHANNELS_DISCORDCHANNEL_H

#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include "../bus/MessageBus.h"
#include "../config/Config.h"
#include "../platform/network/FastNetWebSocketTransport.h"
#include "Channel.h"

namespace yaos::channels {

class DiscordChannel : public QObject, public Channel {
    Q_OBJECT
public:
    DiscordChannel(const config::DiscordConfig &config, bus::MessageBus &bus,
                   QObject *parent = nullptr);
    ~DiscordChannel() override;

    QString name() const override { return "discord"; }
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
    void identify();
    void handleEvent(const QString &eventType, const QJsonObject &data);

    config::DiscordConfig _config;
    bus::MessageBus &_bus;
    platform::network::FastNetWebSocketTransport _webSocket;
    QTimer _reconnectTimer;
    QTimer _heartbeatTimer;
    
    bool _running = false;
    int _lastSeq = -1;
    QString _botUserId;
};

} // namespace yaos::channels

#endif // YAOS_CHANNELS_DISCORDCHANNEL_H
