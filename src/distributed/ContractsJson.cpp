#include "ContractsJson.h"

#include <QVariant>

namespace yaos::distributed::json {

namespace {

QDateTime dateTimeFromJson(const QJsonValue &value) {
    const QString text = value.toString();
    if (text.isEmpty()) {
        return QDateTime();
    }
    return QDateTime::fromString(text, Qt::ISODate);
}

QJsonArray stringListToJson(const QStringList &values) {
    QJsonArray out;
    for (const QString &value : values) {
        out.append(value);
    }
    return out;
}

QStringList stringListFromJson(const QJsonValue &value) {
    QStringList out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        out.append(item.toString());
    }
    return out;
}

} // namespace

QJsonObject toJson(const NodeCapability &capability) {
    return QJsonObject{
        {"name", capability.name},
        {"version", capability.version},
        {"roles", stringListToJson(capability.roles)},
        {"tools", stringListToJson(capability.tools)},
        {"channels", stringListToJson(capability.channels)},
        {"memoryBackends", stringListToJson(capability.memoryBackends)},
        {"maxConcurrency", capability.maxConcurrency},
        {"supportsDelegation", capability.supportsDelegation},
        {"supportsStreaming", capability.supportsStreaming}
    };
}

NodeCapability nodeCapabilityFromJson(const QJsonObject &obj) {
    NodeCapability capability;
    capability.name = obj.value("name").toString();
    capability.version = obj.value("version").toString();
    capability.roles = stringListFromJson(obj.value("roles"));
    capability.tools = stringListFromJson(obj.value("tools"));
    capability.channels = stringListFromJson(obj.value("channels"));
    capability.memoryBackends = stringListFromJson(obj.value("memoryBackends"));
    capability.maxConcurrency = obj.value("maxConcurrency").toInt(capability.maxConcurrency);
    capability.supportsDelegation = obj.value("supportsDelegation").toBool(capability.supportsDelegation);
    capability.supportsStreaming = obj.value("supportsStreaming").toBool(capability.supportsStreaming);
    return capability;
}

QJsonArray toJson(const QList<NodeCapability> &capabilities) {
    QJsonArray out;
    for (const NodeCapability &capability : capabilities) {
        out.append(toJson(capability));
    }
    return out;
}

QList<NodeCapability> nodeCapabilitiesFromJson(const QJsonValue &value) {
    QList<NodeCapability> out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (item.isObject()) {
            out.append(nodeCapabilityFromJson(item.toObject()));
        }
    }
    return out;
}

QJsonObject toJson(const NodeDescriptor &node) {
    return QJsonObject{
        {"nodeId", node.nodeId},
        {"clusterId", node.clusterId},
        {"displayName", node.displayName},
        {"role", node.role},
        {"endpoint", node.endpoint},
        {"runtimeMode", node.runtimeMode},
        {"tags", stringListToJson(node.tags)},
        {"capabilities", toJson(node.capabilities)},
        {"activeTaskCount", node.activeTaskCount},
        {"queuedTaskCount", node.queuedTaskCount},
        {"maxConcurrencyHint", node.maxConcurrencyHint},
        {"weight", node.weight},
        {"online", node.online},
        {"endpointProbeSupported", node.endpointProbeSupported},
        {"endpointHealthChecked", node.endpointHealthChecked},
        {"endpointReachable", node.endpointReachable},
        {"endpointHealthError", node.endpointHealthError}
    };
}

NodeDescriptor nodeDescriptorFromJson(const QJsonObject &obj) {
    NodeDescriptor node;
    node.nodeId = obj.value("nodeId").toString();
    node.clusterId = obj.value("clusterId").toString(node.clusterId);
    node.displayName = obj.value("displayName").toString();
    node.role = obj.value("role").toString(node.role);
    node.endpoint = obj.value("endpoint").toString();
    node.runtimeMode = obj.value("runtimeMode").toString(node.runtimeMode);
    node.tags = stringListFromJson(obj.value("tags"));
    node.capabilities = nodeCapabilitiesFromJson(obj.value("capabilities"));
    node.activeTaskCount = obj.value("activeTaskCount").toInt(obj.value("active_task_count").toInt(node.activeTaskCount));
    node.queuedTaskCount = obj.value("queuedTaskCount").toInt(obj.value("queued_task_count").toInt(node.queuedTaskCount));
    node.maxConcurrencyHint = obj.value("maxConcurrencyHint").toInt(obj.value("max_concurrency_hint").toInt(node.maxConcurrencyHint));
    node.weight = obj.value("weight").toInt(node.weight);
    node.online = obj.value("online").toBool(node.online);
    node.endpointProbeSupported =
        obj.value("endpointProbeSupported").toBool(obj.value("endpoint_probe_supported").toBool(node.endpointProbeSupported));
    node.endpointHealthChecked =
        obj.value("endpointHealthChecked").toBool(obj.value("endpoint_health_checked").toBool(node.endpointHealthChecked));
    node.endpointReachable =
        obj.value("endpointReachable").toBool(obj.value("endpoint_reachable").toBool(node.endpointReachable));
    node.endpointHealthError =
        obj.value("endpointHealthError").toString(obj.value("endpoint_health_error").toString());
    return node;
}

QJsonArray toJson(const QList<NodeDescriptor> &nodes) {
    QJsonArray out;
    for (const NodeDescriptor &node : nodes) {
        out.append(toJson(node));
    }
    return out;
}

QList<NodeDescriptor> nodeDescriptorsFromJson(const QJsonValue &value) {
    QList<NodeDescriptor> out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (item.isObject()) {
            out.append(nodeDescriptorFromJson(item.toObject()));
        }
    }
    return out;
}

