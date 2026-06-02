#ifndef YAOS_UI_STUDIOBRIDGE_H
#define YAOS_UI_STUDIOBRIDGE_H

#include <memory>

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPoint>
#include <QSet>
#include <QSharedPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "../config/Config.h"
#include "StudioBackendTypes.h"

class QWindow;

namespace yaos::ui {

class IStudioBackend;
struct PendingOAuthSession;

struct InstallResult {
    bool ok = false;
    QString message;
    bool configChanged = false;
    config::Config updatedConfig;
};

struct WorkspaceInitializationResult {
    bool ok = false;
    QString message;
};

class StudioBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap config READ config NOTIFY configChanged)
    Q_PROPERTY(QVariantMap resourceSummary READ resourceSummary NOTIFY resourceSummaryChanged)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged)
    Q_PROPERTY(QVariantList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY nodesChanged)
    Q_PROPERTY(QVariantList approvals READ approvals NOTIFY approvalsChanged)
    Q_PROPERTY(QVariantList notifications READ notifications NOTIFY notificationsChanged)
    Q_PROPERTY(QVariantList resources READ resources NOTIFY resourcesChanged)
    Q_PROPERTY(QVariantList automations READ automations NOTIFY automationsChanged)
    Q_PROPERTY(QVariantMap delegationRoutePreview READ delegationRoutePreview NOTIFY delegationRoutePreviewChanged)
    Q_PROPERTY(QVariantList automationRuns READ automationRuns NOTIFY automationRunsChanged)
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantList skills READ skills NOTIFY skillsChanged)
    Q_PROPERTY(QVariantList extensionCatalog READ extensionCatalog NOTIFY extensionCatalogChanged)
    Q_PROPERTY(QVariantList chatHistory READ chatHistory NOTIFY chatHistoryChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool saveInProgress READ saveInProgress NOTIFY saveStateChanged)
    Q_PROPERTY(int saveProgress READ saveProgress NOTIFY saveStateChanged)
    Q_PROPERTY(QString saveMessage READ saveMessage NOTIFY saveStateChanged)
    Q_PROPERTY(int startupProgress READ startupProgress NOTIFY startupChanged)
    Q_PROPERTY(QString startupMessage READ startupMessage NOTIFY startupChanged)
    Q_PROPERTY(qint64 startupElapsedMs READ startupElapsedMs NOTIFY startupChanged)
    Q_PROPERTY(qint64 startupStepElapsedMs READ startupStepElapsedMs NOTIFY startupChanged)
    Q_PROPERTY(QVariantList startupTimeline READ startupTimeline NOTIFY startupChanged)
    Q_PROPERTY(bool startupCanLoadMain READ startupCanLoadMain NOTIFY startupChanged)
    Q_PROPERTY(bool startupComplete READ startupComplete NOTIFY startupChanged)
    Q_PROPERTY(QString initialPage READ initialPage CONSTANT)

