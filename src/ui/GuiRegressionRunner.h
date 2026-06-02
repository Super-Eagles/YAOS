#ifndef YAOS_UI_GUIREGRESSIONRUNNER_H
#define YAOS_UI_GUIREGRESSIONRUNNER_H

#include <QObject>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <memory>

class QObject;

namespace FastNet {
class HttpServer;
}

namespace yaos::ui {

class StudioBridge;
class StudioWindow;

struct GuiRegressionOptions {
    QString caseId = QStringLiteral("runtime-workbench");
    QString jsonOutputPath;
    int switchCount = 12;
    int timeoutMs = 25000;
    int pageSettleMs = 60;
    int heartbeatIntervalMs = 80;
    int maxUiGapMs = 1500;
};

class GuiRegressionRunner : public QObject {
    Q_OBJECT

public:
    explicit GuiRegressionRunner(StudioWindow *window,
                                 const GuiRegressionOptions &options,
                                 QObject *parent = nullptr);
    ~GuiRegressionRunner() override;

    QJsonObject result() const;

public slots:
    void start();

signals:
    void finished(int exitCode);

private slots:
    void handleHeartbeat();
    void handleTimeout();
    void handleStartupChanged();
    void handleStatusChanged();
    void handleChatHistoryChanged();
    void handleExtensionStateChanged();
    void handlePreviewChanged();
    void handleSaveFinished(bool success);
    void handleToast(const QString &title, const QString &body, const QString &tone);
    void performNextPageSwitch();

private:
    enum class Phase {
        Idle,
        WaitingForStartup,
        WaitingForWorkspace,
        WaitingForFixtureSave,
        WaitingForSeedPreview,
        SwitchingPages,
        WaitingForRefreshPreview,
        WaitingForRuntimePage,
        WaitingForProviderModelsPage,
        WaitingForOAuthStatus,
        WaitingForControlTemplates,
        WaitingForRuntimeProviderConfig,
        WaitingForRuntimeConfigSave,
        WaitingForChatResponse,
        WaitingForExtensionCatalog,
        WaitingForPluginInstall,
        WaitingForSkillInstall,
        WaitingForMcpInstall,
        WaitingForExtensionsReload,
        Completed,
        Failed
    };

