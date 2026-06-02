#ifndef YAOS_RUNTIME_REMOTERUNTIMECLIENT_H
#define YAOS_RUNTIME_REMOTERUNTIMECLIENT_H

#include <QJsonObject>
#include <QString>

#include "../config/Config.h"
#include "../distributed/Contracts.h"

namespace yaos::runtime {

class RemoteRuntimeClient : public distributed::IRuntimeClient {
public:
    explicit RemoteRuntimeClient(config::Config config,
                                 int timeoutMs = 6000);
    ~RemoteRuntimeClient() override = default;

    bool ensureReady(QString *error = nullptr) const;
    QString endpoint() const;
    QJsonObject invoke(const QString &method, const QJsonObject &payload) override;

private:
    QJsonObject get(const QString &path, QString *error = nullptr) const;
    QJsonObject post(const QString &path, const QJsonObject &payload, QString *error = nullptr) const;
    bool ping(QString *error = nullptr) const;
    QString buildUrl(const QString &path) const;
    QJsonObject request(const QString &method,
                        const QString &path,
                        const QJsonObject *payload,
                        QString *error) const;

private:
    config::Config _config;
    QString _endpoint;
    int _timeoutMs = 6000;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_REMOTERUNTIMECLIENT_H
