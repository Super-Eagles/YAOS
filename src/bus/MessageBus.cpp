#include "MessageBus.h"
#include <QElapsedTimer>

namespace yaos::bus {

MessageBus::MessageBus(QObject *parent)
    : QObject(parent) {
    qRegisterMetaType<InboundMessage>("yaos::bus::InboundMessage");
    qRegisterMetaType<OutboundMessage>("yaos::bus::OutboundMessage");
}

void MessageBus::publishInbound(const InboundMessage &msg) {
    {
        QMutexLocker lock(&_inboundMutex);
        _inbound.enqueue(msg);
        _inboundCond.wakeOne();
    }
    emit inboundPublished(msg);
}

void MessageBus::publishOutbound(const OutboundMessage &msg) {
    {
        QMutexLocker lock(&_outboundMutex);
        _outbound.enqueue(msg);
        _outboundCond.wakeOne();
    }
    emit outboundPublished(msg);
}

InboundMessage MessageBus::consumeInbound(int timeoutMs, bool *ok) {
    QMutexLocker lock(&_inboundMutex);
    if (_inbound.isEmpty()) {
        if (timeoutMs < 0) {
            while (_inbound.isEmpty()) {
                _inboundCond.wait(&_inboundMutex);
            }
        } else {
            QElapsedTimer timer;
            timer.start();
            while (_inbound.isEmpty()) {
                qint64 remaining = timeoutMs - timer.elapsed();
                if (remaining <= 0 || !_inboundCond.wait(&_inboundMutex, static_cast<unsigned long>(remaining))) {
                    break;
                }
            }
        }
    }
    if (_inbound.isEmpty()) {
        if (ok) *ok = false;
        return InboundMessage();
    }
    if (ok) *ok = true;
    return _inbound.dequeue();
}

OutboundMessage MessageBus::consumeOutbound(int timeoutMs, bool *ok) {
    QMutexLocker lock(&_outboundMutex);
    if (_outbound.isEmpty()) {
        if (timeoutMs < 0) {
            while (_outbound.isEmpty()) {
                _outboundCond.wait(&_outboundMutex);
            }
        } else {
            QElapsedTimer timer;
            timer.start();
            while (_outbound.isEmpty()) {
                qint64 remaining = timeoutMs - timer.elapsed();
                if (remaining <= 0 || !_outboundCond.wait(&_outboundMutex, static_cast<unsigned long>(remaining))) {
                    break;
                }
            }
        }
    }
    if (_outbound.isEmpty()) {
        if (ok) *ok = false;
        return OutboundMessage();
    }
    if (ok) *ok = true;
    return _outbound.dequeue();
}

int MessageBus::inboundSize() const {
    QMutexLocker lock(&_inboundMutex);
    return _inbound.size();
}

int MessageBus::outboundSize() const {
    QMutexLocker lock(&_outboundMutex);
    return _outbound.size();
}

} // namespace yaos::bus

