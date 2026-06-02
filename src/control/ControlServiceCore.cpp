#include "ControlServiceCore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

#include "../runtime/RuntimeServiceSupport.h"

namespace yaos::control {

namespace {

constexpr int kNodeEndpointProbeTimeoutMs = 900;
constexpr int kResolveProbeBudget = 12;
constexpr int kHealthSweepIntervalMs = 15000;
constexpr int kHealthSnapshotFreshMs = 45000;
constexpr int kDefaultHealthSweepLimit = 64;

struct NodeHealthSnapshot {
    QString nodeId;
    QString endpoint;
    bool probeSupported = false;
    bool checked = false;
    bool reachable = false;
    QString error;
    QDateTime checkedAt;
};

QString nodeHealthStorePath(const QString &workspace) {
    return QDir(workspace).filePath(QStringLiteral("runtime/control_node_health.json"));
}

QJsonObject nodeHealthSnapshotToJson(const NodeHealthSnapshot &snapshot) {
    return QJsonObject{
        {"nodeId", snapshot.nodeId},
        {"endpoint", snapshot.endpoint},
        {"probeSupported", snapshot.probeSupported},
        {"checked", snapshot.checked},
        {"reachable", snapshot.reachable},
        {"error", snapshot.error},
        {"checkedAt", snapshot.checkedAt.toString(Qt::ISODate)}
    };
}

NodeHealthSnapshot nodeHealthSnapshotFromJson(const QJsonObject &obj) {
    NodeHealthSnapshot snapshot;
    snapshot.nodeId = obj.value(QStringLiteral("nodeId")).toString();
    snapshot.endpoint = obj.value(QStringLiteral("endpoint")).toString();
    snapshot.probeSupported =
        obj.value(QStringLiteral("probeSupported")).toBool(obj.value(QStringLiteral("probe_supported")).toBool(false));
    snapshot.checked = obj.value(QStringLiteral("checked")).toBool(false);
    snapshot.reachable = obj.value(QStringLiteral("reachable")).toBool(false);
    snapshot.error = obj.value(QStringLiteral("error")).toString();
    snapshot.checkedAt = QDateTime::fromString(obj.value(QStringLiteral("checkedAt")).toString(), Qt::ISODate);
    return snapshot;
}

QVector<NodeHealthSnapshot> loadNodeHealthSnapshots(const QString &path) {
    QVector<NodeHealthSnapshot> snapshots;
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return snapshots;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return snapshots;
    }

    const QJsonArray array = doc.object().value(QStringLiteral("nodes")).toArray();
    snapshots.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (item.isObject()) {
            snapshots.append(nodeHealthSnapshotFromJson(item.toObject()));
        }
    }
    return snapshots;
}

