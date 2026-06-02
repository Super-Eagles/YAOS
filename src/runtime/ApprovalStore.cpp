#include "ApprovalStore.h"

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

QString normalizedScope(const QString &scope) {
    const QString value = scope.trimmed().toLower();
    if (value == "always" || value == "session" || value == "once") {
        return value;
    }
    return "session";
}

QString normalizedState(const QString &state) {
    const QString value = state.trimmed().toLower();
    if (value == "approved" || value == "pending" || value == "denied" || value == "consumed") {
        return value;
    }
    return "pending";
}

} // namespace

ApprovalStore::ApprovalStore(const QString &workspace)
    : _workspace(workspace) {}

QString ApprovalStore::filePath() const {
    return QDir(_workspace).filePath("runtime/approvals.json");
}

QString ApprovalStore::trimText(const QString &text, int maxLen) {
    const QString simplified = text.simplified();
    if (simplified.size() <= maxLen) {
        return simplified;
    }
    return simplified.left(maxLen) + "...";
}

QJsonObject ApprovalStore::toJson(const ApprovalRecord &record) {
    QJsonObject obj;
    obj["id"] = record.id;
    obj["toolName"] = record.toolName;
    obj["sessionKey"] = record.sessionKey;
    obj["channel"] = record.channel;
    obj["state"] = record.state;
    obj["scope"] = record.scope;
    obj["remainingUses"] = record.remainingUses;
    obj["summary"] = record.summary;
    obj["paramsPreview"] = record.paramsPreview;
    obj["note"] = record.note;
    obj["createdAt"] = record.createdAt.toString(Qt::ISODate);
    obj["updatedAt"] = record.updatedAt.toString(Qt::ISODate);
    obj["metadata"] = record.metadata;
    return obj;
}

ApprovalRecord ApprovalStore::fromJson(const QJsonObject &obj) {
    ApprovalRecord record;
    record.id = obj.value("id").toString();
    record.toolName = obj.value("toolName").toString();
    record.sessionKey = obj.value("sessionKey").toString();
    record.channel = obj.value("channel").toString();
    record.state = normalizedState(obj.value("state").toString());
    record.scope = normalizedScope(obj.value("scope").toString());
    record.remainingUses = obj.value("remainingUses").toInt(record.scope == "once" ? 1 : -1);
    record.summary = obj.value("summary").toString();
    record.paramsPreview = obj.value("paramsPreview").toString();
    record.note = obj.value("note").toString();
    record.createdAt = QDateTime::fromString(obj.value("createdAt").toString(), Qt::ISODate);
    record.updatedAt = QDateTime::fromString(obj.value("updatedAt").toString(), Qt::ISODate);
    record.metadata = obj.value("metadata").toObject();
    return record;
}

struct ApprovalCacheEntry {
    QMutex mutex;
    QDateTime lastModified;
    QVector<ApprovalRecord> records;
    bool loaded = false;
};
static QHash<QString, QSharedPointer<ApprovalCacheEntry>> s_approvalCaches;
static QMutex s_approvalCachesMutex;

static QSharedPointer<ApprovalCacheEntry> getApprovalCache(const QString &workspace) {
    QMutexLocker locker(&s_approvalCachesMutex);
    if (!s_approvalCaches.contains(workspace)) {
        s_approvalCaches.insert(workspace, QSharedPointer<ApprovalCacheEntry>::create());
    }
    return s_approvalCaches.value(workspace);
}

QVector<ApprovalRecord> ApprovalStore::loadUnlocked() const {
    const QString path = filePath();
    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        return QVector<ApprovalRecord>();
    }

    QDateTime currentMod = fileInfo.lastModified();
    QSharedPointer<ApprovalCacheEntry> cache = getApprovalCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        if (cache->loaded && cache->lastModified == currentMod) {
            return cache->records;
        }
    }

    QVector<ApprovalRecord> records;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return records;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return records;
    }

    const QJsonArray arr = doc.object().value("approvals").toArray();
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

void ApprovalStore::saveUnlocked(const QVector<ApprovalRecord> &records) const {
    const QString path = filePath();
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QVector<ApprovalRecord> capped = records;
    if (capped.size() > 500) {
        capped = capped.mid(capped.size() - 500);
    }

    QJsonArray arr;
    for (const ApprovalRecord &record : capped) {
        arr.append(toJson(record));
    }
    QJsonObject root;
    root["approvals"] = arr;

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
    QSharedPointer<ApprovalCacheEntry> cache = getApprovalCache(_workspace);
    {
        QMutexLocker cacheLocker(&cache->mutex);
        cache->records = capped;
        cache->lastModified = currentMod;
        cache->loaded = true;
    }
}

