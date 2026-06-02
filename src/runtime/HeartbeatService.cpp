#include "HeartbeatService.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

#include "../providers/ProviderFactory.h"

namespace yaos::runtime {

HeartbeatService::HeartbeatService(
    const QString &workspace,
    const config::Config &config,
    QObject *parent
) : QObject(parent),
    _workspace(workspace),
    _provider(providers::ProviderFactory::create(config)), // ✅ 专属实例，在主线程创建和使用
    _model(config.agentDefaults.model),
    _timer(new QTimer(this)) {
    connect(_timer, &QTimer::timeout, this, &HeartbeatService::tick);
}

void HeartbeatService::setIntervalSeconds(int intervalS) {
    _intervalS = qMax(1, intervalS);
    if (_timer->isActive()) {
        _timer->start(_intervalS * 1000);
    }
}

void HeartbeatService::setEnabled(bool enabled) {
    _enabled = enabled;
    if (!enabled) {
        stop();
    }
}

void HeartbeatService::setOnExecute(const std::function<QString(const QString &)> &callback) {
    _onExecute = callback;
}

void HeartbeatService::setOnNotify(const std::function<void(const QString &)> &callback) {
    _onNotify = callback;
}

void HeartbeatService::start() {
    if (!_enabled || _running) {
        return;
    }
    _running = true;
    _timer->start(_intervalS * 1000);
}

void HeartbeatService::stop() {
    _running = false;
    _timer->stop();
}

bool HeartbeatService::isRunning() const {
    return _running;
}

QString HeartbeatService::triggerNow() {
    const QString content = readHeartbeatFile();
    if (content.trimmed().isEmpty()) {
        return QString();
    }
    const Decision d = decide(content);
    if (d.action != "run" || !_onExecute) {
        return QString();
    }
    return _onExecute(d.tasks);
}

QString HeartbeatService::heartbeatFilePath() const {
    return QDir(_workspace).filePath("HEARTBEAT.md");
}

QString HeartbeatService::readHeartbeatFile() const {
    QFile file(heartbeatFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    return content;
}

HeartbeatService::Decision HeartbeatService::fallbackDecision(const QString &content) const {
    Decision d;
    const QStringList lines = content.split('\n');
    QStringList tasks;
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (t.startsWith("- [ ]")) {
            tasks.append(t.mid(5).trimmed());
        }
    }
    if (!tasks.isEmpty()) {
        d.action = "run";
        d.tasks = tasks.join("\n");
    }
    return d;
}

HeartbeatService::Decision HeartbeatService::decide(const QString &content) const {
    QJsonObject toolParams;
    QJsonObject props;
    props["action"] = QJsonObject{
        {"type", "string"},
        {"enum", QJsonArray{"skip", "run"}}
    };
    props["tasks"] = QJsonObject{
        {"type", "string"}
    };
    toolParams["type"] = "object";
    toolParams["properties"] = props;
    toolParams["required"] = QJsonArray{"action"};

    QJsonObject fn;
    fn["name"] = "heartbeat";
    fn["description"] = "Report heartbeat decision after reviewing tasks.";
    fn["parameters"] = toolParams;

    QJsonArray tools;
    tools.append(QJsonObject{
        {"type", "function"},
        {"function", fn}
    });

    QJsonArray messages;
    messages.append(QJsonObject{
        {"role", "system"},
        {"content", "You are a heartbeat agent. Call the heartbeat tool to report your decision."}
    });
    messages.append(QJsonObject{
        {"role", "user"},
        {"content", QString("Review HEARTBEAT.md and decide if there are active tasks.\n\n%1").arg(content)}
    });

    const auto response = _provider->chat(
        messages,
        tools,
        _model,
        0.0,
        512
    );

    if (!response.hasToolCalls()) {
        return fallbackDecision(content);
    }

    const auto call = response.toolCalls.first();
    Decision d;
    d.action = call.arguments.value("action").toString("skip");
    d.tasks = call.arguments.value("tasks").toString();
    if (d.action != "run") {
        d.action = "skip";
    }
    if (d.action == "run" && d.tasks.trimmed().isEmpty()) {
        return fallbackDecision(content);
    }
    return d;
}

void HeartbeatService::tick() {
    if (!_running) {
        return;
    }

    const QString content = readHeartbeatFile();
    if (content.trimmed().isEmpty()) {
        return;
    }

    const Decision d = decide(content);
    if (d.action != "run" || !_onExecute) {
        return;
    }

    const QString response = _onExecute(d.tasks);
    if (!response.trimmed().isEmpty() && _onNotify) {
        _onNotify(response);
    }
}

} // namespace yaos::runtime
