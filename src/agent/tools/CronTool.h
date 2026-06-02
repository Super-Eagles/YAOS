#ifndef YAOS_AGENT_TOOLS_CRONTOOL_H
#define YAOS_AGENT_TOOLS_CRONTOOL_H

#include "../Tool.h"
#include "../../runtime/CronService.h"

namespace yaos::agent::tools {

class CronTool : public Tool {
public:
    explicit CronTool(runtime::CronService &cronService);

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

    void setContext(const QString &channel, const QString &chatId);
    bool setCronContext(bool active);

private:
    QString addJob(const QJsonObject &params);
    QString listJobs();
    QString removeJob(const QJsonObject &params);

private:
    runtime::CronService &_cron;
    QString _channel;
    QString _chatId;
    bool _inCronContext = false;
};

} // namespace yaos::agent::tools

#endif // YAOS_AGENT_TOOLS_CRONTOOL_H
