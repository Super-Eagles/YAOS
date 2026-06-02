#ifndef YAOS_BUS_MESSAGE_H
#define YAOS_BUS_MESSAGE_H

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace yaos::bus {

struct InboundMessage {
    QString channel;
    QString senderId;
    QString chatId;
    QString content;
    QDateTime timestamp = QDateTime::currentDateTime();
    QStringList media;
    QJsonObject metadata;
    QString sessionKeyOverride;

    QString sessionKey() const {
        if (!sessionKeyOverride.isEmpty()) {
            return sessionKeyOverride;
        }
        return channel + ":" + chatId;
    }
};

struct OutboundMessage {
    QString channel;
    QString chatId;
    QString content;
    QString replyTo;
    QStringList media;
    QJsonObject metadata;
};

} // namespace yaos::bus

Q_DECLARE_METATYPE(yaos::bus::InboundMessage)
Q_DECLARE_METATYPE(yaos::bus::OutboundMessage)

#endif // YAOS_BUS_MESSAGE_H

