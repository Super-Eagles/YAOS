#include "CronTool.h"

#include <QDateTime>
#include <QJsonArray>
#include <QTimeZone>

namespace yaos::agent::tools {

CronTool::CronTool(runtime::CronService &cronService)
    : _cron(cronService) {}

QString CronTool::name() const {
    return "cron";
}

QString CronTool::description() const {
    return "管理和计划定时任务.";
}

QJsonObject CronTool::parameters() const {
    QJsonObject props;
    props["action"] = QJsonObject{
        {"type", "string"},
        {"enum", QJsonArray{"add", "list", "remove"}},
        {"description", "操作类型"}
    };
    props["name"] = QJsonObject{{"type", "string"}, {"description", "任务名称 (用于 add)"}};
    props["schedule_kind"] = QJsonObject{
        {"type", "string"},
        {"enum", QJsonArray{"every", "at", "cron"}},
        {"description", "调度类型"}
    };
    props["schedule_value"] = QJsonObject{{"type", "string"}, {"description", "调度值 (ms 或 cron 表达式)"}};
    props["message"] = QJsonObject{{"type", "string"}, {"description", "触发时发送的消息"}};
    props["job_id"] = QJsonObject{{"type", "string"}, {"description", "任务 ID (用于 remove)"}};

    return QJsonObject{
        {"type", "object"},
        {"properties", props},
        {"required", QJsonArray{"action"}}
    };
}

void CronTool::setContext(const QString &channel, const QString &chatId) {
    _channel = channel;
    _chatId = chatId;
}

bool CronTool::setCronContext(bool active) {
    const bool previous = _inCronContext;
    _inCronContext = active;
    return previous;
}

QString CronTool::addJob(const QJsonObject &params) {
    if (_inCronContext) {
        return "错误: 无法在 cron 任务执行期间调度新任务";
    }

    const QString message = params.value("message").toString().trimmed();
    if (message.isEmpty()) {
        return "错误: 'message' 参数是 add 操作必需的";
    }
    if (_channel.isEmpty() || _chatId.isEmpty()) {
        return "错误: 缺少会话上下文 (channel/chat_id)";
    }

    const QString name = params.value("name").toString().trimmed();
    const QString scheduleKind = params.value("schedule_kind").toString().trimmed();
    const QString scheduleValue = params.value("schedule_value").toString().trimmed();

    runtime::CronSchedule schedule;
    bool deleteAfterRun = false;

    if (scheduleKind == "every") {
        bool ok;
        qint64 everyMs = scheduleValue.toLongLong(&ok);
        if (!ok || everyMs <= 0) {
            return "错误: 'every' 调度类型需要一个正整数毫秒值作为 'schedule_value'";
        }
        schedule.kind = "every";
        schedule.everyMs = everyMs;
    } else if (scheduleKind == "cron") {
        if (scheduleValue.isEmpty()) {
            return "错误: 'cron' 调度类型需要一个 cron 表达式作为 'schedule_value'";
        }
        // The original code had 'tz' as a separate parameter.
        // The new parameters don't include 'tz'.
        // For now, I'll assume 'tz' is not supported with the new schema,
        // or it should be part of schedule_value if needed.
        // Since the instruction only provided the new parameters and not the logic,
        // I'll keep the 'tz' field in the schedule struct but it won't be set here.
        schedule.kind = "cron";
        schedule.expr = scheduleValue;
    } else if (scheduleKind == "at") {
        const QDateTime dt = QDateTime::fromString(scheduleValue, Qt::ISODate);
        if (!dt.isValid()) {
            return QString("错误: 无效的 ISO 日期时间格式 '%1'.期望格式为 YYYY-MM-DDTHH:MM:SS").arg(scheduleValue);
        }
        schedule.kind = "at";
        schedule.atMs = dt.toMSecsSinceEpoch();
        deleteAfterRun = true;
    } else {
        return "错误: 'schedule_kind' 必须是 'every', 'at' 或 'cron'";
    }

    const runtime::CronJob job = _cron.addJob(
        name.isEmpty() ? message.left(30) : name, // Use provided name, or message prefix
        schedule,
        message,
        true,
        _channel,
        _chatId,
        deleteAfterRun
    );
    if (job.id == "error") {
        return "添加任务失败: " + job.name;
    }
    return QString("成功创建任务 '%1' (ID: %2)").arg(job.name, job.id);
}

QString CronTool::listJobs() {
    const QVector<runtime::CronJob> jobs = _cron.listJobs();
    if (jobs.isEmpty()) {
        return "没有计划中的任务.";
    }

    QStringList lines;
    lines << "计划任务列表:";
    for (const runtime::CronJob &job : jobs) {
        lines << QString("- %1 (ID: %2, 模式: %3)").arg(job.name, job.id, job.schedule.kind);
    }
    return lines.join("\n");
}

QString CronTool::removeJob(const QJsonObject &params) {
    const QString jobId = params.value("job_id").toString().trimmed();
    if (jobId.isEmpty()) {
        return "错误: remove 操作需要 'job_id' 参数";
    }
    if (_cron.removeJob(jobId)) {
        return QString("已成功移除任务 %1").arg(jobId);
    }
    return QString("未找到 ID 为 %1 的任务").arg(jobId);
}

QString CronTool::execute(const QJsonObject &params) {
    const QString action = params.value("action").toString().trimmed().toLower();
    if (action == "add") {
        return addJob(params);
    }
    if (action == "list") {
        return listJobs();
    }
    if (action == "remove") {
        return removeJob(params);
    }
    return "Error: unknown action: " + action;
}

} // namespace yaos::agent::tools
