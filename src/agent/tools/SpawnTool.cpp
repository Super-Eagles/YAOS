#include "SpawnTool.h"

#include <QJsonArray>

namespace yaos::agent::tools {

SpawnTool::SpawnTool(runtime::SubagentManager &manager)
    : _manager(manager) {}

QString SpawnTool::name() const {
    return "spawn";
}

QString SpawnTool::description() const {
    return "派生一个异步运行的子智能体.";
}

QJsonObject SpawnTool::parameters() const {
    QJsonObject props;
    props["task"] = QJsonObject{{"type", "string"}, {"description", "子智能体要执行的具体任务"}};
    props["label"] = QJsonObject{{"type", "string"}, {"description", "短标签/显示名称"}};
    props["targetNode"] = QJsonObject{{"type", "string"}, {"description", "可选,指定目标节点 ID"}};
    props["targetRole"] = QJsonObject{{"type", "string"}, {"description", "可选,指定目标节点角色,例如 research / coding"}};
    props["targetTags"] = QJsonObject{{"type", "array"}, {"description", "可选,指定目标节点标签,例如 gpu, research"}, {"items", QJsonObject{{"type", "string"}}}};
    props["requiredTool"] = QJsonObject{{"type", "string"}, {"description", "可选,要求目标节点具备的能力,例如 web / filesystem / exec"}};
    props["requiredChannel"] = QJsonObject{{"type", "string"}, {"description", "可选,要求目标节点支持的频道,例如 gui / telegram / slack"}};
    props["requiredMemoryBackend"] = QJsonObject{{"type", "string"}, {"description", "可选,要求目标节点匹配的记忆后端,例如 hybrid_local / hybrid_cluster"}};
    props["groupLabel"] = QJsonObject{{"type", "string"}, {"description", "可选,批量子任务的聚合标签"}};
    props["tasks"] = QJsonObject{
        {"type", "array"},
        {"description", "可选,批量子任务列表.传入后会并发启动多个子任务,并在全部完成后聚合回报."},
        {"items", QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"task", QJsonObject{{"type", "string"}}},
                {"label", QJsonObject{{"type", "string"}}},
                {"targetNode", QJsonObject{{"type", "string"}}},
                {"targetRole", QJsonObject{{"type", "string"}}},
                {"targetTags", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}}},
                {"requiredTool", QJsonObject{{"type", "string"}}},
                {"requiredChannel", QJsonObject{{"type", "string"}}},
                {"requiredMemoryBackend", QJsonObject{{"type", "string"}}}
            }},
            {"required", QJsonArray{"task"}}
        }}
    };
    return QJsonObject{
        {"type", "object"},
        {"properties", props},
        {"required", QJsonArray()}
    };
}

void SpawnTool::setContext(const QString &channel,
                           const QString &chatId,
                           const QString &parentTaskId,
                           const QString &traceId) {
    _originChannel = channel;
    _originChatId = chatId;
    _sessionKey = channel + ":" + chatId;
    _parentTaskId = parentTaskId.trimmed();
    _traceId = traceId.trimmed();
}

runtime::SubagentManager::SpawnRequest SpawnTool::parseRequest(const QJsonObject &obj) const {
    runtime::SubagentManager::SpawnRequest request;
    request.task = obj.value("task").toString();
    request.label = obj.value("label").toString();
    request.targetNode = obj.value("targetNode").toString(obj.value("target_node").toString());
    request.targetRole = obj.value("targetRole").toString(obj.value("target_role").toString());
    const QJsonArray targetTags = obj.value("targetTags").toArray(obj.value("target_tags").toArray());
    for (const QJsonValue &value : targetTags) {
        const QString tag = value.toString().trimmed();
        if (!tag.isEmpty()) {
            request.targetTags.append(tag);
        }
    }
    request.requiredTool = obj.value("requiredTool").toString(obj.value("required_tool").toString()).trimmed();
    request.requiredChannel = obj.value("requiredChannel").toString(obj.value("required_channel").toString()).trimmed();
    request.requiredMemoryBackend = obj.value("requiredMemoryBackend").toString(obj.value("required_memory_backend").toString()).trimmed();
    return request;
}

QString SpawnTool::execute(const QJsonObject &params) {
    const runtime::SubagentManager::SpawnRequest defaults = parseRequest(params);
    QList<runtime::SubagentManager::SpawnRequest> requests;
    const QJsonArray tasks = params.value("tasks").toArray();
    for (const QJsonValue &value : tasks) {
        if (value.isObject()) {
            requests.append(parseRequest(value.toObject()));
        }
    }

    if (!requests.isEmpty()) {
        const QString groupLabel = params.value("groupLabel").toString(params.value("group_label").toString());
        return _manager.spawnMany(
            requests,
            groupLabel,
            _originChannel,
            _originChatId,
            _sessionKey,
            _parentTaskId,
            _traceId,
            defaults.targetNode,
            defaults.targetRole,
            defaults.targetTags,
            defaults.requiredTool,
            defaults.requiredChannel,
            defaults.requiredMemoryBackend
        );
    }

    return _manager.spawn(
        defaults.task,
        defaults.label,
        _originChannel,
        _originChatId,
        _sessionKey,
        defaults.targetNode,
        defaults.targetRole,
        _parentTaskId,
        _traceId,
        defaults.targetTags,
        defaults.requiredTool,
        defaults.requiredChannel,
        defaults.requiredMemoryBackend
    );
}

} // namespace yaos::agent::tools
