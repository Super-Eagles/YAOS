#ifndef YAOS_AGENT_TOOLS_PLUGINTOOL_H
#define YAOS_AGENT_TOOLS_PLUGINTOOL_H

#include "../Tool.h"
#include "../../config/Config.h"
#include "../../runtime/PluginRegistry.h"

namespace yaos::agent::tools {

class PluginTool : public Tool {
public:
    PluginTool(QString workspace,
               config::Config config,
               runtime::PluginRecord record);

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

private:
    QString executePromptPlugin(const QJsonObject &params) const;
    QString executeCommandPlugin(const QJsonObject &params) const;
    QString effectiveToolName() const;
    config::ExtensionProfileConfig profile() const;

private:
    QString _workspace;
    config::Config _config;
    runtime::PluginRecord _record;
};

} // namespace yaos::agent::tools

#endif // YAOS_AGENT_TOOLS_PLUGINTOOL_H
