#ifndef YAOS_BUS_MESSAGEBUS_H
#define YAOS_BUS_MESSAGEBUS_H

#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QWaitCondition>

#include "Message.h"

namespace yaos::bus {

class MessageBus : public QObject {
    Q_OBJECT
public:
    explicit MessageBus(QObject *parent = nullptr);

    void publishInbound(const InboundMessage &msg);
    void publishOutbound(const OutboundMessage &msg);

    InboundMessage consumeInbound(int timeoutMs = 1000, bool *ok = nullptr);
    OutboundMessage consumeOutbound(int timeoutMs = 1000, bool *ok = nullptr);

    int inboundSize() const;
    int outboundSize() const;

signals:
    void inboundPublished(const yaos::bus::InboundMessage &msg);
    void outboundPublished(const yaos::bus::OutboundMessage &msg);

private:
    mutable QMutex _inboundMutex;
    mutable QMutex _outboundMutex;
    QWaitCondition _inboundCond;
    QWaitCondition _outboundCond;
    QQueue<InboundMessage> _inbound;
    QQueue<OutboundMessage> _outbound;
};

} // namespace yaos::bus

#endif // YAOS_BUS_MESSAGEBUS_H

