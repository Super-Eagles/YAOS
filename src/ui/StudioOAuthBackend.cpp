#include "StudioBackend_p.h"

#include "../providers/ProviderOAuth.h"

namespace yaos::ui {

void preserveOAuthRuntimeFields(config::ProviderConfig *target, const config::ProviderConfig &liveProvider) {
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

void preserveOAuthDefaults(config::ProviderConfig *target, const config::ProviderConfig &liveProvider) {
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

QVariantMap RuntimeFacadeStudioBackend::providerAuthStatus(const config::Config &config,
                                                           const QString &providerId) {
    const QString normalized = providers::normalizedProviderId(providerId);
    const config::ProviderConfig *provider = providerConfigById(config, normalized);
    if (!provider) {
        return QVariantMap{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("provider config not found")},
            {QStringLiteral("providerId"), normalized}
        };
    }
    providers::ProviderOAuthResult result = providers::providerOAuthStatus(normalized, *provider);
    return result.toVariantMap();
}

QVariantMap RuntimeFacadeStudioBackend::startProviderDeviceFlow(config::Config *config,
                                                                 const QString &providerId) {
    if (!config) {
        return operationError(QStringLiteral("Device flow failed"),
                              QStringLiteral("config is not available"));
    }
    const QString normalized = providers::normalizedProviderId(providerId);
    preserveLiveOAuthState(config, config::ConfigLoader::load());
    config::ProviderConfig *provider = providerConfigById(*config, normalized);
    if (!provider) {
        return operationError(QStringLiteral("Device flow failed"),
                              QStringLiteral("provider config not found"));
    }

    providers::ProviderOAuthResult result = providers::startDeviceFlow(normalized, *provider);
    if (result.changed) {
        *provider = result.config;
        config::Config liveConfig = config::ConfigLoader::load();
        config::ProviderConfig *liveProvider = providerConfigById(liveConfig, normalized);
        if (liveProvider) {
            *liveProvider = result.config;
            if (!config::ConfigLoader::save(liveConfig)) {
                result.ok = false;
                result.error = QStringLiteral("failed to persist OAuth state");
            } else {
                *config = liveConfig;
            }
        }
    }
    return result.toVariantMap();
}

QVariantMap RuntimeFacadeStudioBackend::pollProviderDeviceFlow(config::Config *config,
                                                                const QString &providerId) {
    if (!config) {
        return operationError(QStringLiteral("Poll failed"),
                              QStringLiteral("config is not available"));
    }
    const QString normalized = providers::normalizedProviderId(providerId);
    config::ProviderConfig *provider = providerConfigById(*config, normalized);
    if (!provider) {
        return operationError(QStringLiteral("Poll failed"),
                              QStringLiteral("provider config not found"));
    }

    providers::ProviderOAuthResult result = providers::pollDeviceFlow(normalized, *provider);
    if (result.changed) {
        *provider = result.config;
        config::Config liveConfig = config::ConfigLoader::load();
        config::ProviderConfig *liveProvider = providerConfigById(liveConfig, normalized);
        if (liveProvider) {
            *liveProvider = result.config;
            if (!config::ConfigLoader::save(liveConfig)) {
                result.ok = false;
                result.error = QStringLiteral("failed to persist OAuth state");
            } else {
                *config = liveConfig;
                if (result.loggedIn && m_facade) {
                    m_facade->reloadFromDisk();
                }
            }
        }
    }
    return result.toVariantMap();
}

QVariantMap RuntimeFacadeStudioBackend::refreshProviderOAuth(config::Config *config,
                                                              const QString &providerId) {
    if (!config) {
        return operationError(QStringLiteral("Refresh failed"),
                              QStringLiteral("config is not available"));
    }
    const QString normalized = providers::normalizedProviderId(providerId);
    config::ProviderConfig *provider = providerConfigById(*config, normalized);
    if (!provider) {
        return operationError(QStringLiteral("Refresh failed"),
                              QStringLiteral("provider config not found"));
    }

    providers::ProviderOAuthResult result = providers::refreshProviderTokens(normalized, *provider);
    if (result.changed) {
        *provider = result.config;
        config::Config liveConfig = config::ConfigLoader::load();
        config::ProviderConfig *liveProvider = providerConfigById(liveConfig, normalized);
        if (liveProvider) {
            *liveProvider = result.config;
            if (!config::ConfigLoader::save(liveConfig)) {
                result.ok = false;
                result.error = QStringLiteral("failed to persist refreshed credentials");
            } else {
                *config = liveConfig;
                if (m_facade) {
                    m_facade->reloadFromDisk();
                }
            }
        }
    }
    return result.toVariantMap();
}

QVariantMap RuntimeFacadeStudioBackend::logoutProviderOAuth(config::Config *config,
                                                             const QString &providerId) {
    if (!config) {
        return operationError(QStringLiteral("Logout failed"),
                              QStringLiteral("config is not available"));
    }
    const QString normalized = providers::normalizedProviderId(providerId);
    config::ProviderConfig *provider = providerConfigById(*config, normalized);
    if (!provider) {
        return operationError(QStringLiteral("Logout failed"),
                              QStringLiteral("provider config not found"));
    }

    *provider = providers::clearOAuthState(normalized, *provider);
    config::Config liveConfig = config::ConfigLoader::load();
    config::ProviderConfig *liveProvider = providerConfigById(liveConfig, normalized);
    if (liveProvider) {
        *liveProvider = *provider;
    }
    if (!config::ConfigLoader::save(liveConfig)) {
        return operationError(QStringLiteral("Logout failed"),
                              QStringLiteral("Unable to clear stored OAuth credentials."));
    }
    *config = liveConfig;
    if (m_facade) {
        m_facade->reloadFromDisk();
    }
    return QVariantMap{
        {QStringLiteral("ok"), true},
        {QStringLiteral("providerId"), normalized},
        {QStringLiteral("configChanged"), true},
        {QStringLiteral("config"), liveConfig.toJson().toVariantMap()}
    };
}

QVariantMap RuntimeFacadeStudioBackend::startProviderBrowserOAuth(config::Config *config,
                                                                  const QString &providerId,
                                                                  const QString &redirectUri,
                                                                  const QString &state,
                                                                  const QString &codeVerifier) {
    if (!config) {
        return operationError(QStringLiteral("OAuth failed"),
                              QStringLiteral("config is not available"));
    }
    const QString normalized = providers::normalizedProviderId(providerId);
    preserveLiveOAuthState(config, config::ConfigLoader::load());
    config::ProviderConfig *provider = providerConfigById(*config, normalized);
    if (!provider) {
        return operationError(QStringLiteral("OAuth failed"),
                              QStringLiteral("provider config not found"));
    }

    providers::ProviderOAuthResult result =
        providers::startBrowserFlow(normalized,
                                    *provider,
                                    redirectUri,
                                    state,
                                    codeVerifier);
    if (result.changed) {
        *provider = result.config;
        config::Config liveConfig = config::ConfigLoader::load();
        config::ProviderConfig *liveProvider = providerConfigById(liveConfig, normalized);
        if (liveProvider) {
            *liveProvider = result.config;
            if (!config::ConfigLoader::save(liveConfig)) {
                result.ok = false;
                result.error = QStringLiteral("failed to persist provider defaults");
            } else {
                *config = liveConfig;
            }
        }
    }

    QVariantMap map = result.toVariantMap();
    if (result.changed && result.ok) {
        map.insert(QStringLiteral("configChanged"), true);
        map.insert(QStringLiteral("config"), config->toJson().toVariantMap());
    }
    return map;
}

QVariantMap RuntimeFacadeStudioBackend::completeProviderBrowserOAuth(const QString &providerId,
                                                                     const QString &redirectUri,
                                                                     const QString &expectedState,
                                                                     const QString &codeVerifier,
                                                                     const QString &callbackUrl) {
    const QString normalized = providers::normalizedProviderId(providerId);
    config::Config callbackConfig = config::ConfigLoader::load();
    config::ProviderConfig *provider = providerConfigById(callbackConfig, normalized);
    if (!provider) {
        return QVariantMap{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("provider config not found")},
            {QStringLiteral("providerId"), normalized}
        };
    }

    providers::ProviderOAuthResult result =
        providers::completeBrowserFlow(normalized,
                                       *provider,
                                       redirectUri,
                                       expectedState,
                                       codeVerifier,
                                       callbackUrl);
    if (result.changed) {
        *provider = result.config;
        if (!config::ConfigLoader::save(callbackConfig)) {
            result.ok = false;
            result.error = QStringLiteral("failed to persist OAuth credentials");
        } else if (m_facade) {
            m_facade->reloadFromDisk();
        }
    }

    QVariantMap map = result.toVariantMap();
    if (result.changed && result.ok) {
        map.insert(QStringLiteral("configChanged"), true);
        map.insert(QStringLiteral("config"), callbackConfig.toJson().toVariantMap());
    }
    return map;
}

QVariantMap RemoteStudioBackend::providerAuthStatus(const config::Config &,
                                                    const QString &providerId) {
    return invokeStudioMap(QStringLiteral("studio.providerAuthStatus"),
                           QJsonObject{{QStringLiteral("providerId"), providerId}});
}

QVariantMap RemoteStudioBackend::startProviderDeviceFlow(config::Config *config,
                                                         const QString &providerId) {
    return invokeProviderOAuth(config,
                               QStringLiteral("studio.startProviderDeviceFlow"),
                               providerId,
                               QStringLiteral("Device flow failed"));
}

QVariantMap RemoteStudioBackend::pollProviderDeviceFlow(config::Config *config,
                                                        const QString &providerId) {
    return invokeProviderOAuth(config,
                               QStringLiteral("studio.pollProviderDeviceFlow"),
                               providerId,
                               QStringLiteral("Poll failed"));
}

QVariantMap RemoteStudioBackend::refreshProviderOAuth(config::Config *config,
                                                      const QString &providerId) {
    return invokeProviderOAuth(config,
                               QStringLiteral("studio.refreshProviderOAuth"),
                               providerId,
                               QStringLiteral("Refresh failed"));
}

QVariantMap RemoteStudioBackend::logoutProviderOAuth(config::Config *config,
                                                     const QString &providerId) {
    return invokeProviderOAuth(config,
                               QStringLiteral("studio.logoutProviderOAuth"),
                               providerId,
                               QStringLiteral("Logout failed"));
}

QVariantMap RemoteStudioBackend::startProviderBrowserOAuth(config::Config *config,
                                                           const QString &providerId,
                                                           const QString &redirectUri,
                                                           const QString &state,
                                                           const QString &codeVerifier) {
    if (!config) {
        return operationError(QStringLiteral("OAuth failed"), QStringLiteral("config is not available"));
    }
    QVariantMap result = invokeStudioMap(QStringLiteral("studio.startProviderBrowserOAuth"),
                                         QJsonObject{
                                             {QStringLiteral("providerId"), providerId},
                                             {QStringLiteral("redirectUri"), redirectUri},
                                             {QStringLiteral("state"), state},
                                             {QStringLiteral("codeVerifier"), codeVerifier}
                                         });
    if (result.value(QStringLiteral("configChanged")).toBool()) {
        const QVariantMap configMap = result.value(QStringLiteral("config")).toMap();
        if (!configMap.isEmpty()) {
            *config = config::Config::fromJson(QJsonObject::fromVariantMap(configMap));
        }
    }
    return result;
}

QVariantMap RemoteStudioBackend::completeProviderBrowserOAuth(const QString &providerId,
                                                              const QString &redirectUri,
                                                              const QString &expectedState,
                                                              const QString &codeVerifier,
                                                              const QString &callbackUrl) {
    return invokeStudioMap(QStringLiteral("studio.completeProviderBrowserOAuth"),
                           QJsonObject{
                               {QStringLiteral("providerId"), providerId},
                               {QStringLiteral("redirectUri"), redirectUri},
                               {QStringLiteral("expectedState"), expectedState},
                               {QStringLiteral("codeVerifier"), codeVerifier},
                               {QStringLiteral("callbackUrl"), callbackUrl}
                           });
}

} // namespace yaos::ui
