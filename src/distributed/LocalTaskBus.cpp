#include "LocalTaskBus.h"

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

struct TaskBusRecord {
    TaskEnvelope task;
    QString state = "queued";
    QString leasedBy;
    QDateTime leaseExpiresAt;
    QDateTime submittedAt;
    QDateTime updatedAt;
};

struct TaskBusMetrics {
    int submittedCount = 0;
    int claimCount = 0;
    int leaseRenewCount = 0;
    int expiredReclaimedCount = 0;
    int resultPublishedCount = 0;
    int staleSuppressedResultCount = 0;
    int cancelledCount = 0;
};

struct TaskBusEvent {
    QString type;
    QString taskId;
    QString traceId;
    QString nodeId;
    QString state;
    QString message;
    QDateTime timestamp;
};

constexpr int kTaskLeaseDurationMs = 45000;
constexpr int kTaskBusEventHistoryLimit = 120;

QDateTime dateTimeFromJson(const QJsonValue &value) {
    const QString text = value.toString();
    if (text.isEmpty()) {
        return QDateTime();
    }
    return QDateTime::fromString(text, Qt::ISODate);
}

QJsonObject taskRecordToJson(const TaskBusRecord &record) {
    return QJsonObject{
        {"task", json::toJson(record.task)},
        {"state", record.state},
        {"leasedBy", record.leasedBy},
        {"leaseExpiresAt", record.leaseExpiresAt.toString(Qt::ISODate)},
        {"submittedAt", record.submittedAt.toString(Qt::ISODate)},
        {"updatedAt", record.updatedAt.toString(Qt::ISODate)}
    };
}

TaskBusRecord taskRecordFromJson(const QJsonObject &obj) {
    TaskBusRecord record;
    record.task = json::taskEnvelopeFromJson(obj.value("task").toObject());
    record.state = obj.value("state").toString(record.state);
    record.leasedBy = obj.value("leasedBy").toString(obj.value("leased_by").toString());
    record.leaseExpiresAt = dateTimeFromJson(obj.value("leaseExpiresAt"));
    record.submittedAt = dateTimeFromJson(obj.value("submittedAt"));
    record.updatedAt = dateTimeFromJson(obj.value("updatedAt"));
    return record;
}

QJsonObject taskBusMetricsToJson(const TaskBusMetrics &metrics) {
    return QJsonObject{
        {"submittedCount", metrics.submittedCount},
        {"claimCount", metrics.claimCount},
        {"leaseRenewCount", metrics.leaseRenewCount},
        {"expiredReclaimedCount", metrics.expiredReclaimedCount},
        {"resultPublishedCount", metrics.resultPublishedCount},
        {"staleSuppressedResultCount", metrics.staleSuppressedResultCount},
        {"cancelledCount", metrics.cancelledCount}
    };
}

TaskBusMetrics taskBusMetricsFromJson(const QJsonObject &obj) {
    TaskBusMetrics metrics;
    metrics.submittedCount = obj.value("submittedCount").toInt();
    metrics.claimCount = obj.value("claimCount").toInt();
    metrics.leaseRenewCount = obj.value("leaseRenewCount").toInt();
    metrics.expiredReclaimedCount = obj.value("expiredReclaimedCount").toInt();
    metrics.resultPublishedCount = obj.value("resultPublishedCount").toInt();
    metrics.staleSuppressedResultCount = obj.value("staleSuppressedResultCount").toInt();
    metrics.cancelledCount = obj.value("cancelledCount").toInt();
    return metrics;
}

QJsonObject taskBusEventToJson(const TaskBusEvent &event) {
    return QJsonObject{
        {"type", event.type},
        {"taskId", event.taskId},
        {"traceId", event.traceId},
        {"nodeId", event.nodeId},
        {"state", event.state},
        {"message", event.message},
        {"timestamp", event.timestamp.toString(Qt::ISODate)}
    };
}

TaskBusEvent taskBusEventFromJson(const QJsonObject &obj) {
    TaskBusEvent event;
    event.type = obj.value("type").toString();
    event.taskId = obj.value("taskId").toString(obj.value("task_id").toString());
    event.traceId = obj.value("traceId").toString(obj.value("trace_id").toString());
    event.nodeId = obj.value("nodeId").toString(obj.value("node_id").toString());
    event.state = obj.value("state").toString();
    event.message = obj.value("message").toString();
    event.timestamp = dateTimeFromJson(obj.value("timestamp"));
    return event;
}

