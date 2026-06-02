#ifndef YAOS_DISTRIBUTED_CONTRACTS_H
#define YAOS_DISTRIBUTED_CONTRACTS_H

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace yaos::distributed {

struct NodeCapability {
    QString name;
    QString version;
    QStringList roles;
    QStringList tools;
    QStringList channels;
    QStringList memoryBackends;
    int maxConcurrency = 4;
    bool supportsDelegation = true;
    bool supportsStreaming = true;
};

struct NodeDescriptor {
    QString nodeId;
    QString clusterId = "local";
    QString displayName;
    QString role = "general";
    QString endpoint;
    QString runtimeMode = "embedded";
    QStringList tags;
    QList<NodeCapability> capabilities;
    int activeTaskCount = 0;
    int queuedTaskCount = 0;
    int maxConcurrencyHint = 4;
    int weight = 100;
    bool online = true;
    bool endpointProbeSupported = false;
    bool endpointHealthChecked = false;
    bool endpointReachable = false;
    QString endpointHealthError;
};

struct TaskContextRef {
    QString store;
    QString key;
    QString kind;
    QString summary;
};

struct TaskEnvelope {
    QString taskId;
    QString traceId;
    QString parentTaskId;
    QString originNode;
    QString targetNode;
    QString targetRole;
    QStringList targetTags;
    QString requiredTool;
    QString requiredChannel;
    QString requiredMemoryBackend;
    QString sceneKey;
    QString taskType;
    QString replyTo;
    int priority = 1;
    qint64 deadlineEpochMs = 0;
    QStringList labels;
    QJsonObject payload;
    QList<TaskContextRef> contextRefs;
    QDateTime createdAt = QDateTime::currentDateTimeUtc();
};

struct TaskResultEnvelope {
    QString taskId;
    QString traceId;
    QString producerNode;
    QString status = "ok";
    QString message;
    QJsonObject output;
    QList<TaskContextRef> outputRefs;
    QJsonObject error;
    QDateTime finishedAt = QDateTime::currentDateTimeUtc();
};

class INodeRegistryClient {
public:
    virtual ~INodeRegistryClient() = default;

    virtual QList<NodeDescriptor> listNodes() const = 0;
    virtual bool publishPresence(const NodeDescriptor &node) = 0;
};

class ITaskBus {
public:
    virtual ~ITaskBus() = default;

    virtual bool submit(const TaskEnvelope &task) = 0;
    virtual bool publishResult(const TaskResultEnvelope &result) = 0;
    virtual bool cancel(const QString &taskId) = 0;
    virtual bool claim(const QString &taskId,
                       const QString &consumerNode = QString()) = 0;
    virtual QList<TaskEnvelope> pendingTasks(const QString &targetNode = QString(),
                                             const QString &targetRole = QString(),
                                             int limit = 100) const = 0;
    virtual QList<TaskResultEnvelope> recentResults(const QString &taskId = QString(),
                                                    const QString &traceId = QString(),
                                                    int limit = 100) const = 0;
};

class IRuntimeClient {
public:
    virtual ~IRuntimeClient() = default;

    virtual QJsonObject invoke(const QString &method, const QJsonObject &payload) = 0;
};

} // namespace yaos::distributed

#endif // YAOS_DISTRIBUTED_CONTRACTS_H