public:
    explicit StudioBridge(const QString &initialPage = QString(), QObject *parent = nullptr);
    ~StudioBridge() override;

    void attachWindow(QWindow *window);

    QVariantMap status() const;
    QVariantMap config() const;
    QVariantMap resourceSummary() const;
    QVariantList tasks() const;
    QVariantList events() const;
    QVariantList nodes() const;
    QVariantList approvals() const;
    QVariantList notifications() const;
    QVariantList resources() const;
    QVariantList automations() const;
    QVariantMap delegationRoutePreview() const;
    QVariantList automationRuns() const;
    QVariantList plugins() const;
    QVariantList skills() const;
    QVariantList extensionCatalog() const;
    QVariantList chatHistory() const;
    bool busy() const;
    bool saveInProgress() const;
    int saveProgress() const;
    QString saveMessage() const;
    int startupProgress() const;
    QString startupMessage() const;
    qint64 startupElapsedMs() const;
    qint64 startupStepElapsedMs() const;
    QVariantList startupTimeline() const;
    bool startupCanLoadMain() const;
    bool startupComplete() const;
    QString initialPage() const;

    Q_INVOKABLE void beginStartup();
    Q_INVOKABLE void noteMainUiLoaded();
    Q_INVOKABLE void requestRefresh();
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE bool saveConfig(const QVariantMap &configMap);
    Q_INVOKABLE QVariantList fetchProviderModels(const QString &providerId, const QVariantMap &configMap);
    Q_INVOKABLE QVariantMap providerAuthStatus(const QString &providerId);
    Q_INVOKABLE QVariantMap beginProviderOAuth(const QString &providerId, const QString &mode);
    Q_INVOKABLE QVariantMap beginProviderOAuthWithConfig(const QString &providerId,
                                                         const QString &mode,
                                                         const QVariantMap &configMap);
    Q_INVOKABLE QVariantMap pollProviderOAuth(const QString &providerId);
    Q_INVOKABLE QVariantMap refreshProviderOAuth(const QString &providerId);
    Q_INVOKABLE bool logoutProviderOAuth(const QString &providerId);
    Q_INVOKABLE void requestDelegationRoutePreview(const QVariantMap &requestMap);
    Q_INVOKABLE QVariantMap previewDelegationRoute(const QVariantMap &requestMap);
    Q_INVOKABLE QVariantMap submitDelegationRequest(const QVariantMap &requestMap);
    Q_INVOKABLE QVariantMap pushDelegationTemplatesToControl(const QVariantList &records,
                                                             bool replaceExisting = false);
    Q_INVOKABLE QVariantMap pullDelegationTemplatesFromControl(bool replaceExisting = false);
    Q_INVOKABLE void sendMessage(const QString &prompt,
                                 const QString &sessionKey = QStringLiteral("gui:primary"),
                                 const QString &modelOverride = QString(),
                                 const QString &providerOverride = QString());
    Q_INVOKABLE void initializeWorkspace();
    Q_INVOKABLE void installCatalogItem(const QString &catalogId);
    Q_INVOKABLE void startGateway();
    Q_INVOKABLE void stopGateway();
    Q_INVOKABLE void approve(const QString &approvalId, const QString &scope);
    Q_INVOKABLE void deny(const QString &approvalId);
    Q_INVOKABLE void markNotificationsRead();
    Q_INVOKABLE QString saveAutomation(const QVariantMap &recordMap);
    Q_INVOKABLE void runAutomation(const QString &automationId);
    Q_INVOKABLE void deleteAutomation(const QString &automationId);
    Q_INVOKABLE QString markdownToHtml(const QString &markdown) const;
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE void minimizeWindow();
    Q_INVOKABLE void toggleMaximizeWindow();
    Q_INVOKABLE void closeWindow();
    Q_INVOKABLE void beginWindowDrag(qreal x, qreal y);
    Q_INVOKABLE void dragWindow(qreal screenX, qreal screenY);
    Q_INVOKABLE void endWindowDrag();

signals:
    void statusChanged();
    void configChanged();
    void resourceSummaryChanged();
    void tasksChanged();
    void eventsChanged();
    void nodesChanged();
    void approvalsChanged();
    void notificationsChanged();
    void resourcesChanged();
    void automationsChanged();
    void delegationRoutePreviewChanged();
    void automationRunsChanged();
    void pluginsChanged();
    void skillsChanged();
    void extensionCatalogChanged();
    void chatHistoryChanged();
    void busyChanged();
    void saveStateChanged();
    void saveFinished(bool success);
    void startupChanged();
    void toastRequested(const QString &title, const QString &body, const QString &tone);

private slots:
    void handleChatFinished();
    void updateChatProgress();
    void handleCoreRefreshFinished();
    void handleDeferredRefreshFinished();
    void handleDelegationRoutePreviewFinished();
    void handleGatewayActionFinished();
    void handleInstallFinished();
    void handleWorkspaceInitializationFinished();
    void handleFileChanged(const QString &path);

private:
    bool rebuildStudioBackend(const config::Config &config, QString *fallbackReason = nullptr);
    void closeOAuthSession(const QString &providerId);
    void startDelegationRoutePreview();
    void scheduleStartupDataRefreshes();
    void refreshCoreData();
    void refreshDeferredData();
    void runNextCoreRefreshStep();
    void finishCoreRefresh();
    void runNextDeferredRefreshStep();
    void finishDeferredRefresh();
    void maybeCompleteManualRefreshToast();
    void runNextSaveStep();
    void maybeCompleteSave();
    void setBusy(bool busy);
    void setSaveState(bool inProgress, int progress, const QString &message);
    void setStartupCanLoadMain(bool canLoadMain);
    void setStartupState(int progress, const QString &message, bool complete = false);
    void tryFinalizeStartup();
    void appendChatEntry(const QVariantMap &entry);
    void replaceChatEntry(int index, const QVariantMap &entry);
    void resetPendingChatState();
    void applyStreamDelta(const QString &contentDelta, const QString &thinkingDelta);
