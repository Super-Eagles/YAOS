#include "LocalRuntimeClient.h"

#include "../config/ConfigLoader.h"
#include "../config/DelegationTemplateExchange.h"
#include "../distributed/ContractsJson.h"
#include "../distributed/RemoteControlClient.h"
#include "../providers/AnthropicProvider.h"
#include "../providers/OpenAICompatibleProvider.h"
#include "../providers/ProviderOAuth.h"
#include "../providers/ProviderRegistry.h"
#include "ExtensionCatalog.h"
#include "RuntimeSerialization.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace yaos::runtime {

namespace {

QString stringValue(const QJsonObject &payload, const char *key, const QString &fallback = QString()) {
    const QJsonValue value = payload.value(QLatin1String(key));
    return value.isString() ? value.toString() : fallback;
}

int intValue(const QJsonObject &payload, const char *key, int fallback = 0) {
    const QJsonValue value = payload.value(QLatin1String(key));
    return value.isDouble() ? value.toInt(fallback) : fallback;
}

bool boolValue(const QJsonObject &payload, const char *key, bool fallback = false) {
    const QJsonValue value = payload.value(QLatin1String(key));
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isDouble()) {
        return value.toInt() != 0;
    }
    if (value.isString()) {
        const QString normalized = value.toString().trimmed().toLower();
        if (normalized == "1" || normalized == "true" || normalized == "yes") {
            return true;
        }
        if (normalized == "0" || normalized == "false" || normalized == "no") {
            return false;
        }
    }
    return fallback;
}

config::Config configValue(const QJsonObject &payload) {
    const QJsonObject configObject = payload.value(QStringLiteral("config")).toObject();
    return configObject.isEmpty() ? config::ConfigLoader::load()
                                  : config::Config::fromJson(configObject);
}

QString controlPlaneEndpoint(const config::Config &config) {
    const QString control = config.deployment.controlPlaneUrl.trimmed();
    if (!control.isEmpty()) {
        return control;
    }
    return config.deployment.registryUrl.trimmed();
}

QJsonObject operationErrorResponse(const QString &title,
                                   const QString &body,
                                   const QString &error = QString(),
                                   const QString &tone = QStringLiteral("warning")) {
    return QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("title"), title},
        {QStringLiteral("body"), body},
        {QStringLiteral("tone"), tone},
        {QStringLiteral("error"), error.isEmpty() ? body : error}
    };
}

config::ProviderConfig *providerConfigById(config::Config &config, const QString &providerId) {
    return config.providerById(providerId);
}

const config::ProviderConfig *providerConfigById(const config::Config &config, const QString &providerId) {
    return config.providerById(providerId);
}

void preserveOAuthRuntimeFields(config::ProviderConfig *target,
                                const config::ProviderConfig &liveProvider) {
    if (!target) {
        return;
    }

    target->oauthAccessToken = liveProvider.oauthAccessToken;
    target->oauthRefreshToken = liveProvider.oauthRefreshToken;
    target->oauthIdToken = liveProvider.oauthIdToken;
    target->oauthTokenType = liveProvider.oauthTokenType;
    target->oauthAccountId = liveProvider.oauthAccountId;
    target->oauthExpiresAt = liveProvider.oauthExpiresAt;
    target->oauthLastRefreshAt = liveProvider.oauthLastRefreshAt;
    target->oauthDeviceCode = liveProvider.oauthDeviceCode;
    target->oauthDeviceAuthId = liveProvider.oauthDeviceAuthId;
    target->oauthUserCode = liveProvider.oauthUserCode;
    target->oauthVerificationUrl = liveProvider.oauthVerificationUrl;
    target->oauthLastError = liveProvider.oauthLastError;
    target->oauthIntervalSec = liveProvider.oauthIntervalSec;
}

void preserveOAuthDefaults(config::ProviderConfig *target,
                           const config::ProviderConfig &liveProvider) {
    if (!target) {
        return;
    }

    if (target->apiBase.trimmed().isEmpty()) {
        target->apiBase = liveProvider.apiBase;
    }
    if (target->oauthIssuer.trimmed().isEmpty()) {
        target->oauthIssuer = liveProvider.oauthIssuer;
    }
    if (target->oauthClientId.trimmed().isEmpty()) {
        target->oauthClientId = liveProvider.oauthClientId;
    }
    if (target->oauthScope.trimmed().isEmpty()) {
        target->oauthScope = liveProvider.oauthScope;
    }
}

void preserveLiveOAuthProviderState(config::Config *targetConfig,
                                    const config::Config &liveConfig,
                                    const QString &providerId) {
    config::ProviderConfig *targetProvider = providerConfigById(*targetConfig, providerId);
    const config::ProviderConfig *liveProvider = providerConfigById(liveConfig, providerId);
    if (!targetProvider || !liveProvider) {
        return;
    }

    preserveOAuthDefaults(targetProvider, *liveProvider);
    if (providers::normalizedProviderId(providerId) == QStringLiteral("openai_codex")) {
        targetProvider->apiKey = liveProvider->apiKey;
    }
    preserveOAuthRuntimeFields(targetProvider, *liveProvider);
}

