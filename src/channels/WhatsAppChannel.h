#ifndef YAOS_CHANNELS_WHATSAPPCHANNEL_H
#define YAOS_CHANNELS_WHATSAPPCHANNEL_H

#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include "../bus/MessageBus.h"
#include "../config/Config.h"
#include "../platform/network/FastNetWebSocketTransport.h"
#include "Channel.h"

namespace yaos::channels {

class WhatsAppChannel : public QObject, public Channel {
    Q_OBJECT
public:
    WhatsAppChannel(const config::WhatsAppConfig &config, bus::MessageBus &bus,
                    QObject *parent = nullptr);
    ~WhatsAppChannel() override;

    QString name() const override { return "whatsapp"; }
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
    void handleBridgeMessage(const QJsonObject &data);

    config::WhatsAppConfig _config;
    bus::MessageBus &_bus;
    platform::network::FastNetWebSocketTransport _webSocket;
    QTimer _reconnectTimer;
    bool _running = false;
    bool _connected = false;
};

} // namespace yaos::channels

#endif // YAOS_CHANNELS_WHATSAPPCHANNEL_H
