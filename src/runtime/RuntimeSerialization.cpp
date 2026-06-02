#include "RuntimeSerialization.h"

#include <QDateTime>

namespace yaos::runtime::serialization {

namespace {

QStringList stringListFromJson(const QJsonValue &value) {
    QStringList out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        out.append(item.toString());
    }
    return out;
}

QJsonArray stringListToJson(const QStringList &values) {
    QJsonArray out;
    for (const QString &value : values) {
        out.append(value);
    }
    return out;
}

QDateTime dateTimeFromJson(const QJsonValue &value) {
    const QString text = value.toString();
    if (text.isEmpty()) {
        return QDateTime();
    }
    return QDateTime::fromString(text, Qt::ISODate);
}

template <typename T, typename F>
QJsonArray vectorToJson(const QVector<T> &values, F serializeOne) {
    QJsonArray out;
    for (const T &value : values) {
        out.append(serializeOne(value));
    }
    return out;
}

template <typename T, typename F>
QVector<T> vectorFromJson(const QJsonValue &value, F parseOne) {
    QVector<T> out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (item.isObject()) {
            out.append(parseOne(item.toObject()));
        }
    }
    return out;
}

} // namespace

QJsonObject toJson(const StatusSnapshot &snapshot) {
    return QJsonObject{
        {"configPath", snapshot.configPath},
        {"configReady", snapshot.configReady},
        {"workspacePath", snapshot.workspacePath},
        {"workspaceReady", snapshot.workspaceReady},
        {"defaultModel", snapshot.defaultModel},
        {"routedProvider", snapshot.routedProvider},
        {"actualBackend", snapshot.actualBackend},
        {"backendFallback", snapshot.backendFallback},
        {"runtimeMode", snapshot.runtimeMode},
        {"runtimeEndpoint", snapshot.runtimeEndpoint},
        {"runtimeAdvertiseEndpoint", snapshot.runtimeAdvertiseEndpoint},
        {"runtimeServiceEnabled", snapshot.runtimeServiceEnabled},
        {"runtimeServiceReachable", snapshot.runtimeServiceReachable},
        {"runtimeServiceAutoSpawn", snapshot.runtimeServiceAutoSpawn},
        {"controlPlaneEndpoint", snapshot.controlPlaneEndpoint},
        {"controlPlaneReachable", snapshot.controlPlaneReachable},
        {"controlPlaneHealth", snapshot.controlPlaneHealth},
        {"registryEndpoint", snapshot.registryEndpoint},
        {"registryReachable", snapshot.registryReachable},
        {"memoryServiceEndpoint", snapshot.memoryServiceEndpoint},
        {"memoryServiceEnabled", snapshot.memoryServiceEnabled},
        {"memoryServiceReachable", snapshot.memoryServiceReachable},
        {"memoryServiceAutoSpawn", snapshot.memoryServiceAutoSpawn},
        {"mcpServerCount", snapshot.mcpServerCount},
        {"providerOAuthStatuses", snapshot.providerOAuthStatuses},
        {"enabledChannels", stringListToJson(snapshot.enabledChannels)},
        {"restrictToWorkspace", snapshot.restrictToWorkspace},
        {"gatewayRunning", snapshot.gatewayRunning},
        {"heartbeatEnabled", snapshot.heartbeatEnabled},
        {"heartbeatIntervalS", snapshot.heartbeatIntervalS},
        {"cronJobCount", snapshot.cronJobCount},
        {"taskCount", snapshot.taskCount},
        {"eventCount", snapshot.eventCount},
        {"pendingApprovalCount", snapshot.pendingApprovalCount},
        {"unreadNotificationCount", snapshot.unreadNotificationCount},
        {"automationCount", snapshot.automationCount},
        {"pluginCount", snapshot.pluginCount},
        {"skillCount", snapshot.skillCount},
        {"resourceCount", snapshot.resourceCount},
        {"enabledToolCapabilities", stringListToJson(snapshot.enabledToolCapabilities)}
    };
}

