#ifndef YAOS_CHANNELS_MOCHATCHANNEL_H
#define YAOS_CHANNELS_MOCHATCHANNEL_H

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QHash>
#include <QSet>

#include "../bus/MessageBus.h"
#include "../config/Config.h"
#include "../platform/network/FastNetWebSocketTransport.h"
#include "Channel.h"

namespace yaos::channels {

class MochatChannel : public QObject, public Channel {
    Q_OBJECT
public:
    MochatChannel(const config::MochatConfig &config, bus::MessageBus &bus,
                  QObject *parent = nullptr);
    ~MochatChannel() override;

    QString name() const override { return "mochat"; }
    bool start() override;
    void stop() override;
    void send(const bus::OutboundMessage &msg) override;

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString &message);
    void onError(const QString &error);
    void reconnect();
    void sendPing();

private:
    void handleSocketIoMessage(const QString &message);
    void handleEvent(const QString &eventName, const QJsonObject &data);
    void subscribeAll();

    config::MochatConfig _config;
    bus::MessageBus &_bus;
    platform::network::FastNetWebSocketTransport _webSocket;
    QTimer _reconnectTimer;
    QTimer _pingTimer;
    
    bool _running = false;
    bool _ready = false;
    
    QSet<QString> _seenMessageIds;
};

} // namespace yaos::channels

#endif // YAOS_CHANNELS_MOCHATCHANNEL_H
