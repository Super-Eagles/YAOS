#include "SqliteFactStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QUuid>

Q_LOGGING_CATEGORY(lcSqliteFactStore, "yaos.memory.sqlite_fact")

namespace yaos::memory {

namespace {

QString tagsToJson(const QStringList &tags) {
    QJsonArray array;
    for (const QString &tag : tags) {
        array.append(tag);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QStringList jsonToTags(const QString &json) {
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    QStringList tags;
    if (!doc.isArray()) {
        return tags;
    }
    for (const QJsonValue &value : doc.array()) {
        const QString tag = value.toString().trimmed();
        if (!tag.isEmpty()) {
            tags.append(tag);
        }
    }
    tags.removeDuplicates();
    return tags;
}

QString stableFactId(const QString &workspaceId, const MemoryFact &fact) {
    if (!fact.factId.trimmed().isEmpty()) {
        return fact.factId.trimmed();
    }
    const QString seed = workspaceId + "|" + fact.scope + "|" + fact.subject + "|" + fact.predicate + "|" + fact.value;
    return QString::fromLatin1(QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha1).toHex());
}

} // namespace

SqliteFactStore::SqliteFactStore(const QString &databasePath)
    : _databasePath(QDir::cleanPath(databasePath)),
      _connectionName(QStringLiteral("yaos-memory-fact-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
    _ready = ensureOpen() && ensureSchema();
}

SqliteFactStore::~SqliteFactStore() {
    if (_db.isValid()) {
        _db.close();
    }
    _db = QSqlDatabase();
    QSqlDatabase::removeDatabase(_connectionName);
}

bool SqliteFactStore::upsertFacts(const QString &workspaceId, const QList<MemoryFact> &facts) {
    if (facts.isEmpty()) {
        return true;
    }
    if (!_ready && !(ensureOpen() && ensureSchema())) {
        return false;
    }

    QSqlQuery query(_db);
    if (!_db.transaction()) {
        qWarning(lcSqliteFactStore) << "Failed to start fact transaction:" << _db.lastError().text();
        return false;
    }

    query.prepare(
        "INSERT INTO memory_facts "
        "(fact_id, workspace_id, scope, subject, predicate, value, confidence, active, supersedes_fact_id, tags_json, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(fact_id) DO UPDATE SET "
        "workspace_id=excluded.workspace_id, "
        "scope=excluded.scope, "
        "subject=excluded.subject, "
        "predicate=excluded.predicate, "
        "value=excluded.value, "
        "confidence=excluded.confidence, "
        "active=excluded.active, "
        "supersedes_fact_id=excluded.supersedes_fact_id, "
        "tags_json=excluded.tags_json, "
        "updated_at=excluded.updated_at"
    );

    for (const MemoryFact &fact : facts) {
        const QString factId = stableFactId(workspaceId, fact);
        const QString updatedAt = fact.updatedAt.isValid()
            ? fact.updatedAt.toUTC().toString(Qt::ISODate)
            : QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        query.addBindValue(factId);
        query.addBindValue(workspaceId);
        query.addBindValue(fact.scope);
        query.addBindValue(fact.subject);
        query.addBindValue(fact.predicate);
        query.addBindValue(fact.value);
        query.addBindValue(fact.confidence);
        query.addBindValue(fact.active ? 1 : 0);
        query.addBindValue(fact.supersedesFactId);
        query.addBindValue(tagsToJson(fact.tags));
        query.addBindValue(updatedAt);

        if (!query.exec()) {
            qWarning(lcSqliteFactStore) << "Failed to upsert fact:" << query.lastError().text();
            _db.rollback();
            return false;
        }
        query.finish();
    }

    if (!_db.commit()) {
        qWarning(lcSqliteFactStore) << "Failed to commit fact transaction:" << _db.lastError().text();
        _db.rollback();
        return false;
    }
    return true;
}

QList<MemoryFact> SqliteFactStore::findFacts(const MemoryQuery &queryInput) const {
    QList<MemoryFact> facts;
    if (!_ready || queryInput.workspaceId.trimmed().isEmpty()) {
        return facts;
    }

    QSqlQuery query(_db);
    query.prepare(
        "SELECT fact_id, scope, subject, predicate, value, confidence, active, supersedes_fact_id, tags_json, updated_at "
        "FROM memory_facts "
        "WHERE workspace_id = ? AND active = 1 "
        "ORDER BY updated_at DESC "
        "LIMIT 200"
    );
    query.addBindValue(queryInput.workspaceId);

    if (!query.exec()) {
        qWarning(lcSqliteFactStore) << "Failed to query facts:" << query.lastError().text();
        return facts;
    }

    while (query.next()) {
        MemoryFact fact;
        fact.factId = query.value(0).toString();
        fact.scope = query.value(1).toString();
        fact.subject = query.value(2).toString();
        fact.predicate = query.value(3).toString();
        fact.value = query.value(4).toString();
        fact.confidence = query.value(5).toDouble();
        fact.active = query.value(6).toInt() != 0;
        fact.supersedesFactId = query.value(7).toString();
        fact.tags = jsonToTags(query.value(8).toString());
        fact.updatedAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODate);
        facts.append(fact);
    }

    return facts;
}

bool SqliteFactStore::isReady() const {
    return _ready;
}

QString SqliteFactStore::databasePath() const {
    return _databasePath;
}

bool SqliteFactStore::ensureOpen() {
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
        qWarning(lcSqliteFactStore) << "Failed to open sqlite fact store:"
                                    << _databasePath << _db.lastError().text();
        return false;
    }
    return true;
}

bool SqliteFactStore::ensureSchema() {
    QSqlQuery query(_db);
    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS memory_facts ("
            "fact_id TEXT PRIMARY KEY,"
            "workspace_id TEXT NOT NULL,"
            "scope TEXT,"
            "subject TEXT NOT NULL,"
            "predicate TEXT NOT NULL,"
            "value TEXT NOT NULL,"
            "confidence REAL NOT NULL,"
            "active INTEGER NOT NULL,"
            "supersedes_fact_id TEXT,"
            "tags_json TEXT,"
            "updated_at TEXT NOT NULL)"
        )) {
        qWarning(lcSqliteFactStore) << "Failed to create memory_facts table:" << query.lastError().text();
        return false;
    }

    if (!query.exec(
            "CREATE INDEX IF NOT EXISTS idx_memory_facts_workspace_updated "
            "ON memory_facts(workspace_id, updated_at DESC)"
        )) {
        qWarning(lcSqliteFactStore) << "Failed to create fact index:" << query.lastError().text();
        return false;
    }

    return true;
}

} // namespace yaos::memory