StatusSnapshot statusSnapshotFromJson(const QJsonObject &obj) {
    StatusSnapshot snapshot;
    snapshot.configPath = obj.value("configPath").toString();
    snapshot.configReady = obj.value("configReady").toBool();
    snapshot.workspacePath = obj.value("workspacePath").toString();
    snapshot.workspaceReady = obj.value("workspaceReady").toBool();
    snapshot.defaultModel = obj.value("defaultModel").toString();
    snapshot.routedProvider = obj.value("routedProvider").toString();
    snapshot.actualBackend = obj.value("actualBackend").toString();
    snapshot.backendFallback = obj.value("backendFallback").toBool();
    snapshot.runtimeMode = obj.value("runtimeMode").toString(obj.value("runtime_mode").toString());
    snapshot.runtimeEndpoint = obj.value("runtimeEndpoint").toString(obj.value("runtime_endpoint").toString());
    snapshot.runtimeAdvertiseEndpoint =
        obj.value("runtimeAdvertiseEndpoint").toString(obj.value("runtime_advertise_endpoint").toString());
    snapshot.runtimeServiceEnabled = obj.value("runtimeServiceEnabled").toBool(obj.value("runtime_service_enabled").toBool());
    snapshot.runtimeServiceReachable = obj.value("runtimeServiceReachable").toBool(obj.value("runtime_service_reachable").toBool());
    snapshot.runtimeServiceAutoSpawn = obj.value("runtimeServiceAutoSpawn").toBool(obj.value("runtime_service_auto_spawn").toBool());
    snapshot.controlPlaneEndpoint = obj.value("controlPlaneEndpoint").toString(obj.value("control_plane_endpoint").toString());
    snapshot.controlPlaneReachable = obj.value("controlPlaneReachable").toBool(obj.value("control_plane_reachable").toBool());
    snapshot.controlPlaneHealth = obj.value("controlPlaneHealth").toObject();
    snapshot.registryEndpoint = obj.value("registryEndpoint").toString(obj.value("registry_endpoint").toString());
    snapshot.registryReachable = obj.value("registryReachable").toBool(obj.value("registry_reachable").toBool());
    snapshot.memoryServiceEndpoint = obj.value("memoryServiceEndpoint").toString();
    snapshot.memoryServiceEnabled = obj.value("memoryServiceEnabled").toBool();
    snapshot.memoryServiceReachable = obj.value("memoryServiceReachable").toBool();
    snapshot.memoryServiceAutoSpawn = obj.value("memoryServiceAutoSpawn").toBool();
    snapshot.mcpServerCount = obj.value("mcpServerCount").toInt();
    snapshot.providerOAuthStatuses = obj.value("providerOAuthStatuses").toArray();
    snapshot.enabledChannels = stringListFromJson(obj.value("enabledChannels"));
    snapshot.restrictToWorkspace = obj.value("restrictToWorkspace").toBool();
    snapshot.gatewayRunning = obj.value("gatewayRunning").toBool();
    snapshot.heartbeatEnabled = obj.value("heartbeatEnabled").toBool();
    snapshot.heartbeatIntervalS = obj.value("heartbeatIntervalS").toInt();
    snapshot.cronJobCount = obj.value("cronJobCount").toInt();
    snapshot.taskCount = obj.value("taskCount").toInt();
    snapshot.eventCount = obj.value("eventCount").toInt();
    snapshot.pendingApprovalCount = obj.value("pendingApprovalCount").toInt();
    snapshot.unreadNotificationCount = obj.value("unreadNotificationCount").toInt();
    snapshot.automationCount = obj.value("automationCount").toInt();
    snapshot.pluginCount = obj.value("pluginCount").toInt();
    snapshot.skillCount = obj.value("skillCount").toInt();
    snapshot.resourceCount = obj.value("resourceCount").toInt();
    snapshot.enabledToolCapabilities = stringListFromJson(obj.value("enabledToolCapabilities"));
    return snapshot;
}

QJsonObject toJson(const ChatTurnResult &result) {
    return QJsonObject{
        {"content", result.content},
        {"thinking", result.thinking},
        {"taskId", result.taskId},
        {"traceId", result.traceId},
        {"model", result.model},
        {"provider", result.provider},
        {"trace", toJson(result.trace)},
        {"error", result.error}
    };
}

