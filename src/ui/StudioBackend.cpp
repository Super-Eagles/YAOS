#include "StudioBackend_p.h"



#include "StudioBackend.h"

#include <utility>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QtGlobal>
#include <QVector>
#include <QVariantMap>

#include "../config/ConfigLoader.h"
#include "../config/DelegationTemplateExchange.h"
#include "../daemon/DaemonRuntimeClient.h"
#include "../distributed/Contracts.h"
#include "../distributed/RemoteControlClient.h"
#include "../providers/AnthropicProvider.h"
#include "../providers/OpenAICompatibleProvider.h"
#include "../providers/ProviderOAuth.h"
#include "../providers/ProviderRegistry.h"
#include "../runtime/ApprovalStore.h"
#include "../runtime/AutomationStore.h"
#include "../runtime/ExtensionCatalog.h"
#include "../runtime/NotificationCenter.h"
#include "../runtime/PluginRegistry.h"
#include "../runtime/RemoteRuntimeClient.h"
#include "../runtime/ResourceCatalog.h"
#include "../runtime/RuntimeFacade.h"
#include "../runtime/SkillRegistry.h"
#include "../runtime/TaskStore.h"

namespace yaos::ui {




QVariantMap operationError(const QString &title,
                           const QString &body,
                           const QString &error,
                           const QString &tone) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("title"), title},
        {QStringLiteral("body"), body},
        {QStringLiteral("tone"), tone},
        {QStringLiteral("error"), error.trimmed().isEmpty() ? body : error}
    };
}

IStudioBackend::~IStudioBackend() = default;

RuntimeFacadeStudioBackend::RuntimeFacadeStudioBackend(std::unique_ptr<runtime::IRuntimeFacade> facade)
    : m_facade(std::move(facade)) {}

RuntimeFacadeStudioBackend::~RuntimeFacadeStudioBackend() = default;

QVariantMap RuntimeFacadeStudioBackend::status() {
    return statusToVariant(m_facade ? m_facade->statusSnapshot() : runtime::StatusSnapshot{});
}


QVariantList RemoteStudioBackend::extensionCatalog(const config::Config &config) {
    const QVariantMap result = invokeStudioMap(QStringLiteral("studio.extensionCatalog"),
                                               QJsonObject{{QStringLiteral("config"), config.toJson()}});
    if (!result.value(QStringLiteral("ok")).toBool()) {
        return QVariantList{};
    }
    return result.value(QStringLiteral("items")).toList();
}

bool RemoteStudioBackend::installCatalogEntry(config::Config *config,
                                              const QString &catalogId,
                                              QString *message) {
    if (!config) {
        if (message) {
            *message = QStringLiteral("config is not available");
        }
        return false;
    }

    const QVariantMap result = invokeStudioMap(QStringLiteral("studio.installCatalogEntry"),
                                               QJsonObject{
                                                   {QStringLiteral("config"), config->toJson()},
                                                   {QStringLiteral("catalogId"), catalogId}
                                               });
    if (message) {
        *message = result.value(QStringLiteral("message"),
                                result.value(QStringLiteral("error")).toString()).toString();
    }
    if (result.value(QStringLiteral("configChanged")).toBool()) {
        const QVariantMap configMap = result.value(QStringLiteral("config")).toMap();
        if (!configMap.isEmpty()) {
            *config = config::Config::fromJson(QJsonObject::fromVariantMap(configMap));
        }
    }
    return result.value(QStringLiteral("ok")).toBool();
}














bool RuntimeFacadeStudioBackend::initializeWorkspace(QString *message) {
    return m_facade && m_facade->initializeWorkspace(message);
}

bool RuntimeFacadeStudioBackend::reloadFromDisk(const QString &modelOverride,
                                           const QString &providerOverride) {
    return m_facade && m_facade->reloadFromDisk(modelOverride, providerOverride);
}

bool RuntimeFacadeStudioBackend::startGatewayServices() {
    return m_facade && m_facade->startGatewayServices();
}

void RuntimeFacadeStudioBackend::stopGatewayServices() {
    if (m_facade) {
        m_facade->stopGatewayServices();
    }
}