void preserveLiveOAuthState(config::Config *targetConfig, const config::Config &liveConfig) {
    if (!targetConfig) {
        return;
    }

    preserveLiveOAuthProviderState(targetConfig, liveConfig, QStringLiteral("openai_codex"));
    preserveLiveOAuthProviderState(targetConfig, liveConfig, QStringLiteral("github_copilot"));
}

void copyResolvedOAuthRuntimeState(config::ProviderConfig *target,
                                   const config::ProviderConfig &resolvedProvider) {
    if (!target) {
        return;
    }

    target->apiKey = resolvedProvider.apiKey;
    preserveOAuthRuntimeFields(target, resolvedProvider);
}

QString localModelForProvider(const QString &providerId, const QString &model) {
    QString local = model.trimmed();
    if (local.isEmpty()) {
        return local;
    }

    const providers::ProviderSpec spec = providers::findProviderSpec(providers::normalizedProviderId(providerId));
    if (!spec.litellmPrefix.isEmpty()) {
        const QString prefix = spec.litellmPrefix + "/";
        if (local.startsWith(prefix)) {
            local = local.mid(prefix.size());
        }
    }
    for (const QString &skip : spec.skipPrefixes) {
        if (!skip.isEmpty() && local.startsWith(skip)) {
            local = local.mid(skip.size());
            break;
        }
    }
    if (spec.stripModelPrefix && local.contains('/')) {
        local = local.section('/', 1);
    }
    return local.trimmed();
}

QString routedModelForProvider(const QString &providerId, const QString &model) {
    const providers::ProviderSpec spec = providers::findProviderSpec(providers::normalizedProviderId(providerId));
    if (spec.name.isEmpty()) {
        return model.trimmed();
    }
    return providers::routeModelForProvider(spec, model.trimmed());
}

QString preferredModelForProvider(const config::Config &config, const QString &providerId) {
    const config::ProviderConfig *provider = providerConfigById(config, providerId);
    if (!provider) {
        return QString();
    }
    if (!provider->model.trimmed().isEmpty()) {
        return provider->model.trimmed();
    }

    QString activeProvider = providers::normalizedProviderId(config.agentDefaults.provider);
    if (activeProvider.isEmpty() || activeProvider == "auto") {
        activeProvider = providers::normalizedProviderId(config.matchedProviderName(config.agentDefaults.model));
    }
    if (activeProvider == providers::normalizedProviderId(providerId)) {
        return localModelForProvider(providerId, config.agentDefaults.model);
    }
    return QString();
}

QStringList fallbackModelCatalogForProvider(const QString &providerId) {
    const QString normalized = providers::normalizedProviderId(providerId);
    if (normalized == QStringLiteral("github_copilot")) {
        return QStringList{
            QStringLiteral("claude-haiku-4.5"),
            QStringLiteral("claude-sonnet-4"),
            QStringLiteral("claude-sonnet-4.5"),
            QStringLiteral("gpt-4.1"),
            QStringLiteral("gpt-4o"),
            QStringLiteral("gpt-5"),
            QStringLiteral("gpt-5-mini")
        };
    }
    return {};
}

QString defaultApiBaseForProvider(const QString &providerId) {
    const QString normalized = providers::normalizedProviderId(providerId);
    const QString oauthApiBase = providers::defaultApiBaseForProvider(normalized);
    if (!oauthApiBase.trimmed().isEmpty()) {
        return oauthApiBase.trimmed();
    }
    const providers::ProviderSpec spec = providers::findProviderSpec(normalized);
    if (!spec.defaultApiBase.trimmed().isEmpty()) {
        return spec.defaultApiBase.trimmed();
    }
    if (normalized == "vllm") {
        return QStringLiteral("http://127.0.0.1:8000/v1");
    }
    return QString();
}

QString resolvedApiBaseForProvider(const QString &providerId, const QString &apiBase) {
    const QString trimmed = apiBase.trimmed();
    return trimmed.isEmpty() ? defaultApiBaseForProvider(providerId) : trimmed;
}

QJsonArray stringListToJsonArray(const QStringList &values) {
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QJsonObject extensionCatalogEntryToJson(const runtime::ExtensionCatalogEntry &record) {
    return QJsonObject{
        {QStringLiteral("catalogId"), record.catalogId},
        {QStringLiteral("kind"), record.kind},
        {QStringLiteral("installId"), record.installId},
        {QStringLiteral("title"), record.title},
        {QStringLiteral("summary"), record.summary},
        {QStringLiteral("description"), record.description},
        {QStringLiteral("target"), record.target},
        {QStringLiteral("tags"), stringListToJsonArray(record.tags)},
        {QStringLiteral("installed"), record.installed}
    };
}

QJsonObject providerModelErrorResponse(const QString &providerId,
                                       const QString &title,
                                       const QString &body,
                                       const QString &tone = QStringLiteral("warning")) {
    return QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("providerId"), providerId},
        {QStringLiteral("title"), title},
        {QStringLiteral("body"), body},
        {QStringLiteral("tone"), tone},
        {QStringLiteral("models"), QJsonArray()},
        {QStringLiteral("warnings"), QJsonArray()}
    };
}

