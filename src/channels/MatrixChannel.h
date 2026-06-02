#ifndef YAOS_CHANNELS_MATRIXCHANNEL_H
#define YAOS_CHANNELS_MATRIXCHANNEL_H

#include "../platform/network/FastNetHttpTransport.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include "../bus/MessageBus.h"
#include "../config/Config.h"
#include "Channel.h"

namespace yaos::channels {

class MatrixChannel : public QObject, public Channel {
    Q_OBJECT
public:
    MatrixChannel(const config::MatrixConfig &config,
                  bus::MessageBus &bus,
                  QObject *parent = nullptr);

    QString name() const override;
    bool start() override;
    void stop() override;
    void send(const bus::OutboundMessage &msg) override;

private slots:
    void scheduleSync();
    void onSyncReply();

private:
    bool isAllowed(const QString &senderId) const;
    QString clientApiBase() const;
    QMap<QByteArray, QByteArray> authorizedHeaders() const;
    void processSyncResponse(const QJsonObject &root);
    void processRoomEvent(const QString &roomId, const QJsonObject &event);

    config::MatrixConfig _config;
    bus::MessageBus &_bus;
    QPointer<QFutureWatcher<platform::network::HttpResponse>> _pendingSync;
    bool _running = false;
    bool _initialized = false;
    QString _since;
};

} // namespace yaos::channels

#endif // YAOS_CHANNELS_MATRIXCHANNEL_H
