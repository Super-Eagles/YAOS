#ifndef YAOS_MEMORY_SQLITECONVERSATIONSTORE_H
#define YAOS_MEMORY_SQLITECONVERSATIONSTORE_H

#include <QSqlDatabase>
#include <QString>

#include "MemoryBackend.h"

namespace yaos::memory {

class SqliteConversationStore : public IConversationStore {
public:
    explicit SqliteConversationStore(const QString &databasePath);
    ~SqliteConversationStore() override;

    bool appendTurn(const QString &sessionKey, const QList<ConversationMessage> &messages) override;
    QList<ConversationMessage> recentMessages(const QString &sessionKey, int limit) const override;

    bool isReady() const;
    QString databasePath() const;

private:
    bool ensureOpen();
    bool ensureSchema();

    QString _databasePath;
    QString _connectionName;
    mutable QSqlDatabase _db;
    bool _ready = false;
};

} // namespace yaos::memory

#endif // YAOS_MEMORY_SQLITECONVERSATIONSTORE_H