void appendProviderWarning(QJsonArray *warnings,
                           const QString &title,
                           const QString &body,
                           const QString &tone = QStringLiteral("warning")) {
    if (!warnings) {
        return;
    }

    warnings->append(QJsonObject{
        {QStringLiteral("title"), title},
        {QStringLiteral("body"), body},
        {QStringLiteral("tone"), tone}
    });
}

QJsonObject providerConfigNotFoundResponse(const QString &normalized) {
    return QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("providerId"), normalized},
        {QStringLiteral("error"), QStringLiteral("provider config not found")}
    };
}

QJsonObject providerOAuthResultResponse(const providers::ProviderOAuthResult &result,
                                        const config::Config &config,
                                        bool configChanged) {
    QJsonObject response = QJsonObject::fromVariantMap(result.toVariantMap());
    response.insert(QStringLiteral("ok"), result.ok);
    response.insert(QStringLiteral("providerId"), result.providerId);
    if (configChanged) {
        response.insert(QStringLiteral("configChanged"), true);
        response.insert(QStringLiteral("config"), config.toJson());
    }
    return response;
}

QJsonObject providerAuthStatusResponse(const QJsonObject &payload) {
    const QString normalized = providers::normalizedProviderId(stringValue(payload, "providerId"));
    config::Config config = config::ConfigLoader::load();
    const config::ProviderConfig *provider = providerConfigById(config, normalized);
    if (!provider) {
        return providerConfigNotFoundResponse(normalized);
    }

    providers::ProviderOAuthResult result = providers::providerOAuthStatus(normalized, *provider);
    return providerOAuthResultResponse(result, config, false);
}

QJsonObject saveConfigurationResponse(const QJsonObject &payload, RuntimeCore *runtime) {
    const QJsonObject draftObject = payload.value(QStringLiteral("draftConfig")).toObject();
    config::Config savedConfig = draftObject.isEmpty() ? config::ConfigLoader::load()
                                                       : config::Config::fromJson(draftObject);
    const config::Config liveConfig = config::ConfigLoader::load();
    preserveLiveOAuthState(&savedConfig, liveConfig);

    if (!config::ConfigLoader::save(savedConfig)) {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("saved"), false},
            {QStringLiteral("reloadOk"), false},
            {QStringLiteral("configChanged"), false},
            {QStringLiteral("config"), savedConfig.toJson()},
            {QStringLiteral("title"), QStringLiteral("保存失败")},
            {QStringLiteral("body"), QStringLiteral("无法写入配置文件.")},
            {QStringLiteral("tone"), QStringLiteral("warning")}
        };
    }

    const bool reloadOk = runtime && runtime->reloadFromDisk();
    if (!reloadOk) {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("saved"), true},
            {QStringLiteral("reloadOk"), false},
            {QStringLiteral("configChanged"), true},
            {QStringLiteral("config"), savedConfig.toJson()},
            {QStringLiteral("title"), QStringLiteral("运行时重载失败")},
            {QStringLiteral("body"), QStringLiteral("配置已保存,但运行时没有成功切换到新配置.")},
            {QStringLiteral("tone"), QStringLiteral("warning")}
        };
    }

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("saved"), true},
        {QStringLiteral("reloadOk"), true},
        {QStringLiteral("configChanged"), true},
        {QStringLiteral("config"), savedConfig.toJson()},
        {QStringLiteral("title"), QStringLiteral("配置已同步")},
        {QStringLiteral("body"), QStringLiteral("新的系统参数已经写入并重载.")},
        {QStringLiteral("tone"), QStringLiteral("success")}
    };
}

