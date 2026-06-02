#ifndef YAOS_MEMORY_MEMORYSERVICECORE_H
#define YAOS_MEMORY_MEMORYSERVICECORE_H

#include <QJsonObject>
#include <QString>
#include <memory>

#include "../config/Config.h"
#include "LayeredMemoryExporter.h"
#include "LocalMemoryIngestor.h"
#include "LocalMemoryRetriever.h"
#include "SqliteConversationStore.h"
#include "SqliteFactStore.h"

namespace yaos::memory {

class MemoryServiceCore {
public:
    MemoryServiceCore(const QString &workspace,
                      const config::Config &config);

    bool isReady() const;
    QString lastError() const;
    QString workspace() const;

    bool appendConversation(const QString &sessionKey,
                            const QList<ConversationMessage> &messages,
                            QString *error = nullptr);
    QList<ConversationMessage> recentMessages(const QString &sessionKey,
                                             int limit,
                                             QString *error = nullptr) const;

    bool upsertFacts(const QString &workspaceId,
                     const QList<MemoryFact> &facts,
                     QString *error = nullptr);
    QList<MemoryFact> findFacts(MemoryQuery query,
                                QString *error = nullptr) const;
    QList<MemoryRecallItem> recall(MemoryQuery query,
                                   QString *error = nullptr) const;

    bool ingestTurn(const QString &workspaceId,
                    const QString &sessionKey,
                    const QList<ConversationMessage> &messages,
                    QString *error = nullptr);
    bool buildDailySummary(const QString &workspaceId,
                           const QDate &date,
                           QString *error = nullptr);

    QJsonObject health() const;

private:
    QString effectiveWorkspaceId(const QString &workspaceId) const;

    QString _workspace;
    config::Config _config;
    std::unique_ptr<SqliteConversationStore> _conversationStore;
    std::unique_ptr<SqliteFactStore> _factStore;
    std::unique_ptr<LocalMemoryRetriever> _retriever;
    std::unique_ptr<LayeredMemoryExporter> _exporter;
    std::unique_ptr<LocalMemoryIngestor> _ingestor;
    QString _lastError;
    bool _ready = false;
};

} // namespace yaos::memory

#endif // YAOS_MEMORY_MEMORYSERVICECORE_H
