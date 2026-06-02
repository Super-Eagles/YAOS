#ifndef YAOS_CHANNELS_CHANNELMANAGER_H
#define YAOS_CHANNELS_CHANNELMANAGER_H

#include <QHash>
#include <QSharedPointer>
#include <QStringList>

#include "../bus/MessageBus.h"
#include "../config/Config.h"
#include "Channel.h"

namespace yaos::channels {

class ChannelManager {
public:
    ChannelManager(const config::Config &config, bus::MessageBus &bus);

    void registerChannel(const QSharedPointer<Channel> &channel);
    bool has(const QString &name) const;
    QStringList enabledChannels() const;

    bool startAll();
    void stopAll();
    bool dispatch(const bus::OutboundMessage &msg);
    void handleOutbound(const bus::OutboundMessage &msg);

private:
    void initChannels();

private:
    config::Config _config;
    bus::MessageBus &_bus;
    QHash<QString, QSharedPointer<Channel>> _channels;
};

} // namespace yaos::channels

#endif // YAOS_CHANNELS_CHANNELMANAGER_H
