#include "LocalMemoryRetriever.h"

#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>

namespace yaos::memory {

namespace {

QStringList tokenize(const QString &text) {
    QString cleaned = text.toLower();
    cleaned.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    const QStringList rawParts = cleaned.split(' ', Qt::SkipEmptyParts);

    QStringList tokens;
    for (const QString &part : rawParts) {
        const QString token = part.trimmed();
        if (token.size() >= 2) {
            tokens.append(token);
        }
    }
    tokens.removeDuplicates();
    return tokens;
}

double tokenOverlapScore(const QStringList &queryTokens, const QString &candidate) {
    if (queryTokens.isEmpty()) {
        return candidate.trimmed().isEmpty() ? 0.0 : 0.25;
    }

    const QString normalizedCandidate = candidate.toLower();
    int hits = 0;
    for (const QString &token : queryTokens) {
        if (normalizedCandidate.contains(token)) {
            ++hits;
        }
    }
    return queryTokens.isEmpty() ? 0.0 : static_cast<double>(hits) / static_cast<double>(queryTokens.size());
}

double recencyBoost(int reverseIndex, int total) {
    if (total <= 1) {
        return 0.15;
    }
    return 0.05 + (static_cast<double>(reverseIndex) / static_cast<double>(total)) * 0.15;
}

} // namespace

LocalMemoryRetriever::LocalMemoryRetriever(const IConversationStore *conversationStore,
                                           const IFactStore *factStore)
    : _conversationStore(conversationStore), _factStore(factStore) {}

QList<MemoryRecallItem> LocalMemoryRetriever::recall(const MemoryQuery &query) const {
    QList<MemoryRecallItem> items;
    const QStringList queryTokens = tokenize(query.text);

    if (query.includeFacts && _factStore) {
        const QList<MemoryFact> facts = _factStore->findFacts(query);
        for (const MemoryFact &fact : facts) {
            const QString text = QString("%1 %2 %3").arg(fact.subject, fact.predicate, fact.value);
            const double overlap = tokenOverlapScore(queryTokens, text);
            if (!queryTokens.isEmpty() && overlap <= 0.0) {
                continue;
            }

            MemoryRecallItem item;
            item.source = "fact";
            item.key = fact.factId;
            item.text = text;
            item.score = overlap + (fact.confidence * 0.5);
            item.reason = "fact_match";
            item.meta["scope"] = fact.scope;
            item.meta["predicate"] = fact.predicate;
            item.meta["updated_at"] = fact.updatedAt.toString(Qt::ISODate);
            items.append(item);
        }
    }

    if (query.includeEpisodic && _conversationStore && !query.sessionKey.trimmed().isEmpty()) {
        const int fetchLimit = std::max(query.recentWindow * 4, query.topK * 6);
        QList<ConversationMessage> recent = _conversationStore->recentMessages(query.sessionKey, fetchLimit);
        const int cutoff = std::min(query.recentWindow, recent.size());
        if (cutoff > 0) {
            recent = recent.mid(0, recent.size() - cutoff);
        }

        for (int i = 0; i < recent.size(); ++i) {
            const ConversationMessage &message = recent.at(i);
            if (message.content.trimmed().isEmpty()) {
                continue;
            }
            const double overlap = tokenOverlapScore(queryTokens, message.content);
            if (!queryTokens.isEmpty() && overlap <= 0.0) {
                continue;
            }

            MemoryRecallItem item;
            item.source = "conversation";
            item.key = message.messageId;
            item.text = QString("[%1] %2").arg(message.role, message.content);
            item.score = overlap + recencyBoost(i + 1, recent.size());
            item.reason = "episodic_match";
            item.meta["role"] = message.role;
            item.meta["session_key"] = message.sessionKey;
            item.meta["created_at"] = message.createdAt.toString(Qt::ISODate);
            items.append(item);
        }
    }

    std::sort(items.begin(), items.end(), [](const MemoryRecallItem &a, const MemoryRecallItem &b) {
        return a.score > b.score;
    });

    while (items.size() > query.topK) {
        items.removeLast();
    }
    return items;
}

} // namespace yaos::memory
