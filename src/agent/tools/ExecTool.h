#ifndef YAOS_AGENT_TOOLS_EXECTOOL_H
#define YAOS_AGENT_TOOLS_EXECTOOL_H

#include "../Tool.h"

namespace yaos::agent::tools {

class ExecTool : public Tool {
public:
    ExecTool(QString workingDir,
             int timeoutSec = 60,
             QString pathAppend = QString(),
             QString allowedDir = QString());

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

private:
    QString _workingDir;
    int _timeoutSec;
    QString _pathAppend;
    QString _allowedDir;
};

} // namespace yaos::agent::tools

#endif // YAOS_AGENT_TOOLS_EXECTOOL_H
