#ifndef YAOS_CONTROL_CONTROLSERVICECORE_H
#define YAOS_CONTROL_CONTROLSERVICECORE_H

#include <QDateTime>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <memory>
#include <QScopedPointer>

#include "../config/Config.h"
#include "../config/DelegationTemplateExchange.h"
#include "../distributed/Contracts.h"
#include "../distributed/P2PCluster.h"

namespace yaos::control {

class ControlServiceCore {
public:
    ControlServiceCore(const QString &workspace,
                       const config::Config &config);

    bool isReady() const;
    QString lastError() const;
    QString workspace() const;

    QList<distributed::NodeDescriptor> listNodes(bool onlineOnly = false,
                                                 const QString &clusterId = QString(),
                                                 int limit = 256) const;
    QList<distributed::NodeDescriptor> resolveNodes(const QString &clusterId,
                                                    const QString &role,
                                                    const QStringList &tags,
                                                    const QString &tool,
                                                    const QString &channel,
                                                    const QString &memoryBackend,
                                                    int limit = 8) const;
    bool publishPresence(const distributed::NodeDescriptor &node,
                         QString *error = nullptr);
    int refreshNodeHealth(bool force = false,
                          int limit = 64) const;

    bool submitTask(const distributed::TaskEnvelope &task,
                    QString *error = nullptr);
    QList<distributed::TaskEnvelope> pendingTasks(const QString &targetNode = QString(),
                                                  const QString &targetRole = QString(),
                                                  int limit = 100) const;
    bool claimTask(const QString &taskId,
                   const QString &consumerNode = QString(),
                   QString *error = nullptr);
    bool publishResult(const distributed::TaskResultEnvelope &result,
                       QString *error = nullptr);
    QList<distributed::TaskResultEnvelope> recentResults(const QString &taskId = QString(),
                                                         const QString &traceId = QString(),
                                                         int limit = 100) const;
    bool cancelTask(const QString &taskId,
                    QString *error = nullptr);
    QList<config::DelegationTemplateConfig> listDelegationTemplates(int limit = 512) const;
    bool syncDelegationTemplates(const QList<config::DelegationTemplateConfig> &records,
                                 bool replaceExisting,
                                 QString *error = nullptr);

    QJsonObject health() const;

private:
    QString _workspace;
    config::Config _config;
    QScopedPointer<distributed::P2PCluster> _p2pCluster;
    distributed::INodeRegistryClient* _nodeRegistry = nullptr;
    distributed::ITaskBus* _taskBus = nullptr;
    QString _lastError;
    bool _ready = false;
    mutable QMutex _healthMutex;
    mutable QDateTime _lastHealthSweepAt;
};

} // namespace yaos::control

#endif // YAOS_CONTROL_CONTROLSERVICECORE_H
