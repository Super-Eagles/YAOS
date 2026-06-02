#ifndef YAOS_CHANNELS_EMAILCHANNEL_H
#define YAOS_CHANNELS_EMAILCHANNEL_H

#include <QTimer>
#include <QObject>

#include "../bus/MessageBus.h"
#include "../config/Config.h"
#include "Channel.h"

namespace yaos::channels {

class EmailChannel : public QObject, public Channel {
    Q_OBJECT
public:
    EmailChannel(const config::EmailConfig &config,
                 const QString &workspace,
                 bus::MessageBus &bus,
                 QObject *parent = nullptr);

    QString name() const override;
    bool start() override;
    void stop() override;
    void send(const bus::OutboundMessage &msg) override;

private slots:
    void pollInbox();

private:
    bool hasImapConfig() const;
    bool hasSmtpConfig() const;
    bool isAllowed(const QString &senderAddress) const;

    config::EmailConfig _config;
    QString _workspace;
    bus::MessageBus &_bus;
    QTimer _pollTimer;
    bool _running = false;
    bool _initialized = false;
    quint64 _lastSeenUid = 0;
};

} // namespace yaos::channels

#endif // YAOS_CHANNELS_EMAILCHANNEL_H
