#ifndef YAOS_MEMORY_REMOTEMEMORYBACKENDS_H
#define YAOS_MEMORY_REMOTEMEMORYBACKENDS_H

#include <memory>

#include "MemoryBackend.h"

namespace yaos::memory {

class RemoteMemoryClient;

class RemoteConversationStore : public IConversationStore {
public:
    RemoteConversationStore(QString workspaceId,
                            std::shared_ptr<RemoteMemoryClient> client);

    bool appendTurn(const QString &sessionKey, const QList<ConversationMessage> &messages) override;
    QList<ConversationMessage> recentMessages(const QString &sessionKey, int limit) const override;

private:
    QString _workspaceId;
    std::shared_ptr<RemoteMemoryClient> _client;
};

class RemoteFactStore : public IFactStore {
public:
    explicit RemoteFactStore(std::shared_ptr<RemoteMemoryClient> client);

    bool upsertFacts(const QString &workspaceId, const QList<MemoryFact> &facts) override;
    QList<MemoryFact> findFacts(const MemoryQuery &query) const override;

private:
    std::shared_ptr<RemoteMemoryClient> _client;
};

class RemoteMemoryRetriever : public IMemoryRetriever {
public:
    explicit RemoteMemoryRetriever(std::shared_ptr<RemoteMemoryClient> client);

    QList<MemoryRecallItem> recall(const MemoryQuery &query) const override;

private:
    std::shared_ptr<RemoteMemoryClient> _client;
};

class RemoteMemoryIngestor : public IMemoryIngestor {
public:
    RemoteMemoryIngestor(QString workspace,
                         std::shared_ptr<RemoteMemoryClient> client,
                         IMemoryExporter *exporter,
                         bool enableDailySummaries,
                         bool exportMarkdown);

    bool ingestTurn(const QString &workspaceId,
                    const QString &sessionKey,
                    const QList<ConversationMessage> &messages) override;
    bool buildDailySummary(const QString &workspaceId, const QDate &date) override;

private:
    QString _workspace;
    std::shared_ptr<RemoteMemoryClient> _client;
    IMemoryExporter *_exporter = nullptr;
    bool _enableDailySummaries = true;
    bool _exportMarkdown = true;
};

} // namespace yaos::memory

#endif // YAOS_MEMORY_REMOTEMEMORYBACKENDS_H