QJsonObject fetchProviderModelsResponse(const QJsonObject &payload, RuntimeCore *runtime) {
    const QJsonObject draftObject = payload.value(QStringLiteral("draftConfig")).toObject();
    const QJsonObject liveObject = payload.value(QStringLiteral("liveConfig")).toObject();
    config::Config config = draftObject.isEmpty() ? config::ConfigLoader::load()
                                                  : config::Config::fromJson(draftObject);
    const config::Config liveConfig = liveObject.isEmpty() ? config::ConfigLoader::load()
                                                           : config::Config::fromJson(liveObject);
    preserveLiveOAuthState(&config, liveConfig);

    const QString normalized = providers::normalizedProviderId(stringValue(payload, "providerId"));
    config::ProviderConfig *provider = providerConfigById(config, normalized);
    if (!provider) {
        return providerModelErrorResponse(normalized,
                                          QStringLiteral("模型读取失败"),
                                          QStringLiteral("没有找到对应驱动的配置."));
    }

    if (normalized == QStringLiteral("azure_openai")) {
        return providerModelErrorResponse(normalized,
                                          QStringLiteral("Model sync unavailable"),
                                          QStringLiteral("Fill the model name manually for this provider."));
    }

    QJsonArray warnings;
    bool configChanged = false;
    config::Config changedConfig = liveConfig;

    providers::ProviderOAuthResult resolved =
        providers::resolveProviderAccess(normalized, *provider, true);
    if (resolved.changed) {
        *provider = resolved.config;

        config::Config nextLiveConfig = config::ConfigLoader::load();
        config::ProviderConfig *liveProvider = providerConfigById(nextLiveConfig, normalized);
        if (liveProvider) {
            copyResolvedOAuthRuntimeState(liveProvider, resolved.config);
            if (!config::ConfigLoader::save(nextLiveConfig)) {
                appendProviderWarning(&warnings,
                                      QStringLiteral("OAuth persistence warning"),
                                      QStringLiteral("Refreshed provider credentials were used for model sync, but YAOS could not persist them."));
            } else {
                changedConfig = nextLiveConfig;
                configChanged = true;
                if (runtime) {
                    runtime->reloadFromDisk();
                }
            }
        }
    }

    const config::ProviderConfig providerSnapshot = *provider;
    const QString apiBase = resolved.apiBase.trimmed().isEmpty()
        ? resolvedApiBaseForProvider(normalized, providerSnapshot.apiBase)
        : resolved.apiBase.trimmed();
    const bool trustStoredCredentialFallback =
        !(normalized == QStringLiteral("github_copilot") && !resolved.ok);
    const QString apiKey = resolved.apiKey.trimmed().isEmpty()
        ? (trustStoredCredentialFallback ? providerSnapshot.apiKey : QString())
        : resolved.apiKey;
    const QHash<QString, QString> extraHeaders =
        resolved.headers.isEmpty()
            ? (trustStoredCredentialFallback ? providerSnapshot.extraHeaders : QHash<QString, QString>())
            : resolved.headers;
    const bool hasApiKey = !apiKey.trimmed().isEmpty();
    const bool hasExtraHeaders = !extraHeaders.isEmpty();
    const bool requiresApiKey = normalized == "anthropic";
    const bool requiresCredential = normalized != "custom" &&
                                    normalized != "vllm" &&
                                    normalized != "anthropic";

    if (!resolved.ok && !hasApiKey && !hasExtraHeaders) {
        return providerModelErrorResponse(normalized,
                                          QStringLiteral("OAuth login required"),
                                          resolved.error.trimmed().isEmpty()
                                              ? QStringLiteral("Complete the provider login before loading models.")
                                              : resolved.error);
    }
    if ((requiresApiKey && !hasApiKey) || (requiresCredential && !hasApiKey && !hasExtraHeaders)) {
        const bool missingCredential = requiresCredential;
        return providerModelErrorResponse(normalized,
                                          missingCredential
                                              ? QStringLiteral("Missing credential")
                                              : QStringLiteral("Missing API key"),
                                          missingCredential
                                              ? QStringLiteral("Fill API key or extra headers before loading models.")
                                              : QStringLiteral("Fill the API key before loading models."));
    }
    if (normalized != "anthropic" && apiBase.isEmpty()) {
        return providerModelErrorResponse(normalized,
                                          QStringLiteral("缺少 API Base"),
                                          QStringLiteral("请先填写有效的 API Base."));
    }

    const QString selectedModel = preferredModelForProvider(config, normalized);
    QStringList models;
    if (normalized == "anthropic") {
        providers::AnthropicProvider providerClient(apiKey,
                                                    apiBase,
                                                    routedModelForProvider(normalized, selectedModel));
        models = providerClient.listModels();
    } else {
        providers::OpenAICompatibleProvider providerClient(apiKey,
                                                           apiBase,
                                                           routedModelForProvider(normalized, selectedModel),
                                                           normalized,
                                                           config.agentDefaults.reasoningEffort,
                                                           extraHeaders);
        models = providerClient.listModels();
    }

    QStringList normalizedModels;
    QSet<QString> seen;
    for (const QString &model : models) {
        const QString local = localModelForProvider(normalized, model);
        if (local.isEmpty()) {
            continue;
        }
        const QString dedupeKey = local.toLower();
        if (seen.contains(dedupeKey)) {
            continue;
        }
        seen.insert(dedupeKey);
        normalizedModels.append(local);
    }
    normalizedModels.sort(Qt::CaseInsensitive);

    bool usedFallback = false;
    if (normalizedModels.isEmpty()) {
        const QStringList fallbackModels = fallbackModelCatalogForProvider(normalized);
        if (fallbackModels.isEmpty()) {
            return providerModelErrorResponse(normalized,
                                              QStringLiteral("模型列表为空"),
                                              QStringLiteral("当前驱动没有返回模型列表,请检查网络,URL 或凭据."));
        }
        normalizedModels = fallbackModels;
        usedFallback = true;
    }

    config::Config nextLiveConfig = config::ConfigLoader::load();
    config::ProviderConfig *liveProvider = providerConfigById(nextLiveConfig, normalized);
    if (liveProvider && liveProvider->availableModels != normalizedModels) {
        liveProvider->availableModels = normalizedModels;
        if (!config::ConfigLoader::save(nextLiveConfig)) {
            appendProviderWarning(&warnings,
                                  QStringLiteral("Model sync persistence warning"),
                                  QStringLiteral("The model list was fetched, but YAOS could not persist it to the live config."));
        } else {
            changedConfig = nextLiveConfig;
            configChanged = true;
        }
    }

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("providerId"), normalized},
        {QStringLiteral("models"), stringListToJsonArray(normalizedModels)},
        {QStringLiteral("modelCount"), normalizedModels.size()},
        {QStringLiteral("usedFallback"), usedFallback},
        {QStringLiteral("configChanged"), configChanged},
        {QStringLiteral("config"), changedConfig.toJson()},
        {QStringLiteral("warnings"), warnings}
    };
}

