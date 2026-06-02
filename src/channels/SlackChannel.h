#ifndef YAOS_CHANNELS_SLACKCHANNEL_H
#define YAOS_CHANNELS_SLACKCHANNEL_H

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>

#include "../bus/MessageBus.h"
#include "../config/Config.h"
#include "../platform/network/FastNetWebSocketTransport.h"
#include "Channel.h"

namespace yaos::channels {

class SlackChannel : public QObject, public Channel {
    Q_OBJECT
public:
    SlackChannel(const config::SlackConfig &config, bus::MessageBus &bus, QObject *parent = nullptr);

    QString name() const override;
    bool start() override;
    void stop() override;
    void send(const bus::OutboundMessage &msg) override;

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketTextMessage(const QString &message);

private:
    QJsonObject apiPost(const QString &endpoint, const QString &token, const QJsonObject &payload, bool *ok = nullptr, int timeoutMs = 15000);
    QJsonObject apiFormPost(const QString &endpoint, const QString &token, const QUrlQuery &payload, bool *ok = nullptr, int timeoutMs = 15000);
    bool postRawBytes(const QUrl &url, const QByteArray &data, int timeoutMs = 30000);
    bool uploadFiles(const bus::OutboundMessage &msg, const QString &threadTs, bool useThread);
    bool openSocketConnection();
    bool isAllowed(const QString &senderId, const QString &chatId, const QString &channelType) const;
    bool shouldRespondInChannel(const QString &eventType, const QString &text, const QString &chatId) const;
    QString stripBotMention(const QString &text) const;

private:
    config::SlackConfig _config;
    bus::MessageBus &_bus;
    platform::network::FastNetWebSocketTransport _socket;
    bool _running = false;
    QString _botUserId;
};

} // namespace yaos::channels

#endif // YAOS_CHANNELS_SLACKCHANNEL_H
