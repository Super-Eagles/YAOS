#ifndef YAOS_RUNTIME_RESOURCECATALOG_H
#define YAOS_RUNTIME_RESOURCECATALOG_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QHash>
#include <QMutex>

namespace yaos::runtime {

struct ResourceRecord {
    QString id;
    QString kind;
    QString title;
    QString summary;
    QString location;
    QString status;
    QDateTime updatedAt;
    QJsonObject metadata;
};

struct ResourceSummary {
    int sessionCount = 0;
    int taskCount = 0;
    int eventCount = 0;
    int approvalCount = 0;
    int notificationCount = 0;
    int automationCount = 0;
    int pluginCount = 0;
    int skillCount = 0;
    int documentCount = 0;
    int totalCount = 0;
};

class ResourceCatalog {
public:
    explicit ResourceCatalog(const QString &workspace);

    ResourceSummary summary() const;
    QVector<ResourceRecord> recentResources(int limit = 100, const QString &kind = QString()) const;

private:
    QVector<ResourceRecord> collectResources() const;
    bool validateCache(QHash<QString, QDateTime> &currentTimestamps) const;
    void rebuildCacheUnlocked(const QHash<QString, QDateTime> &timestamps) const;

private:
    QString _workspace;

    struct CacheState {
        QHash<QString, QDateTime> timestamps;
        ResourceSummary summary;
        QVector<ResourceRecord> resources;
        bool valid = false;
        qint64 lastValidatedMs = 0;
    };
    mutable CacheState _cache;
    mutable QMutex _cacheMutex;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_RESOURCECATALOG_H
