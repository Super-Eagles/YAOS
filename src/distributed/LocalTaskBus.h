#ifndef YAOS_DISTRIBUTED_LOCALTASKBUS_H
#define YAOS_DISTRIBUTED_LOCALTASKBUS_H

#include <QJsonObject>
#include <QMutex>
#include <QString>

#include "Contracts.h"

namespace yaos::distributed {

class LocalTaskBus : public ITaskBus {
public:
    explicit LocalTaskBus(const QString &workspace);

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
    QJsonObject health(int recentEventLimit = 12) const;

private:
    QString filePath() const;

private:
    QString _workspace;
    mutable QMutex _mutex;
};

} // namespace yaos::distributed

#endif // YAOS_DISTRIBUTED_LOCALTASKBUS_H
