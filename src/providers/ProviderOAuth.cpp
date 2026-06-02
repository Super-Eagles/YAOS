#include "ProviderOAuth.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QUrl>
#include <QUrlQuery>

#include "platform/network/FastNetHttpTransport.h"

namespace yaos::providers {

namespace {

constexpr auto kOpenAIIssuer = "https://auth.openai.com";
constexpr auto kOpenAIApiBase = "https://api.openai.com/v1";
constexpr auto kOpenAIMistakenClientId = "app_OUN5TmfEflyA2tRQAO5jx4OZ";
constexpr auto kOpenAIClientId = "app_EMoamEEZ73f0CkXaXp7hrann";
constexpr auto kOpenAIMistakenScope = "openid profile email offline_access";
constexpr auto kOpenAIScope =
    "openid profile email offline_access api.connectors.read api.connectors.invoke";
constexpr auto kOpenAIOriginator = "codex_cli_rs";

constexpr auto kGitHubIssuer = "https://github.com";
constexpr auto kGitHubApiBase = "https://api.githubcopilot.com";
constexpr auto kGitHubScope = "read:user";
constexpr auto kGitHubCopilotTokenUrl = "https://api.github.com/copilot_internal/v2/token";
constexpr auto kGitHubCopilotTokenApiVersion = "2025-04-01";

QString latin1(const char *value) {
    return QString::fromLatin1(value);
}

QString headerAppName() {
    QString value = QCoreApplication::applicationName().trimmed();
    if (value.isEmpty()) {
        value = QFileInfo(QCoreApplication::applicationFilePath()).baseName().trimmed();
    }
    if (value.isEmpty()) {
        value = QStringLiteral("yaos");
    }
    value.replace(' ', '-');
    return value.toLower();
}

QString headerAppVersion() {
    const QString version = QCoreApplication::applicationVersion().trimmed();
    return version.isEmpty() ? QStringLiteral("0.0.0") : version;
}

QString copilotEditorVersionHeader() {
    return QStringLiteral("%1/%2").arg(headerAppName(), headerAppVersion());
}

QString copilotEditorPluginVersionHeader() {
    return QStringLiteral("%1-desktop/%2").arg(headerAppName(), headerAppVersion());
}

QHash<QByteArray, QByteArray> gitHubCopilotTokenHeaders(const QString &githubToken) {
    QHash<QByteArray, QByteArray> headers;
    headers.insert("Accept", "application/json");
    headers.insert("Authorization", QByteArray("token ") + githubToken.toUtf8());
    headers.insert("Editor-Version", copilotEditorVersionHeader().toUtf8());
    headers.insert("Editor-Plugin-Version", copilotEditorPluginVersionHeader().toUtf8());
    headers.insert("X-GitHub-Api-Version", latin1(kGitHubCopilotTokenApiVersion).toUtf8());
    return headers;
}

bool looksLikeGitHubUserToken(const QString &token) {
    const QString trimmed = token.trimmed();
    return trimmed.startsWith(QStringLiteral("gho_"), Qt::CaseInsensitive);
}

bool looksLikeGitHubFineGrainedPat(const QString &token) {
    return token.trimmed().startsWith(QStringLiteral("github_pat_"), Qt::CaseInsensitive);
}

bool looksLikeGitHubClassicPat(const QString &token) {
    const QString trimmed = token.trimmed();
    return trimmed.startsWith(QStringLiteral("ghp_"), Qt::CaseInsensitive) ||
           trimmed.startsWith(QStringLiteral("ghu_"), Qt::CaseInsensitive) ||
           trimmed.startsWith(QStringLiteral("ghs_"), Qt::CaseInsensitive);
}

bool looksLikeGitHubDirectCopilotCredential(const QString &token) {
    const QString trimmed = token.trimmed();
    return looksLikeGitHubFineGrainedPat(trimmed) ||
           (!trimmed.isEmpty() &&
            !looksLikeGitHubUserToken(trimmed) &&
            !looksLikeGitHubClassicPat(trimmed));
}

QString gitHubClassicPatError() {
    return QStringLiteral(
        "GitHub Copilot does not accept classic GitHub PATs here. Use a fine-grained PAT with Copilot Requests, or use device login.");
}

QString gitHubCopilotCredentialMode(const config::ProviderConfig &cfg) {
    const QString apiKey = cfg.apiKey.trimmed();
    if (looksLikeGitHubFineGrainedPat(apiKey)) {
        return QStringLiteral("fine_grained_pat");
    }
    if (looksLikeGitHubClassicPat(apiKey)) {
        return QStringLiteral("classic_pat");
    }
    if (looksLikeGitHubUserToken(apiKey) || !cfg.oauthAccessToken.trimmed().isEmpty()) {
        return QStringLiteral("oauth_token");
    }
    if (!apiKey.isEmpty()) {
        return QStringLiteral("copilot_runtime_token");
    }
    return QString();
}

void clearGitHubDeviceFlowState(config::ProviderConfig *cfg) {
    if (!cfg) {
        return;
    }
    cfg->oauthDeviceCode.clear();
    cfg->oauthUserCode.clear();
    cfg->oauthVerificationUrl.clear();
}

bool clearStaleGitHubOAuthState(config::ProviderConfig *cfg) {
    if (!cfg) {
        return false;
    }
    const bool hadState = !cfg->oauthLastError.trimmed().isEmpty() ||
                          !cfg->oauthDeviceCode.trimmed().isEmpty() ||
                          !cfg->oauthUserCode.trimmed().isEmpty() ||
                          !cfg->oauthVerificationUrl.trimmed().isEmpty();
    cfg->oauthLastError.clear();
    clearGitHubDeviceFlowState(cfg);
    return hadState;
}

struct HttpResponse {
    int statusCode = 0;
    bool timeout = false;
    QString errorString;
    QByteArray body;

    bool ok() const {
        return !timeout && statusCode >= 200 && statusCode < 300;
    }
};

struct OAuthProviderMetadata {
    QString providerId;
    QString issuer;
    QString apiBase;
    QString clientId;
    QString scope;
    bool browserSupported = false;
    bool deviceSupported = false;
    bool refreshSupported = false;
};

QString trimmedBaseUrl(const QString &value) {
    QString base = value.trimmed();
    while (base.endsWith('/')) {
        base.chop(1);
    }
    return base;
}

QString isoNowUtc() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QByteArray base64UrlDecode(const QString &value) {
    QByteArray encoded = value.toUtf8();
    encoded.replace('-', '+');
    encoded.replace('_', '/');
    while (encoded.size() % 4 != 0) {
        encoded.append('=');
    }
    return QByteArray::fromBase64(encoded);
}

QString base64UrlEncode(const QByteArray &value) {
    QString encoded = QString::fromLatin1(value.toBase64(QByteArray::Base64UrlEncoding));
    encoded.remove('=');
    return encoded;
}

QString pkceCodeChallenge(const QString &verifier) {
    return base64UrlEncode(QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256));
}

QVariantMap headersToVariant(const QHash<QString, QString> &headers) {
    QVariantMap map;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        map.insert(it.key(), it.value());
    }
    return map;
}

