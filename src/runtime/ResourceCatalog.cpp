#include "ResourceCatalog.h"

#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QTextStream>

#include "ApprovalStore.h"
#include "AutomationStore.h"
#include "EventLog.h"
#include "NotificationCenter.h"
#include "PluginRegistry.h"
#include "SkillRegistry.h"
#include "TaskStore.h"

namespace yaos::runtime {

namespace {

struct SessionCountCache {
    QDateTime lastModified;
    QString result;
};
static QHash<QString, SessionCountCache> s_sessionCountCache;
static QMutex s_sessionCountCacheMutex;

QString countSessionMessages(const QString &path) {
    QFileInfo info(path);
    if (!info.exists()) {
        return "0 messages";
    }

    QDateTime currentMod = info.lastModified();
    {
        QMutexLocker locker(&s_sessionCountCacheMutex);
        const auto it = s_sessionCountCache.constFind(path);
        if (it != s_sessionCountCache.constEnd() && it->lastModified == currentMod) {
            return it->result;
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return "0 messages";
    }

    int count = 0;
    char buffer[4096];
    qint64 bytesRead = 0;
    bool lineHasContent = false;
    while ((bytesRead = file.read(buffer, sizeof(buffer))) > 0) {
        for (qint64 i = 0; i < bytesRead; ++i) {
            char c = buffer[i];
            if (c == '\n') {
                if (lineHasContent) {
                    ++count;
                    lineHasContent = false;
                }
            } else if (c != '\r' && c != ' ' && c != '\t') {
                lineHasContent = true;
            }
        }
    }
    if (lineHasContent) {
        ++count;
    }
    file.close();

    QString result = QString("%1 entries").arg(std::max(0, count - 1));
    {
        QMutexLocker locker(&s_sessionCountCacheMutex);
        SessionCountCache entry;
        entry.lastModified = currentMod;
        entry.result = result;
        s_sessionCountCache.insert(path, entry);
    }
    return result;
}

ResourceRecord makeDocumentResource(const QFileInfo &info, const QString &workspace) {
    ResourceRecord record;
    record.id = info.fileName();
    record.kind = "document";
    record.title = info.fileName();
    record.summary = info.absoluteFilePath().mid(QDir(workspace).absolutePath().size()).trimmed();
    record.location = info.absoluteFilePath();
    record.status = info.exists() ? "ready" : "missing";
    record.updatedAt = info.lastModified();
    return record;
}

} // namespace

ResourceCatalog::ResourceCatalog(const QString &workspace)
    : _workspace(workspace) {}

QVector<ResourceRecord> ResourceCatalog::collectResources() const {
    QVector<ResourceRecord> resources;
    const QDir root(_workspace);

    const QStringList topDocs = {"AGENTS.md", "SOUL.md", "USER.md", "TOOLS.md", "HEARTBEAT.md"};
    for (const QString &name : topDocs) {
        const QFileInfo info(root.filePath(name));
        if (info.exists()) {
            resources.append(makeDocumentResource(info, _workspace));
        }
    }

    const QDir memoryDir(root.filePath("memory"));
    const QFileInfoList memoryDocs = memoryDir.entryInfoList(QStringList() << "*.md", QDir::Files, QDir::Name);
    for (const QFileInfo &info : memoryDocs) {
        resources.append(makeDocumentResource(info, _workspace));
    }

    const QDir sessionsDir(root.filePath("sessions"));
    const QFileInfoList sessionFiles = sessionsDir.entryInfoList(QStringList() << "*.jsonl", QDir::Files, QDir::Time);
    for (const QFileInfo &info : sessionFiles) {
        ResourceRecord record;
        record.id = info.completeBaseName();
        record.kind = "session";
        record.title = info.completeBaseName();
        record.summary = countSessionMessages(info.absoluteFilePath());
        record.location = info.absoluteFilePath();
        record.status = "active";
        record.updatedAt = info.lastModified();
        resources.append(record);
    }

    TaskStore taskStore(_workspace);
    const QVector<TaskRecord> tasks = taskStore.recentTasks(200);
    for (const TaskRecord &task : tasks) {
        ResourceRecord record;
        record.id = task.id;
        record.kind = "task";
        record.title = task.title;
        record.summary = task.summary.isEmpty() ? task.kind : task.summary;
        record.location = task.sessionKey;
        record.status = task.state;
        record.updatedAt = task.finishedAt.isValid() ? task.finishedAt : task.createdAt;
        record.metadata = task.metadata;
        resources.append(record);
    }

    EventLog eventLog(_workspace);
    const QVector<EventRecord> events = eventLog.recentEvents(200);
    for (const EventRecord &event : events) {
        ResourceRecord record;
        record.id = event.id;
        record.kind = "event";
        record.title = event.category;
        record.summary = event.message;
        record.location = event.category;
        record.status = event.level;
        record.updatedAt = event.timestamp;
        record.metadata = event.metadata;
        resources.append(record);
    }

    ApprovalStore approvalStore(_workspace);
    const QVector<ApprovalRecord> approvals = approvalStore.recentApprovals(200);
    for (const ApprovalRecord &approval : approvals) {
        ResourceRecord record;
        record.id = approval.id;
        record.kind = "approval";
        record.title = approval.toolName;
        record.summary = approval.summary;
        record.location = approval.sessionKey;
        record.status = approval.state;
        record.updatedAt = approval.updatedAt.isValid() ? approval.updatedAt : approval.createdAt;
        record.metadata = approval.metadata;
        resources.append(record);
    }

    NotificationCenter notificationCenter(_workspace);
    const QVector<NotificationRecord> notifications = notificationCenter.recentNotifications(200, false);
    for (const NotificationRecord &notification : notifications) {
        ResourceRecord record;
        record.id = notification.id;
        record.kind = "notification";
        record.title = notification.title;
        record.summary = notification.body;
        record.location = notification.action;
        record.status = notification.read ? "read" : notification.level;
        record.updatedAt = notification.createdAt;
        record.metadata = notification.metadata;
        resources.append(record);
    }

    AutomationStore automationStore(_workspace);
    const QVector<AutomationRecord> automations = automationStore.list(200);
    for (const AutomationRecord &automation : automations) {
        ResourceRecord record;
        record.id = automation.id;
        record.kind = "automation";
        record.title = automation.name;
        record.summary = automation.trigger + (automation.model.isEmpty() ? QString() : (" · " + automation.model));
        record.location = automation.trigger;
        record.status = automation.enabled ? "enabled" : "disabled";
        record.updatedAt = automation.updatedAt.isValid() ? automation.updatedAt : automation.createdAt;
        record.metadata = automation.metadata;
        resources.append(record);
    }

    PluginRegistry plugins(_workspace);
    const QVector<PluginRecord> discovered = plugins.discover();
    for (const PluginRecord &plugin : discovered) {
        ResourceRecord record;
        record.id = plugin.id;
        record.kind = "plugin";
        record.title = plugin.name;
        record.summary = plugin.description;
        record.location = plugin.rootPath;
        record.status = plugin.state;
        record.updatedAt = plugin.discoveredAt;
        record.metadata = plugin.manifest;
        resources.append(record);
    }

    SkillRegistry skills(_workspace);
    const QVector<SkillRecord> discoveredSkills = skills.discover();
    for (const SkillRecord &skill : discoveredSkills) {
        ResourceRecord record;
        record.id = skill.id;
        record.kind = "skill";
        record.title = skill.name;
        record.summary = skill.description;
        record.location = skill.rootPath;
        record.status = skill.state;
        record.updatedAt = skill.discoveredAt;
        record.metadata = skill.metadata;
        resources.append(record);
    }

    std::sort(resources.begin(), resources.end(), [](const ResourceRecord &a, const ResourceRecord &b) {
        return a.updatedAt > b.updatedAt;
    });
    return resources;
}

bool ResourceCatalog::validateCache(QHash<QString, QDateTime> &currentTimestamps) const {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (_cache.valid && now - _cache.lastValidatedMs < 500) {
        currentTimestamps = _cache.timestamps;
        return true;
    }

    const QDir root(_workspace);
    const QStringList pathsToCheck = {
        root.absolutePath(),
        root.filePath(QStringLiteral("memory")),
        root.filePath(QStringLiteral("sessions")),
        root.filePath(QStringLiteral("runtime/tasks.json")),
        root.filePath(QStringLiteral("runtime/events.jsonl")),
        root.filePath(QStringLiteral("runtime/approvals.json")),
        root.filePath(QStringLiteral("runtime/notifications.json")),
        root.filePath(QStringLiteral("runtime/automations.json")),
        root.filePath(QStringLiteral("plugins.json")),
        root.filePath(QStringLiteral("skills.json"))
    };
    for (const QString &path : pathsToCheck) {
        currentTimestamps.insert(path, QFileInfo(path).lastModified());
    }

    _cache.lastValidatedMs = now;

    if (!_cache.valid) {
        return false;
    }

    if (_cache.timestamps.size() != currentTimestamps.size()) {
        return false;
    }

    for (auto it = currentTimestamps.constBegin(); it != currentTimestamps.constEnd(); ++it) {
        if (_cache.timestamps.value(it.key()) != it.value()) {
            return false;
        }
    }

    return true;
}

void ResourceCatalog::rebuildCacheUnlocked(const QHash<QString, QDateTime> &timestamps) const {
    ResourceSummary summary;
    const QDir root(_workspace);
    summary.documentCount += QFileInfo::exists(root.filePath(QStringLiteral("AGENTS.md"))) ? 1 : 0;
    summary.documentCount += QFileInfo::exists(root.filePath(QStringLiteral("SOUL.md"))) ? 1 : 0;
    summary.documentCount += QFileInfo::exists(root.filePath(QStringLiteral("USER.md"))) ? 1 : 0;
    summary.documentCount += QFileInfo::exists(root.filePath(QStringLiteral("TOOLS.md"))) ? 1 : 0;
    summary.documentCount += QFileInfo::exists(root.filePath(QStringLiteral("HEARTBEAT.md"))) ? 1 : 0;
    summary.documentCount += QDir(root.filePath(QStringLiteral("memory"))).entryInfoList(QStringList() << QStringLiteral("*.md"), QDir::Files).size();

    summary.sessionCount = QDir(root.filePath(QStringLiteral("sessions"))).entryInfoList(QStringList() << QStringLiteral("*.jsonl"), QDir::Files).size();
    summary.taskCount = TaskStore(_workspace).count();
    summary.eventCount = EventLog(_workspace).count();
    summary.approvalCount = ApprovalStore(_workspace).count();
    summary.notificationCount = NotificationCenter(_workspace).count();
    summary.automationCount = AutomationStore(_workspace).count();
    summary.pluginCount = PluginRegistry(_workspace).discover().size();
    summary.skillCount = SkillRegistry(_workspace).discover().size();
    summary.totalCount = summary.documentCount + summary.sessionCount + summary.taskCount +
        summary.eventCount + summary.approvalCount + summary.notificationCount +
        summary.automationCount + summary.pluginCount + summary.skillCount;

    _cache.summary = summary;
    _cache.resources = collectResources();
    _cache.timestamps = timestamps;
    _cache.valid = true;
}

ResourceSummary ResourceCatalog::summary() const {
    QMutexLocker locker(&_cacheMutex);
    QHash<QString, QDateTime> currentTimestamps;
    if (!validateCache(currentTimestamps)) {
        rebuildCacheUnlocked(currentTimestamps);
    }
    return _cache.summary;
}

QVector<ResourceRecord> ResourceCatalog::recentResources(int limit, const QString &kind) const {
    QVector<ResourceRecord> resources;
    {
        QMutexLocker locker(&_cacheMutex);
        QHash<QString, QDateTime> currentTimestamps;
        if (!validateCache(currentTimestamps)) {
            rebuildCacheUnlocked(currentTimestamps);
        }
        resources = _cache.resources;
    }

    if (!kind.trimmed().isEmpty() && kind.trimmed().toLower() != QStringLiteral("all")) {
        QVector<ResourceRecord> filtered;
        const QString wanted = kind.trimmed().toLower();
        for (const ResourceRecord &resource : resources) {
            if (resource.kind == wanted) {
                filtered.append(resource);
            }
        }
        resources = filtered;
    }
    if (limit > 0 && resources.size() > limit) {
        resources = resources.mid(0, limit);
    }
    return resources;
}

} // namespace yaos::runtime