bool saveNodeHealthSnapshots(const QString &path, const QVector<NodeHealthSnapshot> &snapshots) {
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QJsonArray array;
    for (const NodeHealthSnapshot &snapshot : snapshots) {
        array.append(nodeHealthSnapshotToJson(snapshot));
    }

    QJsonObject root;
    root[QStringLiteral("nodes")] = array;
    root[QStringLiteral("updatedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    return file.commit();
}

bool nodeHealthSnapshotFresh(const NodeHealthSnapshot &snapshot, const QDateTime &now) {
    return snapshot.checkedAt.isValid() &&
           snapshot.checkedAt.msecsTo(now) >= 0 &&
           snapshot.checkedAt.msecsTo(now) <= kHealthSnapshotFreshMs;
}

NodeHealthSnapshot nodeHealthSnapshotFor(const distributed::NodeDescriptor &node) {
    NodeHealthSnapshot snapshot;
    snapshot.nodeId = node.nodeId;
    snapshot.endpoint = node.endpoint;
    snapshot.checkedAt = QDateTime::currentDateTimeUtc();

    const runtime::RuntimeServiceEndpointHealth health =
        runtime::runtimeServiceEndpointHealth(node.endpoint, kNodeEndpointProbeTimeoutMs, true);
    snapshot.probeSupported = health.probeSupported;
    snapshot.checked = health.checked;
    snapshot.reachable = health.reachable;
    snapshot.error = health.reachable ? QString() : health.error;
    return snapshot;
}

QHash<QString, NodeHealthSnapshot> nodeHealthSnapshotIndex(const QVector<NodeHealthSnapshot> &snapshots) {
    QHash<QString, NodeHealthSnapshot> index;
    index.reserve(snapshots.size());
    for (const NodeHealthSnapshot &snapshot : snapshots) {
        if (!snapshot.nodeId.trimmed().isEmpty()) {
            index.insert(snapshot.nodeId, snapshot);
        }
    }
    return index;
}

void upsertNodeHealthSnapshot(QVector<NodeHealthSnapshot> *snapshots,
                              const NodeHealthSnapshot &snapshot) {
    if (!snapshots || snapshot.nodeId.trimmed().isEmpty()) {
        return;
    }
    for (NodeHealthSnapshot &existing : *snapshots) {
        if (existing.nodeId == snapshot.nodeId) {
            existing = snapshot;
            return;
        }
    }
    snapshots->append(snapshot);
}

bool containsAllTags(const QStringList &nodeTags, const QStringList &requiredTags) {
    for (const QString &tag : requiredTags) {
        if (!nodeTags.contains(tag.trimmed(), Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

bool capabilitySupportsRole(const distributed::NodeCapability &capability, const QString &role) {
    return role.trimmed().isEmpty() || capability.roles.contains(role.trimmed(), Qt::CaseInsensitive);
}

bool capabilitySupportsTool(const distributed::NodeCapability &capability, const QString &tool) {
    return tool.trimmed().isEmpty() || capability.tools.contains(tool.trimmed(), Qt::CaseInsensitive);
}

bool capabilitySupportsChannel(const distributed::NodeCapability &capability, const QString &channel) {
    return channel.trimmed().isEmpty() || capability.channels.contains(channel.trimmed(), Qt::CaseInsensitive);
}

bool capabilitySupportsMemory(const distributed::NodeCapability &capability, const QString &memoryBackend) {
    return memoryBackend.trimmed().isEmpty() ||
           capability.memoryBackends.contains(memoryBackend.trimmed(), Qt::CaseInsensitive);
}

bool nodeMatches(const distributed::NodeDescriptor &node,
                 const QString &role,
                 const QStringList &tags,
                 const QString &tool,
                 const QString &channel,
                 const QString &memoryBackend) {
    const QString normalizedRole = role.trimmed();
    if (!normalizedRole.isEmpty() &&
        node.role.trimmed().compare(normalizedRole, Qt::CaseInsensitive) != 0) {
        bool anyCapabilityRole = false;
        for (const distributed::NodeCapability &capability : node.capabilities) {
            if (capabilitySupportsRole(capability, normalizedRole)) {
                anyCapabilityRole = true;
                break;
            }
        }
        if (!anyCapabilityRole) {
            return false;
        }
    }

    if (!containsAllTags(node.tags, tags)) {
        return false;
    }

    if (tool.trimmed().isEmpty() &&
        channel.trimmed().isEmpty() &&
        memoryBackend.trimmed().isEmpty()) {
        return true;
    }

    for (const distributed::NodeCapability &capability : node.capabilities) {
        if (capabilitySupportsTool(capability, tool) &&
            capabilitySupportsChannel(capability, channel) &&
            capabilitySupportsMemory(capability, memoryBackend)) {
            return true;
        }
    }
    return false;
}

int nodeDeclaredConcurrency(const distributed::NodeDescriptor &node) {
    int declared = node.maxConcurrencyHint > 0 ? node.maxConcurrencyHint : 0;
    for (const distributed::NodeCapability &capability : node.capabilities) {
        declared = std::max(declared, capability.maxConcurrency);
    }
    return std::max(1, declared);
}

bool nodeHasAvailableCapacity(const distributed::NodeDescriptor &node) {
    return node.activeTaskCount < nodeDeclaredConcurrency(node);
}

double nodeSchedulingPressure(const distributed::NodeDescriptor &node) {
    const double denominator = static_cast<double>(nodeDeclaredConcurrency(node));
    return (static_cast<double>(node.activeTaskCount) +
            static_cast<double>(node.queuedTaskCount) * 0.5) / denominator;
}

void applyNodeHealthSnapshot(distributed::NodeDescriptor *node,
                             const QHash<QString, NodeHealthSnapshot> &snapshots,
                             const QDateTime &now) {
    if (!node) {
        return;
    }

    node->endpointProbeSupported = false;
    node->endpointHealthChecked = false;
    node->endpointReachable = false;
    node->endpointHealthError.clear();

    if (!node->online || node->nodeId.trimmed().isEmpty()) {
        return;
    }

    const auto it = snapshots.constFind(node->nodeId);
    if (it == snapshots.constEnd()) {
        return;
    }

    const NodeHealthSnapshot &snapshot = it.value();
    if (snapshot.endpoint.trimmed() != node->endpoint.trimmed()) {
        return;
    }

    node->endpointProbeSupported = snapshot.probeSupported;
    if (!nodeHealthSnapshotFresh(snapshot, now)) {
        return;
    }

    node->endpointHealthChecked = snapshot.checked;
    node->endpointReachable = snapshot.checked && snapshot.reachable;
    node->endpointHealthError = (snapshot.checked && !snapshot.reachable) ? snapshot.error : QString();
}

void sortNodesForHealthSweep(QList<distributed::NodeDescriptor> *nodes) {
    if (!nodes) {
        return;
    }
    std::sort(nodes->begin(), nodes->end(), [](const distributed::NodeDescriptor &left,
                                               const distributed::NodeDescriptor &right) {
        if (left.online != right.online) {
            return left.online && !right.online;
        }
        if (left.weight != right.weight) {
            return left.weight > right.weight;
        }
        return left.nodeId < right.nodeId;
    });
}

int nodeReachabilityRank(const distributed::NodeDescriptor &node) {
    if (!node.endpointProbeSupported || !node.endpointHealthChecked) {
        return 1;
    }
    return node.endpointReachable ? 2 : 0;
}

QString delegationTemplateStorePath(const QString &workspace) {
    return QDir(workspace).filePath(QStringLiteral("runtime/delegation_profiles.json"));
}

QList<config::DelegationTemplateConfig> loadDelegationTemplates(const QString &workspace) {
    QFile file(delegationTemplateStorePath(workspace));
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {};
    }

    QList<config::DelegationTemplateConfig> records;
    QString error;
    if (!config::parseDelegationTemplateExchangeDocument(document, &records, &error)) {
        return {};
    }
    return records;
}

bool saveDelegationTemplates(const QString &workspace,
                             const QList<config::DelegationTemplateConfig> &records,
                             const config::Config &cfg,
                             QString *error) {
    if (error) {
        error->clear();
    }

    QSaveFile file(delegationTemplateStorePath(workspace));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = file.errorString().isEmpty()
                ? QStringLiteral("Failed to open delegation template store.")
                : file.errorString();
        }
        return false;
    }

    const QJsonDocument document(config::delegationTemplateExchangeEnvelope(records,
                                                                            QString(),
                                                                            QString(),
                                                                            cfg.deployment.clusterId));
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        if (error) {
            *error = file.errorString().isEmpty()
                ? QStringLiteral("Failed to write delegation template store.")
                : file.errorString();
        }
        return false;
    }

    if (!file.commit()) {
        if (error) {
            *error = file.errorString().isEmpty()
                ? QStringLiteral("Failed to finalize delegation template store.")
                : file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace

ControlServiceCore::ControlServiceCore(const QString &workspace,
                                       const config::Config &config)
    : _workspace(workspace.trimmed()),
      _config(config) {
    if (_workspace.isEmpty()) {
        _lastError = QStringLiteral("Workspace path is empty.");
        return;
    }

    // Initialize P2P Cluster Mesh replacing localized registry and bus
    distributed::NodeDescriptor localNode;
    localNode.nodeId = _config.deployment.nodeId.trimmed().isEmpty() ? "control-node" : _config.deployment.nodeId.trimmed();
    localNode.online = true;

    _p2pCluster.reset(new distributed::P2PCluster(localNode));
    _p2pCluster->start();

    _nodeRegistry = _p2pCluster.data();
    _taskBus      = _p2pCluster.data();
    
    _ready = true;
}

bool ControlServiceCore::isReady() const {
    return _ready;
}

QString ControlServiceCore::lastError() const {
    return _lastError;
}

QString ControlServiceCore::workspace() const {
    return _workspace;
}

int ControlServiceCore::refreshNodeHealth(bool force,
                                          int limit) const {
    if (!_ready || !_nodeRegistry) {
        return 0;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    {
        QMutexLocker locker(&_healthMutex);
        if (!force &&
            _lastHealthSweepAt.isValid() &&
            _lastHealthSweepAt.msecsTo(now) >= 0 &&
            _lastHealthSweepAt.msecsTo(now) < kHealthSweepIntervalMs) {
            return 0;
        }
    }

    const QString healthPath = nodeHealthStorePath(_workspace);
    QVector<NodeHealthSnapshot> existingSnapshots;
    {
        QMutexLocker locker(&_healthMutex);
        existingSnapshots = loadNodeHealthSnapshots(healthPath);
    }
    const QHash<QString, NodeHealthSnapshot> existingByNodeId =
        nodeHealthSnapshotIndex(existingSnapshots);

    QList<distributed::NodeDescriptor> nodes = _nodeRegistry->listNodes();
    sortNodesForHealthSweep(&nodes);

    const int maxProbes = limit > 0 ? limit : kDefaultHealthSweepLimit;
    int probesRemaining = maxProbes;
    int refreshed = 0;

    QVector<NodeHealthSnapshot> nextSnapshots;
    nextSnapshots.reserve(nodes.size());
    for (const distributed::NodeDescriptor &node : nodes) {
        if (node.nodeId.trimmed().isEmpty()) {
            continue;
        }

        NodeHealthSnapshot snapshot = existingByNodeId.value(node.nodeId);
        const QString previousEndpoint = snapshot.endpoint.trimmed();
        snapshot.nodeId = node.nodeId;
        snapshot.endpoint = node.endpoint;

        const bool endpointChanged = previousEndpoint != node.endpoint.trimmed();
        const bool fresh = !endpointChanged && nodeHealthSnapshotFresh(snapshot, now);

        if (!node.online || node.endpoint.trimmed().isEmpty()) {
            snapshot.probeSupported = false;
            snapshot.checked = false;
            snapshot.reachable = false;
            snapshot.error.clear();
            snapshot.checkedAt = QDateTime();
        } else if (probesRemaining > 0 && (force || endpointChanged || !fresh)) {
            snapshot = nodeHealthSnapshotFor(node);
            --probesRemaining;
            ++refreshed;
        } else if (endpointChanged) {
            snapshot.probeSupported = false;
            snapshot.checked = false;
            snapshot.reachable = false;
            snapshot.error.clear();
            snapshot.checkedAt = QDateTime();
        }

        nextSnapshots.append(snapshot);
    }

    {
        QMutexLocker locker(&_healthMutex);
        saveNodeHealthSnapshots(healthPath, nextSnapshots);
        _lastHealthSweepAt = now;
    }
    return refreshed;
}

QList<distributed::NodeDescriptor> ControlServiceCore::listNodes(bool onlineOnly,
                                                                 const QString &clusterId,
                                                                 int limit) const {
    if (!_ready || !_nodeRegistry) {
        return {};
    }

    refreshNodeHealth(false);

    const QString normalizedClusterId = clusterId.trimmed();
    const QList<distributed::NodeDescriptor> nodes = _nodeRegistry->listNodes();
    QVector<NodeHealthSnapshot> healthSnapshots;
    {
        QMutexLocker locker(&_healthMutex);
        healthSnapshots = loadNodeHealthSnapshots(nodeHealthStorePath(_workspace));
    }
    const QHash<QString, NodeHealthSnapshot> healthIndex = nodeHealthSnapshotIndex(healthSnapshots);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const int maxItems = limit > 0 ? limit : 256;

    QList<distributed::NodeDescriptor> filtered;
    filtered.reserve(qMin(maxItems, nodes.size()));
    for (const distributed::NodeDescriptor &item : nodes) {
        distributed::NodeDescriptor node = item;
        applyNodeHealthSnapshot(&node, healthIndex, now);
        if (onlineOnly && !node.online) {
            continue;
        }
        if (!normalizedClusterId.isEmpty() &&
            node.clusterId.trimmed().compare(normalizedClusterId, Qt::CaseInsensitive) != 0) {
            continue;
        }
        filtered.append(node);
        if (filtered.size() >= maxItems) {
            break;
        }
    }
    return filtered;
}

QList<distributed::NodeDescriptor> ControlServiceCore::resolveNodes(const QString &clusterId,
                                                                    const QString &role,
                                                                    const QStringList &tags,
                                                                    const QString &tool,
                                                                    const QString &channel,
                                                                    const QString &memoryBackend,
                                                                    int limit) const {
    const QList<distributed::NodeDescriptor> nodes = listNodes(true, clusterId, limit > 0 ? limit * 8 : 64);
    QList<distributed::NodeDescriptor> candidates;
    candidates.reserve(nodes.size());
    for (const distributed::NodeDescriptor &node : nodes) {
        if (nodeMatches(node, role, tags, tool, channel, memoryBackend)) {
            candidates.append(node);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const distributed::NodeDescriptor &left,
                                                       const distributed::NodeDescriptor &right) {
        const bool leftHasCapacity = nodeHasAvailableCapacity(left);
        const bool rightHasCapacity = nodeHasAvailableCapacity(right);
        if (leftHasCapacity != rightHasCapacity) {
            return leftHasCapacity && !rightHasCapacity;
        }
        const double leftPressure = nodeSchedulingPressure(left);
        const double rightPressure = nodeSchedulingPressure(right);
        if (std::abs(leftPressure - rightPressure) > 0.0001) {
            return leftPressure < rightPressure;
        }
        if (left.queuedTaskCount != right.queuedTaskCount) {
            return left.queuedTaskCount < right.queuedTaskCount;
        }
        if (left.weight != right.weight) {
            return left.weight > right.weight;
        }
        return left.nodeId < right.nodeId;
    });

    QVector<NodeHealthSnapshot> updatedSnapshots;
    updatedSnapshots.reserve(qMin(candidates.size(), kResolveProbeBudget));
    int probesUsed = 0;
    for (distributed::NodeDescriptor &node : candidates) {
        if (probesUsed >= kResolveProbeBudget || node.endpointHealthChecked) {
            continue;
        }

        const NodeHealthSnapshot snapshot = nodeHealthSnapshotFor(node);
        node.endpointProbeSupported = snapshot.probeSupported;
        node.endpointHealthChecked = snapshot.checked;
        node.endpointReachable = snapshot.checked && snapshot.reachable;
        node.endpointHealthError = (snapshot.checked && !snapshot.reachable) ? snapshot.error : QString();
        updatedSnapshots.append(snapshot);
        ++probesUsed;
    }

    if (!updatedSnapshots.isEmpty()) {
        QMutexLocker locker(&_healthMutex);
        QVector<NodeHealthSnapshot> persisted = loadNodeHealthSnapshots(nodeHealthStorePath(_workspace));
        for (const NodeHealthSnapshot &snapshot : updatedSnapshots) {
            upsertNodeHealthSnapshot(&persisted, snapshot);
        }
        saveNodeHealthSnapshots(nodeHealthStorePath(_workspace), persisted);
        _lastHealthSweepAt = QDateTime::currentDateTimeUtc();
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const distributed::NodeDescriptor &left,
                                                              const distributed::NodeDescriptor &right) {
        const int leftReachability = nodeReachabilityRank(left);
        const int rightReachability = nodeReachabilityRank(right);
        if (leftReachability != rightReachability) {
            return leftReachability > rightReachability;
        }
        return false;
    });

    const int maxItems = limit > 0 ? limit : 8;
    if (candidates.size() > maxItems) {
        candidates = candidates.mid(0, maxItems);
    }
    return candidates;
}

bool ControlServiceCore::publishPresence(const distributed::NodeDescriptor &node,
                                         QString *error) {
    if (error) {
        error->clear();
    }
    if (!_ready || !_nodeRegistry) {
        if (error) {
            *error = _lastError.isEmpty() ? QStringLiteral("Node registry unavailable.") : _lastError;
        }
        return false;
    }
    if (!_nodeRegistry->publishPresence(node)) {
        if (error) {
            *error = QStringLiteral("Failed to publish node presence.");
        }
        return false;
    }
    return true;
}

bool ControlServiceCore::submitTask(const distributed::TaskEnvelope &task,
                                    QString *error) {
    if (error) {
        error->clear();
    }
    if (!_ready || !_taskBus) {
        if (error) {
            *error = _lastError.isEmpty() ? QStringLiteral("Task bus unavailable.") : _lastError;
        }
        return false;
    }
    if (!_taskBus->submit(task)) {
        if (error) {
            *error = QStringLiteral("Failed to submit task.");
        }
        return false;
    }
    return true;
}

QList<distributed::TaskEnvelope> ControlServiceCore::pendingTasks(const QString &targetNode,
                                                                  const QString &targetRole,
                                                                  int limit) const {
    if (!_ready || !_taskBus) {
        return {};
    }
    return _taskBus->pendingTasks(targetNode, targetRole, limit);
}

bool ControlServiceCore::claimTask(const QString &taskId,
                                   const QString &consumerNode,
                                   QString *error) {
    if (error) {
        error->clear();
    }
    if (!_ready || !_taskBus) {
        if (error) {
            *error = _lastError.isEmpty() ? QStringLiteral("Task bus unavailable.") : _lastError;
        }
        return false;
    }
    if (!_taskBus->claim(taskId, consumerNode)) {
        if (error) {
            *error = QStringLiteral("Failed to claim task.");
        }
        return false;
    }
    return true;
}

bool ControlServiceCore::publishResult(const distributed::TaskResultEnvelope &result,
                                       QString *error) {
    if (error) {
        error->clear();
    }
    if (!_ready || !_taskBus) {
        if (error) {
            *error = _lastError.isEmpty() ? QStringLiteral("Task bus unavailable.") : _lastError;
        }
        return false;
    }
    if (!_taskBus->publishResult(result)) {
        if (error) {
            *error = QStringLiteral("Failed to publish task result.");
        }
        return false;
    }
    return true;
}

QList<distributed::TaskResultEnvelope> ControlServiceCore::recentResults(const QString &taskId,
                                                                         const QString &traceId,
                                                                         int limit) const {
    if (!_ready || !_taskBus) {
        return {};
    }
    return _taskBus->recentResults(taskId, traceId, limit);
}

bool ControlServiceCore::cancelTask(const QString &taskId,
                                    QString *error) {
    if (error) {
        error->clear();
    }
    if (!_ready || !_taskBus) {
        if (error) {
            *error = _lastError.isEmpty() ? QStringLiteral("Task bus unavailable.") : _lastError;
        }
        return false;
    }
    if (!_taskBus->cancel(taskId)) {
        if (error) {
            *error = QStringLiteral("Failed to cancel task.");
        }
        return false;
    }
    return true;
}

QList<config::DelegationTemplateConfig> ControlServiceCore::listDelegationTemplates(int limit) const {
    if (!_ready) {
        return {};
    }

    QList<config::DelegationTemplateConfig> records = loadDelegationTemplates(_workspace);
    const int maxItems = limit > 0 ? limit : 512;
    if (records.size() > maxItems) {
        records = records.mid(0, maxItems);
    }
    return records;
}

bool ControlServiceCore::syncDelegationTemplates(const QList<config::DelegationTemplateConfig> &records,
                                                 bool replaceExisting,
                                                 QString *error) {
    if (error) {
        error->clear();
    }
    if (!_ready) {
        if (error) {
            *error = _lastError.isEmpty()
                ? QStringLiteral("Control service is unavailable.")
                : _lastError;
        }
        return false;
    }

    const QList<config::DelegationTemplateConfig> existing = loadDelegationTemplates(_workspace);
    const QList<config::DelegationTemplateConfig> merged =
        config::mergeDelegationTemplateRecords(existing, records, replaceExisting);
    return saveDelegationTemplates(_workspace, merged, _config, error);
}

QJsonObject ControlServiceCore::health() const {
    const QList<distributed::NodeDescriptor> nodes = listNodes(false, QString(), 512);
    int onlineNodes = 0;
    int checkedNodes = 0;
    int reachableNodes = 0;
    int unreachableNodes = 0;
    int uncheckedNodes = 0;
    for (const distributed::NodeDescriptor &node : nodes) {
        if (node.online) {
            ++onlineNodes;
        }
        if (!node.online) {
            continue;
        }
        if (!node.endpointProbeSupported || !node.endpointHealthChecked) {
            ++uncheckedNodes;
            continue;
        }
        ++checkedNodes;
        if (node.endpointReachable) {
            ++reachableNodes;
        } else {
            ++unreachableNodes;
        }
    }

    const int queuedTaskCount  = _taskBus ? _taskBus->pendingTasks(QString(), QString(), 1000).size() : 0;
    const int recentResultCount = _taskBus ? _taskBus->recentResults(QString(), QString(), 200).size() : 0;
    const QJsonObject taskBusHealth = QJsonObject{
        {"queuedTaskCount",  queuedTaskCount},
        {"leasedTaskCount",  0},
        {"recentResultCount", recentResultCount},
        {"expiredReclaimedCount", 0},
        {"staleSuppressedResultCount", 0}
    };
    const QList<config::DelegationTemplateConfig> templates = listDelegationTemplates(4096);
    QDateTime lastHealthSweepAt;
    {
        QMutexLocker locker(&_healthMutex);
        lastHealthSweepAt = _lastHealthSweepAt;
    }

    return QJsonObject{
        {"ok", _ready},
        {"workspace", _workspace},
        {"clusterId", _config.deployment.clusterId},
        {"nodeCount", nodes.size()},
        {"onlineNodeCount", onlineNodes},
        {"healthCheckedNodeCount", checkedNodes},
        {"reachableNodeCount", reachableNodes},
        {"unreachableNodeCount", unreachableNodes},
        {"uncheckedNodeCount", uncheckedNodes},
        {"lastHealthSweepAt", lastHealthSweepAt.toString(Qt::ISODate)},
        {"queuedTaskCount", taskBusHealth.value(QStringLiteral("queuedTaskCount")).toInt()},
        {"leasedTaskCount", taskBusHealth.value(QStringLiteral("leasedTaskCount")).toInt()},
        {"recentResultCount", taskBusHealth.value(QStringLiteral("recentResultCount")).toInt()},
        {"expiredReclaimedCount", taskBusHealth.value(QStringLiteral("expiredReclaimedCount")).toInt()},
        {"staleSuppressedResultCount", taskBusHealth.value(QStringLiteral("staleSuppressedResultCount")).toInt()},
        {"taskBus", taskBusHealth},
        {"delegationTemplateCount", templates.size()},
        {"error", _ready ? QString() : _lastError}
    };
}

} // namespace yaos::control
