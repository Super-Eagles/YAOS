#include "MemoryServiceSupport.h"

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

#include "RemoteMemoryClient.h"

namespace yaos::memory {

namespace {

constexpr int kDefaultMemoryProbeTimeoutMs = 2000;
constexpr qint64 kEndpointHealthCacheTtlMs = 5000;

struct EndpointHealthCacheEntry {
    bool reachable = false;
    QString error;
    qint64 checkedAtMs = 0;
};

QString normalizedDriver(QString driver) {
    driver = driver.trimmed().toLower();
    driver.replace('-', '_');
    return driver;
}

bool isRemoteDriver(const QString &driver) {
    const QString normalized = normalizedDriver(driver);
    return normalized == "remote" || normalized == "http" || normalized == "service";
}

QString resolvedEndpoint(const config::Config &config) {
    return config.memory.service.endpoint.trimmed();
}

bool isLocalEndpoint(const QString &endpoint) {
    QString candidate = endpoint.trimmed();
    if (candidate.isEmpty()) {
        return false;
    }
    if (!candidate.contains("://")) {
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

QHash<QString, EndpointHealthCacheEntry> &endpointHealthCache() {
    static QHash<QString, EndpointHealthCacheEntry> cache;
    return cache;
}

QMutex &endpointHealthCacheMutex() {
    static QMutex mutex;
    return mutex;
}

QString endpointHealthCacheKey(const QString &endpoint, const QString &apiKey) {
    return endpoint.trimmed().toLower() + QStringLiteral("|") + apiKey.trimmed();
}

bool lookupEndpointHealthCache(const QString &cacheKey, bool *reachable, QString *error) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker locker(&endpointHealthCacheMutex());
    const auto it = endpointHealthCache().constFind(cacheKey);
    if (it == endpointHealthCache().constEnd() ||
        now - it->checkedAtMs > kEndpointHealthCacheTtlMs) {
        return false;
    }

    if (reachable) {
        *reachable = it->reachable;
    }
    if (error) {
        *error = it->error;
    }
    return true;
}

void storeEndpointHealthCache(const QString &cacheKey, bool reachable, const QString &error) {
    EndpointHealthCacheEntry entry;
    entry.reachable = reachable;
    entry.error = error;
    entry.checkedAtMs = QDateTime::currentMSecsSinceEpoch();

    QMutexLocker locker(&endpointHealthCacheMutex());
    endpointHealthCache().insert(cacheKey, entry);
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

        const QString sibling = currentInfo.dir().filePath(
#ifdef Q_OS_WIN
            QStringLiteral("yaos-memory.exe")
#else
            QStringLiteral("yaos-memory")
#endif
        );
        if (QFileInfo::exists(sibling) && sibling != currentProgram) {
            programs << sibling;
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

    }

    programs.removeDuplicates();
    return programs;
}

QStringList spawnArgsForProgram(const QString &program, const QString &endpoint, const QString &apiKey) {
    QStringList args;
    const QString baseName = QFileInfo(program).completeBaseName().toLower();
    if (baseName != QStringLiteral("yaos-memory")) {
        args << QStringLiteral("memory-service");
    }
    args << QStringLiteral("--endpoint") << endpoint;
    if (!apiKey.trimmed().isEmpty()) {
        args << QStringLiteral("--api-key") << apiKey.trimmed();
    }
    return args;
}

bool spawnLocalMemoryService(const config::Config &config, QString *error) {
    const QStringList programs = serviceProgramCandidates();
    if (programs.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Unable to determine yaos-memory executable path.");
        }
        return false;
    }

    const QString endpoint = resolvedEndpoint(config);
    for (const QString &program : programs) {
        if (QProcess::startDetached(program,
                                    spawnArgsForProgram(program, endpoint, config.memory.service.apiKey))) {
            return true;
        }
    }

    if (error) {
        *error = QStringLiteral("Failed to start local yaos-memory process.");
    }
    return false;
}

} // namespace

bool prefersRemoteMemoryService(const config::Config &config) {
    return config.normalizedMemoryBackend() == QStringLiteral("hybrid_cluster") ||
           remoteMemoryServiceEnabled(config);
}

bool remoteMemoryServiceEnabled(const config::Config &config) {
    return config.memory.service.enabled ||
           isRemoteDriver(config.memory.conversation.driver) ||
           isRemoteDriver(config.memory.facts.driver) ||
           isRemoteDriver(config.memory.vector.driver);
}

MemoryServiceAvailability ensureMemoryService(const config::Config &config,
                                              bool allowAutoSpawn,
                                              int probeTimeoutMs) {
    static QSet<QString> spawnAttemptedEndpoints;

    MemoryServiceAvailability availability;
    availability.endpoint = resolvedEndpoint(config);
    availability.preferred = prefersRemoteMemoryService(config);
    availability.enabled = remoteMemoryServiceEnabled(config);
    availability.localEndpoint = isLocalEndpoint(availability.endpoint);
    const QString endpointKey = availability.endpoint.trimmed().toLower();
    const QString cacheKey = endpointHealthCacheKey(availability.endpoint, config.memory.service.apiKey);

    if (!availability.preferred) {
        return availability;
    }
    if (!availability.enabled) {
        availability.error = QStringLiteral("Memory service is not explicitly enabled.");
        return availability;
    }
    if (availability.endpoint.isEmpty()) {
        availability.error = QStringLiteral("Memory service endpoint is empty.");
        return availability;
    }

    const int configuredTimeout = config.memory.service.timeoutMs > 0
        ? config.memory.service.timeoutMs
        : kDefaultMemoryProbeTimeoutMs;
    // Keep health probes short during save/reload. The configured request timeout is
    // still used by real memory HTTP operations, but probing should not repeatedly
    // block the UI for that full duration.
    const int timeoutMs = probeTimeoutMs > 0
        ? probeTimeoutMs
        : qMin(configuredTimeout, kDefaultMemoryProbeTimeoutMs);
    availability.client = std::make_shared<RemoteMemoryClient>(availability.endpoint,
                                                                 config.memory.service.apiKey,
                                                                 timeoutMs);
    if (!availability.client->isReady()) {
        availability.error = QStringLiteral("Memory service endpoint is invalid.");
        availability.client.reset();
        return availability;
    }

    bool cachedReachable = false;
    QString cachedError;
    if (lookupEndpointHealthCache(cacheKey, &cachedReachable, &cachedError)) {
        availability.reachable = cachedReachable;
        availability.error = cachedError;
        if (availability.reachable) {
            return availability;
        }
    } else {
        QString pingError;
        if (availability.client->ping(&pingError)) {
            availability.reachable = true;
            availability.error.clear();
            storeEndpointHealthCache(cacheKey, true, QString());
            return availability;
        }

        availability.error = pingError.isEmpty()
            ? QStringLiteral("Memory service is unreachable.")
            : pingError;
        storeEndpointHealthCache(cacheKey, false, availability.error);
    }

    if (!allowAutoSpawn ||
        !config.memory.service.autoSpawnLocalService ||
        !availability.localEndpoint ||
        spawnAttemptedEndpoints.contains(endpointKey)) {
        return availability;
    }

    spawnAttemptedEndpoints.insert(endpointKey);
    QString spawnError;
    if (!spawnLocalMemoryService(config, &spawnError)) {
        availability.error = spawnError.isEmpty() ? availability.error : spawnError;
        storeEndpointHealthCache(cacheKey, false, availability.error);
        return availability;
    }

    for (int attempt = 0; attempt < 20; ++attempt) {
        QString retryError;
        if (availability.client->ping(&retryError)) {
            availability.reachable = true;
            availability.usedAutoSpawn = true;
            availability.error.clear();
            storeEndpointHealthCache(cacheKey, true, QString());
            return availability;
        }
        QThread::msleep(150);
        availability.error = retryError;
    }

    if (availability.error.isEmpty()) {
        availability.error = QStringLiteral("Local yaos-memory started but did not respond in time.");
    }
    storeEndpointHealthCache(cacheKey, false, availability.error);
    return availability;
}

} // namespace yaos::memory
