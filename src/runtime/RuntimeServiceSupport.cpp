#include "RuntimeServiceSupport.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QSet>
#include <QThread>
#include <QUrl>

#include "RemoteRuntimeClient.h"

namespace yaos::runtime {

namespace {

constexpr int kDefaultRuntimeProbeTimeoutMs = 1500;
constexpr qint64 kEndpointHealthCacheTtlMs = 5000;

struct EndpointHealthCacheEntry {
    RuntimeServiceEndpointHealth health;
    qint64 checkedAtMs = 0;
};

QString configuredEndpoint(const config::Config &config) {
    return config.runtime.endpoint.trimmed();
}

QString normalizedEndpoint(QString endpoint) {
    endpoint = endpoint.trimmed();
    while (endpoint.endsWith('/')) {
        endpoint.chop(1);
    }
    if (endpoint.isEmpty()) {
        return endpoint;
    }
    if (!endpoint.contains(QStringLiteral("://"))) {
        endpoint.prepend(QStringLiteral("http://"));
    }

    QUrl url(endpoint);
    const QString host = url.host().trimmed();
    if (host == QStringLiteral("0.0.0.0")) {
        url.setHost(QStringLiteral("127.0.0.1"));
        endpoint = url.toString(QUrl::FullyEncoded);
    } else if (host == QStringLiteral("::")) {
        url.setHost(QStringLiteral("::1"));
        endpoint = url.toString(QUrl::FullyEncoded);
    }
    return endpoint;
}

bool isLocalEndpoint(const QString &endpoint) {
    QString candidate = endpoint.trimmed();
    if (candidate.isEmpty()) {
        return false;
    }
    if (!candidate.contains(QStringLiteral("://"))) {
        candidate.prepend(QStringLiteral("http://"));
    }

    const QUrl url(candidate);
    const QString host = url.host().trimmed().toLower();
    return host == QStringLiteral("127.0.0.1") ||
           host == QStringLiteral("localhost") ||
           host == QStringLiteral("0.0.0.0") ||
           host == QStringLiteral("::1") ||
           host == QStringLiteral("::");
}

bool isProbeSupportedEndpoint(const QString &endpoint) {
    QString candidate = endpoint.trimmed();
    if (candidate.isEmpty()) {
        return false;
    }
    if (candidate.startsWith(QStringLiteral("local://"), Qt::CaseInsensitive) ||
        candidate.startsWith(QStringLiteral("remote://"), Qt::CaseInsensitive)) {
        return false;
    }
    if (!candidate.contains(QStringLiteral("://"))) {
        candidate.prepend(QStringLiteral("http://"));
    }

    const QUrl url(candidate);
    const QString scheme = url.scheme().trimmed().toLower();
    return (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) &&
           !url.host().trimmed().isEmpty();
}

QHash<QString, EndpointHealthCacheEntry> &endpointHealthCache() {
    static QHash<QString, EndpointHealthCacheEntry> cache;
    return cache;
}

QMutex &endpointHealthCacheMutex() {
    static QMutex mutex;
    return mutex;
}

QStringList serviceProgramCandidates() {
    const QString currentProgram = QCoreApplication::applicationFilePath();
    QFileInfo currentInfo(currentProgram);
    QStringList programs;

    if (currentInfo.exists()) {
        const QString baseName = currentInfo.completeBaseName().toLower();
        if (baseName == QStringLiteral("yaos")) {
            programs << currentProgram;
        }

        const QString siblingRuntime = currentInfo.dir().filePath(
#ifdef Q_OS_WIN
            QStringLiteral("yaos-runtime.exe")
#else
            QStringLiteral("yaos-runtime")
#endif
        );
        if (QFileInfo::exists(siblingRuntime) && siblingRuntime != currentProgram) {
            programs << siblingRuntime;
        }

        const QString siblingYaos = currentInfo.dir().filePath(
#ifdef Q_OS_WIN
            QStringLiteral("yaos.exe")
#else
            QStringLiteral("yaos")
#endif
        );
        if (QFileInfo::exists(siblingYaos) && siblingYaos != currentProgram) {
            programs << siblingYaos;
        }

        if (baseName == QStringLiteral("yaos-runtime")) {
            programs << currentProgram;
        }
    }

    programs.removeDuplicates();
    return programs;
}

QStringList spawnArgsForProgram(const QString &program, const QString &endpoint) {
    QStringList args;
    const QString baseName = QFileInfo(program).completeBaseName().toLower();
    if (baseName != QStringLiteral("yaos-runtime")) {
        args << QStringLiteral("runtime-service");
    }
    args << QStringLiteral("--endpoint") << endpoint;
    return args;
}

bool spawnLocalRuntimeService(const config::Config &config, QString *error) {
    const QStringList programs = serviceProgramCandidates();
    if (programs.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Unable to determine yaos-runtime executable path.");
        }
        return false;
    }

    const QString endpoint = configuredEndpoint(config);
    for (const QString &program : programs) {
        if (QProcess::startDetached(program, spawnArgsForProgram(program, endpoint))) {
            return true;
        }
    }

    if (error) {
        *error = QStringLiteral("Failed to start local yaos-runtime process.");
    }
    return false;
}

} // namespace

