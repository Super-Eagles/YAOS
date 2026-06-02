#include "LocalNodeRegistry.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QVector>

#include "ContractsJson.h"

namespace yaos::distributed {

namespace {

struct NodeSnapshot {
    NodeDescriptor descriptor;
    QDateTime updatedAt;
};

QDateTime dateTimeFromJson(const QJsonValue &value) {
    const QString text = value.toString();
    if (text.isEmpty()) {
        return QDateTime();
    }
    return QDateTime::fromString(text, Qt::ISODate);
}

QJsonObject snapshotToJson(const NodeSnapshot &snapshot) {
    return QJsonObject{
        {"node", json::toJson(snapshot.descriptor)},
        {"updatedAt", snapshot.updatedAt.toString(Qt::ISODate)}
    };
}

NodeSnapshot snapshotFromJson(const QJsonObject &obj) {
    NodeSnapshot snapshot;
    snapshot.descriptor = json::nodeDescriptorFromJson(obj.value("node").toObject());
    snapshot.updatedAt = dateTimeFromJson(obj.value("updatedAt"));
    return snapshot;
}

QVector<NodeSnapshot> loadSnapshots(const QString &path) {
    QVector<NodeSnapshot> snapshots;
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return snapshots;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return snapshots;
    }

    const QJsonArray array = doc.object().value("nodes").toArray();
    snapshots.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (item.isObject()) {
            snapshots.append(snapshotFromJson(item.toObject()));
        }
    }
    return snapshots;
}

bool saveSnapshots(const QString &path, const QVector<NodeSnapshot> &snapshots) {
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QJsonArray array;
    for (const NodeSnapshot &snapshot : snapshots) {
        array.append(snapshotToJson(snapshot));
    }

    QJsonObject root;
    root["nodes"] = array;
    root["updatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    return file.commit();
}

} // namespace

LocalNodeRegistry::LocalNodeRegistry(const QString &workspace, int staleAfterSeconds)
    : _workspace(workspace),
      _staleAfterSeconds(staleAfterSeconds > 0 ? staleAfterSeconds : 300) {}

QString LocalNodeRegistry::filePath() const {
    return QDir(_workspace).filePath("runtime/nodes.json");
}

QList<NodeDescriptor> LocalNodeRegistry::listNodes() const {
    QMutexLocker locker(&_mutex);
    QVector<NodeSnapshot> snapshots = loadSnapshots(filePath());
    const QDateTime now = QDateTime::currentDateTimeUtc();

    QList<NodeDescriptor> nodes;
    nodes.reserve(snapshots.size());
    for (const NodeSnapshot &snapshot : snapshots) {
        NodeDescriptor descriptor = snapshot.descriptor;
        if (!snapshot.updatedAt.isValid() ||
            snapshot.updatedAt.secsTo(now) > _staleAfterSeconds) {
            descriptor.online = false;
        }
        nodes.append(descriptor);
    }

    std::sort(nodes.begin(), nodes.end(), [](const NodeDescriptor &left, const NodeDescriptor &right) {
        if (left.online != right.online) {
            return left.online && !right.online;
        }
        if (left.weight != right.weight) {
            return left.weight > right.weight;
        }
        return left.nodeId < right.nodeId;
    });
    return nodes;
}

bool LocalNodeRegistry::publishPresence(const NodeDescriptor &node) {
    if (node.nodeId.trimmed().isEmpty()) {
        return false;
    }

    QMutexLocker locker(&_mutex);
    QVector<NodeSnapshot> snapshots = loadSnapshots(filePath());
    const QDateTime now = QDateTime::currentDateTimeUtc();

    bool updated = false;
    for (NodeSnapshot &snapshot : snapshots) {
        if (snapshot.descriptor.nodeId == node.nodeId) {
            snapshot.descriptor = node;
            snapshot.updatedAt = now;
            updated = true;
            break;
        }
    }

    if (!updated) {
        NodeSnapshot snapshot;
        snapshot.descriptor = node;
        snapshot.updatedAt = now;
        snapshots.append(snapshot);
    }

    if (snapshots.size() > 128) {
        snapshots = snapshots.mid(snapshots.size() - 128);
    }
    return saveSnapshots(filePath(), snapshots);
}

} // namespace yaos::distributed
