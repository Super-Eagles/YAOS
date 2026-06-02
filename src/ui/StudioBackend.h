#ifndef YAOS_UI_STUDIOBACKEND_H
#define YAOS_UI_STUDIOBACKEND_H

#include <functional>
#include <memory>

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "../config/Config.h"
#include "StudioBackendTypes.h"

namespace yaos::runtime {
class IRuntimeFacade;
}

namespace yaos::distributed {
class IRuntimeClient;
}

namespace yaos::ui {

struct StudioConfigSaveResult;

class IStudioBackend {
public:
    virtual ~IStudioBackend();

    virtual QVariantMap status() = 0;
    virtual bool initializeWorkspace(QString *message = nullptr) = 0;
    virtual bool reloadFromDisk(const QString &modelOverride = QString(),
                                const QString &providerOverride = QString()) = 0;
    virtual bool startGatewayServices() = 0;
    virtual void stopGatewayServices() = 0;
    virtual QVariantList recentApprovals(int limit = 50,
                                         const QString &state = QString()) = 0;
    virtual bool resolveApproval(const QString &approvalId,
                                 const QString &decision,
                                 const QString &scope = QString(),
                                 const QString &note = QString()) = 0;
    virtual QVariantList recentNotifications(int limit = 50,
                                             bool unreadOnly = false) = 0;
    virtual void markAllNotificationsRead() = 0;
    virtual QVariantList recentTasks(int limit = 20) = 0;
    virtual QVariantList recentEvents(int limit = 50) = 0;
    virtual QVariantList recentNodes(int limit = 64,
                                     bool onlineOnly = false) = 0;
    virtual QJsonObject previewDelegationRoute(const QJsonObject &request = QJsonObject()) = 0;
    virtual QJsonObject submitDelegationRequest(const QJsonObject &request = QJsonObject()) = 0;
    virtual QVariantMap resourceSummary() = 0;
    virtual QVariantList recentResources(int limit = 100,
                                         const QString &kind = QString()) = 0;
    virtual QVariantList automations(int limit = 100) = 0;
    virtual QVariantList automationRuns(int limit = 120,
                                        const QString &automationId = QString()) = 0;
    virtual QString saveAutomation(const QVariantMap &recordMap,
                                   QString *error = nullptr) = 0;
    virtual bool removeAutomation(const QString &id) = 0;
    virtual QString runAutomation(const QString &id,
                                  QString *error = nullptr,
                                  const QString &sessionKey = QStringLiteral("automation:manual")) = 0;
    virtual QVariantList plugins() = 0;
    virtual QVariantList skills() = 0;
    virtual QVariantList extensionCatalog(const config::Config &config) = 0;
    virtual bool installCatalogEntry(config::Config *config,
                                     const QString &catalogId,
                                     QString *message = nullptr) = 0;
    virtual StudioConfigSaveResult saveConfiguration(const config::Config &draftConfig,
                                                     const config::Config &liveConfig) = 0;
    virtual QVariantMap fetchProviderModels(const config::Config &draftConfig,
                                            const config::Config &liveConfig,
                                            const QString &providerId) = 0;
    // P1.1: non-browser OAuth implemented in RuntimeFacadeStudioBackend.
    virtual QVariantMap providerAuthStatus(const config::Config &config,
                                           const QString &providerId) = 0;
    virtual QVariantMap startProviderDeviceFlow(config::Config *config,
                                                const QString &providerId) = 0;
    virtual QVariantMap pollProviderDeviceFlow(config::Config *config,
                                               const QString &providerId) = 0;
    virtual QVariantMap refreshProviderOAuth(config::Config *config,
                                             const QString &providerId) = 0;
    virtual QVariantMap logoutProviderOAuth(config::Config *config,
                                            const QString &providerId) = 0;
    virtual QVariantMap startProviderBrowserOAuth(config::Config *config,
                                                  const QString &providerId,
                                                  const QString &redirectUri,
                                                  const QString &state,
                                                  const QString &codeVerifier) = 0;
    virtual QVariantMap completeProviderBrowserOAuth(const QString &providerId,
                                                     const QString &redirectUri,
                                                     const QString &expectedState,
                                                     const QString &codeVerifier,
                                                     const QString &callbackUrl) = 0;
    virtual QVariantMap pushDelegationTemplatesToControl(const config::Config &cfg,
                                                         const QVariantList &records,
                                                         bool replaceExisting) = 0;
    virtual QVariantMap pullDelegationTemplatesFromControl(const config::Config &cfg,
                                                          bool replaceExisting) = 0;
    virtual StudioChatTurnResult processMessageDetailed(const QString &content,
                                                        const QString &sessionKey = QStringLiteral("gui:primary"),
                                                        const QString &channel = QStringLiteral("gui"),
                                                        const QString &chatId = QStringLiteral("desktop"),
                                                        const QString &modelOverride = QString(),
                                                        const QString &providerOverride = QString()) = 0;

