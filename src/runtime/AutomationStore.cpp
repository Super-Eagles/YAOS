#include "AutomationStore.h"

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

namespace {

QString trimTextValue(const QString &text, int maxLen) {
    const QString simplified = text.simplified();
    if (simplified.size() <= maxLen) {
        return simplified;
    }
    return simplified.left(maxLen) + "...";
}

QString triggerFromScheduleKind(const QString &scheduleKind) {
    const QString kind = scheduleKind.trimmed().toLower();
    if (kind == "once" || kind == "every" || kind == "cron") {
        return kind;
    }
    return "manual";
}

QString normalizedScheduleKind(const QString &scheduleKind, const QString &legacyTrigger) {
    QString kind = scheduleKind.trimmed().toLower();
    if (kind == "at") {
        kind = "once";
    }
    if (kind == "scheduled") {
        kind = legacyTrigger.trimmed().toLower() == "cron" ? "cron" : "every";
    }
    if (kind == "manual" || kind == "once" || kind == "every" || kind == "cron") {
        return kind;
    }

    const QString trigger = legacyTrigger.trimmed().toLower();
    if (trigger == "once" || trigger == "every" || trigger == "cron") {
        return trigger;
    }
    if (trigger == "scheduled") {
        return "every";
    }
    return "manual";
}

QString isoString(const QDateTime &value) {
    return value.isValid() ? value.toString(Qt::ISODate) : QString();
}

QDateTime parseIsoDate(const QJsonObject &obj, const QString &key) {
    return QDateTime::fromString(obj.value(key).toString(), Qt::ISODate);
}

} // namespace

AutomationStore::AutomationStore(const QString &workspace)
    : _workspace(workspace) {}

QString AutomationStore::filePath() const {
    return QDir(_workspace).filePath("automations/flows.json");
}

QString AutomationStore::trimText(const QString &text, int maxLen) {
    return trimTextValue(text, maxLen);
}

QJsonObject AutomationStore::toJson(const AutomationRecord &record) {
    QJsonObject obj;
    obj["id"] = record.id;
    obj["name"] = record.name;
    obj["trigger"] = record.trigger;
    obj["provider"] = record.provider;
    obj["model"] = record.model;
    obj["prompt"] = record.prompt;
    obj["enabled"] = record.enabled;
    obj["scheduleKind"] = record.scheduleKind;
    obj["scheduleValue"] = record.scheduleValue;
    obj["timeZone"] = record.timeZone;
    obj["cronJobId"] = record.cronJobId;
    obj["nextRunAt"] = isoString(record.nextRunAt);
    obj["lastRunAt"] = isoString(record.lastRunAt);
    obj["lastStatus"] = record.lastStatus;
    obj["lastError"] = record.lastError;
    obj["lastResultPreview"] = record.lastResultPreview;
    obj["runCount"] = record.runCount;
    obj["createdAt"] = isoString(record.createdAt);
    obj["updatedAt"] = isoString(record.updatedAt);
    obj["metadata"] = record.metadata;

    QJsonArray tags;
    for (const QString &tag : record.tags) {
        tags.append(tag);
    }
    obj["tags"] = tags;
    return obj;
}