private:
    std::shared_ptr<IStudioBackend> m_backend;
    config::Config m_config;
    QVariantMap m_status;
    QVariantMap m_configMap;
    QVariantMap m_resourceSummary;
    QVariantList m_tasks;
    QVariantList m_events;
    QVariantList m_nodes;
    QVariantList m_approvals;
    QVariantList m_notifications;
    QVariantList m_resources;
    QVariantList m_automations;
    QVariantMap m_delegationRoutePreview;
    QVariantList m_automationRuns;
    QVariantList m_plugins;
    QVariantList m_skills;
    QVariantList m_extensionCatalog;
    QVariantList m_chatHistory;
    bool m_busy = false;
    bool m_saveInProgress = false;
    int m_saveProgress = 0;
    QString m_saveMessage;
    int m_saveStep = 0;
    bool m_saveAwaitingRefresh = false;
    bool m_saveResultOk = false;
    config::Config m_pendingSaveConfig;
    QString m_pendingSaveFallbackReason;
    QString m_pendingSaveFailureTitle;
    QString m_pendingSaveFailureBody;
    QString m_pendingSaveFailureTone = QStringLiteral("neutral");
    int m_startupProgress = 2;
    QString m_startupMessage = QStringLiteral("正在准备工作台 / Preparing console");
    qint64 m_startupElapsedMs = 0;
    qint64 m_startupStepElapsedMs = 0;
    QVariantList m_startupTimeline;
    bool m_startupCanLoadMain = false;
    bool m_startupComplete = false;
    qint64 m_startupStartedAtMs = 0;
    qint64 m_startupLastTransitionAtMs = 0;
    QString m_initialPage;
    QWindow *m_window = nullptr;
    QPoint m_dragStartScreenPos;
    QPoint m_dragStartWindowPos;
    bool m_dragActive = false;
    QTimer m_refreshTimer;
    QTimer m_chatProgressTimer;
    QFileSystemWatcher m_fileWatcher;
    QTimer m_fileWatcherDebounceTimer;
    bool m_useTimerFallback = false;
    bool m_coreRefreshRunning = false;
    bool m_coreRefreshQueued = false;
    int m_coreRefreshStep = 0;
    bool m_deferredRefreshRunning = false;
    bool m_deferredRefreshQueued = false;
    int m_deferredRefreshStep = 0;
    QVariantMap m_pendingDelegationRoutePreviewRequest;
    bool m_delegationRoutePreviewQueued = false;
    QFutureWatcher<StudioChatTurnResult> m_chatWatcher;
    QFutureWatcher<QVariantMap> m_coreRefreshWatcher;
    QFutureWatcher<QVariantMap> m_deferredRefreshWatcher;
    QFutureWatcher<QVariantMap> m_delegationRoutePreviewWatcher;
    QFutureWatcher<bool> m_gatewayActionWatcher;
    QFutureWatcher<InstallResult> m_installWatcher;
    QFutureWatcher<WorkspaceInitializationResult> m_workspaceInitWatcher;
    mutable QMutex m_backendMutex;
    QHash<QString, QSharedPointer<PendingOAuthSession>> m_oauthSessions;
    bool m_mainUiLoaded = false;
    bool m_startupDataRefreshScheduled = false;
    bool m_startupBootstrapFinished = false;
    bool m_startupCoreRefreshStarted = false;
    bool m_startupDeferredRefreshStarted = false;
    bool m_gatewayStartRequested = false;
    bool m_manualRefreshToastPending = false;
    int m_pendingChatEntryIndex = -1;
    QString m_pendingChatSessionKey;
    QString m_pendingChatChannel;
    QString m_pendingChatTaskId;
    QString m_pendingChatTraceId;
    QDateTime m_pendingChatStartedAt;
    QString m_pendingChatStreamContent;   // accumulated streaming content
    QString m_pendingChatStreamThinking;  // accumulated streaming thinking
    QVariantList m_pendingChatTrace;
    QSet<QString> m_pendingChatTraceKeys;
    int m_daemonReconnectAttempts = 0;
};

} // namespace yaos::ui

#endif // YAOS_UI_STUDIOBRIDGE_H
