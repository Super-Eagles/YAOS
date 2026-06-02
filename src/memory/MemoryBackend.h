#ifndef YAOS_MEMORY_MEMORYBACKEND_H
#define YAOS_MEMORY_MEMORYBACKEND_H

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace yaos::memory {

struct ConversationMessage {
    QString sessionKey;
    QString messageId;
    QString role;
    QString content;
    QJsonObject meta;
    QDateTime createdAt = QDateTime::currentDateTimeUtc();
};

struct MemoryFact {
    QString factId;
    QString scope;
    QString subject;
    QString predicate;
    QString value;
    double confidence = 0.5;
    bool active = true;
    QString supersedesFactId;
    QStringList tags;
    QDateTime updatedAt = QDateTime::currentDateTimeUtc();
};

struct MemoryChunk {
    QString chunkId;
    QString sessionKey;
    QString sceneKey;
    QString kind = "dialogue";
    QString text;
    QStringList sourceMessageIds;
    QStringList tags;
    double importance = 0.5;
    QDateTime createdAt = QDateTime::currentDateTimeUtc();
};

struct MemoryQuery {
    QString workspaceId;
    QString sessionKey;
    QString userId;
    QString sceneKey;
    QString text;
    QStringList tags;
    int recentWindow = 24;
    int topK = 8;
    bool includeFacts = true;
    bool includeEpisodic = true;
};

struct MemoryRecallItem {
    QString source;
    QString key;
    QString text;
    double score = 0.0;
    QString reason;
    QJsonObject meta;
};

class IConversationStore {
public:
    virtual ~IConversationStore() = default;

    virtual bool appendTurn(const QString &sessionKey, const QList<ConversationMessage> &messages) = 0;
    virtual QList<ConversationMessage> recentMessages(const QString &sessionKey, int limit) const = 0;
};

class IFactStore {
public:
    virtual ~IFactStore() = default;

    virtual bool upsertFacts(const QString &workspaceId, const QList<MemoryFact> &facts) = 0;
    virtual QList<MemoryFact> findFacts(const MemoryQuery &query) const = 0;
};

class IVectorStore {
public:
    virtual ~IVectorStore() = default;

    virtual bool upsertChunks(const QString &workspaceId, const QList<MemoryChunk> &chunks) = 0;
    virtual QList<MemoryRecallItem> search(const MemoryQuery &query) const = 0;
};

class IHotContextStore {
public:
    virtual ~IHotContextStore() = default;

    virtual bool warmSession(const QString &sessionKey, const QList<ConversationMessage> &messages) = 0;
    virtual QList<ConversationMessage> recentMessages(const QString &sessionKey, int limit) const = 0;
};

class IMemoryIngestor {
public:
    virtual ~IMemoryIngestor() = default;

    virtual bool ingestTurn(const QString &workspaceId,
                            const QString &sessionKey,
                            const QList<ConversationMessage> &messages) = 0;
    virtual bool buildDailySummary(const QString &workspaceId, const QDate &date) = 0;
};

class IMemoryRetriever {
public:
    virtual ~IMemoryRetriever() = default;

    virtual QList<MemoryRecallItem> recall(const MemoryQuery &query) const = 0;
};

class IMemoryExporter {
public:
    virtual ~IMemoryExporter() = default;

    virtual bool exportLegacyFiles(const QString &workspacePath) = 0;
};

} // namespace yaos::memory

#endif // YAOS_MEMORY_MEMORYBACKEND_H
