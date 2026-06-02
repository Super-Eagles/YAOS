#ifndef YAOS_RUNTIME_RUNTIMECLIENTFACADE_H
#define YAOS_RUNTIME_RUNTIMECLIENTFACADE_H

#include <memory>

#include "../distributed/Contracts.h"
#include "RuntimeFacade.h"

namespace yaos::runtime {

class RuntimeClientFacade : public IRuntimeFacade {
public:
    explicit RuntimeClientFacade(std::unique_ptr<distributed::IRuntimeClient> client);
    ~RuntimeClientFacade() override = default;

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
    QString processMessage(const QString &content,
                           const QString &sessionKey = QString("gui:primary"),
                           const QString &channel = QString("gui"),
                           const QString &chatId = QString("desktop"),
                           const QString &modelOverride = QString(),
                           const QString &providerOverride = QString()) override;

private:
    QJsonObject invoke(const QString &method, const QJsonObject &payload = QJsonObject()) const;

private:
    mutable std::unique_ptr<distributed::IRuntimeClient> _client;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_RUNTIMECLIENTFACADE_H
