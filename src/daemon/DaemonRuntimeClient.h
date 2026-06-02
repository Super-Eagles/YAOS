#ifndef YAOS_DAEMON_DAEMONRUNTIMECLIENT_H
#define YAOS_DAEMON_DAEMONRUNTIMECLIENT_H

#include <QByteArray>
#include <QJsonObject>
#include <utility>

#include "../config/Config.h"
#include "../distributed/Contracts.h"

namespace yaos::daemon {

class DaemonRuntimeClient : public distributed::IRuntimeClient {
public:
    explicit DaemonRuntimeClient(config::Config config);
    ~DaemonRuntimeClient() override = default;

    bool ensureReady(QString *error = nullptr);
    QJsonObject invoke(const QString &method, const QJsonObject &payload) override;

private:
    bool sendRequest(const QByteArray &request,
                     QByteArray *responseFrame,
                     QString *error,
                     bool allowSpawn) const;
    bool spawnDaemon(QString *error) const;
    QString resolvedServerName() const;

private:
    config::Config _config;
};

} // namespace yaos::daemon

#endif // YAOS_DAEMON_DAEMONRUNTIMECLIENT_H