QJsonObject jsonObjectFromResponse(const QByteArray &body) {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    return doc.isObject() ? doc.object() : QJsonObject();
}

int jsonInt(const QJsonObject &obj, const QString &key, int fallback) {
    const QJsonValue value = obj.value(key);
    if (value.isDouble()) {
        return value.toInt(fallback);
    }
    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().trimmed().toInt(&ok);
        if (ok) {
            return parsed;
        }
    }
    return fallback;
}

qint64 jsonLongLong(const QJsonObject &obj, const QString &key, qint64 fallback) {
    const QJsonValue value = obj.value(key);
    if (value.isDouble()) {
        return static_cast<qint64>(value.toDouble(static_cast<double>(fallback)));
    }
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().trimmed().toLongLong(&ok);
        if (ok) {
            return parsed;
        }
    }
    return fallback;
}

HttpResponse sendRequest(const QString &method,
                         const QUrl &url,
                         const QByteArray &body,
                         const QHash<QByteArray, QByteArray> &headers,
                         const QString &contentType = QString(),
                         int timeoutMs = 15000) {
    HttpResponse out;
    platform::network::HttpRequest request;
    request.method = method.trimmed().isEmpty() ? QStringLiteral("GET") : method.trimmed().toUpper();
    request.url = url.toString(QUrl::FullyEncoded);
    request.body = body;
    request.timeoutMs = timeoutMs;
    if (!contentType.trimmed().isEmpty()) {
        request.headers.insert("Content-Type", contentType.toUtf8());
    }
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        request.headers.insert(it.key(), it.value());
    }

    const platform::network::HttpResponse response = platform::network::FastNetHttpTransport::send(request);
    out.statusCode = response.statusCode;
    out.body = response.body;
    out.errorString = response.error;
    out.timeout = response.error.contains(QStringLiteral("timed out"), Qt::CaseInsensitive);
    return out;
}

HttpResponse postJson(const QUrl &url,
                      const QJsonObject &payload,
                      const QHash<QByteArray, QByteArray> &headers = QHash<QByteArray, QByteArray>(),
                      int timeoutMs = 15000) {
    return sendRequest(QStringLiteral("POST"),
                       url,
                       QJsonDocument(payload).toJson(QJsonDocument::Compact),
                       headers,
                       QStringLiteral("application/json"),
                       timeoutMs);
}

HttpResponse postForm(const QUrl &url,
                      const QHash<QString, QString> &payload,
                      const QHash<QByteArray, QByteArray> &headers = QHash<QByteArray, QByteArray>(),
                      int timeoutMs = 15000) {
    QUrlQuery query;
    for (auto it = payload.begin(); it != payload.end(); ++it) {
        query.addQueryItem(it.key(), it.value());
    }
    return sendRequest(QStringLiteral("POST"),
                       url,
                       query.toString(QUrl::FullyEncoded).toUtf8(),
                       headers,
                       QStringLiteral("application/x-www-form-urlencoded"),
                       timeoutMs);
}

QString responseErrorMessage(const HttpResponse &response) {
    if (response.timeout) {
        return QStringLiteral("request timed out");
    }

    const QJsonObject obj = jsonObjectFromResponse(response.body);
    const QString errorDescription = obj.value(QStringLiteral("error_description")).toString().trimmed();
    if (!errorDescription.isEmpty()) {
        return errorDescription;
    }

    const QJsonValue errorValue = obj.value(QStringLiteral("error"));
    if (errorValue.isString()) {
        const QString errorText = errorValue.toString().trimmed();
        if (!errorText.isEmpty()) {
            return errorText;
        }
    }
    if (errorValue.isObject()) {
        const QString errorMessage = errorValue.toObject().value(QStringLiteral("message")).toString().trimmed();
        if (!errorMessage.isEmpty()) {
            return errorMessage;
        }
    }

    const QString raw = QString::fromUtf8(response.body).trimmed();
    if (!raw.isEmpty()) {
        return raw;
    }
    if (!response.errorString.trimmed().isEmpty()) {
        return response.errorString.trimmed();
    }
    return QStringLiteral("HTTP %1").arg(response.statusCode);
}

