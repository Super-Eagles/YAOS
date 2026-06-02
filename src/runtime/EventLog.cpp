#include "EventLog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSharedPointer>
#include <QUuid>

namespace yaos::runtime {

struct EventCacheEntry {
    QMutex mutex;
    QDateTime lastModified;
    QVector<EventRecord> records;
    bool loaded = false;
};
static QHash<QString, QSharedPointer<EventCacheEntry>> s_eventCaches;
static QMutex s_eventCachesMutex;

static QSharedPointer<EventCacheEntry> getEventCache(const QString &workspace) {
    QMutexLocker locker(&s_eventCachesMutex);
    if (!s_eventCaches.contains(workspace)) {
        s_eventCaches.insert(workspace, QSharedPointer<EventCacheEntry>::create());
    }
    return s_eventCaches.value(workspace);
}

EventLog::EventLog(const QString &workspace)
    : _workspace(workspace) {}

QString EventLog::filePath() const {
    return QDir(_workspace).filePath("runtime/events.jsonl");
}

QJsonObject EventLog::toJson(const EventRecord &record) {
    QJsonObject obj;
    obj["id"] = record.id;
    obj["level"] = record.level;
    obj["category"] = record.category;
    obj["message"] = record.message;
    obj["timestamp"] = record.timestamp.toString(Qt::ISODate);
    obj["metadata"] = record.metadata;
    return obj;
}

EventRecord EventLog::fromJson(const QJsonObject &obj) {
    EventRecord record;
    record.id = obj.value("id").toString();
    record.level = obj.value("level").toString();
    record.category = obj.value("category").toString();
    record.message = obj.value("message").toString();
    record.timestamp = QDateTime::fromString(obj.value("timestamp").toString(), Qt::ISODate);
    if (!record.timestamp.isValid()) {
        record.timestamp = QDateTime::currentDateTime();
    }
    record.metadata = obj.value("metadata").toObject();
    return record;
}

void EventLog::append(const QString &level,
                      const QString &category,
                      const QString &message,
                      const QJsonObject &metadata) {
    QMutexLocker locker(&_mutex);
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    EventRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.level = level.trimmed().isEmpty() ? "info" : level.trimmed();
    record.category = category.trimmed().isEmpty() ? "system" : category.trimmed();
    record.message = message;
    record.timestamp = QDateTime::currentDateTime();
    record.metadata = metadata;

    file.write(QJsonDocument(toJson(record)).toJson(QJsonDocument::Compact));
    file.write("\n");
    file.close();

    QSharedPointer<EventCacheEntry> cache = getEventCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        cache->loaded = false;
    }

    const QFileInfo info(path);
    if (info.size() <= 4 * 1024 * 1024) {
        return;
    }

    QVector<EventRecord> events;
    QFile readFile(path);
    if (readFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!readFile.atEnd()) {
            const QByteArray line = readFile.readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (doc.isObject()) {
                events.append(fromJson(doc.object()));
            }
        }
        readFile.close();
    }

    if (events.size() > 1500) {
        events = events.mid(events.size() - 1500);
    }

    QFile writeFile(path);
    if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }
    for (const EventRecord &event : events) {
        writeFile.write(QJsonDocument(toJson(event)).toJson(QJsonDocument::Compact));
        writeFile.write("\n");
    }
    writeFile.close();
}

QVector<EventRecord> EventLog::recentEvents(int limit) const {
    QMutexLocker locker(&_mutex);
    const QString path = filePath();
    QFileInfo info(path);
    if (!info.exists()) {
        return QVector<EventRecord>();
    }

    QDateTime currentMod = info.lastModified();
    QSharedPointer<EventCacheEntry> cache = getEventCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        if (cache->loaded && cache->lastModified == currentMod) {
            QVector<EventRecord> events = cache->records;
            if (limit > 0 && events.size() > limit) {
                events = events.mid(events.size() - limit);
            }
            return events;
        }
    }

    QVector<EventRecord> events;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return events;
    }

    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            continue;
        }
        events.append(fromJson(doc.object()));
    }
    file.close();

    {
        QMutexLocker cacheLocker(&cache->mutex);
        cache->records = events;
        cache->lastModified = currentMod;
        cache->loaded = true;
    }

    if (limit > 0 && events.size() > limit) {
        events = events.mid(events.size() - limit);
    }
    return events;
}

int EventLog::count() const {
    QMutexLocker locker(&_mutex);
    const QString path = filePath();
    QFileInfo info(path);
    if (!info.exists()) {
        return 0;
    }

    QDateTime currentMod = info.lastModified();
    QSharedPointer<EventCacheEntry> cache = getEventCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        if (cache->loaded && cache->lastModified == currentMod) {
            return cache->records.size();
        }
    }

    int lines = 0;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }
    while (!file.atEnd()) {
        if (!file.readLine().trimmed().isEmpty()) {
            ++lines;
        }
    }
    file.close();
    return lines;
}

} // namespace yaos::runtime