QVariantList RuntimeFacadeStudioBackend::recentApprovals(int limit,
                                                         const QString &state) {
    return m_facade ? recordsToVariant(m_facade->recentApprovals(limit, state), approvalToVariant)
                    : QVariantList{};
}

bool RuntimeFacadeStudioBackend::resolveApproval(const QString &approvalId,
                                            const QString &decision,
                                            const QString &scope,
                                            const QString &note) {
    return m_facade && m_facade->resolveApproval(approvalId, decision, scope, note);
}

QVariantList RuntimeFacadeStudioBackend::recentNotifications(int limit,
                                                             bool unreadOnly) {
    return m_facade ? recordsToVariant(m_facade->recentNotifications(limit, unreadOnly), notificationToVariant)
                    : QVariantList{};
}

void RuntimeFacadeStudioBackend::markAllNotificationsRead() {
    if (m_facade) {
        m_facade->markAllNotificationsRead();
    }
}

QVariantList RuntimeFacadeStudioBackend::recentTasks(int limit) {
    return m_facade ? recordsToVariant(m_facade->recentTasks(limit), taskToVariant) : QVariantList{};
}

QVariantList RuntimeFacadeStudioBackend::recentEvents(int limit) {
    return m_facade ? recordsToVariant(m_facade->recentEvents(limit), eventToVariant) : QVariantList{};
}

QVariantList RuntimeFacadeStudioBackend::recentNodes(int limit,
                                                     bool onlineOnly) {
    return m_facade ? recordsToVariant(m_facade->recentNodes(limit, onlineOnly), nodeToVariant)
                    : QVariantList{};
}

QJsonObject RuntimeFacadeStudioBackend::previewDelegationRoute(const QJsonObject &request) {
    return m_facade ? m_facade->previewDelegationRoute(request) : QJsonObject{};
}

QJsonObject RuntimeFacadeStudioBackend::submitDelegationRequest(const QJsonObject &request) {
    return m_facade ? m_facade->submitDelegationRequest(request) : QJsonObject{};
}

QVariantMap RuntimeFacadeStudioBackend::resourceSummary() {
    return summaryToVariant(m_facade ? m_facade->resourceSummary() : runtime::ResourceSummary{});
}

QVariantList RuntimeFacadeStudioBackend::recentResources(int limit,
                                                         const QString &kind) {
    return m_facade ? recordsToVariant(m_facade->recentResources(limit, kind), resourceToVariant)
                    : QVariantList{};
}






QVariantList RuntimeFacadeStudioBackend::plugins() {
    return m_facade ? recordsToVariant(m_facade->plugins(), pluginToVariant) : QVariantList{};
}

QVariantList RuntimeFacadeStudioBackend::skills() {
    return m_facade ? recordsToVariant(m_facade->skills(), skillToVariant) : QVariantList{};
}

QVariantList RuntimeFacadeStudioBackend::extensionCatalog(const config::Config &config) {
    return recordsToVariant(runtime::buildExtensionCatalog(config.workspacePath(), config), extensionCatalogToVariant);
}

bool RuntimeFacadeStudioBackend::installCatalogEntry(config::Config *config,
                                                     const QString &catalogId,
                                                     QString *message) {
    if (!config) {
        if (message) {
            *message = QStringLiteral("config is not available");
        }
        return false;
    }
    if (!runtime::installCatalogEntry(config->workspacePath(), config, catalogId, message)) {
        return false;
    }

    if (catalogId.trimmed().startsWith(QStringLiteral("mcp."))) {
        if (!config::ConfigLoader::save(*config)) {
            if (message) {
                *message = QStringLiteral("扩展文件已生成,但配置没有成功写回.");
            }
            return false;
        }
        if (m_facade && !m_facade->reloadFromDisk()) {
            if (message) {
                *message = QStringLiteral("扩展已安装,但运行时没有成功切换到新配置.");
            }
            return false;
        }
    }

    return true;
}