bool prefersRemoteRuntimeService(const config::Config &config) {
    return config.usesRemoteRuntime();
}

bool remoteRuntimeServiceEnabled(const config::Config &config) {
    return config.usesRemoteRuntime();
}

QString normalizedRuntimeServiceEndpoint(QString endpoint) {
    return normalizedEndpoint(endpoint);
}

QString runtimeAdvertiseEndpoint(const config::Config &config) {
    const QString configured = config.runtime.advertiseEndpoint.trimmed();
    if (!configured.isEmpty()) {
        return normalizedEndpoint(configured);
    }
    return normalizedEndpoint(configuredEndpoint(config));
}

RuntimeServiceEndpointHealth runtimeServiceEndpointHealth(QString endpoint,
                                                         int timeoutMs,
                                                         bool allowNetwork) {
    RuntimeServiceEndpointHealth health;
    health.endpoint = normalizedEndpoint(endpoint);
    health.probeSupported = isProbeSupportedEndpoint(health.endpoint);
    if (!health.probeSupported) {
        return health;
    }

    const QString cacheKey = health.endpoint.trimmed().toLower();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    {
        QMutexLocker locker(&endpointHealthCacheMutex());
        const auto it = endpointHealthCache().constFind(cacheKey);
        if (it != endpointHealthCache().constEnd()) {
            if (now - it->checkedAtMs <= kEndpointHealthCacheTtlMs || !allowNetwork) {
                return it->health;
            }
        }
    }

    if (!allowNetwork) {
        return health;
    }

    config::Config cfg;
    cfg.runtime.mode = QStringLiteral("remote");
    cfg.runtime.endpoint = health.endpoint;

    RemoteRuntimeClient client(cfg, timeoutMs > 0 ? timeoutMs : kDefaultRuntimeProbeTimeoutMs);
    health.checked = true;
    health.reachable = client.ensureReady(&health.error);
    if (health.reachable) {
        health.error.clear();
    }

    EndpointHealthCacheEntry cacheEntry;
    cacheEntry.health = health;
    cacheEntry.checkedAtMs = now;
    {
        QMutexLocker locker(&endpointHealthCacheMutex());
        endpointHealthCache().insert(cacheKey, cacheEntry);
    }
    return health;
}

config::Config runtimeServiceConfig(const config::Config &baseConfig,
                                    const QString &listenEndpoint,
                                    const QString &advertiseEndpoint) {
    config::Config serviceConfig = baseConfig;
    serviceConfig.runtime.mode = QStringLiteral("remote");
    serviceConfig.runtime.endpoint = normalizedEndpoint(listenEndpoint);
    serviceConfig.runtime.advertiseEndpoint = advertiseEndpoint.trimmed().isEmpty()
        ? runtimeAdvertiseEndpoint(serviceConfig)
        : normalizedEndpoint(advertiseEndpoint);
    return serviceConfig;
}

RuntimeServiceAvailability ensureRuntimeService(const config::Config &config,
                                                bool allowAutoSpawn,
                                                int probeTimeoutMs) {
    static QSet<QString> spawnAttemptedEndpoints;

    RuntimeServiceAvailability availability;
    availability.endpoint = normalizedEndpoint(configuredEndpoint(config));
    availability.preferred = prefersRemoteRuntimeService(config);
    availability.enabled = remoteRuntimeServiceEnabled(config);
    availability.localEndpoint = isLocalEndpoint(availability.endpoint);

    if (!availability.preferred) {
        return availability;
    }
    if (!availability.enabled) {
        availability.error = QStringLiteral("Runtime service is not explicitly enabled.");
        return availability;
    }
    if (availability.endpoint.isEmpty()) {
        availability.error = QStringLiteral("Runtime endpoint is empty.");
        return availability;
    }

    const int timeoutMs = probeTimeoutMs > 0 ? probeTimeoutMs : kDefaultRuntimeProbeTimeoutMs;
    RemoteRuntimeClient client(config, timeoutMs);
    QString pingError;
    if (client.ensureReady(&pingError)) {
        availability.reachable = true;
        return availability;
    }

    availability.error = pingError.isEmpty()
        ? QStringLiteral("Runtime service is unreachable.")
        : pingError;

    const QString endpointKey = availability.endpoint.trimmed().toLower();
    if (!allowAutoSpawn ||
        !config.runtime.autoSpawnLocalService ||
        !availability.localEndpoint ||
        spawnAttemptedEndpoints.contains(endpointKey)) {
        return availability;
    }

    spawnAttemptedEndpoints.insert(endpointKey);
    QString spawnError;
    if (!spawnLocalRuntimeService(config, &spawnError)) {
        availability.error = spawnError.isEmpty() ? availability.error : spawnError;
        return availability;
    }

    for (int attempt = 0; attempt < 20; ++attempt) {
        QString retryError;
        if (client.ensureReady(&retryError)) {
            availability.reachable = true;
            availability.usedAutoSpawn = true;
            availability.error.clear();
            return availability;
        }
        QThread::msleep(150);
        availability.error = retryError;
    }

    if (availability.error.isEmpty()) {
        availability.error = QStringLiteral("Local yaos-runtime started but did not respond in time.");
    }
    return availability;
}

} // namespace yaos::runtime
