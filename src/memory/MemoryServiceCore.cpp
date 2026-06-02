#include "MemoryServiceCore.h"

#include <QDir>
#include <QFileInfo>

namespace yaos::memory {

namespace {

QString resolvePath(const QString &workspace,
                    const QString &configuredPath,
                    const QString &fallbackRelativePath) {
    const QString trimmed = configuredPath.trimmed();
    if (!trimmed.isEmpty()) {
        const QFileInfo info(trimmed);
        return info.isAbsolute() ? trimmed : QDir(workspace).filePath(trimmed);
    }
    return QDir(workspace).filePath(fallbackRelativePath);
}

} // namespace

MemoryServiceCore::MemoryServiceCore(const QString &workspace,
                                     const config::Config &config)
    : _workspace(workspace.trimmed()),
      _config(config) {
    const QString conversationPath = resolvePath(_workspace,
                                                 _config.memory.conversation.path,
                                                 "runtime/conversations.sqlite");
    const QString factPath = resolvePath(_workspace,
                                         _config.memory.facts.path,
                                         "runtime/facts.sqlite");

    _conversationStore = std::make_unique<SqliteConversationStore>(conversationPath);
    if (!_conversationStore->isReady()) {
        _lastError = QStringLiteral("Conversation store is not ready.");
        return;
    }

    _factStore = std::make_unique<SqliteFactStore>(factPath);
    if (!_factStore->isReady()) {
        _lastError = QStringLiteral("Fact store is not ready.");
        return;
    }

    _retriever = std::make_unique<LocalMemoryRetriever>(_conversationStore.get(), _factStore.get());
    if (_config.memory.exports.writeMarkdown) {
        _exporter = std::make_unique<LayeredMemoryExporter>(_workspace, _factStore.get());
    }
    _ingestor = std::make_unique<LocalMemoryIngestor>(_workspace,
                                                      _factStore.get(),
                                                      _exporter.get(),
                                                      _config.memory.enableDailySummaries,
                                                      _config.memory.exports.writeMarkdown);

    _ready = true;
}

bool MemoryServiceCore::isReady() const {
    return _ready;
}

QString MemoryServiceCore::lastError() const {
    return _lastError;
}

QString MemoryServiceCore::workspace() const {
    return _workspace;
}

bool MemoryServiceCore::appendConversation(const QString &sessionKey,
                                           const QList<ConversationMessage> &messages,
                                           QString *error) {
    if (error) {
        error->clear();
    }
    if (!_ready || !_conversationStore) {
        if (error) {
            *error = _lastError.isEmpty() ? QStringLiteral("Conversation store unavailable.") : _lastError;
        }
        return false;
    }
    if (!_conversationStore->appendTurn(sessionKey, messages)) {
        if (error) {
            *error = QStringLiteral("Failed to append conversation turn.");
        }
        return false;
    }
    return true;
}

QList<ConversationMessage> MemoryServiceCore::recentMessages(const QString &sessionKey,
                                                             int limit,
                                                             QString *error) const {
    if (error) {
        error->clear();
    }
    if (!_ready || !_conversationStore) {
        if (error) {
            *error = _lastError.isEmpty() ? QStringLiteral("Conversation store unavailable.") : _lastError;
        }
        return {};
    }
    return _conversationStore->recentMessages(sessionKey, limit);
}

bool MemoryServiceCore::upsertFacts(const QString &workspaceId,
                                    const QList<MemoryFact> &facts,
                                    QString *error) {
    if (error) {
        error->clear();
    }
    if (!_ready || !_factStore) {
        if (error) {
            *error = _lastError.isEmpty() ? QStringLiteral("Fact store unavailable.") : _lastError;
        }
        return false;
    }
    if (!_factStore->upsertFacts(effectiveWorkspaceId(workspaceId), facts)) {
        if (error) {
            *error = QStringLiteral("Failed to upsert facts.");
        }
        return false;
    }
    return true;
}

QList<MemoryFact> MemoryServiceCore::findFacts(MemoryQuery query,
                                               QString *error) const {
    if (error) {
        error->clear();
    }
    if (!_ready || !_factStore) {
        if (error) {
            *error = _lastError.isEmpty() ? QStringLiteral("Fact store unavailable.") : _lastError;
        }
        return {};
    }
    query.workspaceId = effectiveWorkspaceId(query.workspaceId);
    return _factStore->findFacts(query);
}

QList<MemoryRecallItem> MemoryServiceCore::recall(MemoryQuery query,
                                                  QString *error) const {
    if (error) {
        error->clear();
    }
    if (!_ready || !_retriever) {
        if (error) {
            *error = _lastError.isEmpty() ? QStringLiteral("Memory retriever unavailable.") : _lastError;
        }
        return {};
    }
    query.workspaceId = effectiveWorkspaceId(query.workspaceId);
    return _retriever->recall(query);
}

bool MemoryServiceCore::ingestTurn(const QString &workspaceId,
                                   const QString &sessionKey,
                                   const QList<ConversationMessage> &messages,
                                   QString *error) {
    if (error) {
        error->clear();
    }
    if (!_ready || !_ingestor) {
        if (error) {
            *error = _lastError.isEmpty() ? QStringLiteral("Memory ingestor unavailable.") : _lastError;
        }
        return false;
    }
    if (!_ingestor->ingestTurn(effectiveWorkspaceId(workspaceId), sessionKey, messages)) {
        if (error) {
            *error = QStringLiteral("Failed to ingest turn into memory service.");
        }
        return false;
    }
    return true;
}

bool MemoryServiceCore::buildDailySummary(const QString &workspaceId,
                                          const QDate &date,
                                          QString *error) {
    if (error) {
        error->clear();
    }
    if (!_ready || !_ingestor) {
        if (error) {
            *error = _lastError.isEmpty() ? QStringLiteral("Memory ingestor unavailable.") : _lastError;
        }
        return false;
    }
    if (!_ingestor->buildDailySummary(effectiveWorkspaceId(workspaceId), date)) {
        if (error) {
            *error = QStringLiteral("Failed to build daily summary.");
        }
        return false;
    }
    return true;
}

QJsonObject MemoryServiceCore::health() const {
    return QJsonObject{
        {"ok", _ready},
        {"workspace", _workspace},
        {"conversationStore", _conversationStore ? _conversationStore->databasePath() : QString()},
        {"factStore", _factStore ? _factStore->databasePath() : QString()},
        {"writeMarkdown", _config.memory.exports.writeMarkdown},
        {"enableDailySummaries", _config.memory.enableDailySummaries},
        {"error", _ready ? QString() : _lastError}
    };
}

QString MemoryServiceCore::effectiveWorkspaceId(const QString &workspaceId) const {
    const QString trimmed = workspaceId.trimmed();
    return trimmed.isEmpty() ? _workspace : trimmed;
}

} // namespace yaos::memory
