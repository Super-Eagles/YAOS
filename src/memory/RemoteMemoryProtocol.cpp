#include "RemoteMemoryProtocol.h"

namespace yaos::memory {

QJsonObject conversationMessageToJson(const ConversationMessage &message) {
    QJsonObject obj;
    obj["sessionKey"] = message.sessionKey;
    obj["messageId"] = message.messageId;
    obj["role"] = message.role;
    obj["content"] = message.content;
    obj["meta"] = message.meta;
    obj["createdAt"] = message.createdAt.toUTC().toString(Qt::ISODate);
    return obj;
}

ConversationMessage jsonToConversationMessage(const QJsonObject &obj) {
    ConversationMessage message;
    message.sessionKey = obj.value("sessionKey").toString(obj.value("session_key").toString());
    message.messageId = obj.value("messageId").toString(obj.value("message_id").toString());
    message.role = obj.value("role").toString();
    message.content = obj.value("content").toString();
    message.meta = obj.value("meta").toObject();

    const QString createdAt = obj.value("createdAt").toString(obj.value("created_at").toString());
    if (!createdAt.trimmed().isEmpty()) {
        const QDateTime parsed = QDateTime::fromString(createdAt, Qt::ISODate);
        if (parsed.isValid()) {
            message.createdAt = parsed.toUTC();
        }
    }
    return message;
}

QJsonArray conversationMessagesToJson(const QList<ConversationMessage> &messages) {
    QJsonArray array;
    for (const ConversationMessage &message : messages) {
        array.append(conversationMessageToJson(message));
    }
    return array;
}

QList<ConversationMessage> jsonToConversationMessages(const QJsonValue &value) {
    QList<ConversationMessage> messages;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        if (entry.isObject()) {
            messages.append(jsonToConversationMessage(entry.toObject()));
        }
    }
    return messages;
}

QJsonObject memoryFactToJson(const MemoryFact &fact) {
    QJsonObject obj;
    obj["factId"] = fact.factId;
    obj["scope"] = fact.scope;
    obj["subject"] = fact.subject;
    obj["predicate"] = fact.predicate;
    obj["value"] = fact.value;
    obj["confidence"] = fact.confidence;
    obj["active"] = fact.active;
    obj["supersedesFactId"] = fact.supersedesFactId;
    obj["tags"] = QJsonArray::fromStringList(fact.tags);
    obj["updatedAt"] = fact.updatedAt.toUTC().toString(Qt::ISODate);
    return obj;
}

MemoryFact jsonToMemoryFact(const QJsonObject &obj) {
    MemoryFact fact;
    fact.factId = obj.value("factId").toString(obj.value("fact_id").toString());
    fact.scope = obj.value("scope").toString();
    fact.subject = obj.value("subject").toString();
    fact.predicate = obj.value("predicate").toString();
    fact.value = obj.value("value").toString();
    fact.confidence = obj.value("confidence").toDouble(fact.confidence);
    fact.active = obj.value("active").toBool(fact.active);
    fact.supersedesFactId = obj.value("supersedesFactId").toString(obj.value("supersedes_fact_id").toString());

    const QJsonArray tags = obj.value("tags").toArray();
    for (const QJsonValue &tag : tags) {
        fact.tags.append(tag.toString());
    }

    const QString updatedAt = obj.value("updatedAt").toString(obj.value("updated_at").toString());
    if (!updatedAt.trimmed().isEmpty()) {
        const QDateTime parsed = QDateTime::fromString(updatedAt, Qt::ISODate);
        if (parsed.isValid()) {
            fact.updatedAt = parsed.toUTC();
        }
    }
    return fact;
}

QJsonArray memoryFactsToJson(const QList<MemoryFact> &facts) {
    QJsonArray array;
    for (const MemoryFact &fact : facts) {
        array.append(memoryFactToJson(fact));
    }
    return array;
}

QList<MemoryFact> jsonToMemoryFacts(const QJsonValue &value) {
    QList<MemoryFact> facts;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        if (entry.isObject()) {
            facts.append(jsonToMemoryFact(entry.toObject()));
        }
    }
    return facts;
}

QJsonObject memoryQueryToJson(const MemoryQuery &query) {
    QJsonObject obj;
    obj["workspaceId"] = query.workspaceId;
    obj["sessionKey"] = query.sessionKey;
    obj["userId"] = query.userId;
    obj["sceneKey"] = query.sceneKey;
    obj["text"] = query.text;
    obj["tags"] = QJsonArray::fromStringList(query.tags);
    obj["recentWindow"] = query.recentWindow;
    obj["topK"] = query.topK;
    obj["includeFacts"] = query.includeFacts;
    obj["includeEpisodic"] = query.includeEpisodic;
    return obj;
}

MemoryQuery jsonToMemoryQuery(const QJsonObject &obj) {
    MemoryQuery query;
    query.workspaceId = obj.value("workspaceId").toString(obj.value("workspace_id").toString());
    query.sessionKey = obj.value("sessionKey").toString(obj.value("session_key").toString());
    query.userId = obj.value("userId").toString(obj.value("user_id").toString());
    query.sceneKey = obj.value("sceneKey").toString(obj.value("scene_key").toString());
    query.text = obj.value("text").toString();

    const QJsonArray tags = obj.value("tags").toArray();
    for (const QJsonValue &tag : tags) {
        query.tags.append(tag.toString());
    }

    query.recentWindow = obj.value("recentWindow").toInt(obj.value("recent_window").toInt(query.recentWindow));
    query.topK = obj.value("topK").toInt(obj.value("top_k").toInt(query.topK));
    query.includeFacts = obj.value("includeFacts").toBool(obj.value("include_facts").toBool(query.includeFacts));
    query.includeEpisodic =
        obj.value("includeEpisodic").toBool(obj.value("include_episodic").toBool(query.includeEpisodic));
    return query;
}

QJsonObject memoryRecallItemToJson(const MemoryRecallItem &item) {
    QJsonObject obj;
    obj["source"] = item.source;
    obj["key"] = item.key;
    obj["text"] = item.text;
    obj["score"] = item.score;
    obj["reason"] = item.reason;
    obj["meta"] = item.meta;
    return obj;
}

MemoryRecallItem jsonToMemoryRecallItem(const QJsonObject &obj) {
    MemoryRecallItem item;
    item.source = obj.value("source").toString();
    item.key = obj.value("key").toString();
    item.text = obj.value("text").toString();
    item.score = obj.value("score").toDouble();
    item.reason = obj.value("reason").toString();
    item.meta = obj.value("meta").toObject();
    return item;
}

QJsonArray memoryRecallItemsToJson(const QList<MemoryRecallItem> &items) {
    QJsonArray array;
    for (const MemoryRecallItem &item : items) {
        array.append(memoryRecallItemToJson(item));
    }
    return array;
}

QList<MemoryRecallItem> jsonToMemoryRecallItems(const QJsonValue &value) {
    QList<MemoryRecallItem> items;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        if (entry.isObject()) {
            items.append(jsonToMemoryRecallItem(entry.toObject()));
        }
    }
    return items;
}

} // namespace yaos::memory
