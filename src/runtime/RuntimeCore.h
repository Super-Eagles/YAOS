#ifndef YAOS_RUNTIME_RUNTIMECORE_H
#define YAOS_RUNTIME_RUNTIMECORE_H

#include <memory>

#include <QJsonObject>
#include <QMutex>
#include <QSet>
#include <QThread>
#include <QTimer>

#include "../agent/AgentLoop.h"
#include "../bus/MessageBus.h"
#include "../channels/ChannelManager.h"
#include "../config/Config.h"
#include "../distributed/Contracts.h"
#include "../distributed/P2PCluster.h"
#include "../providers/LLMProvider.h"
#include "ApprovalStore.h"
#include "AutomationStore.h"
#include "CronService.h"
#include "EventLog.h"
#include "HeartbeatService.h"
#include "MCPManager.h"
#include "NotificationCenter.h"
#include "PluginRegistry.h"
#include "ResourceCatalog.h"
#include "RuntimeFacade.h"
#include "SkillRegistry.h"
#include "SubagentManager.h"
#include "TaskStore.h"

namespace yaos::runtime {

class RuntimeCore : public IRuntimeFacade {
public:
    RuntimeCore();
    explicit RuntimeCore(const config::Config &configOverride);
    ~RuntimeCore() override;

    StatusSnapshot statusSnapshot() override;
    bool initializeWorkspace(QString *message = nullptr) override;
    bool reloadFromDisk(const QString &modelOverride = QString(),
                        const QString &providerOverride = QString()) override;
    bool startGatewayServices() override;
    void stopGatewayServices() override;
    bool gatewayRunning() const override;
    QVector<ApprovalRecord> recentApprovals(int limit = 50,
                                            const QString &state = QString()) override;
    bool resolveApproval(const QString &approvalId,
                         const QString &decision,
                         const QString &scope = QString(),
                         const QString &note = QString()) override;
    QVector<NotificationRecord> recentNotifications(int limit = 50, bool unreadOnly = false) override;
    void markAllNotificationsRead() override;
    QVector<TaskRecord> recentTasks(int limit = 20) override;
    QVector<EventRecord> recentEvents(int limit = 50) override;
    QVector<distributed::NodeDescriptor> recentNodes(int limit = 64, bool onlineOnly = false) override;
    QJsonObject previewDelegationRoute(const QJsonObject &request = QJsonObject()) override;
    QJsonObject submitDelegationRequest(const QJsonObject &request = QJsonObject()) override;
    ResourceSummary resourceSummary() override;
    QVector<ResourceRecord> recentResources(int limit = 100,
                                            const QString &kind = QString()) override;
    QVector<AutomationRecord> automations(int limit = 100) override;
    QVector<AutomationRunRecord> automationRuns(int limit = 120,
                                                const QString &automationId = QString()) override;
    AutomationRecord automation(const QString &id) override;
    QString saveAutomation(const AutomationRecord &record, QString *error = nullptr) override;
    bool removeAutomation(const QString &id) override;
    QString runAutomation(const QString &id,
                          QString *error = nullptr,
                          const QString &sessionKey = QString("automation:manual")) override;
    QVector<PluginRecord> plugins() override;
    QVector<SkillRecord> skills() override;
    ChatTurnResult processMessageDetailed(const QString &content,
                                         const QString &sessionKey = QString("gui:primary"),
                                         const QString &channel = QString("gui"),
                                         const QString &chatId = QString("desktop"),
                                         const QString &modelOverride = QString(),
                                         const QString &providerOverride = QString()) override;
    void setStreamProgressCallback(StreamProgressCallback cb) override;
    QString processMessage(const QString &content,
                           const QString &sessionKey = QString("gui:primary"),
                           const QString &channel = QString("gui"),
                           const QString &chatId = QString("desktop"),
                           const QString &modelOverride = QString(),
                           const QString &providerOverride = QString()) override;