ChatTurnResult chatTurnResultFromJson(const QJsonObject &obj) {
    ChatTurnResult result;
    result.content = obj.value("content").toString();
    result.thinking = obj.value("thinking").toString();
    result.taskId = obj.value("taskId").toString();
    result.traceId = obj.value("traceId").toString(obj.value("trace_id").toString());
    result.model = obj.value("model").toString();
    result.provider = obj.value("provider").toString();
    result.trace = eventRecordsFromJson(obj.value("trace"));
    result.error = obj.value("error").toBool();
    return result;
}

QJsonObject toJson(const ApprovalRecord &record) {
    return QJsonObject{
        {"id", record.id},
        {"toolName", record.toolName},
        {"sessionKey", record.sessionKey},
        {"channel", record.channel},
        {"state", record.state},
        {"scope", record.scope},
        {"remainingUses", record.remainingUses},
        {"summary", record.summary},
        {"paramsPreview", record.paramsPreview},
        {"note", record.note},
        {"createdAt", record.createdAt.toString(Qt::ISODate)},
        {"updatedAt", record.updatedAt.toString(Qt::ISODate)},
        {"metadata", record.metadata}
    };
}

ApprovalRecord approvalRecordFromJson(const QJsonObject &obj) {
    ApprovalRecord record;
    record.id = obj.value("id").toString();
    record.toolName = obj.value("toolName").toString();
    record.sessionKey = obj.value("sessionKey").toString();
    record.channel = obj.value("channel").toString();
    record.state = obj.value("state").toString();
    record.scope = obj.value("scope").toString();
    record.remainingUses = obj.value("remainingUses").toInt();
    record.summary = obj.value("summary").toString();
    record.paramsPreview = obj.value("paramsPreview").toString();
    record.note = obj.value("note").toString();
    record.createdAt = dateTimeFromJson(obj.value("createdAt"));
    record.updatedAt = dateTimeFromJson(obj.value("updatedAt"));
    record.metadata = obj.value("metadata").toObject();
    return record;
}

QJsonArray toJson(const QVector<ApprovalRecord> &records) {
    return vectorToJson(records, [](const ApprovalRecord &record) { return toJson(record); });
}

QVector<ApprovalRecord> approvalRecordsFromJson(const QJsonValue &value) {
    return vectorFromJson<ApprovalRecord>(value, approvalRecordFromJson);
}

QJsonObject toJson(const NotificationRecord &record) {
    return QJsonObject{
        {"id", record.id},
        {"level", record.level},
        {"title", record.title},
        {"body", record.body},
        {"action", record.action},
        {"targetId", record.targetId},
        {"read", record.read},
        {"createdAt", record.createdAt.toString(Qt::ISODate)},
        {"metadata", record.metadata}
    };
}

NotificationRecord notificationRecordFromJson(const QJsonObject &obj) {
    NotificationRecord record;
    record.id = obj.value("id").toString();
    record.level = obj.value("level").toString();
    record.title = obj.value("title").toString();
    record.body = obj.value("body").toString();
    record.action = obj.value("action").toString();
    record.targetId = obj.value("targetId").toString();
    record.read = obj.value("read").toBool();
    record.createdAt = dateTimeFromJson(obj.value("createdAt"));
    record.metadata = obj.value("metadata").toObject();
    return record;
}

QJsonArray toJson(const QVector<NotificationRecord> &records) {
    return vectorToJson(records, [](const NotificationRecord &record) { return toJson(record); });
}

QVector<NotificationRecord> notificationRecordsFromJson(const QJsonValue &value) {
    return vectorFromJson<NotificationRecord>(value, notificationRecordFromJson);
}

QJsonObject toJson(const TaskRecord &record) {
    return QJsonObject{
        {"id", record.id},
        {"traceId", record.traceId},
        {"parentTaskId", record.parentTaskId},
        {"rootTaskId", record.rootTaskId},
        {"originNode", record.originNode},
        {"targetNode", record.targetNode},
        {"kind", record.kind},
        {"title", record.title},
        {"sessionKey", record.sessionKey},
        {"channel", record.channel},
        {"state", record.state},
        {"summary", record.summary},
        {"resultPreview", record.resultPreview},
        {"error", record.error},
        {"depth", record.depth},
        {"childCount", record.childCount},
        {"descendantCount", record.descendantCount},
        {"hasChildren", record.hasChildren},
        {"createdAt", record.createdAt.toString(Qt::ISODate)},
        {"startedAt", record.startedAt.toString(Qt::ISODate)},
        {"finishedAt", record.finishedAt.toString(Qt::ISODate)},
        {"metadata", record.metadata}
    };
}

