#ifndef YAOS_RUNTIME_SUBAGENTMANAGER_H
#define YAOS_RUNTIME_SUBAGENTMANAGER_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QFutureWatcher>

#include <atomic>
#include <functional>

#include "../bus/MessageBus.h"
#include "../distributed/Contracts.h"

namespace yaos::runtime {

class SubagentManager : public QObject {
    Q_OBJECT
public:
    struct SpawnRequest {
        QString task;
        QString label;
        QString targetNode;
        QString targetRole;
        QStringList targetTags;
        QString requiredTool;
        QString requiredChannel;
        QString requiredMemoryBackend;
    };

    struct SubmitItem {
        QString taskId;
        QString task;
        QString label;
        bool delegated = false;
        QString targetNode;
        QString targetRole;
        QStringList targetTags;
        QString requiredTool;
        QString requiredChannel;
        QString requiredMemoryBackend;
    };

    struct SubmitResult {
        bool ok = false;
        bool grouped = false;
        QString message;
        QString groupId;
        QString groupLabel;
        QList<SubmitItem> started;
        QStringList failures;
    };

    using ExecuteCallback = std::function<QString(
        const QString &task,
        const QString &sessionKey,
        const QString &channel,
        const QString &chatId,
        const QString &taskId,
        const QString &traceId
    )>;

    using DelegateCallback = std::function<QString(
        const QString &taskId,
        const QString &task,
        const QString &label,
        const QString &originChannel,
        const QString &originChatId,
        const QString &sessionKey,
        const QString &targetNode,
        const QString &targetRole,
        const QStringList &targetTags,
        const QString &requiredTool,
        const QString &requiredChannel,
        const QString &requiredMemoryBackend,
        const QString &parentTaskId,
        const QString &traceId
    )>;

    using CancelCallback = std::function<void(
        const QString &taskId,
        const QString &sessionKey,
        const QString &originChannel,
        const QString &originChatId
    )>;

    explicit SubagentManager(bus::MessageBus &bus, QObject *parent = nullptr);

    void setExecuteCallback(const ExecuteCallback &callback);
    void setDelegateCallback(const DelegateCallback &callback);
    void setCancelCallback(const CancelCallback &callback);

    QString spawn(
        const QString &task,
        const QString &label = QString(),
        const QString &originChannel = "cli",
        const QString &originChatId = "direct",
        const QString &sessionKey = QString(),
        const QString &targetNode = QString(),
        const QString &targetRole = QString(),
        const QString &parentTaskId = QString(),
        const QString &traceId = QString(),
        const QStringList &targetTags = QStringList(),
        const QString &requiredTool = QString(),
        const QString &requiredChannel = QString(),
        const QString &requiredMemoryBackend = QString()
    );
    QString spawnMany(const QList<SpawnRequest> &requests,
                      const QString &groupLabel = QString(),
                      const QString &originChannel = "cli",
                      const QString &originChatId = "direct",
                      const QString &sessionKey = QString(),
                      const QString &parentTaskId = QString(),
                      const QString &traceId = QString(),
                      const QString &targetNode = QString(),
                      const QString &targetRole = QString(),
                      const QStringList &targetTags = QStringList(),
                      const QString &requiredTool = QString(),
                      const QString &requiredChannel = QString(),
                      const QString &requiredMemoryBackend = QString());
    SubmitResult submit(const QString &task,
                        const QString &label = QString(),
                        const QString &originChannel = "cli",
                        const QString &originChatId = "direct",
                        const QString &sessionKey = QString(),
                        const QString &targetNode = QString(),
                        const QString &targetRole = QString(),
                        const QString &parentTaskId = QString(),
                        const QString &traceId = QString(),
                        const QStringList &targetTags = QStringList(),
                        const QString &requiredTool = QString(),
                        const QString &requiredChannel = QString(),
                        const QString &requiredMemoryBackend = QString());
    SubmitResult submitMany(const QList<SpawnRequest> &requests,
                            const QString &groupLabel = QString(),
                            const QString &originChannel = "cli",
                            const QString &originChatId = "direct",
                            const QString &sessionKey = QString(),
                            const QString &parentTaskId = QString(),
                            const QString &traceId = QString(),
                            const QString &targetNode = QString(),
                            const QString &targetRole = QString(),
                            const QStringList &targetTags = QStringList(),
                            const QString &requiredTool = QString(),
                            const QString &requiredChannel = QString(),
                            const QString &requiredMemoryBackend = QString());

    QStringList delegatedTaskIds() const;
    void handleDelegatedResult(const distributed::TaskResultEnvelope &result);
    int cancelBySession(const QString &sessionKey);
    int runningCount() const;

private:
    struct TaskEntry {
        QString id;
        QString task;
        QString label;
        QString originChannel;
        QString originChatId;
        QString sessionKey;
        bool delegated = false;
        QString targetNode;
        QString targetRole;
        QStringList targetTags;
        QString requiredTool;
        QString requiredChannel;
        QString requiredMemoryBackend;
        QString groupId;
        QString parentTaskId;
        QString traceId;
        QSharedPointer<QFutureWatcher<QString>> watcher;
        // ✅ 取消令牌：设为 true 后任务结果将被丢弃（底层线程无法强制中断，
        //    但可以防止"幽灵消息"被发送到总线）
        QSharedPointer<std::atomic<bool>> cancelled;
    };

    struct GroupResult {
        QString taskId;
        QString task;
        QString label;
        QString status;
        QString worker;
        QString content;
    };

    struct TaskGroup {
        QString id;
        QString label;
        QString originChannel;
        QString originChatId;
        QString sessionKey;
        int expected = 0;
        QHash<QString, GroupResult> results;
    };

    struct SpawnOutcome {
        bool ok = false;
        QString taskId;
        QString message;
        QString displayLabel;
        SubmitItem item;
    };

    SpawnOutcome spawnSingle(const SpawnRequest &request,
                             const QString &originChannel,
                             const QString &originChatId,
                             const QString &sessionKey,
                             const QString &groupId = QString(),
                             const QString &parentTaskId = QString(),
                             const QString &traceId = QString(),
                             const QString &targetNode = QString(),
                             const QString &targetRole = QString(),
                             const QStringList &targetTags = QStringList(),
                             const QString &requiredTool = QString(),
                             const QString &requiredChannel = QString(),
                             const QString &requiredMemoryBackend = QString());
    void publishSingleAnnouncement(const TaskEntry &entry,
                                   const QString &status,
                                   const QString &result,
                                   const QString &worker = QString());
    void publishGroupedAnnouncement(const TaskGroup &group);
    void recordGroupedCompletion(const TaskEntry &entry,
                                 const QString &status,
                                 const QString &result,
                                 const QString &worker = QString());
    void onTaskFinished(const QString &taskId);

    bus::MessageBus &_bus;
    ExecuteCallback _execute;
    DelegateCallback _delegate;
    CancelCallback _cancel;
    QHash<QString, TaskEntry> _tasks;
    QHash<QString, QSet<QString>> _sessionTasks;
    QHash<QString, TaskGroup> _groups;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_SUBAGENTMANAGER_H