QJsonObject jwtPayload(const QString &jwt) {
    const QStringList parts = jwt.split('.');
    if (parts.size() < 2) {
        return QJsonObject();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(base64UrlDecode(parts.at(1)));
    return doc.isObject() ? doc.object() : QJsonObject();
}

QDateTime jwtExpiry(const QString &jwt) {
    const QJsonObject payload = jwtPayload(jwt);
    const qint64 exp = payload.value(QStringLiteral("exp")).toVariant().toLongLong();
    if (exp <= 0) {
        return QDateTime();
    }
    return QDateTime::fromSecsSinceEpoch(exp, Qt::UTC);
}

QString openAIAccountIdFromJwt(const QString &jwt) {
    const QJsonObject payload = jwtPayload(jwt);
    const QJsonObject auth = payload.value(QStringLiteral("https://api.openai.com/auth")).toObject();
    return auth.value(QStringLiteral("chatgpt_account_id")).toString().trimmed();
}

QString isoFromJwtExpiry(const QString &jwt) {
    const QDateTime expiry = jwtExpiry(jwt);
    return expiry.isValid() ? expiry.toString(Qt::ISODate) : QString();
}

bool isExpiredOrNearExpiry(const QString &isoDateTime, int skewSeconds = 120) {
    if (isoDateTime.trimmed().isEmpty()) {
        return false;
    }
    const QDateTime parsed = QDateTime::fromString(isoDateTime, Qt::ISODate);
    if (!parsed.isValid()) {
        return false;
    }
    return parsed.toUTC() <= QDateTime::currentDateTimeUtc().addSecs(skewSeconds);
}

QString openAITokenEndpoint(const OAuthProviderMetadata &meta) {
    return trimmedBaseUrl(meta.issuer) + QStringLiteral("/oauth/token");
}

QString openAIDeviceUserCodeEndpoint(const OAuthProviderMetadata &meta) {
    return trimmedBaseUrl(meta.issuer) + QStringLiteral("/api/accounts/deviceauth/usercode");
}

QString openAIDeviceTokenEndpoint(const OAuthProviderMetadata &meta) {
    return trimmedBaseUrl(meta.issuer) + QStringLiteral("/api/accounts/deviceauth/token");
}

QString openAIDeviceRedirectUri(const OAuthProviderMetadata &meta) {
    return trimmedBaseUrl(meta.issuer) + QStringLiteral("/deviceauth/callback");
}

QString openAIVerificationUrl(const OAuthProviderMetadata &meta) {
    return trimmedBaseUrl(meta.issuer) + QStringLiteral("/codex/device");
}

QString gitHubDeviceEndpoint(const OAuthProviderMetadata &meta) {
    return trimmedBaseUrl(meta.issuer) + QStringLiteral("/login/device/code");
}

QString gitHubTokenEndpoint(const OAuthProviderMetadata &meta) {
    return trimmedBaseUrl(meta.issuer) + QStringLiteral("/login/oauth/access_token");
}

config::ProviderConfig withDefaults(const QString &providerId,
                                    const config::ProviderConfig &providerConfig) {
    config::ProviderConfig cfg = providerConfig;
    const QString normalized = normalizedProviderId(providerId);
    if (normalized == QStringLiteral("openai_codex")) {
        if (cfg.oauthIssuer.trimmed().isEmpty()) {
            cfg.oauthIssuer = latin1(kOpenAIIssuer);
        }
        const QString clientId = cfg.oauthClientId.trimmed();
        if (clientId.isEmpty() || clientId == latin1(kOpenAIMistakenClientId)) {
            cfg.oauthClientId = latin1(kOpenAIClientId);
        }
        const QString scope = cfg.oauthScope.simplified();
        if (scope.isEmpty() || scope == latin1(kOpenAIMistakenScope)) {
            cfg.oauthScope = latin1(kOpenAIScope);
        }
        if (cfg.apiBase.trimmed().isEmpty()) {
            cfg.apiBase = latin1(kOpenAIApiBase);
        }
    } else if (normalized == QStringLiteral("github_copilot")) {
        if (cfg.oauthIssuer.trimmed().isEmpty()) {
            cfg.oauthIssuer = latin1(kGitHubIssuer);
        }
        if (cfg.oauthScope.trimmed().isEmpty()) {
            cfg.oauthScope = latin1(kGitHubScope);
        }
        if (cfg.apiBase.trimmed().isEmpty()) {
            cfg.apiBase = latin1(kGitHubApiBase);
        }
    }
    return cfg;
}

OAuthProviderMetadata metadataFor(const QString &providerId,
                                  const config::ProviderConfig &providerConfig) {
    const QString normalized = normalizedProviderId(providerId);
    const config::ProviderConfig cfg = withDefaults(normalized, providerConfig);

    OAuthProviderMetadata meta;
    meta.providerId = normalized;
    meta.issuer = cfg.oauthIssuer.trimmed();
    meta.apiBase = cfg.apiBase.trimmed();
    meta.clientId = cfg.oauthClientId.trimmed();
    meta.scope = cfg.oauthScope.trimmed();
    if (normalized == QStringLiteral("openai_codex")) {
        meta.browserSupported = true;
        meta.deviceSupported = true;
        meta.refreshSupported = true;
    } else if (normalized == QStringLiteral("github_copilot")) {
        meta.deviceSupported = true;
        meta.refreshSupported = true;
    }
    return meta;
}

ProviderOAuthResult baseResult(const QString &providerId,
                               const config::ProviderConfig &providerConfig) {
    const QString normalized = normalizedProviderId(providerId);
    ProviderOAuthResult out;
    out.providerId = normalized;
    out.config = withDefaults(normalized, providerConfig);

    const OAuthProviderMetadata meta = metadataFor(normalized, out.config);
    out.issuer = meta.issuer;
    out.clientId = meta.clientId;
    out.scope = meta.scope;
    out.apiBase = out.config.apiBase.trimmed();
    out.apiKey = out.config.apiKey;
    if (normalized == QStringLiteral("github_copilot") &&
        (looksLikeGitHubUserToken(out.apiKey) || looksLikeGitHubClassicPat(out.apiKey))) {
        out.apiKey.clear();
    }
    out.headers = out.config.extraHeaders;
    out.browserSupported = meta.browserSupported;
    out.deviceSupported = meta.deviceSupported;
    out.refreshSupported = meta.refreshSupported;
    out.requiresClientId = normalized == QStringLiteral("github_copilot") && out.clientId.trimmed().isEmpty();
    if (normalized == QStringLiteral("github_copilot")) {
        out.loggedIn = looksLikeGitHubDirectCopilotCredential(out.config.apiKey);
    } else {
        out.loggedIn = isOAuthProvider(normalized)
            ? !out.config.apiKey.trimmed().isEmpty()
            : (!out.config.apiKey.trimmed().isEmpty() ||
               !out.config.oauthAccessToken.trimmed().isEmpty() ||
               !out.headers.isEmpty());
    }
    out.pending = !out.config.oauthUserCode.trimmed().isEmpty() &&
                  (!out.config.oauthDeviceCode.trimmed().isEmpty() ||
                   !out.config.oauthDeviceAuthId.trimmed().isEmpty());
    out.mode = out.pending ? QStringLiteral("device") : QString();
    out.credentialMode = normalized == QStringLiteral("github_copilot")
        ? gitHubCopilotCredentialMode(out.config)
        : QString();
    out.verificationUrl = out.config.oauthVerificationUrl;
    out.userCode = out.config.oauthUserCode;
    out.accountId = out.config.oauthAccountId;
    out.expiresAt = out.config.oauthExpiresAt;
    out.error = (out.loggedIn && !out.pending) ? QString() : out.config.oauthLastError;
    if (normalized == QStringLiteral("github_copilot") &&
        looksLikeGitHubClassicPat(out.config.apiKey) &&
        !out.pending) {
        out.ok = false;
        out.error = gitHubClassicPatError();
    } else if (normalized == QStringLiteral("github_copilot") &&
               looksLikeGitHubUserToken(out.config.apiKey) &&
               out.config.oauthAccessToken.trimmed().isEmpty() &&
               !out.pending) {
        out.ok = false;
        out.error = QStringLiteral(
            "GitHub OAuth access token still needs Copilot token exchange. Use refresh, or switch to a fine-grained PAT with Copilot Requests.");
    }
    if (!out.loggedIn && !out.pending && !out.error.trimmed().isEmpty()) {
        out.ok = false;
    }
    return out;
}

QString openAIApiKeyExchange(const OAuthProviderMetadata &meta,
                             const QString &clientId,
                             const QString &idToken,
                             QString *error) {
    QHash<QString, QString> payload;
    payload.insert(QStringLiteral("grant_type"),
                   QStringLiteral("urn:ietf:params:oauth:grant-type:token-exchange"));
    payload.insert(QStringLiteral("client_id"), clientId);
    payload.insert(QStringLiteral("requested_token"), QStringLiteral("openai-api-key"));
    payload.insert(QStringLiteral("subject_token"), idToken);
    payload.insert(QStringLiteral("subject_token_type"),
                   QStringLiteral("urn:ietf:params:oauth:token-type:id_token"));
    const HttpResponse response = postForm(QUrl(openAITokenEndpoint(meta)), payload);
    if (!response.ok()) {
        if (error) {
            *error = responseErrorMessage(response);
        }
        return QString();
    }

    const QJsonObject obj = jsonObjectFromResponse(response.body);
    const QString token = obj.value(QStringLiteral("access_token")).toString().trimmed();
    if (token.isEmpty() && error) {
        *error = QStringLiteral("OpenAI Codex token exchange returned an empty API key.");
    }
    return token;
}

ProviderOAuthResult finalizeOpenAIAuth(const OAuthProviderMetadata &meta,
                                       config::ProviderConfig cfg,
                                       const QJsonObject &tokenObject) {
    ProviderOAuthResult out = baseResult(meta.providerId, cfg);
    const QString idToken = tokenObject.value(QStringLiteral("id_token")).toString(cfg.oauthIdToken).trimmed();
    const QString accessToken = tokenObject.value(QStringLiteral("access_token")).toString(cfg.oauthAccessToken).trimmed();
    const QString refreshToken = tokenObject.value(QStringLiteral("refresh_token")).toString(cfg.oauthRefreshToken).trimmed();

    cfg.oauthIdToken = idToken;
    cfg.oauthAccessToken = accessToken;
    cfg.oauthRefreshToken = refreshToken;
    cfg.oauthTokenType = QStringLiteral("Bearer");
    cfg.oauthLastRefreshAt = isoNowUtc();
    cfg.oauthLastError.clear();
    cfg.oauthDeviceAuthId.clear();
    cfg.oauthDeviceCode.clear();
    cfg.oauthUserCode.clear();
    cfg.oauthVerificationUrl.clear();
    cfg.oauthIntervalSec = 5;

    const QString expiresAt = !idToken.isEmpty() ? isoFromJwtExpiry(idToken) : isoFromJwtExpiry(accessToken);
    if (!expiresAt.isEmpty()) {
        cfg.oauthExpiresAt = expiresAt;
    }

    const QString accountId = openAIAccountIdFromJwt(idToken);
    if (!accountId.isEmpty()) {
        cfg.oauthAccountId = accountId;
    }

    QString exchangeError;
    if (!idToken.isEmpty()) {
        cfg.apiKey = openAIApiKeyExchange(meta, meta.clientId, idToken, &exchangeError);
    }
    if (cfg.apiKey.trimmed().isEmpty()) {
        out.ok = false;
        cfg.oauthLastError = exchangeError.isEmpty()
            ? QStringLiteral("OpenAI Codex did not return a usable API credential.")
            : exchangeError;
        out.error = cfg.oauthLastError;
    } else {
        out.loggedIn = true;
        out.pending = false;
    }

    out.changed = true;
    out.apiKey = cfg.apiKey;
    out.apiBase = cfg.apiBase;
    out.headers = cfg.extraHeaders;
    out.accountId = cfg.oauthAccountId;
    out.expiresAt = cfg.oauthExpiresAt;
    out.error = cfg.oauthLastError;
    out.config = cfg;
    return out;
}

ProviderOAuthResult refreshGitHubCopilotAuth(const OAuthProviderMetadata &meta,
                                             const config::ProviderConfig &providerConfig) {
    ProviderOAuthResult out = baseResult(meta.providerId, providerConfig);
    config::ProviderConfig cfg = out.config;
    const QString apiKey = cfg.apiKey.trimmed();
    if (looksLikeGitHubFineGrainedPat(apiKey)) {
        out.changed = clearStaleGitHubOAuthState(&cfg);
        out.ok = true;
        out.loggedIn = true;
        out.pending = false;
        out.error.clear();
        out.apiKey = apiKey;
        out.credentialMode = QStringLiteral("fine_grained_pat");
        out.config = cfg;
        return out;
    }
    if (looksLikeGitHubClassicPat(apiKey)) {
        out.ok = false;
        out.loggedIn = false;
        out.pending = false;
        out.error = gitHubClassicPatError();
        out.apiKey.clear();
        out.credentialMode = QStringLiteral("classic_pat");
        return out;
    }
    if (cfg.oauthAccessToken.trimmed().isEmpty()) {
        out.ok = false;
        out.error = QStringLiteral("GitHub Copilot GitHub access token is missing.");
        out.loggedIn = looksLikeGitHubDirectCopilotCredential(apiKey);
        out.apiKey = out.loggedIn ? apiKey : QString();
        out.credentialMode = gitHubCopilotCredentialMode(cfg);
        return out;
    }

    const HttpResponse response = sendRequest(QStringLiteral("GET"),
                                              QUrl(QString::fromLatin1(kGitHubCopilotTokenUrl)),
                                              QByteArray(),
                                              gitHubCopilotTokenHeaders(cfg.oauthAccessToken));
    if (!response.ok()) {
        cfg.oauthLastError = responseErrorMessage(response);
        const bool hasRuntimeToken = looksLikeGitHubDirectCopilotCredential(cfg.apiKey);
        out.ok = false;
        out.error = cfg.oauthLastError;
        out.loggedIn = hasRuntimeToken;
        out.apiKey = hasRuntimeToken ? cfg.apiKey : QString();
        out.credentialMode = gitHubCopilotCredentialMode(cfg);
        out.expiresAt = cfg.oauthExpiresAt;
        out.config = cfg;
        return out;
    }

    const QJsonObject obj = jsonObjectFromResponse(response.body);
    const QString copilotToken = obj.value(QStringLiteral("token")).toString().trimmed();
    if (copilotToken.isEmpty()) {
        cfg.oauthLastError = QStringLiteral("GitHub Copilot token exchange returned an empty token.");
        const bool hasRuntimeToken = looksLikeGitHubDirectCopilotCredential(cfg.apiKey);
        out.ok = false;
        out.error = cfg.oauthLastError;
        out.loggedIn = hasRuntimeToken;
        out.apiKey = hasRuntimeToken ? cfg.apiKey : QString();
        out.credentialMode = gitHubCopilotCredentialMode(cfg);
        out.expiresAt = cfg.oauthExpiresAt;
        out.config = cfg;
        return out;
    }

    const int refreshInSec = jsonInt(obj, QStringLiteral("refresh_in"), 0);
    const qint64 expiresAtSec = jsonLongLong(obj, QStringLiteral("expires_at"), 0);
    QDateTime expiresAtUtc;
    if (refreshInSec > 0) {
        expiresAtUtc = QDateTime::currentDateTimeUtc().addSecs(refreshInSec + 60);
    } else if (expiresAtSec > 0) {
        expiresAtUtc = QDateTime::fromSecsSinceEpoch(expiresAtSec, Qt::UTC);
    }

    const QString previousApiKey = cfg.apiKey;
    const QString previousExpiry = cfg.oauthExpiresAt;
    const QString previousError = cfg.oauthLastError;
    const QString previousRefreshAt = cfg.oauthLastRefreshAt;
    cfg.apiKey = copilotToken;
    cfg.oauthExpiresAt = expiresAtUtc.isValid() ? expiresAtUtc.toString(Qt::ISODate) : QString();
    cfg.oauthLastRefreshAt = isoNowUtc();
    cfg.oauthLastError.clear();

    out.changed = previousApiKey != cfg.apiKey ||
                  previousExpiry != cfg.oauthExpiresAt ||
                  previousError != cfg.oauthLastError ||
                  previousRefreshAt != cfg.oauthLastRefreshAt;
    out.loggedIn = true;
    out.pending = false;
    out.apiKey = cfg.apiKey;
    out.expiresAt = cfg.oauthExpiresAt;
    out.error.clear();
    out.credentialMode = QStringLiteral("copilot_runtime_token");
    out.config = cfg;
    return out;
}

ProviderOAuthResult refreshOpenAIAuth(const OAuthProviderMetadata &meta,
                                      const config::ProviderConfig &providerConfig) {
    ProviderOAuthResult out = baseResult(meta.providerId, providerConfig);
    config::ProviderConfig cfg = out.config;
    if (cfg.oauthRefreshToken.trimmed().isEmpty()) {
        out.ok = false;
        out.error = QStringLiteral("OpenAI Codex refresh token is missing.");
        return out;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("client_id"), meta.clientId);
    payload.insert(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    payload.insert(QStringLiteral("refresh_token"), cfg.oauthRefreshToken);
    const HttpResponse response = postJson(QUrl(openAITokenEndpoint(meta)), payload);
    if (!response.ok()) {
        cfg.oauthLastError = responseErrorMessage(response);
        out.ok = false;
        out.error = cfg.oauthLastError;
        out.config = cfg;
        return out;
    }

    return finalizeOpenAIAuth(meta, cfg, jsonObjectFromResponse(response.body));
}

ProviderOAuthResult exchangeOpenAIAuthorizationCode(const OAuthProviderMetadata &meta,
                                                    const config::ProviderConfig &providerConfig,
                                                    const QString &redirectUri,
                                                    const QString &codeVerifier,
                                                    const QString &code) {
    ProviderOAuthResult out = baseResult(meta.providerId, providerConfig);
    config::ProviderConfig cfg = out.config;

    QHash<QString, QString> payload;
    payload.insert(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    payload.insert(QStringLiteral("code"), code);
    payload.insert(QStringLiteral("redirect_uri"), redirectUri);
    payload.insert(QStringLiteral("client_id"), meta.clientId);
    payload.insert(QStringLiteral("code_verifier"), codeVerifier);
    const HttpResponse response = postForm(QUrl(openAITokenEndpoint(meta)), payload);
    if (!response.ok()) {
        cfg.oauthLastError = responseErrorMessage(response);
        out.ok = false;
        out.error = cfg.oauthLastError;
        out.config = cfg;
        return out;
    }

    return finalizeOpenAIAuth(meta, cfg, jsonObjectFromResponse(response.body));
}

ProviderOAuthResult startOpenAIDeviceFlow(const OAuthProviderMetadata &meta,
                                          const config::ProviderConfig &providerConfig) {
    ProviderOAuthResult out = baseResult(meta.providerId, providerConfig);
    config::ProviderConfig cfg = out.config;

    QJsonObject payload;
    payload.insert(QStringLiteral("client_id"), meta.clientId);
    const HttpResponse response = postJson(QUrl(openAIDeviceUserCodeEndpoint(meta)), payload);
    if (!response.ok()) {
        cfg.oauthLastError = responseErrorMessage(response);
        out.ok = false;
        out.error = cfg.oauthLastError;
        out.config = cfg;
        return out;
    }

    const QJsonObject obj = jsonObjectFromResponse(response.body);
    cfg.oauthDeviceAuthId = obj.value(QStringLiteral("device_auth_id")).toString().trimmed();
    cfg.oauthUserCode = obj.value(QStringLiteral("user_code")).toString(obj.value(QStringLiteral("usercode")).toString()).trimmed();
    cfg.oauthVerificationUrl = openAIVerificationUrl(meta);
    cfg.oauthIntervalSec = qMax(3, jsonInt(obj, QStringLiteral("interval"), cfg.oauthIntervalSec));
    cfg.oauthLastError.clear();

    out.changed = true;
    out.pending = !cfg.oauthDeviceAuthId.isEmpty() && !cfg.oauthUserCode.isEmpty();
    out.mode = QStringLiteral("device");
    out.verificationUrl = cfg.oauthVerificationUrl;
    out.userCode = cfg.oauthUserCode;
    out.config = cfg;
    return out;
}

ProviderOAuthResult pollOpenAIDeviceFlow(const OAuthProviderMetadata &meta,
                                         const config::ProviderConfig &providerConfig) {
    ProviderOAuthResult out = baseResult(meta.providerId, providerConfig);
    config::ProviderConfig cfg = out.config;
    if (cfg.oauthDeviceAuthId.trimmed().isEmpty() || cfg.oauthUserCode.trimmed().isEmpty()) {
        out.ok = false;
        out.error = QStringLiteral("OpenAI Codex device login is not pending.");
        return out;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("device_auth_id"), cfg.oauthDeviceAuthId);
    payload.insert(QStringLiteral("user_code"), cfg.oauthUserCode);
    const HttpResponse response = postJson(QUrl(openAIDeviceTokenEndpoint(meta)), payload);
    if (response.statusCode == 403 || response.statusCode == 404) {
        out.pending = true;
        out.mode = QStringLiteral("device");
        out.verificationUrl = cfg.oauthVerificationUrl;
        out.userCode = cfg.oauthUserCode;
        return out;
    }
    if (!response.ok()) {
        cfg.oauthLastError = responseErrorMessage(response);
        out.ok = false;
        out.error = cfg.oauthLastError;
        out.config = cfg;
        return out;
    }

    const QJsonObject obj = jsonObjectFromResponse(response.body);
    const QString authorizationCode = obj.value(QStringLiteral("authorization_code")).toString().trimmed();
    const QString codeVerifier = obj.value(QStringLiteral("code_verifier")).toString().trimmed();
    if (authorizationCode.isEmpty() || codeVerifier.isEmpty()) {
        cfg.oauthLastError = QStringLiteral("OpenAI Codex device login returned an incomplete authorization response.");
        out.ok = false;
        out.error = cfg.oauthLastError;
        out.config = cfg;
        return out;
    }

    return exchangeOpenAIAuthorizationCode(meta,
                                           cfg,
                                           openAIDeviceRedirectUri(meta),
                                           codeVerifier,
                                           authorizationCode);
}

ProviderOAuthResult startGitHubDeviceFlow(const OAuthProviderMetadata &meta,
                                          const config::ProviderConfig &providerConfig) {
    ProviderOAuthResult out = baseResult(meta.providerId, providerConfig);
    config::ProviderConfig cfg = out.config;
    if (meta.clientId.trimmed().isEmpty()) {
        cfg.oauthLastError = QStringLiteral("GitHub Copilot device flow requires an OAuth client id.");
        out.ok = false;
        out.requiresClientId = true;
        out.error = cfg.oauthLastError;
        out.config = cfg;
        return out;
    }

    QHash<QString, QString> payload;
    payload.insert(QStringLiteral("client_id"), meta.clientId);
    payload.insert(QStringLiteral("scope"), meta.scope.trimmed().isEmpty()
                                             ? latin1(kGitHubScope)
                                             : meta.scope);

    QHash<QByteArray, QByteArray> headers;
    headers.insert("Accept", "application/json");
    const HttpResponse response = postForm(QUrl(gitHubDeviceEndpoint(meta)), payload, headers);
    if (!response.ok()) {
        cfg.oauthLastError = responseErrorMessage(response);
        out.ok = false;
        out.error = cfg.oauthLastError;
        out.config = cfg;
        return out;
    }

    const QJsonObject obj = jsonObjectFromResponse(response.body);
    cfg.oauthDeviceCode = obj.value(QStringLiteral("device_code")).toString().trimmed();
    cfg.oauthUserCode = obj.value(QStringLiteral("user_code")).toString().trimmed();
    cfg.oauthVerificationUrl = obj.value(QStringLiteral("verification_uri")).toString().trimmed();
    cfg.oauthIntervalSec = qMax(3, jsonInt(obj, QStringLiteral("interval"), cfg.oauthIntervalSec));
    cfg.oauthLastError.clear();

    out.changed = true;
    out.pending = !cfg.oauthDeviceCode.isEmpty() && !cfg.oauthUserCode.isEmpty();
    out.mode = QStringLiteral("device");
    out.verificationUrl = cfg.oauthVerificationUrl;
    out.userCode = cfg.oauthUserCode;
    out.config = cfg;
    return out;
}

ProviderOAuthResult pollGitHubDeviceFlow(const OAuthProviderMetadata &meta,
                                         const config::ProviderConfig &providerConfig) {
    ProviderOAuthResult out = baseResult(meta.providerId, providerConfig);
    config::ProviderConfig cfg = out.config;
    if (meta.clientId.trimmed().isEmpty()) {
        out.ok = false;
        out.requiresClientId = true;
        out.error = QStringLiteral("GitHub Copilot device flow requires an OAuth client id.");
        return out;
    }
    if (cfg.oauthDeviceCode.trimmed().isEmpty()) {
        out.ok = false;
        out.error = QStringLiteral("GitHub Copilot device login is not pending.");
        return out;
    }

    QHash<QString, QString> payload;
    payload.insert(QStringLiteral("client_id"), meta.clientId);
    payload.insert(QStringLiteral("device_code"), cfg.oauthDeviceCode);
    payload.insert(QStringLiteral("grant_type"),
                   QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));

    QHash<QByteArray, QByteArray> headers;
    headers.insert("Accept", "application/json");
    const HttpResponse response = postForm(QUrl(gitHubTokenEndpoint(meta)), payload, headers);
    if (!response.ok()) {
        cfg.oauthLastError = responseErrorMessage(response);
        out.ok = false;
        out.error = cfg.oauthLastError;
        out.config = cfg;
        return out;
    }

    const QJsonObject obj = jsonObjectFromResponse(response.body);
    const QString pendingError = obj.value(QStringLiteral("error")).toString().trimmed();
    if (pendingError == QStringLiteral("authorization_pending") ||
        pendingError == QStringLiteral("slow_down")) {
        if (pendingError == QStringLiteral("slow_down")) {
            cfg.oauthIntervalSec = qMax(cfg.oauthIntervalSec + 5, 10);
        }
        out.changed = pendingError == QStringLiteral("slow_down");
        out.pending = true;
        out.mode = QStringLiteral("device");
        out.verificationUrl = cfg.oauthVerificationUrl;
        out.userCode = cfg.oauthUserCode;
        out.config = cfg;
        return out;
    }
    if (!pendingError.isEmpty()) {
        cfg.oauthLastError = obj.value(QStringLiteral("error_description")).toString(pendingError).trimmed();
        cfg.oauthDeviceCode.clear();
        cfg.oauthUserCode.clear();
        cfg.oauthVerificationUrl.clear();
        out.changed = true;
        out.ok = false;
        out.error = cfg.oauthLastError;
        out.config = cfg;
        return out;
    }

    cfg.oauthAccessToken = obj.value(QStringLiteral("access_token")).toString().trimmed();
    cfg.oauthTokenType = obj.value(QStringLiteral("token_type")).toString(QStringLiteral("bearer")).trimmed();
    if (!obj.value(QStringLiteral("scope")).toString().trimmed().isEmpty()) {
        cfg.oauthScope = obj.value(QStringLiteral("scope")).toString().trimmed();
    }
    cfg.apiKey.clear();
    cfg.oauthLastRefreshAt = isoNowUtc();
    cfg.oauthExpiresAt.clear();
    cfg.oauthLastError.clear();
    cfg.oauthDeviceCode.clear();
    cfg.oauthUserCode.clear();
    cfg.oauthVerificationUrl.clear();
    ProviderOAuthResult exchanged = refreshGitHubCopilotAuth(meta, cfg);
    exchanged.changed = true;
    return exchanged;
}

ProviderOAuthResult startOpenAIBrowserFlow(const OAuthProviderMetadata &meta,
                                           const config::ProviderConfig &providerConfig,
                                           const QString &redirectUri,
                                           const QString &state,
                                           const QString &codeVerifier) {
    ProviderOAuthResult out = baseResult(meta.providerId, providerConfig);
    out.changed = out.config.oauthIssuer != providerConfig.oauthIssuer ||
                  out.config.oauthClientId != providerConfig.oauthClientId ||
                  out.config.oauthScope != providerConfig.oauthScope ||
                  out.config.apiBase != providerConfig.apiBase;
    out.mode = QStringLiteral("browser");
    out.pending = true;

    QUrl url(trimmedBaseUrl(meta.issuer) + QStringLiteral("/oauth/authorize"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), meta.clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    query.addQueryItem(QStringLiteral("scope"), meta.scope);
    query.addQueryItem(QStringLiteral("code_challenge"), pkceCodeChallenge(codeVerifier));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("id_token_add_organizations"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("codex_cli_simplified_flow"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("state"), state);
    query.addQueryItem(QStringLiteral("originator"), latin1(kOpenAIOriginator));
    url.setQuery(query);
    out.authUrl = url.toString(QUrl::FullyEncoded);
    return out;
}

ProviderOAuthResult completeOpenAIBrowserFlow(const OAuthProviderMetadata &meta,
                                              const config::ProviderConfig &providerConfig,
                                              const QString &redirectUri,
                                              const QString &expectedState,
                                              const QString &codeVerifier,
                                              const QString &callbackUrl) {
    ProviderOAuthResult out = baseResult(meta.providerId, providerConfig);
    const QUrl url(callbackUrl);
    const QUrlQuery query(url);
    const QString state = query.queryItemValue(QStringLiteral("state"));
    if (state != expectedState) {
        out.ok = false;
        out.error = QStringLiteral("OAuth state mismatch.");
        return out;
    }

    const QString errorCode = query.queryItemValue(QStringLiteral("error"));
    if (!errorCode.trimmed().isEmpty()) {
        const QString errorDescription = query.queryItemValue(QStringLiteral("error_description"));
        out.ok = false;
        out.error = errorDescription.trimmed().isEmpty() ? errorCode : errorDescription.trimmed();
        return out;
    }

    const QString code = query.queryItemValue(QStringLiteral("code"));
    if (code.trimmed().isEmpty()) {
        out.ok = false;
        out.error = QStringLiteral("OAuth callback did not include an authorization code.");
        return out;
    }

    return exchangeOpenAIAuthorizationCode(meta, providerConfig, redirectUri, codeVerifier, code);
}

} // namespace

QVariantMap ProviderOAuthResult::toVariantMap() const {
    return QVariantMap{
        {QStringLiteral("ok"), ok},
        {QStringLiteral("changed"), changed},
        {QStringLiteral("loggedIn"), loggedIn},
        {QStringLiteral("pending"), pending},
        {QStringLiteral("browserSupported"), browserSupported},
        {QStringLiteral("deviceSupported"), deviceSupported},
        {QStringLiteral("refreshSupported"), refreshSupported},
        {QStringLiteral("requiresClientId"), requiresClientId},
        {QStringLiteral("providerId"), providerId},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("credentialMode"), credentialMode},
        {QStringLiteral("issuer"), issuer},
        {QStringLiteral("clientId"), clientId},
        {QStringLiteral("scope"), scope},
        {QStringLiteral("authUrl"), authUrl},
        {QStringLiteral("verificationUrl"), verificationUrl},
        {QStringLiteral("userCode"), userCode},
        {QStringLiteral("error"), error},
        {QStringLiteral("accountId"), accountId},
        {QStringLiteral("expiresAt"), expiresAt},
        {QStringLiteral("expiresSoon"), isExpiredOrNearExpiry(expiresAt)},
        {QStringLiteral("apiBase"), apiBase},
        {QStringLiteral("model"), config.model.trimmed()},
        {QStringLiteral("modelCount"), config.availableModels.size()},
        {QStringLiteral("hasApiKey"), !apiKey.trimmed().isEmpty()},
        {QStringLiteral("hasOAuthAccessToken"), !config.oauthAccessToken.trimmed().isEmpty()},
        {QStringLiteral("hasRefreshToken"), !config.oauthRefreshToken.trimmed().isEmpty()},
        {QStringLiteral("hasIdToken"), !config.oauthIdToken.trimmed().isEmpty()},
        {QStringLiteral("lastRefreshAt"), config.oauthLastRefreshAt},
        {QStringLiteral("headers"), headersToVariant(headers)},
        {QStringLiteral("intervalSec"), config.oauthIntervalSec},
        {QStringLiteral("lastError"), config.oauthLastError}
    };
}

QString normalizedProviderId(const QString &providerId) {
    QString normalized = providerId.trimmed().toLower();
    normalized.replace('-', '_');
    if (normalized == QStringLiteral("azureopenai")) normalized = QStringLiteral("azure_openai");
    if (normalized == QStringLiteral("code_buddy")) normalized = QStringLiteral("codebuddy");
    if (normalized == QStringLiteral("openaicodex")) normalized = QStringLiteral("openai_codex");
    if (normalized == QStringLiteral("githubcopilot")) normalized = QStringLiteral("github_copilot");
    return normalized;
}

bool isOAuthProvider(const QString &providerId) {
    const QString normalized = normalizedProviderId(providerId);
    return normalized == QStringLiteral("openai_codex") ||
           normalized == QStringLiteral("github_copilot");
}

QString defaultApiBaseForProvider(const QString &providerId) {
    const QString normalized = normalizedProviderId(providerId);
    if (normalized == QStringLiteral("openai_codex")) {
        return latin1(kOpenAIApiBase);
    }
    if (normalized == QStringLiteral("github_copilot")) {
        return latin1(kGitHubApiBase);
    }
    return QString();
}

ProviderOAuthResult providerOAuthStatus(const QString &providerId,
                                        const config::ProviderConfig &providerConfig) {
    return baseResult(providerId, providerConfig);
}

ProviderOAuthResult resolveProviderAccess(const QString &providerId,
                                          const config::ProviderConfig &providerConfig,
                                          bool allowRefresh) {
    const QString normalized = normalizedProviderId(providerId);
    ProviderOAuthResult out = baseResult(normalized, providerConfig);
    if (!isOAuthProvider(normalized)) {
        out.loggedIn = !out.apiKey.trimmed().isEmpty() || !out.headers.isEmpty();
        return out;
    }

    const OAuthProviderMetadata meta = metadataFor(normalized, out.config);
    if (normalized == QStringLiteral("openai_codex")) {
        if (allowRefresh && !out.config.oauthRefreshToken.trimmed().isEmpty() &&
            (out.config.apiKey.trimmed().isEmpty() ||
             out.config.oauthIdToken.trimmed().isEmpty() ||
             isExpiredOrNearExpiry(out.config.oauthExpiresAt))) {
            ProviderOAuthResult refreshed = refreshOpenAIAuth(meta, out.config);
            if (refreshed.ok) {
                out = refreshed;
            } else if (out.config.apiKey.trimmed().isEmpty()) {
                return refreshed;
            }
        }

        if (out.config.apiKey.trimmed().isEmpty() && !out.config.oauthIdToken.trimmed().isEmpty()) {
            ProviderOAuthResult exchanged = finalizeOpenAIAuth(meta, out.config, QJsonObject());
            if (exchanged.ok) {
                out = exchanged;
            } else if (out.config.apiKey.trimmed().isEmpty()) {
                return exchanged;
            }
        }

        out.apiBase = out.config.apiBase.trimmed().isEmpty()
            ? latin1(kOpenAIApiBase)
            : out.config.apiBase.trimmed();
        out.apiKey = out.config.apiKey;
        out.headers = out.config.extraHeaders;
        out.loggedIn = !out.apiKey.trimmed().isEmpty();
        if (!out.loggedIn && out.headers.isEmpty()) {
            out.ok = false;
            out.error = out.config.oauthLastError.trimmed().isEmpty()
                ? QStringLiteral("OpenAI Codex is not authorized yet.")
                : out.config.oauthLastError.trimmed();
        } else {
            out.error.clear();
        }
        return out;
    }

    out.apiBase = out.config.apiBase.trimmed().isEmpty()
        ? latin1(kGitHubApiBase)
        : out.config.apiBase.trimmed();
    const QString currentApiKey = out.config.apiKey.trimmed();
    const bool hasFineGrainedPat = looksLikeGitHubFineGrainedPat(currentApiKey);
    const bool hasInlineOAuthToken = looksLikeGitHubUserToken(currentApiKey);
    const bool hasClassicPat = looksLikeGitHubClassicPat(currentApiKey);
    config::ProviderConfig workingConfig = out.config;
    bool importedInlineOAuthToken = false;
    if (hasInlineOAuthToken && workingConfig.oauthAccessToken.trimmed().isEmpty()) {
        workingConfig.oauthAccessToken = currentApiKey;
        workingConfig.apiKey.clear();
        importedInlineOAuthToken = true;
    }
    if (hasFineGrainedPat) {
        out.apiKey = currentApiKey;
        out.headers = workingConfig.extraHeaders;
        out.loggedIn = true;
        out.pending = false;
        out.ok = true;
        out.error.clear();
        out.credentialMode = QStringLiteral("fine_grained_pat");
        out.config = workingConfig;
        out.changed = clearStaleGitHubOAuthState(&out.config);
        return out;
    }
    if (hasClassicPat) {
        out.apiKey.clear();
        out.headers = workingConfig.extraHeaders;
        out.loggedIn = false;
        out.pending = false;
        out.ok = false;
        out.error = gitHubClassicPatError();
        out.credentialMode = QStringLiteral("classic_pat");
        out.config = workingConfig;
        return out;
    }
    if (allowRefresh &&
        !workingConfig.oauthAccessToken.trimmed().isEmpty() &&
        (workingConfig.apiKey.trimmed().isEmpty() ||
         looksLikeGitHubUserToken(workingConfig.apiKey) ||
         isExpiredOrNearExpiry(workingConfig.oauthExpiresAt))) {
        ProviderOAuthResult refreshed = refreshGitHubCopilotAuth(meta, workingConfig);
        if (importedInlineOAuthToken) {
            refreshed.changed = true;
        }
        if (refreshed.ok) {
            out = refreshed;
        } else if (currentApiKey.isEmpty() || hasInlineOAuthToken) {
            return refreshed;
        }
    }

    out.apiKey = looksLikeGitHubDirectCopilotCredential(out.config.apiKey) ? out.config.apiKey : QString();
    out.headers = out.config.extraHeaders;
    out.loggedIn = !out.apiKey.trimmed().isEmpty();
    out.credentialMode = gitHubCopilotCredentialMode(out.config);
    if (!out.loggedIn && out.headers.isEmpty()) {
        out.ok = false;
        if (hasInlineOAuthToken && out.config.oauthAccessToken.trimmed().isEmpty()) {
            out.error = QStringLiteral(
                "GitHub OAuth access token still needs Copilot token exchange. Use refresh, or switch to a fine-grained PAT with Copilot Requests.");
        } else {
            out.error = out.config.oauthLastError.trimmed().isEmpty()
                ? QStringLiteral("GitHub Copilot is not authorized yet.")
                : out.config.oauthLastError.trimmed();
        }
    } else {
        out.error.clear();
    }
    return out;
}

ProviderOAuthResult startDeviceFlow(const QString &providerId,
                                    const config::ProviderConfig &providerConfig) {
    const QString normalized = normalizedProviderId(providerId);
    const OAuthProviderMetadata meta = metadataFor(normalized, providerConfig);
    if (normalized == QStringLiteral("openai_codex")) {
        return startOpenAIDeviceFlow(meta, providerConfig);
    }
    if (normalized == QStringLiteral("github_copilot")) {
        return startGitHubDeviceFlow(meta, providerConfig);
    }

    ProviderOAuthResult out = baseResult(normalized, providerConfig);
    out.ok = false;
    out.error = QStringLiteral("This provider does not support OAuth device flow.");
    return out;
}

ProviderOAuthResult pollDeviceFlow(const QString &providerId,
                                   const config::ProviderConfig &providerConfig) {
    const QString normalized = normalizedProviderId(providerId);
    const OAuthProviderMetadata meta = metadataFor(normalized, providerConfig);
    if (normalized == QStringLiteral("openai_codex")) {
        return pollOpenAIDeviceFlow(meta, providerConfig);
    }
    if (normalized == QStringLiteral("github_copilot")) {
        return pollGitHubDeviceFlow(meta, providerConfig);
    }

    ProviderOAuthResult out = baseResult(normalized, providerConfig);
    out.ok = false;
    out.error = QStringLiteral("This provider does not support OAuth device flow.");
    return out;
}

ProviderOAuthResult startBrowserFlow(const QString &providerId,
                                     const config::ProviderConfig &providerConfig,
                                     const QString &redirectUri,
                                     const QString &state,
                                     const QString &codeVerifier) {
    const QString normalized = normalizedProviderId(providerId);
    const OAuthProviderMetadata meta = metadataFor(normalized, providerConfig);
    if (normalized == QStringLiteral("openai_codex")) {
        return startOpenAIBrowserFlow(meta, providerConfig, redirectUri, state, codeVerifier);
    }

    ProviderOAuthResult out = baseResult(normalized, providerConfig);
    out.ok = false;
    out.error = QStringLiteral("This provider does not support browser OAuth in YAOS yet.");
    return out;
}

ProviderOAuthResult completeBrowserFlow(const QString &providerId,
                                        const config::ProviderConfig &providerConfig,
                                        const QString &redirectUri,
                                        const QString &expectedState,
                                        const QString &codeVerifier,
                                        const QString &callbackUrl) {
    const QString normalized = normalizedProviderId(providerId);
    const OAuthProviderMetadata meta = metadataFor(normalized, providerConfig);
    if (normalized == QStringLiteral("openai_codex")) {
        return completeOpenAIBrowserFlow(meta,
                                         providerConfig,
                                         redirectUri,
                                         expectedState,
                                         codeVerifier,
                                         callbackUrl);
    }

    ProviderOAuthResult out = baseResult(normalized, providerConfig);
    out.ok = false;
    out.error = QStringLiteral("This provider does not support browser OAuth in YAOS yet.");
    return out;
}

ProviderOAuthResult refreshProviderTokens(const QString &providerId,
                                          const config::ProviderConfig &providerConfig) {
    const QString normalized = normalizedProviderId(providerId);
    const OAuthProviderMetadata meta = metadataFor(normalized, providerConfig);
    if (normalized == QStringLiteral("openai_codex")) {
        return refreshOpenAIAuth(meta, providerConfig);
    }
    if (normalized == QStringLiteral("github_copilot")) {
        if (looksLikeGitHubFineGrainedPat(providerConfig.apiKey)) {
            return refreshGitHubCopilotAuth(meta, providerConfig);
        }
        if (looksLikeGitHubClassicPat(providerConfig.apiKey)) {
            ProviderOAuthResult out = baseResult(normalized, providerConfig);
            out.ok = false;
            out.loggedIn = false;
            out.error = gitHubClassicPatError();
            out.apiKey.clear();
            out.credentialMode = QStringLiteral("classic_pat");
            return out;
        }
        if (looksLikeGitHubUserToken(providerConfig.apiKey) &&
            providerConfig.oauthAccessToken.trimmed().isEmpty()) {
            config::ProviderConfig cfg = providerConfig;
            cfg.oauthAccessToken = cfg.apiKey.trimmed();
            cfg.apiKey.clear();
            ProviderOAuthResult refreshed = refreshGitHubCopilotAuth(meta, cfg);
            refreshed.changed = true;
            return refreshed;
        }
        return refreshGitHubCopilotAuth(meta, providerConfig);
    }

    ProviderOAuthResult out = baseResult(normalized, providerConfig);
    out.ok = false;
    out.error = QStringLiteral("This provider does not support token refresh.");
    return out;
}

config::ProviderConfig clearOAuthState(const QString &providerId,
                                       const config::ProviderConfig &providerConfig) {
    config::ProviderConfig cfg = withDefaults(providerId, providerConfig);
    if (!isOAuthProvider(providerId)) {
        return cfg;
    }

    cfg.apiKey.clear();
    cfg.oauthAccessToken.clear();
    cfg.oauthRefreshToken.clear();
    cfg.oauthIdToken.clear();
    cfg.oauthTokenType.clear();
    cfg.oauthAccountId.clear();
    cfg.oauthExpiresAt.clear();
    cfg.oauthLastRefreshAt.clear();
    cfg.oauthDeviceCode.clear();
    cfg.oauthDeviceAuthId.clear();
    cfg.oauthUserCode.clear();
    cfg.oauthVerificationUrl.clear();
    cfg.oauthLastError.clear();
    cfg.oauthIntervalSec = 5;
    return cfg;
}

} // namespace yaos::providers