bool ApprovalStore::matchesScope(const ApprovalRecord &record, const QString &sessionKey) {
    if (record.scope == "always") {
        return true;
    }
    if (record.scope == "session" || record.scope == "once") {
        return record.sessionKey == sessionKey;
    }
    return false;
}

QString ApprovalStore::createPending(const QString &toolName,
                                     const QString &sessionKey,
                                     const QString &channel,
                                     const QString &summary,
                                     const QString &paramsPreview,
                                     const QJsonObject &metadata) {
    QMutexLocker locker(&_mutex);
    QVector<ApprovalRecord> records = loadUnlocked();
    for (const ApprovalRecord &record : records) {
        if (record.state == "pending" &&
            record.toolName == toolName &&
            record.sessionKey == sessionKey &&
            record.paramsPreview == paramsPreview) {
            return record.id;
        }
    }

    ApprovalRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    record.toolName = toolName.trimmed();
    record.sessionKey = sessionKey.trimmed();
    record.channel = channel.trimmed();
    record.state = "pending";
    record.scope = "session";
    record.remainingUses = 0;
    record.summary = trimText(summary, 180);
    record.paramsPreview = trimText(paramsPreview, 280);
    record.createdAt = QDateTime::currentDateTime();
    record.updatedAt = record.createdAt;
    record.metadata = metadata;
    records.append(record);
    saveUnlocked(records);
    return record.id;
}

bool ApprovalStore::resolve(const QString &approvalId,
                            const QString &decision,
                            const QString &scope,
                            const QString &note) {
    if (approvalId.trimmed().isEmpty()) {
        return false;
    }

    QMutexLocker locker(&_mutex);
    QVector<ApprovalRecord> records = loadUnlocked();
    const QString normalizedDecision = decision.trimmed().toLower();
    for (ApprovalRecord &record : records) {
        if (record.id != approvalId.trimmed()) {
            continue;
        }

        record.updatedAt = QDateTime::currentDateTime();
        record.note = trimText(note, 300);
        if (normalizedDecision == "approve" || normalizedDecision == "approved") {
            record.state = "approved";
            record.scope = normalizedScope(scope);
            record.remainingUses = (record.scope == "once") ? 1 : -1;
        } else {
            record.state = "denied";
            record.scope = normalizedScope(scope);
            record.remainingUses = 0;
        }
        saveUnlocked(records);
        return true;
    }
    return false;
}

bool ApprovalStore::consumeGrant(const QString &toolName,
                                 const QString &sessionKey,
                                 QString *approvalId) {
    QMutexLocker locker(&_mutex);
    QVector<ApprovalRecord> records = loadUnlocked();
    for (ApprovalRecord &record : records) {
        if (record.state != "approved" || record.toolName != toolName || !matchesScope(record, sessionKey)) {
            continue;
        }

        if (approvalId) {
            *approvalId = record.id;
        }
        if (record.scope == "once") {
            record.remainingUses = std::max(0, record.remainingUses - 1);
            record.updatedAt = QDateTime::currentDateTime();
            if (record.remainingUses == 0) {
                record.state = "consumed";
            }
            saveUnlocked(records);
        }
        return true;
    }
    return false;
}

QVector<ApprovalRecord> ApprovalStore::recentApprovals(int limit, const QString &state) const {
    QMutexLocker locker(&_mutex);
    QVector<ApprovalRecord> records = loadUnlocked();
    if (!state.trimmed().isEmpty()) {
        QVector<ApprovalRecord> filtered;
        const QString wanted = normalizedState(state);
        for (const ApprovalRecord &record : records) {
            if (record.state == wanted) {
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

int ApprovalStore::count() const {
    QMutexLocker locker(&_mutex);
    return loadUnlocked().size();
}

int ApprovalStore::pendingCount() const {
    QMutexLocker locker(&_mutex);
    int count = 0;
    const QVector<ApprovalRecord> records = loadUnlocked();
    for (const ApprovalRecord &record : records) {
        if (record.state == "pending") {
            ++count;
        }
    }
    return count;
}

} // namespace yaos::runtime
