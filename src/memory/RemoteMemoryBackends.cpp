#include "RemoteMemoryBackends.h"

#include <QJsonArray>
#include <QLoggingCategory>
#include <utility>

#include "RemoteMemoryClient.h"
#include "RemoteMemoryProtocol.h"

Q_LOGGING_CATEGORY(lcRemoteMemory, "yaos.memory.remote")

namespace yaos::memory {

namespace {

bool responseAccepted(const QJsonObject &response) {
    if (response.isEmpty()) {
        return false;
    }
    if (response.contains("ok")) {
        return response.value("ok").toBool();
    }
    if (response.contains("success")) {
        return response.value("success").toBool();
    }
    return true;
}

} // namespace

RemoteConversationStore::RemoteConversationStore(QString workspaceId,
                                                 std::shared_ptr<RemoteMemoryClient> client)
    : _workspaceId(std::move(workspaceId)),
      _client(std::move(client)) {}

bool RemoteConversationStore::appendTurn(const QString &sessionKey,
                                         const QList<ConversationMessage> &messages) {
    if (!_client) {
        return false;
    }

    QString error;
    const QJsonObject response = _client->post(
        "/v1/memory/conversations/append",
        QJsonObject{
            {"workspaceId", _workspaceId},
            {"sessionKey", sessionKey},
            {"messages", conversationMessagesToJson(messages)}
        },
        &error);
    if (!error.isEmpty()) {
        qWarning(lcRemoteMemory) << "Remote conversation append failed:" << error;
        return false;
    }
    return responseAccepted(response);
}

QList<ConversationMessage> RemoteConversationStore::recentMessages(const QString &sessionKey,
                                                                   int limit) const {
    if (!_client) {
        return {};
    }

    QString error;
    const QJsonObject response = _client->post(
        "/v1/memory/conversations/recent",
        QJsonObject{
            {"workspaceId", _workspaceId},
            {"sessionKey", sessionKey},
            {"limit", limit}
        },
        &error);
    if (!error.isEmpty()) {
        qWarning(lcRemoteMemory) << "Remote conversation lookup failed:" << error;
        return {};
    }

    return jsonToConversationMessages(response.value("messages"));
}

RemoteFactStore::RemoteFactStore(std::shared_ptr<RemoteMemoryClient> client)
    : _client(std::move(client)) {}

bool RemoteFactStore::upsertFacts(const QString &workspaceId,
                                  const QList<MemoryFact> &facts) {
    if (!_client) {
        return false;
    }

    QString error;
    const QJsonObject response = _client->post(
        "/v1/memory/facts/upsert",
        QJsonObject{
            {"workspaceId", workspaceId},
            {"facts", memoryFactsToJson(facts)}
        },
        &error);
    if (!error.isEmpty()) {
        qWarning(lcRemoteMemory) << "Remote fact upsert failed:" << error;
        return false;
    }
    return responseAccepted(response);
}

QList<MemoryFact> RemoteFactStore::findFacts(const MemoryQuery &query) const {
    if (!_client) {
        return {};
    }

    QString error;
    const QJsonObject response = _client->post(
        "/v1/memory/facts/find",
        memoryQueryToJson(query),
        &error);
    if (!error.isEmpty()) {
        qWarning(lcRemoteMemory) << "Remote fact query failed:" << error;
        return {};
    }

    return jsonToMemoryFacts(response.value("facts"));
}

RemoteMemoryRetriever::RemoteMemoryRetriever(std::shared_ptr<RemoteMemoryClient> client)
    : _client(std::move(client)) {}

QList<MemoryRecallItem> RemoteMemoryRetriever::recall(const MemoryQuery &query) const {
    if (!_client) {
        return {};
    }

    QString error;
    const QJsonObject response = _client->post(
        "/v1/memory/recall",
        memoryQueryToJson(query),
        &error);
    if (!error.isEmpty()) {
        qWarning(lcRemoteMemory) << "Remote memory recall failed:" << error;
        return {};
    }

    return jsonToMemoryRecallItems(response.value("items"));
}

RemoteMemoryIngestor::RemoteMemoryIngestor(QString workspace,
                                           std::shared_ptr<RemoteMemoryClient> client,
                                           IMemoryExporter *exporter,
                                           bool enableDailySummaries,
                                           bool exportMarkdown)
    : _workspace(std::move(workspace)),
      _client(std::move(client)),
      _exporter(exporter),
      _enableDailySummaries(enableDailySummaries),
      _exportMarkdown(exportMarkdown) {}

bool RemoteMemoryIngestor::ingestTurn(const QString &workspaceId,
                                      const QString &sessionKey,
                                      const QList<ConversationMessage> &messages) {
    if (!_client || messages.isEmpty()) {
        return true;
    }

    QString error;
    const QJsonObject response = _client->post(
        "/v1/memory/ingest/turn",
        QJsonObject{
            {"workspaceId", workspaceId},
            {"sessionKey", sessionKey},
            {"messages", conversationMessagesToJson(messages)}
        },
        &error);
    if (!error.isEmpty()) {
        qWarning(lcRemoteMemory) << "Remote memory ingest failed:" << error;
        return false;
    }
    if (!responseAccepted(response)) {
        return false;
    }

    if (_enableDailySummaries) {
        buildDailySummary(workspaceId, QDate::currentDate());
    }
    if (_exportMarkdown && _exporter) {
        _exporter->exportLegacyFiles(_workspace);
    }
    return true;
}

bool RemoteMemoryIngestor::buildDailySummary(const QString &workspaceId, const QDate &date) {
    if (!_client) {
        return false;
    }

    QString error;
    const QJsonObject response = _client->post(
        "/v1/memory/ingest/daily-summary",
        QJsonObject{
            {"workspaceId", workspaceId},
            {"date", date.toString("yyyy-MM-dd")}
        },
        &error);
    if (!error.isEmpty()) {
        qWarning(lcRemoteMemory) << "Remote daily summary failed:" << error;
        return false;
    }
    return responseAccepted(response);
}

} // namespace yaos::memory
