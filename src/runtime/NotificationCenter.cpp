#include "NotificationCenter.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSharedPointer>
#include <QUuid>

namespace yaos::runtime {

NotificationCenter::NotificationCenter(const QString &workspace)
    : _workspace(workspace) {}

QString NotificationCenter::filePath() const {
    return QDir(_workspace).filePath("runtime/notifications.json");
}

QString NotificationCenter::trimText(const QString &text, int maxLen) {
    const QString simplified = text.simplified();
    if (simplified.size() <= maxLen) {
        return simplified;
    }
    return simplified.left(maxLen) + "...";
}

QJsonObject NotificationCenter::toJson(const NotificationRecord &record) {
    QJsonObject obj;
    obj["id"] = record.id;
    obj["level"] = record.level;
    obj["title"] = record.title;
    obj["body"] = record.body;
    obj["action"] = record.action;
    obj["targetId"] = record.targetId;
    obj["read"] = record.read;
    obj["createdAt"] = record.createdAt.toString(Qt::ISODate);
    obj["metadata"] = record.metadata;
    return obj;
}

NotificationRecord NotificationCenter::fromJson(const QJsonObject &obj) {
    NotificationRecord record;
    record.id = obj.value("id").toString();
    record.level = obj.value("level").toString();
    record.title = obj.value("title").toString();
    record.body = obj.value("body").toString();
    record.action = obj.value("action").toString();
    record.targetId = obj.value("targetId").toString();
    record.read = obj.value("read").toBool();
    record.createdAt = QDateTime::fromString(obj.value("createdAt").toString(), Qt::ISODate);
    record.metadata = obj.value("metadata").toObject();
    return record;
}

struct NotificationCacheEntry {
    QMutex mutex;
    QDateTime lastModified;
    QVector<NotificationRecord> records;
    bool loaded = false;
};
static QHash<QString, QSharedPointer<NotificationCacheEntry>> s_notificationCaches;
static QMutex s_notificationCachesMutex;

static QSharedPointer<NotificationCacheEntry> getNotificationCache(const QString &workspace) {
    QMutexLocker locker(&s_notificationCachesMutex);
    if (!s_notificationCaches.contains(workspace)) {
        s_notificationCaches.insert(workspace, QSharedPointer<NotificationCacheEntry>::create());
    }
    return s_notificationCaches.value(workspace);
}

QVector<NotificationRecord> NotificationCenter::loadUnlocked() const {
    const QString path = filePath();
    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        return QVector<NotificationRecord>();
    }

    QDateTime currentMod = fileInfo.lastModified();
    QSharedPointer<NotificationCacheEntry> cache = getNotificationCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        if (cache->loaded && cache->lastModified == currentMod) {
            return cache->records;
        }
    }

    QVector<NotificationRecord> records;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return records;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return records;
    }

    const QJsonArray arr = doc.object().value("notifications").toArray();
    for (const QJsonValue &value : arr) {
        if (value.isObject()) {
            records.append(fromJson(value.toObject()));
        }
    }

    {
        QMutexLocker cacheLocker(&cache->mutex);
        cache->records = records;
        cache->lastModified = currentMod;
        cache->loaded = true;
    }

    return records;
}

void NotificationCenter::saveUnlocked(const QVector<NotificationRecord> &records) const {
    const QString path = filePath();
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QJsonArray arr;
    for (const NotificationRecord &record : records) {
        arr.append(toJson(record));
    }
    QJsonObject root;
    root["notifications"] = arr;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        return;
    }
    file.commit();

    QFileInfo updatedInfo(path);
    QDateTime currentMod = updatedInfo.lastModified();
    QSharedPointer<NotificationCacheEntry> cache = getNotificationCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        cache->records = records;
        cache->lastModified = currentMod;
        cache->loaded = true;
    }
}

QString NotificationCenter::push(const QString &level,
                                 const QString &title,
                                 const QString &body,
                                 const QString &action,
                                 const QString &targetId,
                                 const QJsonObject &metadata) {
    QMutexLocker locker(&_mutex);
    QVector<NotificationRecord> records = loadUnlocked();

    NotificationRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    record.level = level.trimmed().isEmpty() ? "info" : level.trimmed().toLower();
    record.title = trimText(title, 120);
    record.body = trimText(body, 400);
    record.action = action.trimmed();
    record.targetId = targetId.trimmed();
    record.createdAt = QDateTime::currentDateTime();
    record.metadata = metadata;
    records.append(record);

    if (records.size() > 500) {
        records = records.mid(records.size() - 500);
    }

    saveUnlocked(records);
    return record.id;
}

QVector<NotificationRecord> NotificationCenter::recentNotifications(int limit, bool unreadOnly) const {
    QMutexLocker locker(&_mutex);
    QVector<NotificationRecord> records = loadUnlocked();
    if (unreadOnly) {
        QVector<NotificationRecord> filtered;
        for (const NotificationRecord &record : records) {
            if (!record.read) {
                filtered.append(record);
            }
        }
        records = filtered;
    }
    if (limit > 0 && records.size() > limit) {
        records = records.mid(records.size() - limit);
    }
    std::reverse(records.begin(), records.end());
    return records;
}

int NotificationCenter::count() const {
    QMutexLocker locker(&_mutex);
    return loadUnlocked().size();
}

bool NotificationCenter::markRead(const QString &id, bool read) {
    QMutexLocker locker(&_mutex);
    QVector<NotificationRecord> records = loadUnlocked();
    for (NotificationRecord &record : records) {
        if (record.id != id.trimmed()) {
            continue;
        }
        record.read = read;
        saveUnlocked(records);
        return true;
    }
    return false;
}

void NotificationCenter::markAllRead() {
    QMutexLocker locker(&_mutex);
    QVector<NotificationRecord> records = loadUnlocked();
    for (NotificationRecord &record : records) {
        record.read = true;
    }
    saveUnlocked(records);
}

int NotificationCenter::unreadCount() const {
    QMutexLocker locker(&_mutex);
    int count = 0;
    const QVector<NotificationRecord> records = loadUnlocked();
    for (const NotificationRecord &record : records) {
        if (!record.read) {
            ++count;
        }
    }
    return count;
}

} // namespace yaos::runtime