QJsonObject toJson(const TaskContextRef &ref) {
    return QJsonObject{
        {"store", ref.store},
        {"key", ref.key},
        {"kind", ref.kind},
        {"summary", ref.summary}
    };
}

TaskContextRef taskContextRefFromJson(const QJsonObject &obj) {
    TaskContextRef ref;
    ref.store = obj.value("store").toString();
    ref.key = obj.value("key").toString();
    ref.kind = obj.value("kind").toString();
    ref.summary = obj.value("summary").toString();
    return ref;
}

QJsonArray toJson(const QList<TaskContextRef> &refs) {
    QJsonArray out;
    for (const TaskContextRef &ref : refs) {
        out.append(toJson(ref));
    }
    return out;
}

QList<TaskContextRef> taskContextRefsFromJson(const QJsonValue &value) {
    QList<TaskContextRef> out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (item.isObject()) {
            out.append(taskContextRefFromJson(item.toObject()));
        }
    }
    return out;
}

QJsonObject toJson(const TaskEnvelope &task) {
    return QJsonObject{
        {"taskId", task.taskId},
        {"traceId", task.traceId},
        {"parentTaskId", task.parentTaskId},
        {"originNode", task.originNode},
        {"targetNode", task.targetNode},
        {"targetRole", task.targetRole},
        {"targetTags", stringListToJson(task.targetTags)},
        {"requiredTool", task.requiredTool},
        {"requiredChannel", task.requiredChannel},
        {"requiredMemoryBackend", task.requiredMemoryBackend},
        {"sceneKey", task.sceneKey},
        {"taskType", task.taskType},
        {"replyTo", task.replyTo},
        {"priority", task.priority},
        {"deadlineEpochMs", QString::number(task.deadlineEpochMs)},
        {"labels", stringListToJson(task.labels)},
        {"payload", task.payload},
        {"contextRefs", toJson(task.contextRefs)},
        {"createdAt", task.createdAt.toString(Qt::ISODate)}
    };
}

TaskEnvelope taskEnvelopeFromJson(const QJsonObject &obj) {
    TaskEnvelope task;
    task.taskId = obj.value("taskId").toString();
    task.traceId = obj.value("traceId").toString();
    task.parentTaskId = obj.value("parentTaskId").toString();
    task.originNode = obj.value("originNode").toString();
    task.targetNode = obj.value("targetNode").toString();
    task.targetRole = obj.value("targetRole").toString();
    task.targetTags = stringListFromJson(obj.value("targetTags").isUndefined() ? obj.value("target_tags") : obj.value("targetTags"));
    task.requiredTool = obj.value("requiredTool").toString(obj.value("required_tool").toString());
    task.requiredChannel = obj.value("requiredChannel").toString(obj.value("required_channel").toString());
    task.requiredMemoryBackend = obj.value("requiredMemoryBackend").toString(obj.value("required_memory_backend").toString());
    task.sceneKey = obj.value("sceneKey").toString();
    task.taskType = obj.value("taskType").toString();
    task.replyTo = obj.value("replyTo").toString();
    task.priority = obj.value("priority").toInt(task.priority);
    task.deadlineEpochMs = obj.value("deadlineEpochMs").toVariant().toLongLong();
    task.labels = stringListFromJson(obj.value("labels"));
    task.payload = obj.value("payload").toObject();
    task.contextRefs = taskContextRefsFromJson(obj.value("contextRefs"));
    task.createdAt = dateTimeFromJson(obj.value("createdAt"));
    return task;
}

QJsonArray toJson(const QList<TaskEnvelope> &tasks) {
    QJsonArray out;
    for (const TaskEnvelope &task : tasks) {
        out.append(toJson(task));
    }
    return out;
}

QList<TaskEnvelope> taskEnvelopesFromJson(const QJsonValue &value) {
    QList<TaskEnvelope> out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (item.isObject()) {
            out.append(taskEnvelopeFromJson(item.toObject()));
        }
    }
    return out;
}

QJsonObject toJson(const TaskResultEnvelope &result) {
    return QJsonObject{
        {"taskId", result.taskId},
        {"traceId", result.traceId},
        {"producerNode", result.producerNode},
        {"status", result.status},
        {"message", result.message},
        {"output", result.output},
        {"outputRefs", toJson(result.outputRefs)},
        {"error", result.error},
        {"finishedAt", result.finishedAt.toString(Qt::ISODate)}
    };
}

TaskResultEnvelope taskResultEnvelopeFromJson(const QJsonObject &obj) {
    TaskResultEnvelope result;
    result.taskId = obj.value("taskId").toString();
    result.traceId = obj.value("traceId").toString();
    result.producerNode = obj.value("producerNode").toString();
    result.status = obj.value("status").toString(result.status);
    result.message = obj.value("message").toString();
    result.output = obj.value("output").toObject();
    result.outputRefs = taskContextRefsFromJson(obj.value("outputRefs"));
    result.error = obj.value("error").toObject();
    result.finishedAt = dateTimeFromJson(obj.value("finishedAt"));
    return result;
}

QJsonArray toJson(const QList<TaskResultEnvelope> &results) {
    QJsonArray out;
    for (const TaskResultEnvelope &result : results) {
        out.append(toJson(result));
    }
    return out;
}

QList<TaskResultEnvelope> taskResultEnvelopesFromJson(const QJsonValue &value) {
    QList<TaskResultEnvelope> out;
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &item : array) {
        if (item.isObject()) {
            out.append(taskResultEnvelopeFromJson(item.toObject()));
        }
    }
    return out;
}

} // namespace yaos::distributed::json