TaskRecord taskRecordFromJson(const QJsonObject &obj) {
    TaskRecord record;
    record.id = obj.value("id").toString();
    record.traceId = obj.value("traceId").toString(obj.value("trace_id").toString());
    record.parentTaskId = obj.value("parentTaskId").toString(obj.value("parent_task_id").toString());
    record.rootTaskId = obj.value("rootTaskId").toString(obj.value("root_task_id").toString());
    record.originNode = obj.value("originNode").toString(obj.value("origin_node").toString());
    record.targetNode = obj.value("targetNode").toString(obj.value("target_node").toString());
    record.kind = obj.value("kind").toString();
    record.title = obj.value("title").toString();
    record.sessionKey = obj.value("sessionKey").toString();
    record.channel = obj.value("channel").toString();
    record.state = obj.value("state").toString();
    record.summary = obj.value("summary").toString();
    record.resultPreview = obj.value("resultPreview").toString();
    record.error = obj.value("error").toString();
    record.depth = obj.value("depth").toInt();
    record.childCount = obj.value("childCount").toInt(obj.value("child_count").toInt());
    record.descendantCount = obj.value("descendantCount").toInt(obj.value("descendant_count").toInt());
    record.hasChildren = obj.contains("hasChildren")
        ? obj.value("hasChildren").toBool()
        : obj.value("has_children").toBool();
    record.createdAt = dateTimeFromJson(obj.value("createdAt"));
    record.startedAt = dateTimeFromJson(obj.value("startedAt"));
    record.finishedAt = dateTimeFromJson(obj.value("finishedAt"));
    record.metadata = obj.value("metadata").toObject();
    return record;
}

QJsonArray toJson(const QVector<TaskRecord> &records) {
    return vectorToJson(records, [](const TaskRecord &record) { return toJson(record); });
}

QVector<TaskRecord> taskRecordsFromJson(const QJsonValue &value) {
    return vectorFromJson<TaskRecord>(value, taskRecordFromJson);
}

QJsonObject toJson(const EventRecord &record) {
    return QJsonObject{
        {"id", record.id},
        {"level", record.level},
        {"category", record.category},
        {"message", record.message},
        {"timestamp", record.timestamp.toString(Qt::ISODate)},
        {"metadata", record.metadata}
    };
}

EventRecord eventRecordFromJson(const QJsonObject &obj) {
    EventRecord record;
    record.id = obj.value("id").toString();
    record.level = obj.value("level").toString();
    record.category = obj.value("category").toString();
    record.message = obj.value("message").toString();
    record.timestamp = dateTimeFromJson(obj.value("timestamp"));
    record.metadata = obj.value("metadata").toObject();
    return record;
}

QJsonArray toJson(const QVector<EventRecord> &records) {
    return vectorToJson(records, [](const EventRecord &record) { return toJson(record); });
}

QVector<EventRecord> eventRecordsFromJson(const QJsonValue &value) {
    return vectorFromJson<EventRecord>(value, eventRecordFromJson);
}

QJsonObject toJson(const ResourceSummary &summary) {
    return QJsonObject{
        {"sessionCount", summary.sessionCount},
        {"taskCount", summary.taskCount},
        {"eventCount", summary.eventCount},
        {"approvalCount", summary.approvalCount},
        {"notificationCount", summary.notificationCount},
        {"automationCount", summary.automationCount},
        {"pluginCount", summary.pluginCount},
        {"skillCount", summary.skillCount},
        {"documentCount", summary.documentCount},
        {"totalCount", summary.totalCount}
    };
}

ResourceSummary resourceSummaryFromJson(const QJsonObject &obj) {
    ResourceSummary summary;
    summary.sessionCount = obj.value("sessionCount").toInt();
    summary.taskCount = obj.value("taskCount").toInt();
    summary.eventCount = obj.value("eventCount").toInt();
    summary.approvalCount = obj.value("approvalCount").toInt();
    summary.notificationCount = obj.value("notificationCount").toInt();
    summary.automationCount = obj.value("automationCount").toInt();
    summary.pluginCount = obj.value("pluginCount").toInt();
    summary.skillCount = obj.value("skillCount").toInt();
    summary.documentCount = obj.value("documentCount").toInt();
    summary.totalCount = obj.value("totalCount").toInt();
    return summary;
}

