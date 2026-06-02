#include "MemoryRuntimeFactory.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <utility>

#include "LayeredMemoryExporter.h"
#include "MemoryServiceSupport.h"
#include "RemoteMemoryBackends.h"
#include "RemoteMemoryClient.h"
#include "LocalMemoryRetriever.h"
#include "LocalMemoryIngestor.h"
#include "SqliteConversationStore.h"
#include "SqliteFactStore.h"

Q_LOGGING_CATEGORY(lcMemoryRuntimeFactory, "yaos.memory.runtime_factory")

namespace yaos::memory {

namespace {

QString normalizedDriver(QString driver) {
    driver = driver.trimmed().toLower();
    driver.replace('-', '_');
    return driver;
}

bool isRemoteDriver(const QString &driver) {
    const QString normalized = normalizedDriver(driver);
    return normalized == "remote" || normalized == "http" || normalized == "service";
}

QString resolveConversationDriver(const config::Config &config) {
    QString driver = normalizedDriver(config.memory.conversation.driver);
    if (driver.isEmpty() || driver == "auto") {
        driver = config.usesLayeredMemory() ? "sqlite" : "jsonl";
    }

    if (config.usesLayeredMemory() && driver == "jsonl") {
        return "sqlite";
    }
    return driver;
}

QString resolveConversationPath(const QString &workspace, const config::Config &config) {
    const QString configured = config.memory.conversation.path.trimmed();
    if (!configured.isEmpty()) {
        const QFileInfo info(configured);
        return info.isAbsolute() ? configured : QDir(workspace).filePath(configured);
    }
    return QDir(workspace).filePath("runtime/conversations.sqlite");
}

QString resolveFactDriver(const config::Config &config) {
    QString driver = normalizedDriver(config.memory.facts.driver);
    if (driver.isEmpty() || driver == "auto" || driver == "none") {
        driver = config.usesLayeredMemory() ? "sqlite" : "none";
    }
    return driver;
}

QString resolveFactPath(const QString &workspace, const config::Config &config) {
    const QString configured = config.memory.facts.path.trimmed();
    if (!configured.isEmpty()) {
        const QFileInfo info(configured);
        return info.isAbsolute() ? configured : QDir(workspace).filePath(configured);
    }
    return QDir(workspace).filePath("runtime/facts.sqlite");
}

std::shared_ptr<RemoteMemoryClient> createRemoteClient(const config::Config &config) {
    static bool warnedMissingEnablement = false;
    static bool warnedUnavailable = false;
    static bool warnedReachability = false;

    const MemoryServiceAvailability availability = ensureMemoryService(config, true);
    if (!availability.preferred) {
        return nullptr;
    }

    if (!availability.enabled) {
        if (!warnedMissingEnablement) {
            qWarning(lcMemoryRuntimeFactory)
                << "Cluster memory backend requested without memory.service.enabled or remote drivers; falling back to local stores.";
            warnedMissingEnablement = true;
        }
        return nullptr;
    }

    if (!availability.client) {
        if (!warnedUnavailable) {
            qWarning(lcMemoryRuntimeFactory)
                << "Memory service endpoint is unavailable:" << availability.endpoint
                << "; falling back to local stores.";
            warnedUnavailable = true;
        }
        return nullptr;
    }

    if (!availability.reachable) {
        if (!warnedReachability) {
            qWarning(lcMemoryRuntimeFactory)
                << "Memory service did not become reachable:" << availability.error
                << "; falling back to local stores.";
            warnedReachability = true;
        }
        return nullptr;
    }

    return availability.client;
}

std::unique_ptr<IConversationStore> createLocalConversationStore(const QString &workspace,
                                                                 const config::Config &config) {
    QString driver = resolveConversationDriver(config);
    if (isRemoteDriver(driver)) {
        qWarning(lcMemoryRuntimeFactory)
            << "Remote conversation driver requested but unavailable; falling back to SQLite.";
        driver = "sqlite";
    }
    if (driver == "sqlite") {
        const QString path = resolveConversationPath(workspace, config);
        auto store = std::make_unique<SqliteConversationStore>(path);
        if (!store->isReady()) {
            qWarning(lcMemoryRuntimeFactory) << "SQLite conversation store is not ready:" << path;
            return nullptr;
        }
        return store;
    }

    qWarning(lcMemoryRuntimeFactory) << "Unsupported conversation driver for current phase:" << driver;
    return nullptr;
}

std::unique_ptr<IFactStore> createLocalFactStore(const QString &workspace,
                                                 const config::Config &config) {
    QString driver = resolveFactDriver(config);
    if (isRemoteDriver(driver)) {
        qWarning(lcMemoryRuntimeFactory)
            << "Remote fact driver requested but unavailable; falling back to SQLite.";
        driver = "sqlite";
    }
    if (driver == "sqlite") {
        const QString path = resolveFactPath(workspace, config);
        auto store = std::make_unique<SqliteFactStore>(path);
        if (!store->isReady()) {
            qWarning(lcMemoryRuntimeFactory) << "SQLite fact store is not ready:" << path;
            return nullptr;
        }
        return store;
    }
    if (driver == "none") {
        return nullptr;
    }

    qWarning(lcMemoryRuntimeFactory) << "Unsupported fact driver for current phase:" << driver;
    return nullptr;
}

} // namespace

std::unique_ptr<IConversationStore> MemoryRuntimeFactory::createConversationStore(const QString &workspace,
                                                                                  const config::Config &config) {
    if (!config.usesLayeredMemory()) {
        return nullptr;
    }

    if (auto client = createRemoteClient(config)) {
        return std::make_unique<RemoteConversationStore>(workspace, std::move(client));
    }

    return createLocalConversationStore(workspace, config);
}

std::unique_ptr<IFactStore> MemoryRuntimeFactory::createFactStore(const QString &workspace,
                                                                  const config::Config &config) {
    if (!config.usesLayeredMemory()) {
        return nullptr;
    }

    if (auto client = createRemoteClient(config)) {
        return std::make_unique<RemoteFactStore>(std::move(client));
    }

    return createLocalFactStore(workspace, config);
}

std::unique_ptr<IMemoryRetriever> MemoryRuntimeFactory::createRetriever(const config::Config &config,
                                                                        const IConversationStore *conversationStore,
                                                                        const IFactStore *factStore) {
    if (!config.usesLayeredMemory()) {
        return nullptr;
    }
    if (!conversationStore && !factStore) {
        return nullptr;
    }
    if (auto client = createRemoteClient(config)) {
        return std::make_unique<RemoteMemoryRetriever>(std::move(client));
    }
    return std::make_unique<LocalMemoryRetriever>(conversationStore, factStore);
}

std::unique_ptr<IMemoryExporter> MemoryRuntimeFactory::createExporter(const QString &workspace,
                                                                      const config::Config &config,
                                                                      const IFactStore *factStore) {
    if (!config.usesLayeredMemory() || !config.memory.exports.writeMarkdown) {
        return nullptr;
    }
    return std::make_unique<LayeredMemoryExporter>(workspace, factStore);
}

std::unique_ptr<IMemoryIngestor> MemoryRuntimeFactory::createIngestor(const QString &workspace,
                                                                      const config::Config &config,
                                                                      const IFactStore *factStore,
                                                                      IMemoryExporter *exporter) {
    if (!config.usesLayeredMemory()) {
        return nullptr;
    }
    if (auto client = createRemoteClient(config)) {
        return std::make_unique<RemoteMemoryIngestor>(workspace,
                                                      std::move(client),
                                                      exporter,
                                                      config.memory.enableDailySummaries,
                                                      config.memory.exports.writeMarkdown);
    }
    return std::make_unique<LocalMemoryIngestor>(workspace,
                                                 factStore,
                                                 exporter,
                                                 config.memory.enableDailySummaries,
                                                 config.memory.exports.writeMarkdown);
}

} // namespace yaos::memory