AutomationRecord AutomationStore::fromJson(const QJsonObject &obj) {
    AutomationRecord record;
    record.id = obj.value("id").toString();
    record.name = obj.value("name").toString();
    record.trigger = obj.value("trigger").toString(record.trigger);
    record.provider = obj.value("provider").toString(record.provider);
    record.model = obj.value("model").toString();
    record.prompt = obj.value("prompt").toString();
    record.enabled = obj.value("enabled").toBool(record.enabled);
    record.scheduleKind = normalizedScheduleKind(obj.value("scheduleKind").toString(), record.trigger);
    record.scheduleValue = obj.value("scheduleValue").toString();
    record.timeZone = obj.value("timeZone").toString(obj.value("timezone").toString());
    record.cronJobId = obj.value("cronJobId").toString();
    record.nextRunAt = parseIsoDate(obj, "nextRunAt");
    record.lastRunAt = parseIsoDate(obj, "lastRunAt");
    record.lastStatus = obj.value("lastStatus").toString();
    record.lastError = obj.value("lastError").toString();
    record.lastResultPreview = obj.value("lastResultPreview").toString();
    record.runCount = obj.value("runCount").toInt(record.runCount);
    record.createdAt = parseIsoDate(obj, "createdAt");
    record.updatedAt = parseIsoDate(obj, "updatedAt");
    record.metadata = obj.value("metadata").toObject();

    if (record.provider.trimmed().isEmpty()) {
        record.provider = record.metadata.value("provider").toString(record.provider);
    }
    if (record.scheduleValue.isEmpty()) {
        record.scheduleValue = record.metadata.value("scheduleValue").toString();
    }
    if (record.timeZone.isEmpty()) {
        record.timeZone = record.metadata.value("timeZone").toString();
    }
    if (record.cronJobId.isEmpty()) {
        record.cronJobId = record.metadata.value("cronJobId").toString();
    }
    if (!record.nextRunAt.isValid()) {
        record.nextRunAt = QDateTime::fromString(record.metadata.value("nextRunAt").toString(), Qt::ISODate);
    }
    if (!record.lastRunAt.isValid()) {
        record.lastRunAt = QDateTime::fromString(record.metadata.value("lastRunAt").toString(), Qt::ISODate);
    }
    if (record.lastStatus.isEmpty()) {
        record.lastStatus = record.metadata.value("lastStatus").toString();
    }
    if (record.lastError.isEmpty()) {
        record.lastError = record.metadata.value("lastError").toString();
    }
    if (record.lastResultPreview.isEmpty()) {
        record.lastResultPreview = record.metadata.value("lastResultPreview").toString();
    }
    if (record.runCount <= 0) {
        record.runCount = record.metadata.value("runCount").toInt(record.runCount);
    }

    const QJsonArray tags = obj.value("tags").toArray();
    for (const QJsonValue &value : tags) {
        if (value.isString()) {
            record.tags.append(value.toString());
        }
    }
    return record;
}

struct AutomationCacheEntry {
    QMutex mutex;
    QDateTime lastModified;
    QVector<AutomationRecord> records;
    bool loaded = false;
};
static QHash<QString, QSharedPointer<AutomationCacheEntry>> s_automationCaches;
static QMutex s_automationCachesMutex;

static QSharedPointer<AutomationCacheEntry> getAutomationCache(const QString &workspace) {
    QMutexLocker locker(&s_automationCachesMutex);
    if (!s_automationCaches.contains(workspace)) {
        s_automationCaches.insert(workspace, QSharedPointer<AutomationCacheEntry>::create());
    }
    return s_automationCaches.value(workspace);
}

QVector<AutomationRecord> AutomationStore::loadUnlocked() const {
    const QString path = filePath();
    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        return QVector<AutomationRecord>();
    }

    QDateTime currentMod = fileInfo.lastModified();
    QSharedPointer<AutomationCacheEntry> cache = getAutomationCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        if (cache->loaded && cache->lastModified == currentMod) {
            return cache->records;
        }
    }

    QVector<AutomationRecord> records;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return records;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return records;
    }

    const QJsonArray arr = doc.object().value("automations").toArray();
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

void AutomationStore::saveUnlocked(const QVector<AutomationRecord> &records) const {
    const QString path = filePath();
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QJsonArray arr;
    for (const AutomationRecord &record : records) {
        arr.append(toJson(record));
    }
    QJsonObject root;
    root["automations"] = arr;

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
    QSharedPointer<AutomationCacheEntry> cache = getAutomationCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        cache->records = records;
        cache->lastModified = currentMod;
        cache->loaded = true;
    }
}

QVector<AutomationRecord> AutomationStore::list(int limit) const {
    QMutexLocker locker(&_mutex);
    QVector<AutomationRecord> records = loadUnlocked();
    if (limit > 0 && records.size() > limit) {
        records = records.mid(records.size() - limit);
    }
    std::reverse(records.begin(), records.end());
    return records;
}

AutomationRecord AutomationStore::get(const QString &id) const {
    QMutexLocker locker(&_mutex);
    const QVector<AutomationRecord> records = loadUnlocked();
    for (const AutomationRecord &record : records) {
        if (record.id == id.trimmed()) {
            return record;
        }
    }
    return AutomationRecord();
}