StudioConfigSaveResult RuntimeFacadeStudioBackend::saveConfiguration(const config::Config &draftConfig,
                                                                     const config::Config &liveConfig) {
    StudioConfigSaveResult result;
    result.config = draftConfig;
    result.tone = QStringLiteral("warning");

    preserveLiveOAuthState(&result.config, liveConfig);
    if (!config::ConfigLoader::save(result.config)) {
        result.title = QStringLiteral("保存失败");
        result.body = QStringLiteral("无法写入配置文件.");
        return result;
    }

    result.saved = true;
    result.configChanged = true;

    StudioBackendSelection selection = createStudioBackend(result.config);
    result.fallbackReason = selection.fallbackReason;
    if (!selection.backend) {
        result.title = QStringLiteral("运行时切换失败");
        result.body = QStringLiteral("配置已保存,但新的 runtime facade 没有成功初始化.");
        return result;
    }

    const bool remoteTransport = selection.activeMode == QStringLiteral("daemon") ||
                                 selection.activeMode == QStringLiteral("remote");
    result.reloadOk = remoteTransport ? true : selection.backend->reloadFromDisk();
    result.backend = std::move(selection.backend);
    if (!result.reloadOk) {
        result.title = QStringLiteral("运行时重载失败");
        result.body = QStringLiteral("配置已保存,但运行时没有成功切换到新配置.");
        return result;
    }

    result.ok = true;
    result.title = QStringLiteral("配置已同步");
    result.body = QStringLiteral("新的系统参数已经写入并重载.");
    result.tone = QStringLiteral("success");
    return result;
}


// ---------------------------------------------------------------------------
// P1.1: non-browser OAuth status (side-effect free)
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// P1.1: non-browser OAuth device flow start
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// P1.1: non-browser OAuth device flow poll
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// P1.1: non-browser OAuth token refresh
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// P1.1: non-browser OAuth logout / clear state
// ---------------------------------------------------------------------------








StudioChatTurnResult RuntimeFacadeStudioBackend::processMessageDetailed(const QString &content,
                                                                   const QString &sessionKey,
                                                                   const QString &channel,
                                                                   const QString &chatId,
                                                                   const QString &modelOverride,
                                                                   const QString &providerOverride) {
    if (!m_facade) {
        StudioChatTurnResult result;
        result.content = QStringLiteral("Error: runtime facade is not initialized");
        result.error = true;
        return result;
    }
    return chatTurnToStudioResult(m_facade->processMessageDetailed(content,
                                                                   sessionKey,
                                                                   channel,
                                                                   chatId,
                                                                   modelOverride,
                                                                   providerOverride));
}

void RuntimeFacadeStudioBackend::setStreamProgressCallback(StreamProgressCallback cb) {
    if (m_facade) {
        m_facade->setStreamProgressCallback(cb);
    }
}

std::unique_ptr<distributed::IRuntimeClient> createRemoteStudioClient(const config::Config &config,
                                                                      const QString &activeMode) {
    if (activeMode == QStringLiteral("daemon")) {
        return std::make_unique<daemon::DaemonRuntimeClient>(config);
    }
    if (activeMode == QStringLiteral("remote")) {
        return std::make_unique<runtime::RemoteRuntimeClient>(config);
    }
    return nullptr;
}

StudioBackendSelection createStudioBackend(const config::Config &config) {
    runtime::RuntimeFacadeSelection runtimeSelection = runtime::createRuntimeFacade(config);

    StudioBackendSelection selection;
    selection.requestedMode = runtimeSelection.requestedMode;
    selection.activeMode = runtimeSelection.activeMode;
    selection.fallbackReason = runtimeSelection.fallbackReason;
    if (runtimeSelection.facade) {
        if (runtimeSelection.activeMode == QStringLiteral("daemon") ||
            runtimeSelection.activeMode == QStringLiteral("remote")) {
            selection.backend = std::make_unique<RemoteStudioBackend>(std::move(runtimeSelection.facade),
                                                                      createRemoteStudioClient(config,
                                                                                               runtimeSelection.activeMode),
                                                                      runtimeSelection.activeMode);
        } else {
            selection.backend = std::make_unique<RuntimeFacadeStudioBackend>(std::move(runtimeSelection.facade));
        }
    }
    return selection;
}

} // namespace yaos::ui

