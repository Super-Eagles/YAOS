#include "SubagentManager.h"

#include <algorithm>
#include <QDateTime>
#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QThreadPool>
#include <QUuid>
#include <QtConcurrent>

Q_LOGGING_CATEGORY(lcSubagent, "yaos.subagent")

namespace yaos::runtime {

SubagentManager::SubagentManager(bus::MessageBus &bus, QObject *parent)
    : QObject(parent),
      _bus(bus) {}

void SubagentManager::setExecuteCallback(const ExecuteCallback &callback) {
    _execute = callback;
}

void SubagentManager::setDelegateCallback(const DelegateCallback &callback) {
    _delegate = callback;
}

void SubagentManager::setCancelCallback(const CancelCallback &callback) {
    _cancel = callback;
}

SubagentManager::SpawnOutcome SubagentManager::spawnSingle(
    const SpawnRequest &request,
    const QString &originChannel,
    const QString &originChatId,
    const QString &sessionKey,
    const QString &groupId,
    const QString &parentTaskId,
    const QString &traceId,
    const QString &targetNode,
    const QString &targetRole,
    const QStringList &targetTags,
    const QString &requiredTool,
    const QString &requiredChannel,
    const QString &requiredMemoryBackend
) {
    SpawnOutcome outcome;
    const QString task = request.task.trimmed();
    if (task.isEmpty()) {
        outcome.message = "Error: task is required";
        return outcome;
    }

    const QString resolvedTargetNode = request.targetNode.trimmed().isEmpty()
        ? targetNode.trimmed()
        : request.targetNode.trimmed();
    const QString resolvedTargetRole = request.targetRole.trimmed().isEmpty()
        ? targetRole.trimmed()
        : request.targetRole.trimmed();
    const QStringList resolvedTargetTags = request.targetTags.isEmpty() ? targetTags : request.targetTags;
    const QString resolvedRequiredTool = request.requiredTool.trimmed().isEmpty()
        ? requiredTool.trimmed()
        : request.requiredTool.trimmed();
    const QString resolvedRequiredChannel = request.requiredChannel.trimmed().isEmpty()
        ? requiredChannel.trimmed()
        : request.requiredChannel.trimmed();
    const QString resolvedRequiredMemoryBackend = request.requiredMemoryBackend.trimmed().isEmpty()
        ? requiredMemoryBackend.trimmed()
        : request.requiredMemoryBackend.trimmed();

    const bool delegatedRequest = !resolvedTargetNode.isEmpty() ||
                                  !resolvedTargetRole.isEmpty() ||
                                  !resolvedTargetTags.isEmpty() ||
                                  !resolvedRequiredTool.isEmpty() ||
                                  !resolvedRequiredChannel.isEmpty() ||
                                  !resolvedRequiredMemoryBackend.isEmpty();
    if (!delegatedRequest && !_execute) {
        outcome.message = "Error: subagent execution callback not configured";
        return outcome;
    }
    if (delegatedRequest && !_delegate) {
        outcome.message = "Error: delegated subagent execution is not configured";
        return outcome;
    }

    const QString taskId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const QString displayLabel = request.label.trimmed().isEmpty()
                                 ? (task.left(30) + (task.size() > 30 ? "..." : ""))
                                 : request.label.trimmed();

    const QString parentSession = sessionKey.trimmed().isEmpty()
                                  ? (originChannel + ":" + originChatId)
                                  : sessionKey;
    const QString subSession = "subagent:" + taskId + ":" + parentSession;

    auto cancelled = QSharedPointer<std::atomic<bool>>::create(false);

    TaskEntry entry;
    entry.id = taskId;
    entry.task = task;
    entry.label = displayLabel;
    entry.originChannel = originChannel;
    entry.originChatId = originChatId;
    entry.sessionKey = parentSession;
    entry.cancelled = cancelled;
    entry.delegated = delegatedRequest;
    entry.targetNode = resolvedTargetNode;
    entry.targetRole = resolvedTargetRole;
    entry.targetTags = resolvedTargetTags;
    entry.requiredTool = resolvedRequiredTool;
    entry.requiredChannel = resolvedRequiredChannel;
    entry.requiredMemoryBackend = resolvedRequiredMemoryBackend;
    entry.groupId = groupId.trimmed();
    entry.parentTaskId = parentTaskId.trimmed();
    entry.traceId = traceId.trimmed().isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : traceId.trimmed();

    if (delegatedRequest) {
        const QString delegatedTarget = _delegate(taskId,
                                                  task,
                                                  displayLabel,
                                                  originChannel,
                                                  originChatId,
                                                  parentSession,
                                                  entry.targetNode,
                                                  entry.targetRole,
                                                  entry.targetTags,
                                                  entry.requiredTool,
                                                  entry.requiredChannel,
                                                  entry.requiredMemoryBackend,
                                                  entry.parentTaskId,
                                                  entry.traceId);
        if (delegatedTarget.startsWith("Error:", Qt::CaseInsensitive)) {
            outcome.message = delegatedTarget;
            return outcome;
        }

        if (!delegatedTarget.trimmed().isEmpty()) {
            entry.targetNode = delegatedTarget.trimmed();
        }
        _tasks.insert(taskId, entry);
        _sessionTasks[parentSession].insert(taskId);
        qDebug(lcSubagent) << "Delegated subagent" << taskId << "label:" << displayLabel
                           << "target:" << entry.targetNode << "role:" << entry.targetRole
                           << "channel:" << entry.requiredChannel;
        outcome.ok = true;
        outcome.taskId = taskId;
        outcome.displayLabel = displayLabel;
        outcome.item.taskId = taskId;
        outcome.item.task = task;
        outcome.item.label = displayLabel;
        outcome.item.delegated = true;
        outcome.item.targetNode = entry.targetNode;
        outcome.item.targetRole = entry.targetRole;
        outcome.item.targetTags = entry.targetTags;
        outcome.item.requiredTool = entry.requiredTool;
        outcome.item.requiredChannel = entry.requiredChannel;
        outcome.item.requiredMemoryBackend = entry.requiredMemoryBackend;
        outcome.message = QString("Subagent [%1] delegated to %2 (id: %3). I'll notify you when it completes.")
            .arg(displayLabel,
                 entry.targetNode.isEmpty() ? (entry.targetRole.isEmpty() ? "remote worker" : entry.targetRole)
                                            : entry.targetNode,
                 taskId);
        return outcome;
    }

    entry.watcher = QSharedPointer<QFutureWatcher<QString>>::create();

    connect(entry.watcher.data(), &QFutureWatcher<QString>::finished,
            this, [this, taskId]() { onTaskFinished(taskId); },
            Qt::QueuedConnection);

    auto callback = _execute;
    QFuture<QString> future = QtConcurrent::run(
        [callback, task, subSession, originChannel, originChatId, taskId, traceId = entry.traceId, cancelled]() -> QString {
            const QString result = callback(task, subSession, originChannel, originChatId, taskId, traceId);
            // ✅ 如果已取消，返回空字符串，onTaskFinished 会忽略
            if (cancelled->load()) {
                return QString();
            }
            return result;
        }
    );
    entry.watcher->setFuture(future);

    _tasks.insert(taskId, entry);
    _sessionTasks[parentSession].insert(taskId);

    qDebug(lcSubagent) << "Spawned subagent" << taskId << "label:" << displayLabel;
    outcome.ok = true;
    outcome.taskId = taskId;
    outcome.displayLabel = displayLabel;
    outcome.item.taskId = taskId;
    outcome.item.task = task;
    outcome.item.label = displayLabel;
    outcome.item.delegated = false;
    outcome.item.targetNode = entry.targetNode;
    outcome.item.targetRole = entry.targetRole;
    outcome.item.targetTags = entry.targetTags;
    outcome.item.requiredTool = entry.requiredTool;
    outcome.item.requiredChannel = entry.requiredChannel;
    outcome.item.requiredMemoryBackend = entry.requiredMemoryBackend;
    outcome.message = QString("Subagent [%1] started (id: %2). I'll notify you when it completes.")
        .arg(displayLabel, taskId);
    return outcome;
}

QString SubagentManager::spawn(
    const QString &task,
    const QString &label,
    const QString &originChannel,
    const QString &originChatId,
    const QString &sessionKey,
    const QString &targetNode,
    const QString &targetRole,
    const QString &parentTaskId,
    const QString &traceId,
    const QStringList &targetTags,
    const QString &requiredTool,
    const QString &requiredChannel,
    const QString &requiredMemoryBackend
) {
    return submit(task,
                  label,
                  originChannel,
                  originChatId,
                  sessionKey,
                  targetNode,
                  targetRole,
                  parentTaskId,
                  traceId,
                  targetTags,
                  requiredTool,
                  requiredChannel,
                  requiredMemoryBackend).message;
}

SubagentManager::SubmitResult SubagentManager::submit(
    const QString &task,
    const QString &label,
    const QString &originChannel,
    const QString &originChatId,
    const QString &sessionKey,
    const QString &targetNode,
    const QString &targetRole,
    const QString &parentTaskId,
    const QString &traceId,
    const QStringList &targetTags,
    const QString &requiredTool,
    const QString &requiredChannel,
    const QString &requiredMemoryBackend
) {
    SpawnRequest request;
    request.task = task;
    request.label = label;
    request.targetNode = targetNode;
    request.targetRole = targetRole;
    request.targetTags = targetTags;
    request.requiredTool = requiredTool;
    request.requiredChannel = requiredChannel;
    request.requiredMemoryBackend = requiredMemoryBackend;
    const SpawnOutcome outcome = spawnSingle(request,
                                             originChannel,
                                             originChatId,
                                             sessionKey,
                                             QString(),
                                             parentTaskId,
                                             traceId,
                                             targetNode,
                                             targetRole,
                                             targetTags,
                                             requiredTool,
                                             requiredChannel,
                                             requiredMemoryBackend);
    SubmitResult result;
    result.ok = outcome.ok;
    result.grouped = false;
    result.message = outcome.message;
    if (outcome.ok) {
        result.started.append(outcome.item);
    } else if (!outcome.message.trimmed().isEmpty()) {
        result.failures.append(outcome.message);
    }
    return result;
}

QString SubagentManager::spawnMany(const QList<SpawnRequest> &requests,
                                   const QString &groupLabel,
                                   const QString &originChannel,
                                   const QString &originChatId,
                                   const QString &sessionKey,
                                   const QString &parentTaskId,
                                   const QString &traceId,
                                   const QString &targetNode,
                                   const QString &targetRole,
                                   const QStringList &targetTags,
                                   const QString &requiredTool,
                                   const QString &requiredChannel,
                                   const QString &requiredMemoryBackend) {
    return submitMany(requests,
                      groupLabel,
                      originChannel,
                      originChatId,
                      sessionKey,
                      parentTaskId,
                      traceId,
                      targetNode,
                      targetRole,
                      targetTags,
                      requiredTool,
                      requiredChannel,
                      requiredMemoryBackend).message;
}

SubagentManager::SubmitResult SubagentManager::submitMany(const QList<SpawnRequest> &requests,
                                                          const QString &groupLabel,
                                                          const QString &originChannel,
                                                          const QString &originChatId,
                                                          const QString &sessionKey,
                                                          const QString &parentTaskId,
                                                          const QString &traceId,
                                                          const QString &targetNode,
                                                          const QString &targetRole,
                                                          const QStringList &targetTags,
                                                          const QString &requiredTool,
                                                          const QString &requiredChannel,
                                                          const QString &requiredMemoryBackend) {
    if (requests.isEmpty()) {
        SubmitResult result;
        result.message = QStringLiteral("Error: tasks is required");
        result.failures.append(result.message);
        return result;
    }

    if (requests.size() == 1) {
        const SpawnRequest &only = requests.first();
        return submit(only.task,
                      only.label,
                      originChannel,
                      originChatId,
                      sessionKey,
                      only.targetNode.trimmed().isEmpty() ? targetNode : only.targetNode,
                      only.targetRole.trimmed().isEmpty() ? targetRole : only.targetRole,
                      parentTaskId,
                      traceId,
                      only.targetTags.isEmpty() ? targetTags : only.targetTags,
                      only.requiredTool.trimmed().isEmpty() ? requiredTool : only.requiredTool,
                      only.requiredChannel.trimmed().isEmpty() ? requiredChannel : only.requiredChannel,
                      only.requiredMemoryBackend.trimmed().isEmpty() ? requiredMemoryBackend : only.requiredMemoryBackend);
    }

    const QString parentSession = sessionKey.trimmed().isEmpty()
                                  ? (originChannel + ":" + originChatId)
                                  : sessionKey;
    const QString groupId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(10);

    TaskGroup group;
    group.id = groupId;
    group.label = groupLabel.trimmed().isEmpty()
        ? QString("Subagent batch (%1 tasks)").arg(requests.size())
        : groupLabel.trimmed();
    group.originChannel = originChannel;
    group.originChatId = originChatId;
    group.sessionKey = parentSession;
    group.expected = requests.size();
    _groups.insert(groupId, group);

    SubmitResult result;
    result.grouped = true;
    result.groupId = groupId;
    result.groupLabel = group.label;

    QStringList started;
    int failureIndex = 0;
    for (const SpawnRequest &request : requests) {
        const SpawnOutcome outcome = spawnSingle(request,
                                                 originChannel,
                                                 originChatId,
                                                 parentSession,
                                                 groupId,
                                                 parentTaskId,
                                                 traceId,
                                                 targetNode,
                                                 targetRole,
                                                 targetTags,
                                                 requiredTool,
                                                 requiredChannel,
                                                 requiredMemoryBackend);
        if (outcome.ok) {
            started.append(QString("%1 (%2)").arg(outcome.displayLabel, outcome.taskId));
            result.started.append(outcome.item);
        } else {
            result.failures.append(outcome.message);
            if (_groups.contains(groupId)) {
                GroupResult item;
                item.taskId = QString("failed-%1").arg(++failureIndex);
                item.task = request.task.trimmed();
                item.label = request.label.trimmed().isEmpty() ? QStringLiteral("failed-task") : request.label.trimmed();
                item.status = QStringLiteral("failed");
                item.content = outcome.message.trimmed().isEmpty()
                    ? QStringLiteral("Failed to start grouped subagent")
                    : outcome.message.trimmed();
                _groups[groupId].results.insert(item.taskId, item);
            }
        }
    }

    if (started.isEmpty()) {
        _groups.remove(groupId);
        result.ok = false;
        result.message = result.failures.isEmpty()
            ? QStringLiteral("Error: failed to start grouped subagents")
            : result.failures.join("\n");
        return result;
    }

    if (_groups.contains(groupId) && _groups[groupId].results.size() >= _groups[groupId].expected) {
        const TaskGroup completed = _groups[groupId];
        _groups.remove(groupId);
        publishGroupedAnnouncement(completed);
    }

    QString message = QString("Started %1 grouped subagents for '%2'. I'll send one aggregated update when all complete.")
        .arg(started.size())
        .arg(group.label);
    if (!started.isEmpty()) {
        message += QString("\nTasks: %1").arg(started.join(", "));
    }
    if (!result.failures.isEmpty()) {
        message += QString("\nFailed to start %1 task(s): %2").arg(result.failures.size()).arg(result.failures.join(" | "));
    }
    result.ok = true;
    result.message = message;
    return result;
}

QStringList SubagentManager::delegatedTaskIds() const {
    QStringList out;
    out.reserve(_tasks.size());
    for (auto it = _tasks.constBegin(); it != _tasks.constEnd(); ++it) {
        if (it.value().delegated) {
            out.append(it.key());
        }
    }
    return out;
}

void SubagentManager::publishSingleAnnouncement(const TaskEntry &entry,
                                                const QString &status,
                                                const QString &result,
                                                const QString &worker) {
    const QString normalized = status.trimmed().toLower();
    QString statusText = "failed";
    if (normalized == "ok" || normalized == "success" || normalized == "succeeded") {
        statusText = "completed successfully";
    } else if (normalized == "cancelled" || normalized == "canceled") {
        statusText = "was cancelled";
    }

    const QString workerText = worker.trimmed().isEmpty() ? QString() : QString("\nWorker: %1").arg(worker.trimmed());
    const QString announce = QString(
        "[Subagent '%1' %2]\n\n"
        "Task: %3%4\n\n"
        "Result:\n%5\n\n"
        "Summarize this naturally for the user in 1-2 sentences."
    ).arg(entry.label, statusText, entry.task, workerText, result);

    bus::InboundMessage msg;
    msg.channel = "system";
    msg.senderId = "subagent";
    msg.chatId = entry.originChannel + ":" + entry.originChatId;
    msg.content = announce;
    _bus.publishInbound(msg);
}

void SubagentManager::publishGroupedAnnouncement(const TaskGroup &group) {
    QList<GroupResult> results = group.results.values();
    std::sort(results.begin(), results.end(), [](const GroupResult &left, const GroupResult &right) {
        return left.taskId < right.taskId;
    });

    QStringList lines;
    lines << QString("[Subagent Group '%1' completed]").arg(group.label);
    lines << QString();
    lines << QString("All %1 grouped subtasks finished.").arg(group.expected);
    lines << QString();
    for (const GroupResult &item : results) {
        QString line = QString("- [%1] %2").arg(item.status, item.label);
        if (!item.worker.trimmed().isEmpty()) {
            line += QString(" @ %1").arg(item.worker.trimmed());
        }
        lines << line;
        lines << QString("  Task: %1").arg(item.task);
        lines << QString("  Result: %1").arg(item.content.simplified());
    }
    lines << QString();
    lines << QString("Summarize the combined outcome for the user, compare the subtasks, and give the integrated next step in 2-4 sentences.");

    bus::InboundMessage msg;
    msg.channel = "system";
    msg.senderId = "subagent";
    msg.chatId = group.originChannel + ":" + group.originChatId;
    msg.content = lines.join("\n");
    _bus.publishInbound(msg);
}

void SubagentManager::recordGroupedCompletion(const TaskEntry &entry,
                                              const QString &status,
                                              const QString &result,
                                              const QString &worker) {
    if (entry.groupId.trimmed().isEmpty() || !_groups.contains(entry.groupId)) {
        publishSingleAnnouncement(entry, status, result, worker);
        return;
    }

    TaskGroup &group = _groups[entry.groupId];
    GroupResult item;
    item.taskId = entry.id;
    item.task = entry.task;
    item.label = entry.label;
    const QString normalized = status.trimmed().toLower();
    if (normalized == "ok" || normalized == "success" || normalized == "succeeded") {
        item.status = QStringLiteral("succeeded");
    } else if (normalized == "cancelled" || normalized == "canceled") {
        item.status = QStringLiteral("cancelled");
    } else if (normalized.isEmpty()) {
        item.status = QStringLiteral("unknown");
    } else {
        item.status = QStringLiteral("failed");
    }
    item.worker = worker.trimmed();
    item.content = result.trimmed().isEmpty() ? QStringLiteral("No response content was returned.") : result.trimmed();
    group.results.insert(entry.id, item);

    if (group.results.size() >= group.expected) {
        const TaskGroup completed = group;
        _groups.remove(entry.groupId);
        publishGroupedAnnouncement(completed);
    }
}

void SubagentManager::handleDelegatedResult(const distributed::TaskResultEnvelope &result) {
    if (!_tasks.contains(result.taskId)) {
        return;
    }

    const TaskEntry entry = _tasks.value(result.taskId);
    if (!entry.delegated) {
        return;
    }

    if (entry.cancelled && entry.cancelled->load()) {
        _tasks.remove(result.taskId);
        if (_sessionTasks.contains(entry.sessionKey)) {
            _sessionTasks[entry.sessionKey].remove(result.taskId);
            if (_sessionTasks[entry.sessionKey].isEmpty()) {
                _sessionTasks.remove(entry.sessionKey);
            }
        }
        return;
    }

    const QString status = result.status.trimmed().toLower();
    QString response = result.output.value("content").toString().trimmed();
    if (response.isEmpty()) {
        response = !result.message.trimmed().isEmpty()
            ? result.message.trimmed()
            : result.error.value("message").toString().trimmed();
    }
    if (response.isEmpty()) {
        response = "No response content was returned.";
    }
    recordGroupedCompletion(entry,
                            status,
                            response,
                            result.producerNode.isEmpty() ? QStringLiteral("unknown-node") : result.producerNode);

    qDebug(lcSubagent) << "Delegated subagent" << result.taskId << status;

    _tasks.remove(result.taskId);
    if (_sessionTasks.contains(entry.sessionKey)) {
        _sessionTasks[entry.sessionKey].remove(result.taskId);
        if (_sessionTasks[entry.sessionKey].isEmpty()) {
            _sessionTasks.remove(entry.sessionKey);
        }
    }
}

void SubagentManager::onTaskFinished(const QString &taskId) {
    if (!_tasks.contains(taskId)) {
        // ✅ 任务已被 cancel 移除，结果直接丢弃，防止幽灵消息
        qDebug(lcSubagent) << "Subagent" << taskId << "finished but was already cancelled, discarding";
        return;
    }

    const TaskEntry entry = _tasks.value(taskId);
    if (entry.delegated) {
        return;
    }

    // ✅ 双重检查取消令牌
    if (entry.cancelled && entry.cancelled->load()) {
        qDebug(lcSubagent) << "Subagent" << taskId << "cancelled, discarding result";
        _tasks.remove(taskId);
        if (_sessionTasks.contains(entry.sessionKey)) {
            _sessionTasks[entry.sessionKey].remove(taskId);
            if (_sessionTasks[entry.sessionKey].isEmpty()) {
                _sessionTasks.remove(entry.sessionKey);
            }
        }
        return;
    }

    QString result;
    QString status = "ok";
    try {
        result = entry.watcher->future().result();
    } catch (...) {
        status = "error";
        result = "Error: subagent execution failed with exception";
    }
    if (result.trimmed().isEmpty()) {
        // 空结果可能来自取消，也不发送
        _tasks.remove(taskId);
        return;
    }
    if (result.startsWith("Error")) {
        status = "error";
    }

    recordGroupedCompletion(entry, status, result, QStringLiteral("local-subagent"));

    qDebug(lcSubagent) << "Subagent" << taskId << status;

    _tasks.remove(taskId);
    if (_sessionTasks.contains(entry.sessionKey)) {
        _sessionTasks[entry.sessionKey].remove(taskId);
        if (_sessionTasks[entry.sessionKey].isEmpty()) {
            _sessionTasks.remove(entry.sessionKey);
        }
    }
}

int SubagentManager::cancelBySession(const QString &sessionKey) {
    if (!_sessionTasks.contains(sessionKey)) {
        return 0;
    }

    const QSet<QString> ids = _sessionTasks.value(sessionKey);
    QSet<QString> groupIds;
    int cancelled = 0;
    for (const QString &id : ids) {
        if (!_tasks.contains(id)) continue;

        TaskEntry &entry = _tasks[id];
        // ✅ 设置取消令牌：底层 LLM 调用无法中断，但结果会被丢弃
        if (entry.cancelled) {
            entry.cancelled->store(true);
        }
        if (entry.delegated && _cancel) {
            _cancel(id, entry.sessionKey, entry.originChannel, entry.originChatId);
        }
        if (!entry.groupId.trimmed().isEmpty()) {
            groupIds.insert(entry.groupId);
        }
        ++cancelled;
        qDebug(lcSubagent) << "Cancellation token set for subagent" << id;
    }

    // 从索引中移除（onTaskFinished 通过 entry 不存在来跳过）
    for (const QString &id : ids) {
        _tasks.remove(id);
    }
    _sessionTasks.remove(sessionKey);
    for (const QString &groupId : groupIds) {
        _groups.remove(groupId);
    }

    return cancelled;
}

int SubagentManager::runningCount() const {
    return _tasks.size();
}

} // namespace yaos::runtime
