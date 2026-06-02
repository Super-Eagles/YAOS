#ifndef YAOS_DISTRIBUTED_P2PCLUSTER_H
#define YAOS_DISTRIBUTED_P2PCLUSTER_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QSharedPointer>
#include <QTimer>
#include <memory>

#include "Contracts.h"

namespace FastNet {
class TcpServer;
class UdpSocket;
}

namespace yaos::distributed {

enum class ClusterRole {
    SOLDIER,
    COMMANDER
};

class P2PCluster : public QObject, public INodeRegistryClient, public ITaskBus {
    Q_OBJECT
public:
    explicit P2PCluster(const NodeDescriptor& localNode, QObject *parent = nullptr);
    ~P2PCluster() override;

    void start();
    ClusterRole role() const;
    QString commanderId() const;

    // INodeRegistryClient
    QList<NodeDescriptor> listNodes() const override;
    bool publishPresence(const NodeDescriptor &node) override;

    // ITaskBus
    bool submit(const TaskEnvelope &task) override;
    bool publishResult(const TaskResultEnvelope &result) override;
    bool cancel(const QString &taskId) override;
    bool claim(const QString &taskId, const QString &consumerNode = QString()) override;
    QList<TaskEnvelope> pendingTasks(const QString &targetNode = QString(),
                                     const QString &targetRole = QString(),
                                     int limit = 100) const override;
    QList<TaskResultEnvelope> recentResults(const QString &taskId = QString(),
                                            const QString &traceId = QString(),
                                            int limit = 100) const override;

private slots:
    void onHeartbeatTimeout();
    void onElectionTimeout();
    void onStalePeerCleanup();

private:
    void handleUdpDatagram(const QByteArray &datagram);
    void handleTcpData(quint64 clientId, const QByteArray &chunk);
    void handleTcpDisconnected(quint64 clientId);
    void dispatchNetworkPayload(const QJsonObject &doc);
    void processHeartbeat(const QJsonObject& doc);
    void processTask(const QJsonObject& doc);
    void processResult(const QJsonObject& doc);
    void updateRole(ClusterRole newRole);
    void broadcast(const QJsonObject& payload);
    bool forwardToCommander(const TaskEnvelope &task);

private:
    NodeDescriptor _localNode;
    ClusterRole _role = ClusterRole::SOLDIER;
    QString _commanderId;

    std::unique_ptr<FastNet::UdpSocket> _udpSocket;
    std::unique_ptr<FastNet::TcpServer> _tcpServer;
    QTimer* _heartbeatTimer;
    QTimer* _electionTimer;
    QTimer* _cleanupTimer;

    mutable QMutex _mutex;
    QHash<quint64, QByteArray> _tcpBuffers;
    QHash<QString, NodeDescriptor> _peers;
    QHash<QString, qint64> _peerLastSeen;

    QList<TaskEnvelope> _localTaskQueue;
    QList<TaskEnvelope> _claimedTasks;
    QList<TaskResultEnvelope> _resultQueue;
};

// ── Proxy adapters ─────────────────────────────────────────────────────────
// Allows ownership-segregation: RuntimeCore holds unique_ptr<INodeRegistryClient>
// while both proxies share the same P2PCluster instance.

class P2PRegistryProxy : public INodeRegistryClient {
public:
    explicit P2PRegistryProxy(QSharedPointer<P2PCluster> cluster) : _cluster(std::move(cluster)) {}

    QList<NodeDescriptor> listNodes() const override {
        return _cluster->listNodes();
    }
    bool publishPresence(const NodeDescriptor &node) override {
        return _cluster->publishPresence(node);
    }

private:
    QSharedPointer<P2PCluster> _cluster;
};

class P2PTaskBusProxy : public ITaskBus {
public:
    explicit P2PTaskBusProxy(QSharedPointer<P2PCluster> cluster) : _cluster(std::move(cluster)) {}

    bool submit(const TaskEnvelope &task) override { return _cluster->submit(task); }
    bool publishResult(const TaskResultEnvelope &result) override { return _cluster->publishResult(result); }
    bool cancel(const QString &taskId) override { return _cluster->cancel(taskId); }
    bool claim(const QString &taskId, const QString &consumerNode = QString()) override {
        return _cluster->claim(taskId, consumerNode);
    }
    QList<TaskEnvelope> pendingTasks(const QString &targetNode = QString(),
                                     const QString &targetRole = QString(),
                                     int limit = 100) const override {
        return _cluster->pendingTasks(targetNode, targetRole, limit);
    }
    QList<TaskResultEnvelope> recentResults(const QString &taskId = QString(),
                                            const QString &traceId = QString(),
                                            int limit = 100) const override {
        return _cluster->recentResults(taskId, traceId, limit);
    }

private:
    QSharedPointer<P2PCluster> _cluster;
};

} // namespace yaos::distributed

#endif // YAOS_DISTRIBUTED_P2PCLUSTER_H