QJsonArray taskBusEventsToJson(const QVector<TaskBusEvent> &events) {
    QJsonArray array;
    for (const TaskBusEvent &event : events) {
        array.append(taskBusEventToJson(event));
    }
    return array;
}

QVector<TaskBusEvent> taskBusEventsFromJson(const QJsonValue &value) {
    QVector<TaskBusEvent> events;
    const QJsonArray array = value.toArray();
    events.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (item.isObject()) {
            events.append(taskBusEventFromJson(item.toObject()));
        }
    }
    return events;
}

QJsonArray resultArrayToJson(const QVector<TaskResultEnvelope> &results) {
    QJsonArray array;
    for (const TaskResultEnvelope &result : results) {
        array.append(json::toJson(result));
    }
    return array;
}

QVector<TaskResultEnvelope> resultsFromJson(const QJsonValue &value) {
    QVector<TaskResultEnvelope> results;
    const QJsonArray array = value.toArray();
    results.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (item.isObject()) {
            results.append(json::taskResultEnvelopeFromJson(item.toObject()));
        }
    }
    return results;
}

struct TaskBusState {
    QVector<TaskBusRecord> tasks;
    QVector<TaskResultEnvelope> results;
    TaskBusMetrics metrics;
    QVector<TaskBusEvent> events;
};

TaskBusState loadState(const QString &path) {
    TaskBusState state;
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return state;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return state;
    }

    const QJsonObject root = doc.object();
    const QJsonArray tasks = root.value("tasks").toArray();
    state.tasks.reserve(tasks.size());
    for (const QJsonValue &item : tasks) {
        if (item.isObject()) {
            state.tasks.append(taskRecordFromJson(item.toObject()));
        }
    }
    state.results = resultsFromJson(root.value("results"));
    state.metrics = taskBusMetricsFromJson(root.value("metrics").toObject());
    state.events = taskBusEventsFromJson(root.value("events"));
    return state;
}

bool saveState(const QString &path, const TaskBusState &state) {
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QJsonArray tasks;
    for (const TaskBusRecord &record : state.tasks) {
        tasks.append(taskRecordToJson(record));
    }

    QJsonObject root;
    root["tasks"] = tasks;
    root["results"] = resultArrayToJson(state.results);
    root["metrics"] = taskBusMetricsToJson(state.metrics);
    root["events"] = taskBusEventsToJson(state.events);
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

QString stateForResult(const QString &status) {
    const QString normalized = status.trimmed().toLower();
    if (normalized == "ok" || normalized == "succeeded" || normalized == "success") {
        return "succeeded";
    }
    if (normalized == "cancelled" || normalized == "canceled") {
        return "cancelled";
    }
    return "failed";
}

bool isPendingState(const QString &state) {
    const QString normalized = state.trimmed().toLower();
    return normalized.isEmpty() ||
           normalized == "queued" ||
           normalized == "pending";
}

bool isLeasedState(const QString &state) {
    return state.trimmed().compare(QStringLiteral("leased"), Qt::CaseInsensitive) == 0;
}

bool isTerminalState(const QString &state) {
    return !isPendingState(state) && !isLeasedState(state);
}

bool isSucceededState(const QString &state) {
    const QString normalized = state.trimmed().toLower();
    return normalized == QStringLiteral("succeeded") ||
           normalized == QStringLiteral("success") ||
           normalized == QStringLiteral("completed") ||
           normalized == QStringLiteral("ok");
}

bool isCancelledState(const QString &state) {
    const QString normalized = state.trimmed().toLower();
    return normalized == QStringLiteral("cancelled") ||
           normalized == QStringLiteral("canceled");
}

bool leaseExpired(const TaskBusRecord &record,
                  const QDateTime &now) {
    return isLeasedState(record.state) &&
           record.leaseExpiresAt.isValid() &&
           record.leaseExpiresAt <= now;
}

void appendTaskBusEvent(TaskBusState *state,
                        const QString &type,
                        const QString &taskId,
                        const QString &traceId,
                        const QString &nodeId,
                        const QString &taskState,
                        const QString &message,
                        const QDateTime &timestamp) {
    if (!state) {
        return;
    }

    TaskBusEvent event;
    event.type = type.trimmed();
    event.taskId = taskId.trimmed();
    event.traceId = traceId.trimmed();
    event.nodeId = nodeId.trimmed();
    event.state = taskState.trimmed();
    event.message = message.trimmed();
    event.timestamp = timestamp;
    state->events.append(event);
    if (state->events.size() > kTaskBusEventHistoryLimit) {
        state->events = state->events.mid(state->events.size() - kTaskBusEventHistoryLimit);
    }
}

bool normalizeExpiredLeases(TaskBusState *state,
                            const QDateTime &now) {
    if (!state) {
        return false;
    }

    bool changed = false;
    for (TaskBusRecord &record : state->tasks) {
        if (!leaseExpired(record, now)) {
            continue;
        }
        record.state = QStringLiteral("queued");
        const QString previousLeaseOwner = record.leasedBy;
        record.leasedBy.clear();
        record.leaseExpiresAt = QDateTime();
        record.updatedAt = now;
        ++state->metrics.expiredReclaimedCount;
        appendTaskBusEvent(state,
                           QStringLiteral("lease_expired_reclaimed"),
                           record.task.taskId,
                           record.task.traceId,
                           previousLeaseOwner,
                           record.state,
                           QStringLiteral("Expired delegated lease returned to the pending queue."),
                           now);
        changed = true;
    }
    return changed;
}

bool isClaimableRecord(const TaskBusRecord &record,
                       const QDateTime &now) {
    return isPendingState(record.state) || leaseExpired(record, now);
}

QDateTime lastTaskBusActivityAt(const TaskBusState &state) {
    QDateTime latest;
    auto maybeTake = [&latest](const QDateTime &value) {
        if (!value.isValid()) {
            return;
        }
        if (!latest.isValid() || latest < value) {
            latest = value;
        }
    };

    for (const TaskBusRecord &record : state.tasks) {
        maybeTake(record.submittedAt);
        maybeTake(record.updatedAt);
    }
    for (const TaskResultEnvelope &result : state.results) {
        maybeTake(result.finishedAt);
    }
    for (const TaskBusEvent &event : state.events) {
        maybeTake(event.timestamp);
    }
    return latest;
}

QJsonArray recentTaskBusEventsToJson(const QVector<TaskBusEvent> &events,
                                     int limit) {
    const int maxItems = limit > 0 ? limit : 12;
    QJsonArray array;
    for (int i = events.size() - 1; i >= 0 && array.size() < maxItems; --i) {
        array.append(taskBusEventToJson(events.at(i)));
    }
    return array;
}

} // namespace