QJsonObject toJson(const ResourceRecord &record) {
    return QJsonObject{
        {"id", record.id},
        {"kind", record.kind},
        {"title", record.title},
        {"summary", record.summary},
        {"location", record.location},
        {"status", record.status},
        {"updatedAt", record.updatedAt.toString(Qt::ISODate)},
        {"metadata", record.metadata}
    };
}

ResourceRecord resourceRecordFromJson(const QJsonObject &obj) {
    ResourceRecord record;
    record.id = obj.value("id").toString();
    record.kind = obj.value("kind").toString();
    record.title = obj.value("title").toString();
    record.summary = obj.value("summary").toString();
    record.location = obj.value("location").toString();
    record.status = obj.value("status").toString();
    record.updatedAt = dateTimeFromJson(obj.value("updatedAt"));
    record.metadata = obj.value("metadata").toObject();
    return record;
}

QJsonArray toJson(const QVector<ResourceRecord> &records) {
    return vectorToJson(records, [](const ResourceRecord &record) { return toJson(record); });
}

QVector<ResourceRecord> resourceRecordsFromJson(const QJsonValue &value) {
    return vectorFromJson<ResourceRecord>(value, resourceRecordFromJson);
}

QJsonObject toJson(const AutomationRecord &record) {
    return QJsonObject{
        {"id", record.id},
        {"name", record.name},
        {"trigger", record.trigger},
        {"provider", record.provider},
        {"model", record.model},
        {"prompt", record.prompt},
        {"tags", stringListToJson(record.tags)},
        {"enabled", record.enabled},
        {"scheduleKind", record.scheduleKind},
        {"scheduleValue", record.scheduleValue},
        {"timeZone", record.timeZone},
        {"cronJobId", record.cronJobId},
        {"nextRunAt", record.nextRunAt.toString(Qt::ISODate)},
        {"lastRunAt", record.lastRunAt.toString(Qt::ISODate)},
        {"lastStatus", record.lastStatus},
        {"lastError", record.lastError},
        {"lastResultPreview", record.lastResultPreview},
        {"runCount", record.runCount},
        {"createdAt", record.createdAt.toString(Qt::ISODate)},
        {"updatedAt", record.updatedAt.toString(Qt::ISODate)},
        {"metadata", record.metadata}
    };
}

AutomationRecord automationRecordFromJson(const QJsonObject &obj) {
    AutomationRecord record;
    record.id = obj.value("id").toString();
    record.name = obj.value("name").toString();
    record.trigger = obj.value("trigger").toString(record.trigger);
    record.provider = obj.value("provider").toString(record.provider);
    record.model = obj.value("model").toString();
    record.prompt = obj.value("prompt").toString();
    record.tags = stringListFromJson(obj.value("tags"));
    record.enabled = obj.contains("enabled") ? obj.value("enabled").toBool() : record.enabled;
    record.scheduleKind = obj.value("scheduleKind").toString(record.scheduleKind);
    record.scheduleValue = obj.value("scheduleValue").toString();
    record.timeZone = obj.value("timeZone").toString();
    record.cronJobId = obj.value("cronJobId").toString();
    record.nextRunAt = dateTimeFromJson(obj.value("nextRunAt"));
    record.lastRunAt = dateTimeFromJson(obj.value("lastRunAt"));
    record.lastStatus = obj.value("lastStatus").toString();
    record.lastError = obj.value("lastError").toString();
    record.lastResultPreview = obj.value("lastResultPreview").toString();
    record.runCount = obj.value("runCount").toInt();
    record.createdAt = dateTimeFromJson(obj.value("createdAt"));
    record.updatedAt = dateTimeFromJson(obj.value("updatedAt"));
    record.metadata = obj.value("metadata").toObject();
    return record;
}

QJsonArray toJson(const QVector<AutomationRecord> &records) {
    return vectorToJson(records, [](const AutomationRecord &record) { return toJson(record); });
}

QVector<AutomationRecord> automationRecordsFromJson(const QJsonValue &value) {
    return vectorFromJson<AutomationRecord>(value, automationRecordFromJson);
}

