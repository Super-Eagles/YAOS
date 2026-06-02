#include "TaskStore.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSharedPointer>
#include <QStringList>
#include <QUuid>

namespace yaos::runtime {

namespace {

struct TaskTreeInfo {
    QString rootTaskId;
    int depth = 0;
};

TaskTreeInfo resolveTaskTreeInfo(int index,
                                 const QVector<TaskRecord> &tasks,
                                 const QHash<QString, int> &indexById,
                                 QHash<QString, TaskTreeInfo> *cache,
                                 QHash<QString, bool> *visiting) {
    const QString taskId = tasks.value(index).id.trimmed();
    if (taskId.isEmpty()) {
        return TaskTreeInfo{};
    }
    if (cache->contains(taskId)) {
        return cache->value(taskId);
    }
    if (visiting->contains(taskId)) {
        return TaskTreeInfo{taskId, 0};
    }

    visiting->insert(taskId, true);
    TaskTreeInfo info;
    const QString parentTaskId = tasks.value(index).parentTaskId.trimmed();
    if (parentTaskId.isEmpty()) {
        info.rootTaskId = taskId;
        info.depth = 0;
    } else if (indexById.contains(parentTaskId)) {
        const TaskTreeInfo parentInfo = resolveTaskTreeInfo(indexById.value(parentTaskId),
                                                            tasks,
                                                            indexById,
                                                            cache,
                                                            visiting);
        info.rootTaskId = parentInfo.rootTaskId.isEmpty() ? parentTaskId : parentInfo.rootTaskId;
        info.depth = parentInfo.depth + 1;
    } else {
        info.rootTaskId = parentTaskId;
        info.depth = 1;
    }

    visiting->remove(taskId);
    cache->insert(taskId, info);
    return info;
}

int descendantCountForTask(const QString &taskId,
                           const QHash<QString, QStringList> &childrenByParent,
                           QHash<QString, int> *cache,
                           QHash<QString, bool> *visiting) {
    if (taskId.isEmpty()) {
        return 0;
    }
    if (cache->contains(taskId)) {
        return cache->value(taskId);
    }
    if (visiting->contains(taskId)) {
        return 0;
    }

    visiting->insert(taskId, true);
    int total = 0;
    const QStringList children = childrenByParent.value(taskId);
    for (const QString &childId : children) {
        total += 1;
        total += descendantCountForTask(childId, childrenByParent, cache, visiting);
    }
    visiting->remove(taskId);
    cache->insert(taskId, total);
    return total;
}

void hydrateTaskTree(QVector<TaskRecord> *tasks) {
    if (!tasks || tasks->isEmpty()) {
        return;
    }

    QHash<QString, int> indexById;
    QHash<QString, QStringList> childrenByParent;
    for (int i = 0; i < tasks->size(); ++i) {
        const QString taskId = tasks->at(i).id.trimmed();
        if (!taskId.isEmpty()) {
            indexById.insert(taskId, i);
        }
    }
    const QVector<TaskRecord> currentTasks = *tasks;
    for (const TaskRecord &task : currentTasks) {
        const QString taskId = task.id.trimmed();
        const QString parentTaskId = task.parentTaskId.trimmed();
        if (!taskId.isEmpty() && !parentTaskId.isEmpty()) {
            childrenByParent[parentTaskId].append(taskId);
        }
    }

    QHash<QString, TaskTreeInfo> infoCache;
    QHash<QString, int> descendantCache;
    QHash<QString, bool> treeVisiting;
    QHash<QString, bool> descendantVisiting;
    for (int i = 0; i < tasks->size(); ++i) {
        TaskRecord &task = (*tasks)[i];
        const TaskTreeInfo info = resolveTaskTreeInfo(i,
                                                      *tasks,
                                                      indexById,
                                                      &infoCache,
                                                      &treeVisiting);
        const QString taskId = task.id.trimmed();
        task.rootTaskId = info.rootTaskId.isEmpty() ? taskId : info.rootTaskId;
        task.depth = std::max(0, info.depth);
        task.childCount = childrenByParent.value(taskId).size();
        task.descendantCount = descendantCountForTask(taskId,
                                                      childrenByParent,
                                                      &descendantCache,
                                                      &descendantVisiting);
        task.hasChildren = task.childCount > 0;
    }
}

} // namespace

TaskStore::TaskStore(const QString &workspace)
    : _workspace(workspace) {}

QString TaskStore::filePath() const {
    return QDir(_workspace).filePath("runtime/tasks.json");
}

QString TaskStore::trimText(const QString &text, int maxLen) {
    const QString simplified = text.simplified();
    if (simplified.size() <= maxLen) {
        return simplified;
    }
    return simplified.left(maxLen) + "...";
}

QString TaskStore::metadataString(const QJsonObject &metadata,
                                  const char *primaryKey,
                                  const char *alternateKey) {
    const QString primary = metadata.value(QLatin1String(primaryKey)).toString();
    if (!primary.trimmed().isEmpty()) {
        return primary.trimmed();
    }
    if (alternateKey) {
        return metadata.value(QLatin1String(alternateKey)).toString().trimmed();
    }
    return QString();
}

QJsonObject TaskStore::toJson(const TaskRecord &task) {
    QJsonObject obj;
    obj["id"] = task.id;
    obj["traceId"] = task.traceId;
    obj["parentTaskId"] = task.parentTaskId;
    obj["rootTaskId"] = task.rootTaskId;
    obj["originNode"] = task.originNode;
    obj["targetNode"] = task.targetNode;
    obj["kind"] = task.kind;
    obj["title"] = task.title;
    obj["sessionKey"] = task.sessionKey;
    obj["channel"] = task.channel;
    obj["state"] = task.state;
    obj["summary"] = task.summary;
    obj["resultPreview"] = task.resultPreview;
    obj["error"] = task.error;
    obj["depth"] = task.depth;
    obj["childCount"] = task.childCount;
    obj["descendantCount"] = task.descendantCount;
    obj["hasChildren"] = task.hasChildren;
    obj["createdAt"] = task.createdAt.toString(Qt::ISODate);
    obj["startedAt"] = task.startedAt.toString(Qt::ISODate);
    obj["finishedAt"] = task.finishedAt.toString(Qt::ISODate);
    obj["metadata"] = task.metadata;
    return obj;
}

TaskRecord TaskStore::fromJson(const QJsonObject &obj) {
    TaskRecord task;
    task.id = obj.value("id").toString();
    task.traceId = obj.value("traceId").toString(obj.value("trace_id").toString());
    task.parentTaskId = obj.value("parentTaskId").toString(obj.value("parent_task_id").toString());
    task.rootTaskId = obj.value("rootTaskId").toString(obj.value("root_task_id").toString());
    task.originNode = obj.value("originNode").toString(obj.value("origin_node").toString());
    task.targetNode = obj.value("targetNode").toString(obj.value("target_node").toString());
    task.kind = obj.value("kind").toString();
    task.title = obj.value("title").toString();
    task.sessionKey = obj.value("sessionKey").toString();
    task.channel = obj.value("channel").toString();
    task.state = obj.value("state").toString();
    task.summary = obj.value("summary").toString();
    task.resultPreview = obj.value("resultPreview").toString();
    task.error = obj.value("error").toString();
    task.depth = obj.value("depth").toInt();
    task.childCount = obj.value("childCount").toInt();
    task.descendantCount = obj.value("descendantCount").toInt();
    task.hasChildren = obj.value("hasChildren").toBool(task.childCount > 0);
    task.createdAt = QDateTime::fromString(obj.value("createdAt").toString(), Qt::ISODate);
    task.startedAt = QDateTime::fromString(obj.value("startedAt").toString(), Qt::ISODate);
    task.finishedAt = QDateTime::fromString(obj.value("finishedAt").toString(), Qt::ISODate);
    task.metadata = obj.value("metadata").toObject();
    if (task.traceId.isEmpty()) {
        task.traceId = metadataString(task.metadata, "trace_id", "traceId");
    }
    if (task.parentTaskId.isEmpty()) {
        task.parentTaskId = metadataString(task.metadata, "parent_task_id", "parentTaskId");
    }
    if (task.rootTaskId.isEmpty()) {
        task.rootTaskId = metadataString(task.metadata, "root_task_id", "rootTaskId");
    }
    if (task.originNode.isEmpty()) {
        task.originNode = metadataString(task.metadata, "origin_node", "originNode");
    }
    if (task.targetNode.isEmpty()) {
        task.targetNode = metadataString(task.metadata, "target_node", "targetNode");
    }
    return task;
}

struct TaskCacheEntry {
    QMutex mutex;
    QDateTime lastModified;
    QVector<TaskRecord> records;
    bool loaded = false;
};
static QHash<QString, QSharedPointer<TaskCacheEntry>> s_taskCaches;
static QMutex s_taskCachesMutex;

static QSharedPointer<TaskCacheEntry> getTaskCache(const QString &workspace) {
    QMutexLocker locker(&s_taskCachesMutex);
    if (!s_taskCaches.contains(workspace)) {
        s_taskCaches.insert(workspace, QSharedPointer<TaskCacheEntry>::create());
    }
    return s_taskCaches.value(workspace);
}

QVector<TaskRecord> TaskStore::loadUnlocked() const {
    const QString path = filePath();
    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        return QVector<TaskRecord>();
    }

