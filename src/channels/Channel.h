#ifndef YAOS_CHANNELS_CHANNEL_H
#define YAOS_CHANNELS_CHANNEL_H

#include <QString>

#include "../bus/Message.h"

namespace yaos::channels {

class Channel {
public:
    virtual ~Channel() = default;
    virtual QString name() const = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void send(const bus::OutboundMessage &msg) = 0;
};

} // namespace yaos::channels

#endif // YAOS_CHANNELS_CHANNEL_H

