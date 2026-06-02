#ifndef YAOS_APP_APPLICATIONCONTROLLER_H
#define YAOS_APP_APPLICATIONCONTROLLER_H

#include <memory>

#include <QStringList>
#include <QTimer>

#include "../daemon/LocalDaemonServer.h"
#include "../runtime/RuntimeCore.h"
#include "../runtime/RuntimeTypes.h"

namespace yaos::memory {
class MemoryHttpServer;
class MemoryServiceCore;
}

namespace yaos::control {
class ControlHttpServer;
class ControlServiceCore;
}

namespace yaos::runtime {
class LocalRuntimeClient;
class RuntimeHttpServer;
}

namespace yaos::app {

enum class RunResult {
    Ok,
    Error,
    EnterEventLoop
};

using StatusSnapshot = runtime::StatusSnapshot;
using ChatTurnResult = runtime::ChatTurnResult;

class ApplicationController {
public:
    ApplicationController();
    ~ApplicationController();

    RunResult run(const QStringList &args);
    StatusSnapshot statusSnapshot();
    bool initializeWorkspace(QString *message = nullptr);
    bool reloadFromDisk(const QString &modelOverride = QString(),
                        const QString &providerOverride = QString());
    bool startGatewayServices();
    void stopGatewayServices();
    bool gatewayRunning() const;
    QVector<runtime::ApprovalRecord> recentApprovals(int limit = 50, const QString &state = QString());
    bool resolveApproval(const QString &approvalId,
                         const QString &decision,
                         const QString &scope = QString(),
                         const QString &note = QString());
    QVector<runtime::NotificationRecord> recentNotifications(int limit = 50, bool unreadOnly = false);
    void markAllNotificationsRead();
    QVector<runtime::TaskRecord> recentTasks(int limit = 20);
    QVector<runtime::EventRecord> recentEvents(int limit = 50);
    runtime::ResourceSummary resourceSummary();
    QVector<runtime::ResourceRecord> recentResources(int limit = 100, const QString &kind = QString());
    QVector<runtime::AutomationRecord> automations(int limit = 100);
    QVector<runtime::AutomationRunRecord> automationRuns(int limit = 120,
                                                         const QString &automationId = QString());
    runtime::AutomationRecord automation(const QString &id);
    QString saveAutomation(const runtime::AutomationRecord &record, QString *error = nullptr);
    bool removeAutomation(const QString &id);
    QString runAutomation(const QString &id,
                          QString *error = nullptr,
                          const QString &sessionKey = QString("automation:manual"));
    QVector<runtime::PluginRecord> plugins();
    QVector<runtime::SkillRecord> skills();
    ChatTurnResult processMessageDetailed(const QString &content,
                                         const QString &sessionKey = QString("gui:primary"),
                                         const QString &channel = QString("gui"),
                                         const QString &chatId = QString("desktop"),
                                         const QString &modelOverride = QString(),
                                         const QString &providerOverride = QString());
    QString processMessage(const QString &content,
                           const QString &sessionKey = QString("gui:primary"),
                           const QString &channel = QString("gui"),
                           const QString &chatId = QString("desktop"),
                           const QString &modelOverride = QString(),
                           const QString &providerOverride = QString());

private:
    RunResult init();
    RunResult config();
    RunResult status();
    RunResult providerLogin(const QStringList &args);
    RunResult providerPoll(const QStringList &args);
    RunResult providerRefresh(const QStringList &args);
    RunResult providerLogout(const QStringList &args);
    RunResult providerStatus(const QStringList &args);
    RunResult providerModels(const QStringList &args);
    RunResult routePreview(const QStringList &args);
    RunResult submitDelegation(const QStringList &args);
    RunResult templateExport(const QStringList &args);
    RunResult templateImport(const QStringList &args);
    RunResult templatePush(const QStringList &args);
    RunResult templatePull(const QStringList &args);
    RunResult agent(const QStringList &args);
    RunResult gateway();
    RunResult daemon(const QStringList &args);
    RunResult runtimeService(const QStringList &args);
    RunResult memoryService(const QStringList &args);
    RunResult controlService(const QStringList &args);
    RunResult help();

private:
    std::unique_ptr<::yaos::daemon::LocalDaemonServer> _daemonServer;
    std::unique_ptr<::yaos::runtime::LocalRuntimeClient> _runtimeServiceClient;
    std::unique_ptr<::yaos::runtime::RuntimeHttpServer> _runtimeHttpServer;
    std::unique_ptr<::yaos::memory::MemoryServiceCore> _memoryServiceCore;
    std::unique_ptr<::yaos::memory::MemoryHttpServer> _memoryHttpServer;
    std::unique_ptr<::yaos::control::ControlServiceCore> _controlServiceCore;
    std::unique_ptr<::yaos::control::ControlHttpServer> _controlHttpServer;
    std::unique_ptr<QTimer> _controlHealthTimer;
    std::unique_ptr<runtime::RuntimeCore> _runtime;
};

} // namespace yaos::app

#endif // YAOS_APP_APPLICATIONCONTROLLER_H
