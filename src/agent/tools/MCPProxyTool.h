#ifndef YAOS_AGENT_TOOLS_MCPPROXYTOOL_H
#define YAOS_AGENT_TOOLS_MCPPROXYTOOL_H

#include "../Tool.h"
#include "../../runtime/MCPManager.h"

namespace yaos::agent::tools {

class MCPProxyTool : public Tool {
public:
    MCPProxyTool(runtime::MCPManager &manager,
                 runtime::MCPRemoteTool tool);

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

private:
    runtime::MCPManager &_manager;
    runtime::MCPRemoteTool _tool;
};

} // namespace yaos::agent::tools

#endif // YAOS_AGENT_TOOLS_MCPPROXYTOOL_H
