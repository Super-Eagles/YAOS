#ifndef YAOS_DISTRIBUTED_REMOTETASKBUS_H
#define YAOS_DISTRIBUTED_REMOTETASKBUS_H

#include "Contracts.h"
#include "RemoteControlClient.h"

namespace yaos::distributed {

class RemoteTaskBus : public ITaskBus {
public:
    explicit RemoteTaskBus(QString endpoint,
                           int timeoutMs = 3500);

    bool submit(const TaskEnvelope &task) override;
    bool publishResult(const TaskResultEnvelope &result) override;
    bool cancel(const QString &taskId) override;
    bool claim(const QString &taskId,
               const QString &consumerNode = QString()) override;
    QList<TaskEnvelope> pendingTasks(const QString &targetNode = QString(),
                                     const QString &targetRole = QString(),
                                     int limit = 100) const override;
    QList<TaskResultEnvelope> recentResults(const QString &taskId = QString(),
                                            const QString &traceId = QString(),
                                            int limit = 100) const override;

    bool isReady() const;
    bool ping(QString *error = nullptr) const;
    QString endpoint() const;

private:
    RemoteControlClient _client;
};

} // namespace yaos::distributed

#endif // YAOS_DISTRIBUTED_REMOTETASKBUS_H
