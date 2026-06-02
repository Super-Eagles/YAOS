#include "StudioBackend_p.h"

#include "../distributed/Contracts.h"
#include "../runtime/RuntimeFacade.h"
#include <QJsonArray>
#include <QJsonObject>

namespace yaos::ui {

QVariantList stringListToVariant(const QStringList &values) {
    QVariantList list;
    list.reserve(values.size());
    for (const QString &value : values) {
        list.append(value);
    }
    return list;
}

QVariantMap statusToVariant(const runtime::StatusSnapshot &snapshot) {
    return QVariantMap{
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
        {"controlPlaneHealth", snapshot.controlPlaneHealth.toVariantMap()},
        {"registryEndpoint", snapshot.registryEndpoint},
        {"registryReachable", snapshot.registryReachable},
        {"mcpServerCount", snapshot.mcpServerCount},
        {"enabledChannels", stringListToVariant(snapshot.enabledChannels)},
        {"restrictToWorkspace", snapshot.restrictToWorkspace},
        {"gatewayRunning", snapshot.gatewayRunning},
        {"heartbeatEnabled", snapshot.heartbeatEnabled},
        {"heartbeatIntervalS", snapshot.heartbeatIntervalS},
        {"memoryServiceEndpoint", snapshot.memoryServiceEndpoint},
        {"memoryServiceEnabled", snapshot.memoryServiceEnabled},
        {"memoryServiceReachable", snapshot.memoryServiceReachable},
        {"memoryServiceAutoSpawn", snapshot.memoryServiceAutoSpawn},
        {"cronJobCount", snapshot.cronJobCount},
        {"taskCount", snapshot.taskCount},
        {"eventCount", snapshot.eventCount},
        {"pendingApprovalCount", snapshot.pendingApprovalCount},
        {"unreadNotificationCount", snapshot.unreadNotificationCount},
        {"automationCount", snapshot.automationCount},
        {"pluginCount", snapshot.pluginCount},
        {"skillCount", snapshot.skillCount},
        {"resourceCount", snapshot.resourceCount},
        {"enabledToolCapabilities", stringListToVariant(snapshot.enabledToolCapabilities)}
    };
}

QVariantMap taskToVariant(const runtime::TaskRecord &task) {
    return QVariantMap{
        {"id", task.id},
        {"traceId", task.traceId},
        {"parentTaskId", task.parentTaskId},
        {"rootTaskId", task.rootTaskId},
        {"originNode", task.originNode},
        {"targetNode", task.targetNode},
        {"kind", task.kind},
        {"title", task.title},
        {"sessionKey", task.sessionKey},
        {"channel", task.channel},
        {"state", task.state},
        {"summary", task.summary},
        {"resultPreview", task.resultPreview},
        {"error", task.error},
        {"depth", task.depth},
        {"childCount", task.childCount},
        {"descendantCount", task.descendantCount},
        {"hasChildren", task.hasChildren},
        {"createdAt", task.createdAt.toString(Qt::ISODate)},
        {"startedAt", task.startedAt.toString(Qt::ISODate)},
        {"finishedAt", task.finishedAt.toString(Qt::ISODate)},
        {"metadata", task.metadata.toVariantMap()}
    };
}

QVariantMap nodeToVariant(const distributed::NodeDescriptor &node) {
    QVariantList tags;
    for (const QString &tag : node.tags) {
        tags.append(tag);
    }

    int declaredConcurrency = node.maxConcurrencyHint;
    QVariantList capabilities;
    for (const distributed::NodeCapability &capability : node.capabilities) {
        QVariantMap capabilityMap;
        capabilityMap.insert("name", capability.name);
        capabilityMap.insert("version", capability.version);
        capabilityMap.insert("roles", stringListToVariant(capability.roles));
        capabilityMap.insert("tools", stringListToVariant(capability.tools));
        capabilityMap.insert("channels", stringListToVariant(capability.channels));
        capabilityMap.insert("memoryBackends", stringListToVariant(capability.memoryBackends));
        capabilityMap.insert("maxConcurrency", capability.maxConcurrency);
        capabilityMap.insert("supportsDelegation", capability.supportsDelegation);
        capabilityMap.insert("supportsStreaming", capability.supportsStreaming);
        declaredConcurrency = qMax(declaredConcurrency, capability.maxConcurrency);
        capabilities.append(capabilityMap);
    }

    return QVariantMap{
        {"nodeId", node.nodeId},
        {"clusterId", node.clusterId},
        {"displayName", node.displayName},
        {"role", node.role},
        {"endpoint", node.endpoint},
        {"runtimeMode", node.runtimeMode},
        {"tags", tags},
        {"capabilities", capabilities},
        {"activeTaskCount", node.activeTaskCount},
        {"queuedTaskCount", node.queuedTaskCount},
        {"maxConcurrencyHint", node.maxConcurrencyHint},
        {"declaredConcurrency", declaredConcurrency},
        {"availableSlots", qMax(0, declaredConcurrency - node.activeTaskCount)},
        {"weight", node.weight},
        {"online", node.online},
        {"endpointProbeSupported", node.endpointProbeSupported},
        {"endpointHealthChecked", node.endpointHealthChecked},
        {"endpointReachable", node.endpointReachable},
        {"endpointHealthError", node.endpointHealthError}
    };
}

QVariantMap eventToVariant(const runtime::EventRecord &event) {
    return QVariantMap{
        {"id", event.id},
        {"level", event.level},
        {"category", event.category},
        {"message", event.message},
        {"timestamp", event.timestamp.toString(Qt::ISODate)},
        {"metadata", event.metadata.toVariantMap()}
    };
}

QVariantMap approvalToVariant(const runtime::ApprovalRecord &record) {
    return QVariantMap{
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
        {"metadata", record.metadata.toVariantMap()}
    };
}

QVariantMap notificationToVariant(const runtime::NotificationRecord &record) {
    return QVariantMap{
        {"id", record.id},
        {"level", record.level},
        {"title", record.title},
        {"body", record.body},
        {"action", record.action},
        {"targetId", record.targetId},
        {"read", record.read},
        {"createdAt", record.createdAt.toString(Qt::ISODate)},
        {"metadata", record.metadata.toVariantMap()}
    };
}

QVariantMap resourceToVariant(const runtime::ResourceRecord &record) {
    return QVariantMap{
        {"id", record.id},
        {"kind", record.kind},
        {"title", record.title},
        {"summary", record.summary},
        {"location", record.location},
        {"status", record.status},
        {"updatedAt", record.updatedAt.toString(Qt::ISODate)},
        {"metadata", record.metadata.toVariantMap()}
    };
}

QVariantMap automationToVariant(const runtime::AutomationRecord &record) {
    return QVariantMap{
        {"id", record.id},
        {"name", record.name},
        {"trigger", record.trigger},
        {"provider", record.provider},
        {"model", record.model},
        {"prompt", record.prompt},
        {"tags", stringListToVariant(record.tags)},
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
        {"metadata", record.metadata.toVariantMap()}
    };
}

QVariantMap automationRunToVariant(const runtime::AutomationRunRecord &record) {
    return QVariantMap{
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
        {"metadata", record.metadata.toVariantMap()}
    };
}

QVariantMap pluginToVariant(const runtime::PluginRecord &record) {
    return QVariantMap{
        {"id", record.id},
        {"name", record.name},
        {"version", record.version},
        {"description", record.description},
        {"rootPath", record.rootPath},
        {"entryPoint", record.entryPoint},
        {"toolName", record.toolName},
        {"executorType", record.executorType},
        {"state", record.state},
        {"capabilities", stringListToVariant(record.capabilities)},
        {"discoveredAt", record.discoveredAt.toString(Qt::ISODate)},
        {"manifest", record.manifest.toVariantMap()}
    };
}

QVariantMap skillToVariant(const runtime::SkillRecord &record) {
    return QVariantMap{
        {"id", record.id},
        {"name", record.name},
        {"description", record.description},
        {"rootPath", record.rootPath},
        {"skillFile", record.skillFile},
        {"state", record.state},
        {"discoveredAt", record.discoveredAt.toString(Qt::ISODate)},
        {"metadata", record.metadata.toVariantMap()}
    };
}

QVariantMap extensionCatalogToVariant(const runtime::ExtensionCatalogEntry &record) {
    return QVariantMap{
        {"catalogId", record.catalogId},
        {"kind", record.kind},
        {"installId", record.installId},
        {"title", record.title},
        {"summary", record.summary},
        {"description", record.description},
        {"target", record.target},
        {"tags", stringListToVariant(record.tags)},
        {"installed", record.installed}
    };
}

QVariantMap summaryToVariant(const runtime::ResourceSummary &summary) {
    return QVariantMap{
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


runtime::AutomationRecord automationFromVariant(const QVariantMap &recordMap) {
    runtime::AutomationRecord record;
    record.id = recordMap.value("id").toString();
    record.name = recordMap.value("name").toString();
    record.trigger = recordMap.value("trigger").toString();
    record.provider = recordMap.value("provider", QStringLiteral("auto")).toString();
    record.model = recordMap.value("model").toString();
    record.prompt = recordMap.value("prompt").toString();
    record.scheduleKind = recordMap.value("scheduleKind", record.trigger).toString();
    record.scheduleValue = recordMap.value("scheduleValue").toString();
    record.timeZone = recordMap.value("timeZone").toString();
    record.cronJobId = recordMap.value("cronJobId").toString();
    const QVariantList tags = recordMap.value("tags").toList();
    for (const QVariant &tag : tags) {
        const QString value = tag.toString().trimmed();
        if (!value.isEmpty()) {
            record.tags.append(value);
        }
    }
    record.enabled = recordMap.value("enabled", true).toBool();
    record.metadata = QJsonObject::fromVariantMap(recordMap.value("metadata").toMap());
    return record;
}

StudioChatTurnResult chatTurnToStudioResult(const runtime::ChatTurnResult &turn) {
    StudioChatTurnResult result;
    result.content = turn.content;
    result.thinking = turn.thinking;
    result.taskId = turn.taskId;
    result.traceId = turn.traceId;
    result.model = turn.model;
    result.provider = turn.provider;
    result.error = turn.error;
    for (const runtime::EventRecord &event : turn.trace) {
        result.trace.append(eventToVariant(event));
    }
    return result;
}

} // namespace yaos::ui