    QDateTime currentMod = fileInfo.lastModified();
    QSharedPointer<TaskCacheEntry> cache = getTaskCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        if (cache->loaded && cache->lastModified == currentMod) {
            return cache->records;
        }
    }

    QVector<TaskRecord> tasks;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return tasks;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return tasks;
    }

    const QJsonArray arr = doc.object().value("tasks").toArray();
    for (const QJsonValue &value : arr) {
        if (value.isObject()) {
            tasks.append(fromJson(value.toObject()));
        }
    }
    hydrateTaskTree(&tasks);

    {
        QMutexLocker cacheLocker(&cache->mutex);
        cache->records = tasks;
        cache->lastModified = currentMod;
        cache->loaded = true;
    }

    return tasks;
}

void TaskStore::saveUnlocked(const QVector<TaskRecord> &tasks) const {
    const QString path = filePath();
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QVector<TaskRecord> capped = tasks;
    if (capped.size() > 1000) {
        capped = capped.mid(capped.size() - 1000);
    }

    QJsonArray arr;
    for (const TaskRecord &task : capped) {
        arr.append(toJson(task));
    }
    QJsonObject root;
    root["tasks"] = arr;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        return;
    }
    file.commit();

    QFileInfo updatedInfo(path);
    QDateTime currentMod = updatedInfo.lastModified();
    QSharedPointer<TaskCacheEntry> cache = getTaskCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        cache->records = capped;
        cache->lastModified = currentMod;
        cache->loaded = true;
    }
}

