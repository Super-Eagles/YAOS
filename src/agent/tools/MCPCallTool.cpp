#include "MCPCallTool.h"

#include <QJsonArray>

namespace yaos::agent::tools {

MCPCallTool::MCPCallTool(runtime::MCPManager &manager)
    : _manager(manager) {}

QString MCPCallTool::name() const {
    return "mcp_call";
}

QString MCPCallTool::description() const {
    return "调用已配置的 MCP 服务器工具.";
}

QJsonObject MCPCallTool::parameters() const {
    QJsonObject props;
    props["server"] = QJsonObject{{"type", "string"}, {"description", "MCP 服务器名称"}};
    props["tool"] = QJsonObject{{"type", "string"}, {"description", "工具名称"}};
    props["arguments"] = QJsonObject{
        {"type", "object"},
        {"description", "传递给工具的参数"}
    };

    return QJsonObject{
        {"type", "object"},
        {"properties", props},
        {"required", QJsonArray{"server", "tool"}}
    };
}

QString MCPCallTool::execute(const QJsonObject &params) {
    const QString server = params.value("server").toString().trimmed();
    const QString tool = params.value("tool").toString().trimmed();
    const QJsonObject arguments = params.value("arguments").toObject();

    if (server.isEmpty()) {
        return "错误：服务器名称不能为空.";
    }
    if (tool.isEmpty()) {
        return "错误：工具名称不能为空.";
    }

    const runtime::MCPManager::CallResult res = _manager.call(server, tool, arguments);
    if (res.ok) {
        return res.output;
    }
    return "MCP 调用失败: " + res.output;
}

} // namespace yaos::agent::tools
