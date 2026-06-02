#ifndef YAOS_PROVIDERS_PROVIDEROAUTH_H
#define YAOS_PROVIDERS_PROVIDEROAUTH_H

#include <QHash>
#include <QString>
#include <QVariantMap>

#include "../config/Config.h"

namespace yaos::providers {

struct ProviderOAuthResult {
    bool ok = true;
    bool changed = false;
    bool loggedIn = false;
    bool pending = false;
    bool browserSupported = false;
    bool deviceSupported = false;
    bool refreshSupported = false;
    bool requiresClientId = false;
    QString providerId;
    QString mode;
    QString credentialMode;
    QString issuer;
    QString clientId;
    QString scope;
    QString authUrl;
    QString verificationUrl;
    QString userCode;
    QString error;
    QString accountId;
    QString expiresAt;
    QString apiKey;
    QString apiBase;
    QHash<QString, QString> headers;
    config::ProviderConfig config;

    QVariantMap toVariantMap() const;
};

QString normalizedProviderId(const QString &providerId);
bool isOAuthProvider(const QString &providerId);
QString defaultApiBaseForProvider(const QString &providerId);

ProviderOAuthResult providerOAuthStatus(const QString &providerId,
                                        const config::ProviderConfig &providerConfig);
ProviderOAuthResult resolveProviderAccess(const QString &providerId,
                                          const config::ProviderConfig &providerConfig,
                                          bool allowRefresh = true);
ProviderOAuthResult startDeviceFlow(const QString &providerId,
                                    const config::ProviderConfig &providerConfig);
ProviderOAuthResult pollDeviceFlow(const QString &providerId,
                                   const config::ProviderConfig &providerConfig);
ProviderOAuthResult startBrowserFlow(const QString &providerId,
                                     const config::ProviderConfig &providerConfig,
                                     const QString &redirectUri,
                                     const QString &state,
                                     const QString &codeVerifier);
ProviderOAuthResult completeBrowserFlow(const QString &providerId,
                                        const config::ProviderConfig &providerConfig,
                                        const QString &redirectUri,
                                        const QString &expectedState,
                                        const QString &codeVerifier,
                                        const QString &callbackUrl);
ProviderOAuthResult refreshProviderTokens(const QString &providerId,
                                          const config::ProviderConfig &providerConfig);
config::ProviderConfig clearOAuthState(const QString &providerId,
                                       const config::ProviderConfig &providerConfig);

} // namespace yaos::providers

#endif // YAOS_PROVIDERS_PROVIDEROAUTH_H