LocalTaskBus::LocalTaskBus(const QString &workspace)
    : _workspace(workspace) {}

QString LocalTaskBus::filePath() const {
    return QDir(_workspace).filePath("runtime/task_bus.json");
}

bool LocalTaskBus::submit(const TaskEnvelope &task) {
    if (task.taskId.trimmed().isEmpty()) {
        return false;
    }

    QMutexLocker locker(&_mutex);
    TaskBusState state = loadState(filePath());
    const QDateTime now = QDateTime::currentDateTimeUtc();

    bool updated = false;
    for (TaskBusRecord &record : state.tasks) {
        if (record.task.taskId == task.taskId) {
            record.task = task;
            if (!record.submittedAt.isValid()) {
                record.submittedAt = now;
            }
            if (record.state.isEmpty()) {
                record.state = "queued";
            }
            if (isPendingState(record.state)) {
                record.leasedBy.clear();
                record.leaseExpiresAt = QDateTime();
            }
            record.updatedAt = now;
            updated = true;
            break;
        }
    }

    if (!updated) {
        TaskBusRecord record;
        record.task = task;
        record.state = "queued";
        record.submittedAt = now;
        record.updatedAt = now;
        state.tasks.append(record);
    }

    if (state.tasks.size() > 1000) {
        state.tasks = state.tasks.mid(state.tasks.size() - 1000);
    }
    ++state.metrics.submittedCount;
    appendTaskBusEvent(&state,
                       QStringLiteral("submitted"),
                       task.taskId,
                       task.traceId,
                       task.targetNode,
                       QStringLiteral("queued"),
                       updated
                           ? QStringLiteral("Delegated task submission updated an existing task record.")
                           : QStringLiteral("Delegated task submitted to the local task bus."),
                       now);
    return saveState(filePath(), state);
}