QString TaskStore::createTask(const QString &kind,
                              const QString &title,
                              const QString &sessionKey,
                              const QString &channel,
                              const QJsonObject &metadata) {
    QMutexLocker locker(&_mutex);
    QVector<TaskRecord> tasks = loadUnlocked();

    TaskRecord task;
    task.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    task.kind = kind.trimmed().isEmpty() ? "task" : kind.trimmed();
    task.title = trimText(title, 120);
    task.sessionKey = sessionKey;
    task.channel = channel;
    task.state = "queued";
    task.createdAt = QDateTime::currentDateTime();
    task.metadata = metadata;
    task.traceId = metadataString(metadata, "trace_id", "traceId");
    task.parentTaskId = metadataString(metadata, "parent_task_id", "parentTaskId");
    task.rootTaskId = metadataString(metadata, "root_task_id", "rootTaskId");
    task.originNode = metadataString(metadata, "origin_node", "originNode");
    task.targetNode = metadataString(metadata, "target_node", "targetNode");
    tasks.append(task);
    saveUnlocked(tasks);
    return task.id;
}

bool TaskStore::upsertTask(const TaskRecord &input) {
    if (input.id.trimmed().isEmpty()) {
        return false;
    }

    QMutexLocker locker(&_mutex);
    QVector<TaskRecord> tasks = loadUnlocked();

    TaskRecord task = input;
    if (!task.createdAt.isValid()) {
        task.createdAt = QDateTime::currentDateTime();
    }
    if (task.state.trimmed().isEmpty()) {
        task.state = "queued";
    }

    for (TaskRecord &existing : tasks) {
        if (existing.id != task.id) {
            continue;
        }
        if (!existing.createdAt.isValid()) {
            existing.createdAt = task.createdAt;
        }
        if (!task.createdAt.isValid()) {
            task.createdAt = existing.createdAt;
        }
        if (!task.startedAt.isValid()) {
            task.startedAt = existing.startedAt;
        }
        if (!task.finishedAt.isValid()) {
            task.finishedAt = existing.finishedAt;
        }
        if (task.traceId.isEmpty()) {
            task.traceId = existing.traceId;
        }
        if (task.parentTaskId.isEmpty()) {
            task.parentTaskId = existing.parentTaskId;
        }
        if (task.rootTaskId.isEmpty()) {
            task.rootTaskId = existing.rootTaskId;
        }
        if (task.originNode.isEmpty()) {
            task.originNode = existing.originNode;
        }
        if (task.targetNode.isEmpty()) {
            task.targetNode = existing.targetNode;
        }
        if (task.summary.isEmpty()) {
            task.summary = existing.summary;
        }
        if (task.resultPreview.isEmpty()) {
            task.resultPreview = existing.resultPreview;
        }
        if (task.error.isEmpty()) {
            task.error = existing.error;
        }
        if (task.metadata.isEmpty()) {
            task.metadata = existing.metadata;
        }
        existing = task;
        saveUnlocked(tasks);
        return true;
    }

    tasks.append(task);
    saveUnlocked(tasks);
    return true;
}