using ProviderOAuthAction = providers::ProviderOAuthResult (*)(const QString &,
                                                              const config::ProviderConfig &);

QJsonObject providerOAuthActionResponse(const QJsonObject &payload,
                                        RuntimeCore *runtime,
                                        ProviderOAuthAction action,
                                        const QString &persistError,
                                        bool reloadOnChange,
                                        bool reloadOnlyWhenLoggedIn = false) {
    const QString normalized = providers::normalizedProviderId(stringValue(payload, "providerId"));
    config::Config config = config::ConfigLoader::load();
    config::ProviderConfig *provider = providerConfigById(config, normalized);
    if (!provider) {
        return providerConfigNotFoundResponse(normalized);
    }

    providers::ProviderOAuthResult result = action(normalized, *provider);
    bool configChanged = false;
    if (result.changed) {
        *provider = result.config;
        if (!config::ConfigLoader::save(config)) {
            result.ok = false;
            result.error = persistError;
        } else {
            configChanged = true;
            if (runtime && reloadOnChange && (!reloadOnlyWhenLoggedIn || result.loggedIn)) {
                runtime->reloadFromDisk();
            }
        }
    }
    return providerOAuthResultResponse(result, config, configChanged);
}

QJsonObject logoutProviderOAuthResponse(const QJsonObject &payload, RuntimeCore *runtime) {
    const QString normalized = providers::normalizedProviderId(stringValue(payload, "providerId"));
    config::Config config = config::ConfigLoader::load();
    config::ProviderConfig *provider = providerConfigById(config, normalized);
    if (!provider) {
        return providerConfigNotFoundResponse(normalized);
    }

    *provider = providers::clearOAuthState(normalized, *provider);
    if (!config::ConfigLoader::save(config)) {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("providerId"), normalized},
            {QStringLiteral("error"), QStringLiteral("Unable to clear stored OAuth credentials.")}
        };
    }
    if (runtime) {
        runtime->reloadFromDisk();
    }
    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("providerId"), normalized},
        {QStringLiteral("configChanged"), true},
        {QStringLiteral("config"), config.toJson()}
    };
}

QJsonObject startProviderBrowserOAuthResponse(const QJsonObject &payload) {
    const QString normalized = providers::normalizedProviderId(stringValue(payload, "providerId"));
    config::Config config = config::ConfigLoader::load();
    config::ProviderConfig *provider = providerConfigById(config, normalized);
    if (!provider) {
        return providerConfigNotFoundResponse(normalized);
    }

    providers::ProviderOAuthResult result =
        providers::startBrowserFlow(normalized,
                                    *provider,
                                    stringValue(payload, "redirectUri"),
                                    stringValue(payload, "state"),
                                    stringValue(payload, "codeVerifier"));
    bool configChanged = false;
    if (result.changed) {
        *provider = result.config;
        if (!config::ConfigLoader::save(config)) {
            result.ok = false;
            result.error = QStringLiteral("failed to persist provider defaults");
        } else {
            configChanged = true;
        }
    }
    return providerOAuthResultResponse(result, config, configChanged);
}

QJsonObject completeProviderBrowserOAuthResponse(const QJsonObject &payload, RuntimeCore *runtime) {
    const QString normalized = providers::normalizedProviderId(stringValue(payload, "providerId"));
    config::Config config = config::ConfigLoader::load();
    config::ProviderConfig *provider = providerConfigById(config, normalized);
    if (!provider) {
        return providerConfigNotFoundResponse(normalized);
    }

    providers::ProviderOAuthResult result =
        providers::completeBrowserFlow(normalized,
                                       *provider,
                                       stringValue(payload, "redirectUri"),
                                       stringValue(payload, "expectedState"),
                                       stringValue(payload, "codeVerifier"),
                                       stringValue(payload, "callbackUrl"));
    bool configChanged = false;
    if (result.changed) {
        *provider = result.config;
        if (!config::ConfigLoader::save(config)) {
            result.ok = false;
            result.error = QStringLiteral("failed to persist OAuth credentials");
        } else {
            configChanged = true;
            if (runtime) {
                runtime->reloadFromDisk();
            }
        }
    }
    return providerOAuthResultResponse(result, config, configChanged);
}

QJsonObject extensionCatalogResponse(const QJsonObject &payload) {
    const config::Config config = configValue(payload);
    QJsonArray items;
    const QVector<runtime::ExtensionCatalogEntry> entries =
        runtime::buildExtensionCatalog(config.workspacePath(), config);
    for (const runtime::ExtensionCatalogEntry &entry : entries) {
        items.append(extensionCatalogEntryToJson(entry));
    }
    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("items"), items}
    };
}