    // Optional: register a callback that receives incremental content/thinking
    // deltas during a streaming turn. Called on the FastNet IO thread.
    using StreamProgressCallback = std::function<void(const QString &contentDelta, const QString &thinkingDelta)>;
    virtual void setStreamProgressCallback(StreamProgressCallback cb) { Q_UNUSED(cb) }
};

class RuntimeFacadeStudioBackend : public IStudioBackend {
public:
    explicit RuntimeFacadeStudioBackend(std::unique_ptr<runtime::IRuntimeFacade> facade);
    ~RuntimeFacadeStudioBackend() override;

    QVariantMap status() override;
    bool initializeWorkspace(QString *message = nullptr) override;
    bool reloadFromDisk(const QString &modelOverride = QString(),
                        const QString &providerOverride = QString()) override;
    bool startGatewayServices() override;
    void stopGatewayServices() override;
    QVariantList recentApprovals(int limit = 50,
                                 const QString &state = QString()) override;
    bool resolveApproval(const QString &approvalId,
                         const QString &decision,
                         const QString &scope = QString(),
                         const QString &note = QString()) override;
    QVariantList recentNotifications(int limit = 50,
                                     bool unreadOnly = false) override;
    void markAllNotificationsRead() override;
    QVariantList recentTasks(int limit = 20) override;
    QVariantList recentEvents(int limit = 50) override;
    QVariantList recentNodes(int limit = 64,
                             bool onlineOnly = false) override;
    QJsonObject previewDelegationRoute(const QJsonObject &request = QJsonObject()) override;
    QJsonObject submitDelegationRequest(const QJsonObject &request = QJsonObject()) override;
    QVariantMap resourceSummary() override;
    QVariantList recentResources(int limit = 100,
                                 const QString &kind = QString()) override;
    QVariantList automations(int limit = 100) override;
    QVariantList automationRuns(int limit = 120,
                                const QString &automationId = QString()) override;
    QString saveAutomation(const QVariantMap &recordMap,
                           QString *error = nullptr) override;
    bool removeAutomation(const QString &id) override;
    QString runAutomation(const QString &id,
                          QString *error = nullptr,
                          const QString &sessionKey = QStringLiteral("automation:manual")) override;
    QVariantList plugins() override;
    QVariantList skills() override;
    QVariantList extensionCatalog(const config::Config &config) override;
    bool installCatalogEntry(config::Config *config,
                             const QString &catalogId,
                             QString *message = nullptr) override;
    StudioConfigSaveResult saveConfiguration(const config::Config &draftConfig,
                                             const config::Config &liveConfig) override;
    QVariantMap fetchProviderModels(const config::Config &draftConfig,
                                    const config::Config &liveConfig,
                                    const QString &providerId) override;
    // P1.1: non-browser OAuth.
    QVariantMap providerAuthStatus(const config::Config &config,
                                   const QString &providerId) override;
    QVariantMap startProviderDeviceFlow(config::Config *config,
                                        const QString &providerId) override;
    QVariantMap pollProviderDeviceFlow(config::Config *config,
                                       const QString &providerId) override;
    QVariantMap refreshProviderOAuth(config::Config *config,
                                     const QString &providerId) override;
    QVariantMap logoutProviderOAuth(config::Config *config,
                                    const QString &providerId) override;
    QVariantMap startProviderBrowserOAuth(config::Config *config,
                                          const QString &providerId,
                                          const QString &redirectUri,
                                          const QString &state,
                                          const QString &codeVerifier) override;
    QVariantMap completeProviderBrowserOAuth(const QString &providerId,
                                             const QString &redirectUri,
                                             const QString &expectedState,
                                             const QString &codeVerifier,
                                             const QString &callbackUrl) override;
    QVariantMap pushDelegationTemplatesToControl(const config::Config &cfg,
                                                 const QVariantList &records,
                                                 bool replaceExisting) override;
    QVariantMap pullDelegationTemplatesFromControl(const config::Config &cfg,
                                                   bool replaceExisting) override;
    StudioChatTurnResult processMessageDetailed(const QString &content,
                                               const QString &sessionKey = QStringLiteral("gui:primary"),
                                               const QString &channel = QStringLiteral("gui"),
                                               const QString &chatId = QStringLiteral("desktop"),
                                               const QString &modelOverride = QString(),
                                               const QString &providerOverride = QString()) override;

