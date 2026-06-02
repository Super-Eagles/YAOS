#ifndef YAOS_RUNTIME_HEARTBEATSERVICE_H
#define YAOS_RUNTIME_HEARTBEATSERVICE_H

#include <memory>

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

#include "../config/Config.h"
#include "../providers/LLMProvider.h"

namespace yaos::runtime {

class HeartbeatService : public QObject {
    Q_OBJECT
public:
    struct Decision {
        QString action = "skip"; // skip or run
        QString tasks;
    };

    // Own a provider instance so heartbeat work does not share mutable provider state
    // with AgentLoop across threads.
    HeartbeatService(
        const QString &workspace,
        const config::Config &config,
        QObject *parent = nullptr
    );

    void setIntervalSeconds(int intervalS);
    void setEnabled(bool enabled);
    void setOnExecute(const std::function<QString(const QString &)> &callback);
    void setOnNotify(const std::function<void(const QString &)> &callback);

    void start();
    void stop();
    bool isRunning() const;
    QString triggerNow();

private slots:
    void tick();

private:
    QString heartbeatFilePath() const;
    QString readHeartbeatFile() const;
    Decision decide(const QString &content) const;
    Decision fallbackDecision(const QString &content) const;

    QString _workspace;
    std::unique_ptr<providers::LLMProvider> _provider;
    QString _model;
    int _intervalS = 30 * 60;
    bool _enabled = true;
    bool _running = false;
    QTimer *_timer = nullptr;
    std::function<QString(const QString &)> _onExecute;
    std::function<void(const QString &)> _onNotify;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_HEARTBEATSERVICE_H