QString AutomationStore::save(const AutomationRecord &recordIn, QString *error) {
    AutomationRecord record = recordIn;
    if (record.name.trimmed().isEmpty()) {
        if (error) {
            *error = "Automation name is required.";
        }
        return QString();
    }
    if (record.prompt.trimmed().isEmpty()) {
        if (error) {
            *error = "Automation prompt is required.";
        }
        return QString();
    }

    QMutexLocker locker(&_mutex);
    QVector<AutomationRecord> records = loadUnlocked();
    const QDateTime now = QDateTime::currentDateTime();
    record.name = trimText(record.name, 120);
    record.provider = record.provider.trimmed().isEmpty() ? "auto" : record.provider.trimmed();
    record.scheduleKind = normalizedScheduleKind(record.scheduleKind, record.trigger);
    record.trigger = triggerFromScheduleKind(record.scheduleKind);
    if (record.scheduleKind == "manual") {
        record.scheduleValue.clear();
        record.timeZone.clear();
        record.cronJobId.clear();
        record.nextRunAt = QDateTime();
    }
    record.prompt = record.prompt.trimmed();
    record.lastResultPreview = trimText(record.lastResultPreview, 320);
    record.updatedAt = now;

    if (record.id.trimmed().isEmpty()) {
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
        record.createdAt = now;
        records.append(record);
        saveUnlocked(records);
        return record.id;
    }

    for (AutomationRecord &existing : records) {
        if (existing.id != record.id.trimmed()) {
            continue;
        }
        if (!existing.createdAt.isValid()) {
            record.createdAt = now;
        } else {
            record.createdAt = existing.createdAt;
        }
        existing = record;
        saveUnlocked(records);
        return existing.id;
    }

    record.createdAt = now;
    records.append(record);
    saveUnlocked(records);
    return record.id;
}

bool AutomationStore::remove(const QString &id) {
    QMutexLocker locker(&_mutex);
    QVector<AutomationRecord> records = loadUnlocked();
    const int originalSize = records.size();
    records.erase(std::remove_if(records.begin(), records.end(), [id](const AutomationRecord &record) {
        return record.id == id.trimmed();
    }), records.end());
    if (records.size() == originalSize) {
        return false;
    }
    saveUnlocked(records);
    return true;
}

int AutomationStore::count() const {
    QMutexLocker locker(&_mutex);
    return loadUnlocked().size();
}

AutomationRunStore::AutomationRunStore(const QString &workspace)
    : _workspace(workspace) {}

QString AutomationRunStore::filePath() const {
    return QDir(_workspace).filePath("automations/runs.json");
}

QString AutomationRunStore::trimText(const QString &text, int maxLen) {
    return trimTextValue(text, maxLen);
}

QJsonObject AutomationRunStore::toJson(const AutomationRunRecord &record) {
    QJsonObject obj;
    obj["id"] = record.id;
    obj["automationId"] = record.automationId;
    obj["automationName"] = record.automationName;
    obj["triggerSource"] = record.triggerSource;
    obj["sessionKey"] = record.sessionKey;
    obj["provider"] = record.provider;
    obj["model"] = record.model;
    obj["promptPreview"] = record.promptPreview;
    obj["result"] = record.result;
    obj["resultPreview"] = record.resultPreview;
    obj["status"] = record.status;
    obj["error"] = record.error;
    obj["createdAt"] = isoString(record.createdAt);
    obj["finishedAt"] = isoString(record.finishedAt);
    obj["metadata"] = record.metadata;
    return obj;
}

AutomationRunRecord AutomationRunStore::fromJson(const QJsonObject &obj) {
    AutomationRunRecord record;
    record.id = obj.value("id").toString();
    record.automationId = obj.value("automationId").toString();
    record.automationName = obj.value("automationName").toString();
    record.triggerSource = obj.value("triggerSource").toString(record.triggerSource);
    record.sessionKey = obj.value("sessionKey").toString();
    record.provider = obj.value("provider").toString();
    record.model = obj.value("model").toString();
    record.promptPreview = obj.value("promptPreview").toString();
    record.result = obj.value("result").toString();
    record.resultPreview = obj.value("resultPreview").toString();
    record.status = obj.value("status").toString(record.status);
    record.error = obj.value("error").toString();
    record.createdAt = parseIsoDate(obj, "createdAt");
    record.finishedAt = parseIsoDate(obj, "finishedAt");
    record.metadata = obj.value("metadata").toObject();
    return record;
}