bool LocalTaskBus::publishResult(const TaskResultEnvelope &result) {
    if (result.taskId.trimmed().isEmpty()) {
        return false;
    }

    QMutexLocker locker(&_mutex);
    TaskBusState state = loadState(filePath());
    const QDateTime now = QDateTime::currentDateTimeUtc();

    const bool normalized = normalizeExpiredLeases(&state, now);
    bool matchedTask = false;
    for (TaskBusRecord &record : state.tasks) {
        if (record.task.taskId == result.taskId) {
            matchedTask = true;
            const QString producerNode = result.producerNode.trimmed();
            const QString leasedBy = record.leasedBy.trimmed();
            if (isTerminalState(record.state)) {
                if (normalized) {
                    saveState(filePath(), state);
                }
                return false;
            }
            if (isLeasedState(record.state) &&
                !leasedBy.isEmpty() &&
                !producerNode.isEmpty() &&
                leasedBy.compare(producerNode, Qt::CaseInsensitive) != 0) {
                ++state.metrics.staleSuppressedResultCount;
                appendTaskBusEvent(&state,
                                   QStringLiteral("stale_result_suppressed"),
                                   result.taskId,
                                   result.traceId,
                                   producerNode,
                                   record.state,
                                   QStringLiteral("Late delegated result was rejected after another worker reclaimed the task."),
                                   now);
                saveState(filePath(), state);
                return false;
            }
            record.state = stateForResult(result.status);
            record.leasedBy.clear();
            record.leaseExpiresAt = QDateTime();
            record.updatedAt = now;
            ++state.metrics.resultPublishedCount;
            appendTaskBusEvent(&state,
                               QStringLiteral("result_published"),
                               result.taskId,
                               result.traceId,
                               producerNode,
                               record.state,
                               result.message.trimmed().isEmpty()
                                   ? QStringLiteral("Delegated task result accepted by the local task bus.")
                                   : result.message,
                               now);
            break;
        }
    }

    if (!matchedTask) {
        if (normalized) {
            saveState(filePath(), state);
        }
        return false;
    }

    state.results.append(result);
    if (state.results.size() > 1000) {
        state.results = state.results.mid(state.results.size() - 1000);
    }

    return saveState(filePath(), state);
}

bool LocalTaskBus::cancel(const QString &taskId) {
    if (taskId.trimmed().isEmpty()) {
        return false;
    }

    QMutexLocker locker(&_mutex);
    TaskBusState state = loadState(filePath());
    const QDateTime now = QDateTime::currentDateTimeUtc();

    for (TaskBusRecord &record : state.tasks) {
        if (record.task.taskId == taskId) {
            const QString previousState = record.state;
            const QString previousLeaseOwner = record.leasedBy;
            record.state = "cancelled";
            record.leasedBy.clear();
            record.leaseExpiresAt = QDateTime();
            record.updatedAt = now;
            if (previousState.compare(QStringLiteral("cancelled"), Qt::CaseInsensitive) != 0) {
                ++state.metrics.cancelledCount;
                appendTaskBusEvent(&state,
                                   QStringLiteral("cancelled"),
                                   record.task.taskId,
                                   record.task.traceId,
                                   previousLeaseOwner,
                                   record.state,
                                   QStringLiteral("Delegated task was cancelled before completion."),
                                   now);
            }
            return saveState(filePath(), state);
        }
    }
    return false;
}

bool LocalTaskBus::claim(const QString &taskId,
                         const QString &consumerNode) {
    if (taskId.trimmed().isEmpty()) {
        return false;
    }

    QMutexLocker locker(&_mutex);
    TaskBusState state = loadState(filePath());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const bool normalized = normalizeExpiredLeases(&state, now);

    for (TaskBusRecord &record : state.tasks) {
        if (record.task.taskId != taskId) {
            continue;
        }
        const QString normalizedConsumer = consumerNode.trimmed();
        if (isLeasedState(record.state) &&
            !leaseExpired(record, now) &&
            !normalizedConsumer.isEmpty() &&
            record.leasedBy.trimmed().compare(normalizedConsumer, Qt::CaseInsensitive) == 0) {
            record.leaseExpiresAt = now.addMSecs(kTaskLeaseDurationMs);
            record.updatedAt = now;
            ++state.metrics.leaseRenewCount;
            appendTaskBusEvent(&state,
                               QStringLiteral("lease_renewed"),
                               record.task.taskId,
                               record.task.traceId,
                               normalizedConsumer,
                               record.state,
                               QStringLiteral("Delegated worker renewed its task lease."),
                               now);
            return saveState(filePath(), state);
        }
        if (!isClaimableRecord(record, now)) {
            if (normalized) {
                saveState(filePath(), state);
            }
            return false;
        }
        record.state = "leased";
        record.leasedBy = normalizedConsumer;
        record.leaseExpiresAt = now.addMSecs(kTaskLeaseDurationMs);
        record.updatedAt = now;
        ++state.metrics.claimCount;
        appendTaskBusEvent(&state,
                           QStringLiteral("leased"),
                           record.task.taskId,
                           record.task.traceId,
                           normalizedConsumer,
                           record.state,
                           QStringLiteral("Delegated task leased to a worker node."),
                           now);
        return saveState(filePath(), state);
    }
    if (normalized) {
        saveState(filePath(), state);
    }
    return false;
}