QJsonObject toJson(const AutomationRunRecord &record) {
    return QJsonObject{
        {"id", record.id},
        {"automationId", record.automationId},
        {"automationName", record.automationName},
        {"triggerSource", record.triggerSource},
        {"sessionKey", record.sessionKey},
        {"provider", record.provider},
        {"model", record.model},
        {"promptPreview", record.promptPreview},
        {"result", record.result},
        {"resultPreview", record.resultPreview},
        {"status", record.status},
        {"error", record.error},
        {"createdAt", record.createdAt.toString(Qt::ISODate)},
        {"finishedAt", record.finishedAt.toString(Qt::ISODate)},
        {"metadata", record.metadata}
    };
}

AutomationRunRecord automationRunRecordFromJson(const QJsonObject &obj) {
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
    record.createdAt = dateTimeFromJson(obj.value("createdAt"));
    record.finishedAt = dateTimeFromJson(obj.value("finishedAt"));
    record.metadata = obj.value("metadata").toObject();
    return record;
}

QJsonArray toJson(const QVector<AutomationRunRecord> &records) {
    return vectorToJson(records, [](const AutomationRunRecord &record) { return toJson(record); });
}

QVector<AutomationRunRecord> automationRunRecordsFromJson(const QJsonValue &value) {
    return vectorFromJson<AutomationRunRecord>(value, automationRunRecordFromJson);
}

QJsonObject toJson(const PluginRecord &record) {
    return QJsonObject{
        {"id", record.id},
        {"name", record.name},
        {"version", record.version},
        {"description", record.description},
        {"rootPath", record.rootPath},
        {"entryPoint", record.entryPoint},
        {"toolName", record.toolName},
        {"executorType", record.executorType},
        {"state", record.state},
        {"capabilities", stringListToJson(record.capabilities)},
        {"discoveredAt", record.discoveredAt.toString(Qt::ISODate)},
        {"manifest", record.manifest}
    };
}

PluginRecord pluginRecordFromJson(const QJsonObject &obj) {
    PluginRecord record;
    record.id = obj.value("id").toString();
    record.name = obj.value("name").toString();
    record.version = obj.value("version").toString();
    record.description = obj.value("description").toString();
    record.rootPath = obj.value("rootPath").toString();
    record.entryPoint = obj.value("entryPoint").toString();
    record.toolName = obj.value("toolName").toString();
    record.executorType = obj.value("executorType").toString();
    record.state = obj.value("state").toString();
    record.capabilities = stringListFromJson(obj.value("capabilities"));
    record.discoveredAt = dateTimeFromJson(obj.value("discoveredAt"));
    record.manifest = obj.value("manifest").toObject();
    return record;
}

QJsonArray toJson(const QVector<PluginRecord> &records) {
    return vectorToJson(records, [](const PluginRecord &record) { return toJson(record); });
}

QVector<PluginRecord> pluginRecordsFromJson(const QJsonValue &value) {
    return vectorFromJson<PluginRecord>(value, pluginRecordFromJson);
}

QJsonObject toJson(const SkillRecord &record) {
    return QJsonObject{
        {"id", record.id},
        {"name", record.name},
        {"description", record.description},
        {"rootPath", record.rootPath},
        {"skillFile", record.skillFile},
        {"state", record.state},
        {"discoveredAt", record.discoveredAt.toString(Qt::ISODate)},
        {"metadata", record.metadata}
    };
}

SkillRecord skillRecordFromJson(const QJsonObject &obj) {
    SkillRecord record;
    record.id = obj.value("id").toString();
    record.name = obj.value("name").toString();
    record.description = obj.value("description").toString();
    record.rootPath = obj.value("rootPath").toString();
    record.skillFile = obj.value("skillFile").toString();
    record.state = obj.value("state").toString();
    record.discoveredAt = dateTimeFromJson(obj.value("discoveredAt"));
    record.metadata = obj.value("metadata").toObject();
    return record;
}

QJsonArray toJson(const QVector<SkillRecord> &records) {
    return vectorToJson(records, [](const SkillRecord &record) { return toJson(record); });
}

QVector<SkillRecord> skillRecordsFromJson(const QJsonValue &value) {
    return vectorFromJson<SkillRecord>(value, skillRecordFromJson);
}

} // namespace yaos::runtime::serialization
