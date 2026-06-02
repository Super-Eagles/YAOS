#ifndef YAOS_CHANNELS_TELEGRAMCHANNEL_H
#define YAOS_CHANNELS_TELEGRAMCHANNEL_H

#include "../platform/network/FastNetHttpTransport.h"

#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>

#include "../bus/MessageBus.h"
#include "../config/Config.h"
#include "Channel.h"

namespace yaos::channels {

class TelegramChannel : public QObject, public Channel {
    Q_OBJECT
public:
    TelegramChannel(const config::Config &appConfig,
                    const QString &workspace,
                    bus::MessageBus &bus,
                    QObject *parent = nullptr);

    QString name() const override;
    bool start() override;
    void stop() override;
    void send(const bus::OutboundMessage &msg) override;

private slots:
    // ✅ Long polling：上一次请求完成后立即发起下一次,无 Timer
    void schedulePoll();
    void onPollReply();

private:
    bool isAllowed(const QString &senderId) const;
    bool shouldTranscribeKind(const QString &kind) const;
    QString botApiUrl(const QString &method) const;
    QString fileApiUrl(const QString &filePath) const;
    QString defaultTranscriptionProvider() const;
    QString resolvedTranscriptionProvider() const;
    QString resolvedTranscriptionModel() const;
    const config::ProviderConfig *providerConfigFor(const QString &providerName) const;
    QJsonObject postJsonSync(const QString &method, const QJsonObject &payload,
                             bool *ok = nullptr, int timeoutMs = 15000);
    QString downloadTelegramFile(const QString &fileId,
                                 const QString &kind,
                                 const QString &chatId,
                                 const QString &messageId,
                                 const QString &preferredName,
                                 const QString &fallbackExtension,
                                 QJsonObject *mediaMetadata = nullptr);
    QJsonArray extractMedia(const QJsonObject &message,
                            const QString &chatId,
                            const QString &messageId,
                            QStringList *downloadedFiles);
    QStringList transcribeMedia(QJsonArray *mediaMetadata) const;
    void configureProxy();
    void processUpdate(const QJsonObject &update);

    config::Config _appConfig;
    config::TelegramConfig _config;
    QString _workspace;
    bus::MessageBus &_bus;
    QPointer<QFutureWatcher<platform::network::HttpResponse>> _pendingPoll;
    bool _running = false;
    qint64 _offset = 0;
};

} // namespace yaos::channels

#endif // YAOS_CHANNELS_TELEGRAMCHANNEL_H