QList<TaskEnvelope> LocalTaskBus::pendingTasks(const QString &targetNode,
                                               const QString &targetRole,
                                               int limit) const {
    QMutexLocker locker(&_mutex);
    TaskBusState state = loadState(filePath());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const bool changed = normalizeExpiredLeases(&state, now);
    if (changed) {
        saveState(filePath(), state);
    }
    const QString normalizedTargetNode = targetNode.trimmed();
    const QString normalizedTargetRole = targetRole.trimmed();
    const int maxItems = limit > 0 ? limit : 100;

    QList<TaskEnvelope> out;
    out.reserve(qMin(maxItems, state.tasks.size()));
    for (const TaskBusRecord &record : state.tasks) {
        if (!isClaimableRecord(record, now)) {
            continue;
        }
        if (!normalizedTargetNode.isEmpty() &&
            record.task.targetNode.trimmed() != normalizedTargetNode) {
            continue;
        }
        if (!normalizedTargetRole.isEmpty() &&
            record.task.targetRole.trimmed() != normalizedTargetRole) {
            continue;
        }
        out.append(record.task);
        if (out.size() >= maxItems) {
            break;
        }
    }
    return out;
}

QList<TaskResultEnvelope> LocalTaskBus::recentResults(const QString &taskId,
                                                      const QString &traceId,
                                                      int limit) const {
    QMutexLocker locker(&_mutex);
    const TaskBusState state = loadState(filePath());
    const QString normalizedTaskId = taskId.trimmed();
    const QString normalizedTraceId = traceId.trimmed();
    const int maxItems = limit > 0 ? limit : 100;

    QList<TaskResultEnvelope> out;
    out.reserve(qMin(maxItems, state.results.size()));
    for (int i = state.results.size() - 1; i >= 0; --i) {
        const TaskResultEnvelope &result = state.results.at(i);
        if (!normalizedTaskId.isEmpty() &&
            result.taskId.trimmed() != normalizedTaskId) {
            continue;
        }
        if (!normalizedTraceId.isEmpty() &&
            result.traceId.trimmed() != normalizedTraceId) {
            continue;
        }
        out.append(result);
        if (out.size() >= maxItems) {
            break;
        }
    }
    return out;
}

QJsonObject LocalTaskBus::health(int recentEventLimit) const {
    QMutexLocker locker(&_mutex);
    TaskBusState state = loadState(filePath());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const bool changed = normalizeExpiredLeases(&state, now);
    if (changed) {
        saveState(filePath(), state);
    }

    int queuedTasks = 0;
    int leasedTasks = 0;
    int succeededTasks = 0;
    int failedTasks = 0;
    int cancelledTasks = 0;
    for (const TaskBusRecord &record : state.tasks) {
        if (isPendingState(record.state)) {
            ++queuedTasks;
        } else if (isLeasedState(record.state)) {
            ++leasedTasks;
        } else if (isSucceededState(record.state)) {
            ++succeededTasks;
        } else if (isCancelledState(record.state)) {
            ++cancelledTasks;
        } else {
            ++failedTasks;
        }
    }

    const QDateTime lastActivityAt = lastTaskBusActivityAt(state);
    return QJsonObject{
        {"leaseDurationMs", kTaskLeaseDurationMs},
        {"taskCount", state.tasks.size()},
        {"queuedTaskCount", queuedTasks},
        {"leasedTaskCount", leasedTasks},
        {"succeededTaskCount", succeededTasks},
        {"failedTaskCount", failedTasks},
        {"cancelledTaskCount", cancelledTasks},
        {"terminalTaskCount", succeededTasks + failedTasks + cancelledTasks},
        {"recentResultCount", state.results.size()},
        {"submittedCount", state.metrics.submittedCount},
        {"claimCount", state.metrics.claimCount},
        {"leaseRenewCount", state.metrics.leaseRenewCount},
        {"expiredReclaimedCount", state.metrics.expiredReclaimedCount},
        {"resultPublishedCount", state.metrics.resultPublishedCount},
        {"staleSuppressedResultCount", state.metrics.staleSuppressedResultCount},
        {"cancelledCount", state.metrics.cancelledCount},
        {"lastActivityAt", lastActivityAt.toString(Qt::ISODate)},
        {"recentEvents", recentTaskBusEventsToJson(state.events, recentEventLimit)}
    };
}

} // namespace yaos::distributed