bool TaskStore::markRunning(const QString &taskId) {
    QMutexLocker locker(&_mutex);
    QVector<TaskRecord> tasks = loadUnlocked();
    for (TaskRecord &task : tasks) {
        if (task.id != taskId) {
            continue;
        }
        task.state = "running";
        task.startedAt = QDateTime::currentDateTime();
        saveUnlocked(tasks);
        return true;
    }
    return false;
}

bool TaskStore::markCompleted(const QString &taskId,
                              const QString &resultPreview,
                              const QString &summary) {
    QMutexLocker locker(&_mutex);
    QVector<TaskRecord> tasks = loadUnlocked();
    for (TaskRecord &task : tasks) {
        if (task.id != taskId) {
            continue;
        }
        task.state = "succeeded";
        task.resultPreview = trimText(resultPreview, 400);
        task.summary = trimText(summary.isEmpty() ? resultPreview : summary, 160);
        task.error.clear();
        if (!task.startedAt.isValid()) {
            task.startedAt = QDateTime::currentDateTime();
        }
        task.finishedAt = QDateTime::currentDateTime();
        saveUnlocked(tasks);
        return true;
    }
    return false;
}

bool TaskStore::markFailed(const QString &taskId,
                           const QString &error,
                           const QString &summary) {
    QMutexLocker locker(&_mutex);
    QVector<TaskRecord> tasks = loadUnlocked();
    for (TaskRecord &task : tasks) {
        if (task.id != taskId) {
            continue;
        }
        task.state = "failed";
        task.error = trimText(error, 400);
        task.summary = trimText(summary.isEmpty() ? error : summary, 160);
        if (!task.startedAt.isValid()) {
            task.startedAt = QDateTime::currentDateTime();
        }
        task.finishedAt = QDateTime::currentDateTime();
        saveUnlocked(tasks);
        return true;
    }
    return false;
}

bool TaskStore::markCancelled(const QString &taskId,
                              const QString &summary) {
    QMutexLocker locker(&_mutex);
    QVector<TaskRecord> tasks = loadUnlocked();
    for (TaskRecord &task : tasks) {
        if (task.id != taskId) {
            continue;
        }
        task.state = "cancelled";
        task.resultPreview.clear();
        task.error.clear();
        task.summary = trimText(summary.isEmpty() ? QStringLiteral("Cancelled") : summary, 160);
        if (!task.startedAt.isValid()) {
            task.startedAt = QDateTime::currentDateTime();
        }
        task.finishedAt = QDateTime::currentDateTime();
        saveUnlocked(tasks);
        return true;
    }
    return false;
}

QVector<TaskRecord> TaskStore::recentTasks(int limit) const {
    QMutexLocker locker(&_mutex);
    QVector<TaskRecord> tasks = loadUnlocked();
    if (limit > 0 && tasks.size() > limit) {
        tasks = tasks.mid(tasks.size() - limit);
    }
    std::reverse(tasks.begin(), tasks.end());
    return tasks;
}

int TaskStore::count() const {
    QMutexLocker locker(&_mutex);
    return loadUnlocked().size();
}

} // namespace yaos::runtime