    void setStreamProgressCallback(StreamProgressCallback cb) override;

private:
    std::unique_ptr<runtime::IRuntimeFacade> m_facade;
};

class RemoteStudioBackend final : public RuntimeFacadeStudioBackend {
public:
    RemoteStudioBackend(std::unique_ptr<runtime::IRuntimeFacade> facade,
                        std::unique_ptr<distributed::IRuntimeClient> client,
                        const QString &transportMode);

    QVariantMap status() override;
    QVariantList extensionCatalog(const config::Config &config) override;
    bool installCatalogEntry(config::Config *config,
                             const QString &catalogId,
                             QString *message = nullptr) override;
    StudioConfigSaveResult saveConfiguration(const config::Config &draftConfig,
                                             const config::Config &liveConfig) override;
    QVariantMap fetchProviderModels(const config::Config &draftConfig,
                                    const config::Config &liveConfig,
                                    const QString &providerId) override;
    QVariantMap providerAuthStatus(const config::Config &config,
                                   const QString &providerId) override;
    QVariantMap startProviderDeviceFlow(config::Config *config,
                                        const QString &providerId) override;
    QVariantMap pollProviderDeviceFlow(config::Config *config,
                                       const QString &providerId) override;
    QVariantMap refreshProviderOAuth(config::Config *config,
                                     const QString &providerId) override;
    QVariantMap logoutProviderOAuth(config::Config *config,
                                    const QString &providerId) override;
    QVariantMap startProviderBrowserOAuth(config::Config *config,
                                          const QString &providerId,
                                          const QString &redirectUri,
                                          const QString &state,
                                          const QString &codeVerifier) override;
    QVariantMap completeProviderBrowserOAuth(const QString &providerId,
                                             const QString &redirectUri,
                                             const QString &expectedState,
                                             const QString &codeVerifier,
                                             const QString &callbackUrl) override;
    QVariantMap pushDelegationTemplatesToControl(const config::Config &cfg,
                                                const QVariantList &records,
                                                bool replaceExisting) override;
    QVariantMap pullDelegationTemplatesFromControl(const config::Config &cfg,
                                                  bool replaceExisting) override;

private:
    QVariantMap invokeStudioMap(const QString &method,
                                const QJsonObject &payload = QJsonObject()) const;
    QVariantMap invokeProviderOAuth(config::Config *config,
                                    const QString &method,
                                    const QString &providerId,
                                    const QString &errorTitle) const;

private:
    mutable std::unique_ptr<distributed::IRuntimeClient> m_client;
    QString m_transportMode;
};

struct StudioConfigSaveResult {
    bool ok = false;
    bool saved = false;
    bool reloadOk = false;
    bool configChanged = false;
    config::Config config;
    std::unique_ptr<IStudioBackend> backend;
    QString fallbackReason;
    QString title;
    QString body;
    QString tone = QStringLiteral("neutral");
};

struct StudioBackendSelection {
    std::unique_ptr<IStudioBackend> backend;
    QString requestedMode;
    QString activeMode;
    QString fallbackReason;

    bool usedFallback() const {
        return !requestedMode.isEmpty() && activeMode != requestedMode;
    }
};

StudioBackendSelection createStudioBackend(const config::Config &config);

} // namespace yaos::ui

#endif // YAOS_UI_STUDIOBACKEND_H