    bool ensureModelReady(const QString &modelOverride = QString(),
                          const QString &providerOverride = QString());
    QJsonObject serviceHealth(const QString &modelOverride = QString(),
                              const QString &providerOverride = QString());
    config::Config activeConfig() const;
    CronStatus cronStatus() const;
    QStringList enabledChannels() const;
    QString agentThreadName() const;

private:
    bool ensureInitialized(const QString &modelOverride = QString(),
                           const QString &providerOverride = QString());
    void ensureSystemStores(const QString &workspace);
    void logEvent(const QString &level,
                  const QString &category,
                  const QString &message,
                  const QJsonObject &metadata = QJsonObject());
    AutomationRecord hydrateAutomationRecord(const AutomationRecord &record);
    QVector<AutomationRecord> hydrateAutomationRecords(const QVector<AutomationRecord> &records);
    bool syncAutomationSchedule(AutomationRecord &record, QString *error = nullptr);
    void reconcileAutomationSchedules();
    ChatTurnResult executeAutomationRecord(const AutomationRecord &record,
                                          const QString &triggerSource,
                                          const QString &sessionKey = QString(),
                                          const QString &cronJobId = QString());
    QString submitDelegatedSubagent(const QString &taskId,
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
                                    const QString &traceId);
    bool cancelDelegatedSubagent(const QString &taskId,
                                 const QString &sessionKey,
                                 const QString &originChannel,
                                 const QString &originChatId);
    QList<distributed::NodeDescriptor> resolveDelegationTargets(const QString &targetRole,
                                                                const QString &channel,
                                                                const QStringList &targetTags = QStringList(),
                                                                const QString &requiredTool = QString(),
                                                                const QString &requiredMemoryBackend = QString(),
                                                                int limit = 8,
                                                                QString *resolutionSource = nullptr) const;
    void startDelegationPolling();
    void stopDelegationPolling();
    void pollDelegatedTaskBus();
    void executeDelegatedTask(distributed::TaskEnvelope task);
    void publishNodePresence(bool online,
                             int minIntervalMs = 0,
                             const QString &reason = QString());
    void teardownRuntime();
    QString invokeProcessDirect(const QString &content,
                                const QString &sessionKey,
                                const QString &channel,
                                const QString &chatId,
                                const QString &modelOverride = QString(),
                                const QString &providerOverride = QString(),
                                const QJsonObject &runtimeMetadata = QJsonObject());
    agent::AgentTurnResult invokeProcessDirectDetailed(const QString &content,
                                                       const QString &sessionKey,
                                                       const QString &channel,
                                                       const QString &chatId,
                                                       const QString &modelOverride = QString(),
                                                       const QString &providerOverride = QString(),
                                                       const QJsonObject &runtimeMetadata = QJsonObject());
    static QString actualProviderLabel(const providers::LLMProvider &provider);

private:
    config::Config _config;
    config::Config _configOverride;
    bool _hasConfigOverride = false;
    std::unique_ptr<bus::MessageBus> _bus;
    std::unique_ptr<providers::LLMProvider> _provider;
    std::unique_ptr<CronService> _cron;
    std::unique_ptr<ApprovalStore> _approvals;
    std::unique_ptr<AutomationStore> _automations;
    std::unique_ptr<AutomationRunStore> _automationRuns;
    std::unique_ptr<EventLog> _eventLog;
    std::unique_ptr<MCPManager> _mcp;
    std::unique_ptr<NotificationCenter> _notifications;
    std::unique_ptr<PluginRegistry> _plugins;
    std::unique_ptr<SkillRegistry> _skills;
    std::unique_ptr<ResourceCatalog> _resources;
    std::unique_ptr<distributed::INodeRegistryClient> _nodeRegistry;
    std::unique_ptr<SubagentManager> _subagents;
    std::unique_ptr<distributed::ITaskBus> _taskBus;
    QScopedPointer<distributed::P2PCluster> _p2pClusterOwner; // owns QObject lifetime
    std::unique_ptr<TaskStore> _taskStore;
    std::unique_ptr<HeartbeatService> _heartbeat;
    std::unique_ptr<channels::ChannelManager> _channels;
    std::unique_ptr<agent::AgentLoop> _agent;
    std::unique_ptr<QThread> _agentThread;
    std::unique_ptr<QTimer> _delegationPoller;
    mutable QMutex _presencePublishMutex;
    mutable QMutex _delegatedWorkerLeaseMutex;
    QSet<QString> _activeDelegatedWorkerTasks;
    qint64 _lastNodePresencePublishAtMs = 0;
    bool _initialized = false;
    bool _gatewayRunning = false;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_RUNTIMECORE_H
