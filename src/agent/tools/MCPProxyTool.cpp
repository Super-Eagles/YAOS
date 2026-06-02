#include "MCPProxyTool.h"

#include <QRegularExpression>
#include <utility>

namespace yaos::agent::tools {

namespace {

QString sanitizeToolName(const QString &serverName, const QString &toolName) {
    QString name = QString("mcp_%1_%2").arg(serverName, toolName).toLower();
    name.replace(QRegularExpression("[^a-z0-9_]+"), "_");
    name.replace(QRegularExpression("_+"), "_");
    name.remove(QRegularExpression("^_+|_+$"));
    if (name.size() > 56) {
        const QString hash = QString::number(qHash(serverName + ":" + toolName), 16);
        name = name.left(47) + "_" + hash.left(8);
    }
    return name;
}

} // namespace

MCPProxyTool::MCPProxyTool(runtime::MCPManager &manager, runtime::MCPRemoteTool tool)
    : _manager(manager), _tool(std::move(tool)) {}

QString MCPProxyTool::name() const {
    return sanitizeToolName(_tool.serverName, _tool.name);
}

QString MCPProxyTool::description() const {
    QString text = _tool.description.trimmed();
    if (text.isEmpty()) {
        text = QString("Call MCP tool '%1' on server '%2'.").arg(_tool.name, _tool.serverName);
    }
    return text;
}

QJsonObject MCPProxyTool::parameters() const {
    if (_tool.inputSchema.value("type").toString() == "object") {
        return _tool.inputSchema;
    }
    return QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{}},
        {"additionalProperties", true}
    };
}

QString MCPProxyTool::execute(const QJsonObject &params) {
    const runtime::MCPManager::CallResult res = _manager.call(_tool.serverName, _tool.name, params);
    return res.ok ? res.output : QString("MCP 调用失败: %1").arg(res.output);
}

} // namespace yaos::agent::tools
