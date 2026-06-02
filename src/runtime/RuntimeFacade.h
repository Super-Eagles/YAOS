#ifndef YAOS_RUNTIME_RUNTIMEFACADE_H
#define YAOS_RUNTIME_RUNTIMEFACADE_H

#include <functional>
#include <memory>

#include <QJsonObject>
#include <QString>
#include <QVector>

#include "../config/Config.h"
#include "../distributed/Contracts.h"
#include "ApprovalStore.h"
#include "AutomationStore.h"
#include "NotificationCenter.h"
#include "PluginRegistry.h"
#include "ResourceCatalog.h"
#include "RuntimeTypes.h"
#include "SkillRegistry.h"
#include "TaskStore.h"

namespace yaos::runtime {

using StreamProgressCallback = std::function<void(const QString &contentDelta, const QString &thinkingDelta)>;

class IRuntimeFacade {
public:
    virtual ~IRuntimeFacade() = default;

    virtual StatusSnapshot statusSnapshot() = 0;
    virtual bool initializeWorkspace(QString *message = nullptr) = 0;
    virtual bool reloadFromDisk(const QString &modelOverride = QString(),
                                const QString &providerOverride = QString()) = 0;
    virtual bool startGatewayServices() = 0;
    virtual void stopGatewayServices() = 0;
    virtual bool gatewayRunning() const = 0;
    virtual QVector<ApprovalRecord> recentApprovals(int limit = 50,
                                                    const QString &state = QString()) = 0;
    virtual bool resolveApproval(const QString &approvalId,
                                 const QString &decision,
                                 const QString &scope = QString(),
                                 const QString &note = QString()) = 0;
    virtual QVector<NotificationRecord> recentNotifications(int limit = 50, bool unreadOnly = false) = 0;
    virtual void markAllNotificationsRead() = 0;
    virtual QVector<TaskRecord> recentTasks(int limit = 20) = 0;
    virtual QVector<EventRecord> recentEvents(int limit = 50) = 0;
    virtual QVector<distributed::NodeDescriptor> recentNodes(int limit = 64, bool onlineOnly = false) = 0;
    virtual QJsonObject previewDelegationRoute(const QJsonObject &request = QJsonObject()) = 0;
    virtual QJsonObject submitDelegationRequest(const QJsonObject &request = QJsonObject()) = 0;
    virtual ResourceSummary resourceSummary() = 0;
    virtual QVector<ResourceRecord> recentResources(int limit = 100,
                                                    const QString &kind = QString()) = 0;
    virtual QVector<AutomationRecord> automations(int limit = 100) = 0;
    virtual QVector<AutomationRunRecord> automationRuns(int limit = 120,
                                                        const QString &automationId = QString()) = 0;
    virtual AutomationRecord automation(const QString &id) = 0;
    virtual QString saveAutomation(const AutomationRecord &record, QString *error = nullptr) = 0;
    virtual bool removeAutomation(const QString &id) = 0;
    virtual QString runAutomation(const QString &id,
                                  QString *error = nullptr,
                                  const QString &sessionKey = QString("automation:manual")) = 0;
    virtual QVector<PluginRecord> plugins() = 0;
    virtual QVector<SkillRecord> skills() = 0;
    virtual ChatTurnResult processMessageDetailed(const QString &content,
                                                  const QString &sessionKey = QString("gui:primary"),
                                                  const QString &channel = QString("gui"),
                                                  const QString &chatId = QString("desktop"),
                                                  const QString &modelOverride = QString(),
                                                  const QString &providerOverride = QString()) = 0;
    virtual void setStreamProgressCallback(StreamProgressCallback cb) { Q_UNUSED(cb) }
    virtual QString processMessage(const QString &content,
                                   const QString &sessionKey = QString("gui:primary"),
                                   const QString &channel = QString("gui"),
                                   const QString &chatId = QString("desktop"),
                                   const QString &modelOverride = QString(),
                                   const QString &providerOverride = QString()) = 0;
};

struct RuntimeFacadeSelection {
    std::unique_ptr<IRuntimeFacade> facade;
    QString requestedMode;
    QString activeMode;
    QString fallbackReason;

    bool usedFallback() const {
        return !requestedMode.isEmpty() && activeMode != requestedMode;
    }
};

RuntimeFacadeSelection createRuntimeFacade(const config::Config &config);

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_RUNTIMEFACADE_H
