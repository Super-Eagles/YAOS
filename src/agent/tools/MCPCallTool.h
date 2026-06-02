#ifndef YAOS_AGENT_TOOLS_MCPCALLTOOL_H
#define YAOS_AGENT_TOOLS_MCPCALLTOOL_H

#include "../Tool.h"
#include "../../runtime/MCPManager.h"

namespace yaos::agent::tools {

class MCPCallTool : public Tool {
public:
    explicit MCPCallTool(runtime::MCPManager &manager);

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

private:
    runtime::MCPManager &_manager;
};

} // namespace yaos::agent::tools

#endif // YAOS_AGENT_TOOLS_MCPCALLTOOL_H
