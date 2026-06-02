#include "StudioBackend_p.h"

#include "../providers/ProviderRegistry.h"
#include "../providers/AnthropicProvider.h"
#include "../providers/OpenAICompatibleProvider.h"
#include "../providers/ProviderOAuth.h"
#include <QSet>
#include <QHash>

namespace yaos::ui {

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

QVariantMap providerModelError(const QString &providerId,
                               const QString &title,
                               const QString &body,
                               const QString &tone) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("providerId"), providerId},
        {QStringLiteral("title"), title},
        {QStringLiteral("body"), body},
        {QStringLiteral("tone"), tone},
        {QStringLiteral("models"), QVariantList()},
        {QStringLiteral("warnings"), QVariantList()}
    };
}

void appendProviderWarning(QVariantList *warnings,
                           const QString &title,
                           const QString &body,
                           const QString &tone) {
    if (!warnings) {
        return;
    }
    warnings->append(QVariantMap{
        {QStringLiteral("title"), title},
        {QStringLiteral("body"), body},
        {QStringLiteral("tone"), tone}
    });
}

QVariantMap RuntimeFacadeStudioBackend::fetchProviderModels(const config::Config &draftConfig,
                                                            const config::Config &liveConfig,
                                                            const QString &providerId) {
    config::Config cfg = draftConfig;
    preserveLiveOAuthState(&cfg, liveConfig);

    const QString normalized = providers::normalizedProviderId(providerId);
    config::ProviderConfig *provider = providerConfigById(cfg, normalized);
    if (!provider) {
        return providerModelError(normalized,
                                  QStringLiteral("模型读取失败"),
                                  QStringLiteral("没有找到对应驱动的配置."));
    }

    if (normalized == QStringLiteral("azure_openai")) {
        return providerModelError(normalized,
                                  QStringLiteral("Model sync unavailable"),
                                  QStringLiteral("Fill the model name manually for this provider."));
    }

    QVariantList warnings;
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
                if (m_facade) {
                    m_facade->reloadFromDisk();
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
        return providerModelError(normalized,
                                  QStringLiteral("OAuth login required"),
                                  resolved.error.trimmed().isEmpty()
                                      ? QStringLiteral("Complete the provider login before loading models.")
                                      : resolved.error);
    }
    if ((requiresApiKey && !hasApiKey) || (requiresCredential && !hasApiKey && !hasExtraHeaders)) {
        const bool missingCredential = requiresCredential;
        return providerModelError(normalized,
                                  missingCredential
                                      ? QStringLiteral("Missing credential")
                                      : QStringLiteral("Missing API key"),
                                  missingCredential
                                      ? QStringLiteral("Fill API key or extra headers before loading models.")
                                      : QStringLiteral("Fill the API key before loading models."));
    }
    if (normalized != "anthropic" && apiBase.isEmpty()) {
        return providerModelError(normalized,
                                  QStringLiteral("缺少 API Base"),
                                  QStringLiteral("请先填写有效的 API Base."));
    }

    const QString selectedModel = preferredModelForProvider(cfg, normalized);
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
                                                           cfg.agentDefaults.reasoningEffort,
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
            return providerModelError(normalized,
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

    return QVariantMap{
        {QStringLiteral("ok"), true},
        {QStringLiteral("providerId"), normalized},
        {QStringLiteral("models"), stringListToVariant(normalizedModels)},
        {QStringLiteral("modelCount"), normalizedModels.size()},
        {QStringLiteral("usedFallback"), usedFallback},
        {QStringLiteral("configChanged"), configChanged},
        {QStringLiteral("config"), changedConfig.toJson().toVariantMap()},
        {QStringLiteral("warnings"), warnings}
    };
}

QVariantMap RemoteStudioBackend::fetchProviderModels(const config::Config &draftConfig,
                                                     const config::Config &liveConfig,
                                                     const QString &providerId) {
    return invokeStudioMap(QStringLiteral("studio.fetchProviderModels"),
                           QJsonObject{
                               {QStringLiteral("draftConfig"), draftConfig.toJson()},
                               {QStringLiteral("liveConfig"), liveConfig.toJson()},
                               {QStringLiteral("providerId"), providerId}
                           });
}

} // namespace yaos::ui