QJsonObject installCatalogEntryResponse(const QJsonObject &payload, RuntimeCore *runtime) {
    config::Config config = configValue(payload);
    const QString catalogId = stringValue(payload, "catalogId");
    QString message;
    if (!runtime::installCatalogEntry(config.workspacePath(), &config, catalogId, &message)) {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("message"), message},
            {QStringLiteral("error"), message.isEmpty()
                                       ? QStringLiteral("Unable to install selected extension.")
                                       : message}
        };
    }

    const bool configChanged = catalogId.trimmed().startsWith(QStringLiteral("mcp."));
    if (configChanged) {
        if (!config::ConfigLoader::save(config)) {
            const QString error = QStringLiteral("扩展文件已生成,但配置没有成功写回.");
            return QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("message"), error},
                {QStringLiteral("error"), error}
            };
        }
        if (runtime && !runtime->reloadFromDisk()) {
            const QString error = QStringLiteral("扩展已安装,但运行时没有成功切换到新配置.");
            return QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("message"), error},
                {QStringLiteral("error"), error},
                {QStringLiteral("configChanged"), true},
                {QStringLiteral("config"), config.toJson()}
            };
        }
    }

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("message"), message},
        {QStringLiteral("catalogId"), catalogId},
        {QStringLiteral("configChanged"), configChanged},
        {QStringLiteral("config"), config.toJson()}
    };
}

QJsonObject pushDelegationTemplatesToControlResponse(const QJsonObject &payload) {
    const config::Config config = configValue(payload);
    const QString endpoint = controlPlaneEndpoint(config);
    if (endpoint.trimmed().isEmpty()) {
        const QString message = QStringLiteral("Control plane endpoint is not configured.");
        return operationErrorResponse(QStringLiteral("同步失败"), message);
    }

    QList<config::DelegationTemplateConfig> normalized;
    QString error;
    const QJsonDocument document(payload.value(QStringLiteral("records")).toArray());
    if (!config::parseDelegationTemplateExchangeDocument(document, &normalized, &error)) {
        return operationErrorResponse(QStringLiteral("同步失败"),
                                      error.isEmpty()
                                          ? QStringLiteral("没有可推送的 delegation templates.")
                                          : error,
                                      error);
    }

    distributed::RemoteControlClient client(endpoint, 5000);
    if (!client.ping(&error)) {
        return operationErrorResponse(QStringLiteral("同步失败"),
                                      error.isEmpty()
                                          ? QStringLiteral("当前 control plane 不可达.")
                                          : error,
                                      error);
    }

    const bool replaceExisting = boolValue(payload, "replace", false);
    QJsonObject request;
    request.insert(QStringLiteral("replace"), replaceExisting);
    request.insert(QStringLiteral("envelope"),
                   config::delegationTemplateExchangeEnvelope(normalized,
                                                             config::ConfigLoader::defaultConfigPath(),
                                                             config.deployment.nodeId,
                                                             config.deployment.clusterId));

    QJsonObject response = client.post(QStringLiteral("/v1/control/delegation-templates/sync"),
                                       request,
                                       &error);
    if (response.isEmpty() || !response.value(QStringLiteral("ok")).toBool(false)) {
        const QString message = !error.trimmed().isEmpty()
            ? error
            : response.value(QStringLiteral("error")).toString();
        return operationErrorResponse(QStringLiteral("同步失败"),
                                      message.isEmpty()
                                          ? QStringLiteral("无法推送 delegation templates.")
                                          : message,
                                      message);
    }

    response.insert(QStringLiteral("ok"), true);
    response.insert(QStringLiteral("replace"), replaceExisting);
    response.insert(QStringLiteral("pushedCount"), normalized.size());
    response.insert(QStringLiteral("endpoint"), client.endpoint());
    response.insert(QStringLiteral("title"),
                    replaceExisting ? QStringLiteral("模板已替换推送")
                                    : QStringLiteral("模板已推送"));
    response.insert(QStringLiteral("body"),
                    QStringLiteral("已推送 %1 条 delegation templates.").arg(normalized.size()));
    response.insert(QStringLiteral("tone"), QStringLiteral("success"));
    return response;
}

QJsonObject pullDelegationTemplatesFromControlResponse(const QJsonObject &payload,
                                                       RuntimeCore *runtime) {
    const config::Config config = configValue(payload);
    const QString endpoint = controlPlaneEndpoint(config);
    if (endpoint.trimmed().isEmpty()) {
        const QString message = QStringLiteral("Control plane endpoint is not configured.");
        return operationErrorResponse(QStringLiteral("拉取失败"), message);
    }

    distributed::RemoteControlClient client(endpoint, 5000);
    QString error;
    if (!client.ping(&error)) {
        return operationErrorResponse(QStringLiteral("拉取失败"),
                                      error.isEmpty()
                                          ? QStringLiteral("当前 control plane 不可达.")
                                          : error,
                                      error);
    }

    QJsonObject response = client.post(QStringLiteral("/v1/control/delegation-templates/list"),
                                       QJsonObject{{QStringLiteral("limit"), 1024}},
                                       &error);
    if (response.isEmpty() || !response.value(QStringLiteral("ok")).toBool(false)) {
        const QString message = !error.trimmed().isEmpty()
            ? error
            : response.value(QStringLiteral("error")).toString();
        return operationErrorResponse(QStringLiteral("拉取失败"),
                                      message.isEmpty()
                                          ? QStringLiteral("无法从 control plane 拉取 delegation templates.")
                                          : message,
                                      message);
    }

    const QJsonObject envelope = response.value(QStringLiteral("envelope")).toObject();
    QList<config::DelegationTemplateConfig> imported;
    if (!config::parseDelegationTemplateExchangeDocument(QJsonDocument(envelope.isEmpty() ? response : envelope),
                                                         &imported,
                                                         &error)) {
        return operationErrorResponse(QStringLiteral("拉取失败"),
                                      error.isEmpty()
                                          ? QStringLiteral("control plane 返回的模板内容无效.")
                                          : error,
                                      error);
    }

    const bool replaceExisting = boolValue(payload, "replace", false);
    config::Config nextConfig = config;
    nextConfig.memory.delegationTemplates =
        config::mergeDelegationTemplateRecords(nextConfig.memory.delegationTemplates,
                                               imported,
                                               replaceExisting);
    if (!config::ConfigLoader::save(nextConfig)) {
        const QString message = QStringLiteral("Failed to save updated config.");
        return operationErrorResponse(QStringLiteral("拉取失败"), message);
    }

    if (runtime) {
        runtime->reloadFromDisk();
    }

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("replace"), replaceExisting},
        {QStringLiteral("pulledCount"), imported.size()},
        {QStringLiteral("totalTemplates"), nextConfig.memory.delegationTemplates.size()},
        {QStringLiteral("endpoint"), client.endpoint()},
        {QStringLiteral("configChanged"), true},
        {QStringLiteral("config"), nextConfig.toJson()},
        {QStringLiteral("title"), replaceExisting ? QStringLiteral("模板已替换拉取")
                                                  : QStringLiteral("模板已拉取")},
        {QStringLiteral("body"), QStringLiteral("已拉取 %1 条 delegation templates.").arg(imported.size())},
        {QStringLiteral("tone"), QStringLiteral("success")}
    };
}

} // namespace

