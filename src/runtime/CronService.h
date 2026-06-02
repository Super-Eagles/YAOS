#ifndef YAOS_RUNTIME_CRONSERVICE_H
#define YAOS_RUNTIME_CRONSERVICE_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <functional>

namespace yaos::runtime {

struct CronSchedule {
    QString kind = "every"; // at, every, cron
    qint64 atMs = -1;
    qint64 everyMs = -1;
    QString expr;
    QString tz;
};

struct CronPayload {
    QString kind = "agent_turn";
    QString message;
    bool deliver = false;
    QString channel;
    QString to;
};

struct CronJobState {
    qint64 nextRunAtMs = -1;
    qint64 lastRunAtMs = -1;
    QString lastStatus;
    QString lastError;
};

struct CronJob {
    QString id;
    QString name;
    bool enabled = true;
    CronSchedule schedule;
    CronPayload payload;
    CronJobState state;
    qint64 createdAtMs = 0;
    qint64 updatedAtMs = 0;
    bool deleteAfterRun = false;
};

struct CronStatus {
    bool enabled = false;
    int jobs = 0;
    qint64 nextWakeAtMs = -1;
};

class CronService : public QObject {
    Q_OBJECT
public:
    explicit CronService(const QString &storePath, QObject *parent = nullptr);

    void start();
    void stop();
    bool isRunning() const;

    QVector<CronJob> listJobs(bool includeDisabled = false);
    CronJob addJob(
        const QString &name,
        const CronSchedule &schedule,
        const QString &message,
        bool deliver = false,
        const QString &channel = QString(),
        const QString &to = QString(),
        bool deleteAfterRun = false,
        const QString &payloadKind = QStringLiteral("agent_turn")
    );
    bool removeJob(const QString &jobId);
    CronJob enableJob(const QString &jobId, bool enabled = true, bool *ok = nullptr);
    bool runJob(const QString &jobId, bool force = false);
    CronStatus status();

    void setOnJobCallback(const std::function<QString(const CronJob &)> &callback);

private slots:
    void onTimer();

private:
    void loadStore();
    void saveStore();
    void recomputeNextRuns();
    qint64 getNextWakeMs() const;
    void armTimer();
    void executeJob(int index);

    static qint64 nowMs();
    static qint64 computeNextRun(const CronSchedule &schedule, qint64 nowMs);
    static bool validateScheduleForAdd(const CronSchedule &schedule, QString *error = nullptr);

private:
    QString _storePath;
    QVector<CronJob> _jobs;
    int _version = 1;
    bool _running = false;
    bool _loaded = false;
    QDateTime _lastMtimeUtc;
    QTimer *_timer = nullptr;
    std::function<QString(const CronJob &)> _onJob;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_CRONSERVICE_H