    void maybeAdvance();
    void beginWorkspaceInitialization();
    void beginSeedPreview();
    void beginSwitchRounds();
    void beginRefreshPreview();
    void beginRuntimePageCheck();
    void beginProviderModelsPage();
    void beginOAuthStatus();
    void beginControlTemplates();
    void beginRuntimeProviderConfig();
    void beginChatProvider();
    void beginChatTurn();
    void beginExtensionsCatalog();
    void beginPluginInstall();
    void beginSkillInstall();
    void beginMcpInstall();
    void beginExtensionsReload();
    bool beginCaseFixture();
    bool caseNeedsFixture() const;
    bool caseNeedsProviderMock() const;
    bool startProviderMockServer();
    bool startControlMockServer();
    bool startRuntimeMockServer();
    void stopMockServers();
    void releaseMockServersForProcessExit();
    QString runtimeProviderConfigMode() const;
    void startPreviewWait(const QString &reason);
    void completePreviewWait(const QVariantMap &preview);
    QObject *resolvePageObject() const;
    QObject *resolveRuntimePageObject() const;
    QObject *resolveRuntimePreviewObject() const;
    QVariant invokeRootMethod(const char *methodName,
                              const QVariant &arg1 = QVariant()) const;
    QVariantMap providerPageSnapshot() const;
    QVariantMap runtimePageSnapshot() const;
    void recordPageSwitch(const QString &page);
    bool setCurrentPage(const QString &page);
    bool triggerRuntimePreviewRefresh() const;
    QString currentPage() const;
    QString workspacePath() const;
    int statusCount(const QString &key) const;
    QVariantMap catalogEntry(const QString &catalogId) const;
    QVariantMap extensionRecord(const QVariantList &records, const QString &id) const;
    QVariantMap mcpServerRecord(const QString &id) const;
    bool findChatReplyToast(QString *bodyPreview = nullptr) const;
    QString chatSessionFilePath() const;
    QJsonObject chatSummaryFromSessionFallback() const;
    bool hasInstallToastSince(int startIndex, const QString &target) const;
    bool allFilesExist(const QStringList &paths, QJsonArray *existingFiles = nullptr) const;
    QStringList pluginInstallFiles() const;
    QStringList skillInstallFiles() const;
    bool workspaceReady() const;
    void finishSuccess();
    void fail(const QString &message);
    void finishWithExitCode(int exitCode, const QString &error);
    bool writeResultDocument(const QJsonObject &document, QString *error) const;
    QJsonObject buildResultDocument(bool ok, const QString &error) const;
    QJsonObject statusSummary() const;
    QJsonObject extensionSummary() const;
    QJsonObject previewSummary(const QString &reason,
                               const QVariantMap &preview,
                               int latencyMs) const;

private:
    QPointer<StudioWindow> m_window;
    QPointer<StudioBridge> m_bridge;
    mutable QPointer<QObject> m_rootObject;
    GuiRegressionOptions m_options;
    Phase m_phase = Phase::Idle;
    QTimer m_timeoutTimer;
    QTimer m_heartbeatTimer;
    QElapsedTimer m_runClock;
    QElapsedTimer m_heartbeatClock;
    bool m_heartbeatStarted = false;
    bool m_started = false;
    bool m_finished = false;
    bool m_advancing = false;
    bool m_workspaceReadyBeforeInit = false;
    int m_startupMs = -1;
    int m_maxUiGapMs = 0;
    int m_completedRounds = 0;
    bool m_onSecurityPage = false;
    bool m_waitingForPreview = false;
    bool m_previewSawPending = false;
    int m_previewStartedAtMs = 0;
    int m_extensionStateRevision = 0;
    int m_waitStartRevision = 0;
    int m_installToastStartIndex = 0;
    int m_actionStartedAtMs = 0;
    int m_reloadStartedAtMs = 0;
    int m_chatStartedAtMs = 0;
    int m_chatToastStartIndex = 0;
    int m_initialPluginCount = -1;
    int m_initialSkillCount = -1;
    int m_initialMcpServerCount = -1;
    int m_initialChatHistoryCount = 0;
    int m_providerModelsStep = 0;
    int m_providerModelsStartedAtMs = 0;
    int m_runtimePageStartedAtMs = 0;
    int m_oauthStep = 0;
    int m_fixtureStartedAtMs = 0;
    int m_runtimeProviderConfigStartedAtMs = 0;
    int m_runtimeProviderConfigStep = 0;
    quint16 m_providerServerPort = 0;
    quint16 m_controlServerPort = 0;
    quint16 m_runtimeServerPort = 0;
    bool m_caseFixturePrepared = false;
    QString m_previewReason;
    QString m_chatSessionKey = QStringLiteral("gui:regression:chat-provider");
    QString m_chatProviderId = QStringLiteral("custom");
    QString m_chatPrompt = QStringLiteral("GUI provider regression prompt");
    QString m_chatModel;
    QString m_oauthConfigBeforeJson;
    QJsonArray m_pageSwitches;
    QJsonArray m_toasts;
    QJsonObject m_seedPreview;
    QJsonObject m_refreshPreview;
    QJsonObject m_providerModelSync;
    QJsonObject m_providerPageSummary;
    QJsonObject m_runtimePageSummary;
    QJsonObject m_oauthSummary;
    QJsonObject m_controlSummary;
    QJsonObject m_runtimeMockConfig;
    QJsonObject m_runtimeProviderConfigSummary;
    QJsonObject m_chatSummary;
    QJsonObject m_extensionsSummaryData;
    QJsonObject m_pluginInstall;
    QJsonObject m_skillInstall;
    QJsonObject m_mcpInstall;
    QJsonObject m_extensionsReload;
    QJsonObject m_result;
    QString m_error;
    std::unique_ptr<FastNet::HttpServer> m_providerServer;
    std::unique_ptr<FastNet::HttpServer> m_controlServer;
    std::unique_ptr<FastNet::HttpServer> m_runtimeServer;
};

} // namespace yaos::ui

#endif // YAOS_UI_GUIREGRESSIONRUNNER_H
