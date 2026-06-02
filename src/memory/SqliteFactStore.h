#ifndef YAOS_MEMORY_SQLITEFACTSTORE_H
#define YAOS_MEMORY_SQLITEFACTSTORE_H

#include <QSqlDatabase>
#include <QString>

#include "MemoryBackend.h"

namespace yaos::memory {

class SqliteFactStore : public IFactStore {
public:
    explicit SqliteFactStore(const QString &databasePath);
    ~SqliteFactStore() override;

    bool upsertFacts(const QString &workspaceId, const QList<MemoryFact> &facts) override;
    QList<MemoryFact> findFacts(const MemoryQuery &query) const override;

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

#endif // YAOS_MEMORY_SQLITEFACTSTORE_H