LocalRuntimeClient::LocalRuntimeClient()
    : _runtime(std::make_unique<RuntimeCore>()) {}

LocalRuntimeClient::LocalRuntimeClient(std::unique_ptr<RuntimeCore> runtime)
    : _runtime(std::move(runtime)) {
    if (!_runtime) {
        _runtime = std::make_unique<RuntimeCore>();
    }
}

QJsonObject LocalRuntimeClient::invoke(const QString &method, const QJsonObject &payload) {
    if (!_runtime) {
        return failure("Runtime core is not initialized.");
    }

    if (method == "studio.providerAuthStatus") {
        return providerAuthStatusResponse(payload);
    }
    if (method == "studio.saveConfiguration") {
        return saveConfigurationResponse(payload, _runtime.get());
    }
    if (method == "studio.fetchProviderModels") {
        return fetchProviderModelsResponse(payload, _runtime.get());
    }
    if (method == "studio.startProviderDeviceFlow") {
        return providerOAuthActionResponse(payload,
                                           _runtime.get(),
                                           providers::startDeviceFlow,
                                           QStringLiteral("failed to persist OAuth state"),
                                           false);
    }
    if (method == "studio.pollProviderDeviceFlow") {
        return providerOAuthActionResponse(payload,
                                           _runtime.get(),
                                           providers::pollDeviceFlow,
                                           QStringLiteral("failed to persist OAuth state"),
                                           true,
                                           true);
    }
    if (method == "studio.refreshProviderOAuth") {
        return providerOAuthActionResponse(payload,
                                           _runtime.get(),
                                           providers::refreshProviderTokens,
                                           QStringLiteral("failed to persist refreshed credentials"),
                                           true);
    }
    if (method == "studio.logoutProviderOAuth") {
        return logoutProviderOAuthResponse(payload, _runtime.get());
    }
    if (method == "studio.startProviderBrowserOAuth") {
        return startProviderBrowserOAuthResponse(payload);
    }
    if (method == "studio.completeProviderBrowserOAuth") {
        return completeProviderBrowserOAuthResponse(payload, _runtime.get());
    }
    if (method == "studio.extensionCatalog") {
        return extensionCatalogResponse(payload);
    }
    if (method == "studio.installCatalogEntry") {
        return installCatalogEntryResponse(payload, _runtime.get());
    }
    if (method == "studio.pushDelegationTemplatesToControl") {
        return pushDelegationTemplatesToControlResponse(payload);
    }
    if (method == "studio.pullDelegationTemplatesFromControl") {
        return pullDelegationTemplatesFromControlResponse(payload, _runtime.get());
    }

    if (method == "statusSnapshot") {
        return success(QJsonObject{{"status", serialization::toJson(_runtime->statusSnapshot())}});
    }
    if (method == "serviceHealth") {
        return _runtime->serviceHealth(stringValue(payload, "modelOverride"),
                                       stringValue(payload, "providerOverride"));
    }
    if (method == "initializeWorkspace") {
        QString message;
        const bool initialized = _runtime->initializeWorkspace(&message);
        return success(QJsonObject{{"value", initialized}, {"message", message}});
    }
    if (method == "reloadFromDisk") {
        return success(QJsonObject{
            {"value", _runtime->reloadFromDisk(stringValue(payload, "modelOverride"),
                                               stringValue(payload, "providerOverride"))}
        });
    }
    if (method == "startGatewayServices") {
        return success(QJsonObject{{"value", _runtime->startGatewayServices()}});
    }
    if (method == "stopGatewayServices") {
        _runtime->stopGatewayServices();
        return success();
    }
    if (method == "gatewayRunning") {
        return success(QJsonObject{{"value", _runtime->gatewayRunning()}});
    }
    if (method == "recentApprovals") {
        return success(QJsonObject{
            {"items", serialization::toJson(_runtime->recentApprovals(intValue(payload, "limit", 50),
                                                                       stringValue(payload, "state")))}
        });
    }
    if (method == "resolveApproval") {
        return success(QJsonObject{
            {"value", _runtime->resolveApproval(stringValue(payload, "approvalId"),
                                                stringValue(payload, "decision"),
                                                stringValue(payload, "scope"),
                                                stringValue(payload, "note"))}
        });
    }
    if (method == "recentNotifications") {
        return success(QJsonObject{
            {"items", serialization::toJson(_runtime->recentNotifications(intValue(payload, "limit", 50),
                                                                           boolValue(payload, "unreadOnly", false)))}
        });
    }
    if (method == "markAllNotificationsRead") {
        _runtime->markAllNotificationsRead();
        return success();
    }
    if (method == "recentTasks") {
        return success(QJsonObject{
            {"items", serialization::toJson(_runtime->recentTasks(intValue(payload, "limit", 20)))}
        });
    }
    if (method == "recentEvents") {
        return success(QJsonObject{
            {"items", serialization::toJson(_runtime->recentEvents(intValue(payload, "limit", 50)))}
        });
    }
    if (method == "recentNodes") {
        const QVector<distributed::NodeDescriptor> nodes =
            _runtime->recentNodes(intValue(payload, "limit", 64),
                                  boolValue(payload, "onlineOnly", false));
        return success(QJsonObject{
            {"items", distributed::json::toJson(QList<distributed::NodeDescriptor>(nodes.begin(), nodes.end()))}
        });
    }
    if (method == "previewDelegationRoute") {
        return success(QJsonObject{
            {"preview", _runtime->previewDelegationRoute(payload)}
        });
    }
    if (method == "submitDelegationRequest") {
        return success(QJsonObject{
            {"result", _runtime->submitDelegationRequest(payload)}
        });
    }
    if (method == "resourceSummary") {
        return success(QJsonObject{{"summary", serialization::toJson(_runtime->resourceSummary())}});
    }
    if (method == "recentResources") {
        return success(QJsonObject{
            {"items", serialization::toJson(_runtime->recentResources(intValue(payload, "limit", 100),
                                                                       stringValue(payload, "kind")))}
        });
    }
    if (method == "automations") {
        return success(QJsonObject{
            {"items", serialization::toJson(_runtime->automations(intValue(payload, "limit", 100)))}
        });
    }
    if (method == "automationRuns") {
        return success(QJsonObject{
            {"items", serialization::toJson(_runtime->automationRuns(intValue(payload, "limit", 120),
                                                                      stringValue(payload, "automationId")))}
        });
    }
    if (method == "automation") {
        return success(QJsonObject{
            {"record", serialization::toJson(_runtime->automation(stringValue(payload, "id")))}
        });
    }
    if (method == "saveAutomation") {
        QString error;
        const QString id = _runtime->saveAutomation(
            serialization::automationRecordFromJson(payload.value("record").toObject()),
            &error);
        return success(QJsonObject{{"value", id}, {"error", error}});
    }
    if (method == "removeAutomation") {
        return success(QJsonObject{{"value", _runtime->removeAutomation(stringValue(payload, "id"))}});
    }
    if (method == "runAutomation") {
        QString error;
        const QString result = _runtime->runAutomation(stringValue(payload, "id"),
                                                       &error,
                                                       stringValue(payload, "sessionKey",
                                                                   QStringLiteral("automation:manual")));
        return success(QJsonObject{{"value", result}, {"error", error}});
    }
    if (method == "plugins") {
        return success(QJsonObject{{"items", serialization::toJson(_runtime->plugins())}});
    }
    if (method == "skills") {
        return success(QJsonObject{{"items", serialization::toJson(_runtime->skills())}});
    }
    if (method == "processMessageDetailed") {
        return success(QJsonObject{
            {"turn", serialization::toJson(_runtime->processMessageDetailed(
                         stringValue(payload, "content"),
                         stringValue(payload, "sessionKey", QStringLiteral("gui:primary")),
                         stringValue(payload, "channel", QStringLiteral("gui")),
                         stringValue(payload, "chatId", QStringLiteral("desktop")),
                         stringValue(payload, "modelOverride"),
                         stringValue(payload, "providerOverride")))}
        });
    }
    if (method == "processMessage") {
        return success(QJsonObject{
            {"value", _runtime->processMessage(
                          stringValue(payload, "content"),
                          stringValue(payload, "sessionKey", QStringLiteral("gui:primary")),
                          stringValue(payload, "channel", QStringLiteral("gui")),
                          stringValue(payload, "chatId", QStringLiteral("desktop")),
                          stringValue(payload, "modelOverride"),
                          stringValue(payload, "providerOverride"))}
        });
    }

    return failure(QString("Unknown runtime method: %1").arg(method));
}

QJsonObject LocalRuntimeClient::success(const QJsonObject &payload) {
    QJsonObject response = payload;
    response.insert("ok", true);
    return response;
}

QJsonObject LocalRuntimeClient::failure(const QString &message) {
    return QJsonObject{
        {"ok", false},
        {"error", message}
    };
}

} // namespace yaos::runtime
