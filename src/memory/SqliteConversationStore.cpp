#include "SqliteConversationStore.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QUuid>

Q_LOGGING_CATEGORY(lcSqliteConversationStore, "yaos.memory.sqlite_conversation")

namespace yaos::memory {

SqliteConversationStore::SqliteConversationStore(const QString &databasePath)
    : _databasePath(QDir::cleanPath(databasePath)),
      _connectionName(QStringLiteral("yaos-memory-conversation-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
    _ready = ensureOpen() && ensureSchema();
}

SqliteConversationStore::~SqliteConversationStore() {
    if (_db.isValid()) {
        _db.close();
    }
    _db = QSqlDatabase();
    QSqlDatabase::removeDatabase(_connectionName);
}

bool SqliteConversationStore::appendTurn(const QString &sessionKey, const QList<ConversationMessage> &messages) {
    if (messages.isEmpty()) {
        return true;
    }
    if (!_ready && !(ensureOpen() && ensureSchema())) {
        return false;
    }

    QSqlQuery query(_db);
    if (!_db.transaction()) {
        qWarning(lcSqliteConversationStore) << "Failed to start transaction:" << _db.lastError().text();
        return false;
    }

    query.prepare(
        "INSERT INTO conversation_messages "
        "(session_key, message_id, role, content, meta_json, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"
    );

    for (const ConversationMessage &message : messages) {
        const QByteArray metaJson = QJsonDocument(message.meta).toJson(QJsonDocument::Compact);
        const QString createdAt = message.createdAt.isValid()
            ? message.createdAt.toUTC().toString(Qt::ISODate)
            : QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        query.addBindValue(sessionKey);
        query.addBindValue(message.messageId);
        query.addBindValue(message.role);
        query.addBindValue(message.content);
        query.addBindValue(QString::fromUtf8(metaJson));
        query.addBindValue(createdAt);

        if (!query.exec()) {
            qWarning(lcSqliteConversationStore) << "Failed to insert conversation row:" << query.lastError().text();
            _db.rollback();
            return false;
        }
        query.finish();
    }

    if (!_db.commit()) {
        qWarning(lcSqliteConversationStore) << "Failed to commit transaction:" << _db.lastError().text();
        _db.rollback();
        return false;
    }
    return true;
}

QList<ConversationMessage> SqliteConversationStore::recentMessages(const QString &sessionKey, int limit) const {
    QList<ConversationMessage> messages;
    if (limit <= 0) {
        return messages;
    }
    if (!_ready) {
        return messages;
    }

    QSqlQuery query(_db);
    query.prepare(
        "SELECT message_id, role, content, meta_json, created_at "
        "FROM conversation_messages "
        "WHERE session_key = ? "
        "ORDER BY id DESC "
        "LIMIT ?"
    );
    query.addBindValue(sessionKey);
    query.addBindValue(limit);

    if (!query.exec()) {
        qWarning(lcSqliteConversationStore) << "Failed to query recent messages:" << query.lastError().text();
        return messages;
    }

    while (query.next()) {
        ConversationMessage message;
        message.sessionKey = sessionKey;
        message.messageId = query.value(0).toString();
        message.role = query.value(1).toString();
        message.content = query.value(2).toString();
        const QByteArray metaBytes = query.value(3).toString().toUtf8();
        const QJsonDocument metaDoc = QJsonDocument::fromJson(metaBytes);
        if (metaDoc.isObject()) {
            message.meta = metaDoc.object();
        }
        message.createdAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODate);
        messages.prepend(message);
    }

    return messages;
}

bool SqliteConversationStore::isReady() const {
    return _ready;
}

QString SqliteConversationStore::databasePath() const {
    return _databasePath;
}

bool SqliteConversationStore::ensureOpen() {
    if (_db.isValid() && _db.isOpen()) {
        return true;
    }

    const QFileInfo info(_databasePath);
    QDir().mkpath(info.absolutePath());

    if (!_db.isValid()) {
        _db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), _connectionName);
        _db.setDatabaseName(_databasePath);
    }
    if (!_db.open()) {
        qWarning(lcSqliteConversationStore) << "Failed to open sqlite conversation store:"
                                            << _databasePath << _db.lastError().text();
        return false;
    }
    return true;
}

bool SqliteConversationStore::ensureSchema() {
    QSqlQuery query(_db);
    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS conversation_messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "session_key TEXT NOT NULL,"
            "message_id TEXT,"
            "role TEXT NOT NULL,"
            "content TEXT NOT NULL,"
            "meta_json TEXT,"
            "created_at TEXT NOT NULL)"
        )) {
        qWarning(lcSqliteConversationStore) << "Failed to create conversation_messages table:"
                                            << query.lastError().text();
        return false;
    }

    if (!query.exec(
            "CREATE INDEX IF NOT EXISTS idx_conversation_messages_session_id "
            "ON conversation_messages(session_key, id)"
        )) {
        qWarning(lcSqliteConversationStore) << "Failed to create conversation index:"
                                            << query.lastError().text();
        return false;
    }

    return true;
}

} // namespace yaos::memory
