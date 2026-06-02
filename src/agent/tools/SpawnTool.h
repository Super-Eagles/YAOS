#ifndef YAOS_AGENT_TOOLS_SPAWNTOOL_H
#define YAOS_AGENT_TOOLS_SPAWNTOOL_H

#include "../Tool.h"
#include "../../runtime/SubagentManager.h"

namespace yaos::agent::tools {

class SpawnTool : public Tool {
public:
    explicit SpawnTool(runtime::SubagentManager &manager);

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

    void setContext(const QString &channel,
                    const QString &chatId,
                    const QString &parentTaskId = QString(),
                    const QString &traceId = QString());

private:
    runtime::SubagentManager::SpawnRequest parseRequest(const QJsonObject &obj) const;
    runtime::SubagentManager &_manager;
    QString _originChannel = "cli";
    QString _originChatId = "direct";
    QString _sessionKey = "cli:direct";
    QString _parentTaskId;
    QString _traceId;
};

} // namespace yaos::agent::tools

#endif // YAOS_AGENT_TOOLS_SPAWNTOOL_H