struct AutomationRunCacheEntry {
    QMutex mutex;
    QDateTime lastModified;
    QVector<AutomationRunRecord> records;
    bool loaded = false;
};
static QHash<QString, QSharedPointer<AutomationRunCacheEntry>> s_automationRunCaches;
static QMutex s_automationRunCachesMutex;

static QSharedPointer<AutomationRunCacheEntry> getAutomationRunCache(const QString &workspace) {
    QMutexLocker locker(&s_automationRunCachesMutex);
    if (!s_automationRunCaches.contains(workspace)) {
        s_automationRunCaches.insert(workspace, QSharedPointer<AutomationRunCacheEntry>::create());
    }
    return s_automationRunCaches.value(workspace);
}

QVector<AutomationRunRecord> AutomationRunStore::loadUnlocked() const {
    const QString path = filePath();
    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        return QVector<AutomationRunRecord>();
    }

    QDateTime currentMod = fileInfo.lastModified();
    QSharedPointer<AutomationRunCacheEntry> cache = getAutomationRunCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        if (cache->loaded && cache->lastModified == currentMod) {
            return cache->records;
        }
    }

    QVector<AutomationRunRecord> records;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return records;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return records;
    }

    const QJsonArray arr = doc.object().value("runs").toArray();
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

void AutomationRunStore::saveUnlocked(const QVector<AutomationRunRecord> &records) const {
    const QString path = filePath();
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QJsonArray arr;
    for (const AutomationRunRecord &record : records) {
        arr.append(toJson(record));
    }
    QJsonObject root;
    root["runs"] = arr;

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
    QSharedPointer<AutomationRunCacheEntry> cache = getAutomationRunCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        cache->records = records;
        cache->lastModified = currentMod;
        cache->loaded = true;
    }
}

QVector<AutomationRunRecord> AutomationRunStore::list(int limit, const QString &automationId) const {
    QMutexLocker locker(&_mutex);
    const QVector<AutomationRunRecord> loaded = loadUnlocked();
    QVector<AutomationRunRecord> filtered;
    const QString trimmedId = automationId.trimmed();
    for (const AutomationRunRecord &record : loaded) {
        if (trimmedId.isEmpty() || record.automationId == trimmedId) {
            filtered.append(record);
        }
    }
    if (limit > 0 && filtered.size() > limit) {
        filtered = filtered.mid(filtered.size() - limit);
    }
    std::reverse(filtered.begin(), filtered.end());
    return filtered;
}

AutomationRunRecord AutomationRunStore::latest(const QString &automationId) const {
    const QVector<AutomationRunRecord> records = list(1, automationId);
    return records.isEmpty() ? AutomationRunRecord() : records.first();
}

void AutomationRunStore::append(const AutomationRunRecord &recordIn) {
    AutomationRunRecord record = recordIn;
    if (record.id.trimmed().isEmpty()) {
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    }
    if (!record.createdAt.isValid()) {
        record.createdAt = QDateTime::currentDateTime();
    }
    if (!record.finishedAt.isValid()) {
        record.finishedAt = record.createdAt;
    }
    record.automationName = trimText(record.automationName, 120);
    record.promptPreview = trimText(record.promptPreview, 240);
    record.resultPreview = trimText(record.resultPreview, 320);

    QMutexLocker locker(&_mutex);
    QVector<AutomationRunRecord> records = loadUnlocked();
    records.append(record);
    const int maxRecords = 500;
    if (records.size() > maxRecords) {
        records = records.mid(records.size() - maxRecords);
    }
    saveUnlocked(records);
}

void AutomationRunStore::removeForAutomation(const QString &automationId) {
    QMutexLocker locker(&_mutex);
    QVector<AutomationRunRecord> records = loadUnlocked();
    const QString trimmedId = automationId.trimmed();
    records.erase(std::remove_if(records.begin(), records.end(), [trimmedId](const AutomationRunRecord &record) {
        return record.automationId == trimmedId;
    }), records.end());
    saveUnlocked(records);
}

int AutomationRunStore::count(const QString &automationId) const {
    QMutexLocker locker(&_mutex);
    const QVector<AutomationRunRecord> records = loadUnlocked();
    const QString trimmedId = automationId.trimmed();
    if (trimmedId.isEmpty()) {
        return records.size();
    }
    int matches = 0;
    for (const AutomationRunRecord &record : records) {
        if (record.automationId == trimmedId) {
            ++matches;
        }
    }
    return matches;
}

} // namespace yaos::runtime
