#ifndef YAOS_MEMORY_REMOTEMEMORYPROTOCOL_H
#define YAOS_MEMORY_REMOTEMEMORYPROTOCOL_H

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>

#include "MemoryBackend.h"

namespace yaos::memory {

QJsonObject conversationMessageToJson(const ConversationMessage &message);
ConversationMessage jsonToConversationMessage(const QJsonObject &obj);
QJsonArray conversationMessagesToJson(const QList<ConversationMessage> &messages);
QList<ConversationMessage> jsonToConversationMessages(const QJsonValue &value);

QJsonObject memoryFactToJson(const MemoryFact &fact);
MemoryFact jsonToMemoryFact(const QJsonObject &obj);
QJsonArray memoryFactsToJson(const QList<MemoryFact> &facts);
QList<MemoryFact> jsonToMemoryFacts(const QJsonValue &value);

QJsonObject memoryQueryToJson(const MemoryQuery &query);
MemoryQuery jsonToMemoryQuery(const QJsonObject &obj);

QJsonObject memoryRecallItemToJson(const MemoryRecallItem &item);
MemoryRecallItem jsonToMemoryRecallItem(const QJsonObject &obj);
QJsonArray memoryRecallItemsToJson(const QList<MemoryRecallItem> &items);
QList<MemoryRecallItem> jsonToMemoryRecallItems(const QJsonValue &value);

} // namespace yaos::memory

#endif // YAOS_MEMORY_REMOTEMEMORYPROTOCOL_H
