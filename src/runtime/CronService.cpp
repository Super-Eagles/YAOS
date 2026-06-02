#include "CronService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <exception>
#include <limits>

namespace yaos::runtime {

namespace {

QStringList splitCronFields(const QString &expr) {
    return expr.simplified().split(' ', Qt::SkipEmptyParts);
}

bool parseInt(const QString &text, int *out) {
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok) {
        return false;
    }
    *out = value;
    return true;
}

int normalizeDow(int value) {
    if (value == 7) {
        return 0;
    }
    return value;
}

int qtDowToCron(int qtDow) {
    // Qt: Monday=1 ... Sunday=7, cron: Sunday=0 (or 7) ... Saturday=6
    return qtDow == 7 ? 0 : qtDow;
}

bool tokenMatches(const QString &rawToken, int value, int minVal, int maxVal, bool dayOfWeekField) {
    QString token = rawToken.trimmed();
    if (token.isEmpty()) {
        return false;
    }
    if (token == "*") {
        return true;
    }

    int step = 1;
    if (token.contains('/')) {
        const QStringList parts = token.split('/', Qt::KeepEmptyParts);
        if (parts.size() != 2) {
            return false;
        }
        bool ok = false;
        step = parts.at(1).toInt(&ok);
        if (!ok || step <= 0) {
            return false;
        }
        token = parts.at(0).trimmed();
        if (token.isEmpty()) {
            token = "*";
        }
    }

    int start = minVal;
    int end = maxVal;
    if (token != "*") {
        if (token.contains('-')) {
            const QStringList range = token.split('-', Qt::KeepEmptyParts);
            if (range.size() != 2) {
                return false;
            }
            if (!parseInt(range.at(0), &start) || !parseInt(range.at(1), &end)) {
                return false;
            }
        } else {
            if (!parseInt(token, &start)) {
                return false;
            }
            end = start;
        }
    }

    if (dayOfWeekField) {
        start = normalizeDow(start);
        end = normalizeDow(end);
        value = normalizeDow(value);
    }

    if (start < minVal || end < minVal || start > maxVal || end > maxVal) {
        return false;
    }

    // Sunday wrap for DOW, e.g. 5-1.
    if (dayOfWeekField && start > end) {
        const bool inWrapped = value >= start || value <= end;
        if (!inWrapped) {
            return false;
        }
        const int span = (maxVal - start + 1) + (end - minVal + 1);
        if (step <= 1) {
            return true;
        }
        int offset = value >= start ? (value - start) : ((maxVal - start + 1) + (value - minVal));
        if (offset < 0 || offset >= span) {
            return false;
        }
        return (offset % step) == 0;
    }

    if (value < start || value > end) {
        return false;
    }
    if (step <= 1) {
        return true;
    }
    return ((value - start) % step) == 0;
}

bool fieldMatches(const QString &fieldExpr, int value, int minVal, int maxVal, bool dayOfWeekField = false) {
    const QStringList tokens = fieldExpr.split(',', Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        if (tokenMatches(token, value, minVal, maxVal, dayOfWeekField)) {
            return true;
        }
    }
    return false;
}

bool cronMatches(const QString &expr, const QDateTime &dt) {
    const QStringList parts = splitCronFields(expr);
    if (parts.size() != 5) {
        return false;
    }

    const int minute = dt.time().minute();
    const int hour = dt.time().hour();
    const int dom = dt.date().day();
    const int month = dt.date().month();
    const int dow = qtDowToCron(dt.date().dayOfWeek());

    const bool minuteMatch = fieldMatches(parts.at(0), minute, 0, 59);
    const bool hourMatch = fieldMatches(parts.at(1), hour, 0, 23);
    const bool domMatch = fieldMatches(parts.at(2), dom, 1, 31);
    const bool monthMatch = fieldMatches(parts.at(3), month, 1, 12);
    const bool dowMatch = fieldMatches(parts.at(4), dow, 0, 7, true);

    const bool domAny = parts.at(2).trimmed() == "*";
    const bool dowAny = parts.at(4).trimmed() == "*";
    const bool dayMatch = (!domAny && !dowAny) ? (domMatch || dowMatch) : (domMatch && dowMatch);

    return minuteMatch && hourMatch && monthMatch && dayMatch;
}

} // namespace

CronService::CronService(const QString &storePath, QObject *parent)
    : QObject(parent),
      _storePath(storePath),
      _timer(new QTimer(this)) {
    _timer->setSingleShot(true);
    connect(_timer, &QTimer::timeout, this, &CronService::onTimer);
}

void CronService::start() {
    if (_running) {
        return;
    }
    _running = true;
    loadStore();
    recomputeNextRuns();
    saveStore();
    armTimer();
}

void CronService::stop() {
    _running = false;
    if (_timer) {
        _timer->stop();
    }
}

bool CronService::isRunning() const {
    return _running;
}

qint64 CronService::nowMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

bool CronService::validateScheduleForAdd(const CronSchedule &schedule, QString *error) {
    if (!schedule.tz.isEmpty() && schedule.kind != "cron") {
        if (error) *error = "tz can only be used with cron schedules";
        return false;
    }
    if (schedule.kind == "at") {
        if (schedule.atMs <= 0) {
            if (error) *error = "at schedule requires atMs";
            return false;
        }
        return true;
    }
    if (schedule.kind == "every") {
        if (schedule.everyMs <= 0) {
            if (error) *error = "every schedule requires everyMs > 0";
            return false;
        }
        return true;
    }
    if (schedule.kind == "cron") {
        if (splitCronFields(schedule.expr).size() != 5) {
            if (error) *error = "cron expression must have 5 fields";
            return false;
        }
        if (!schedule.tz.isEmpty()) {
            QTimeZone zone(schedule.tz.toUtf8());
            if (!zone.isValid()) {
                if (error) *error = "unknown timezone '" + schedule.tz + "'";
                return false;
            }
        }
        return true;
    }
    if (error) *error = "unknown schedule kind";
    return false;
}

qint64 CronService::computeNextRun(const CronSchedule &schedule, qint64 currentMs) {
    if (schedule.kind == "at") {
        return schedule.atMs > currentMs ? schedule.atMs : -1;
    }

    if (schedule.kind == "every") {
        if (schedule.everyMs <= 0) {
            return -1;
        }
        return currentMs + schedule.everyMs;
    }

    if (schedule.kind == "cron") {
        if (splitCronFields(schedule.expr).size() != 5) {
            return -1;
        }
        const QTimeZone zone = schedule.tz.isEmpty()
                               ? QTimeZone::systemTimeZone()
                               : QTimeZone(schedule.tz.toUtf8());
        if (!zone.isValid()) {
            return -1;
        }

        QDateTime candidate = QDateTime::fromMSecsSinceEpoch(currentMs, zone);
        candidate = candidate.addSecs(60);
        candidate.setTime(QTime(candidate.time().hour(), candidate.time().minute(), 0));

        const int maxMinutes = 366 * 24 * 60;
        for (int i = 0; i < maxMinutes; ++i) {
            const QDateTime probe = candidate.addSecs(i * 60);
            if (cronMatches(schedule.expr, probe)) {
                return probe.toMSecsSinceEpoch();
            }
        }
    }

    return -1;
}

void CronService::loadStore() {
    const QFileInfo info(_storePath);
    if (_loaded && info.exists()) {
        const QDateTime mtime = info.lastModified().toUTC();
        if (_lastMtimeUtc.isValid() && mtime == _lastMtimeUtc) {
            return;
        }
    }

    _jobs.clear();
    _version = 1;

    QFile file(_storePath);
    if (!file.exists()) {
        _loaded = true;
        _lastMtimeUtc = QDateTime();
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        _loaded = true;
        _lastMtimeUtc = info.lastModified().toUTC();
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        _loaded = true;
        _lastMtimeUtc = info.lastModified().toUTC();
        return;
    }

    const QJsonObject root = doc.object();
    _version = root.value("version").toInt(1);
    const QJsonArray jobs = root.value("jobs").toArray();
    for (const QJsonValue &v : jobs) {
        const QJsonObject j = v.toObject();
        CronJob job;
        job.id = j.value("id").toString();
        job.name = j.value("name").toString();
        job.enabled = j.value("enabled").toBool(true);
        const QJsonObject s = j.value("schedule").toObject();
        job.schedule.kind = s.value("kind").toString("every");
        job.schedule.atMs = s.value("atMs").toVariant().toLongLong();
        job.schedule.everyMs = s.value("everyMs").toVariant().toLongLong();
        job.schedule.expr = s.value("expr").toString();
        job.schedule.tz = s.value("tz").toString();

        const QJsonObject p = j.value("payload").toObject();
        job.payload.kind = p.value("kind").toString("agent_turn");
        job.payload.message = p.value("message").toString();
        job.payload.deliver = p.value("deliver").toBool(false);
        job.payload.channel = p.value("channel").toString();
        job.payload.to = p.value("to").toString();

        const QJsonObject st = j.value("state").toObject();
        job.state.nextRunAtMs = st.value("nextRunAtMs").toVariant().toLongLong();
        job.state.lastRunAtMs = st.value("lastRunAtMs").toVariant().toLongLong();
        job.state.lastStatus = st.value("lastStatus").toString();
        job.state.lastError = st.value("lastError").toString();

        job.createdAtMs = j.value("createdAtMs").toVariant().toLongLong();
        job.updatedAtMs = j.value("updatedAtMs").toVariant().toLongLong();
        job.deleteAfterRun = j.value("deleteAfterRun").toBool(false);

        if (!job.id.isEmpty()) {
            _jobs.append(job);
        }
    }

    _loaded = true;
    _lastMtimeUtc = info.lastModified().toUTC();
}

void CronService::saveStore() {
    const QFileInfo info(_storePath);
    QDir().mkpath(info.absolutePath());

    QJsonArray jobs;
    for (const CronJob &job : _jobs) {
        QJsonObject j;
        j["id"] = job.id;
        j["name"] = job.name;
        j["enabled"] = job.enabled;
        j["schedule"] = QJsonObject{
            {"kind", job.schedule.kind},
            {"atMs", static_cast<double>(job.schedule.atMs)},
            {"everyMs", static_cast<double>(job.schedule.everyMs)},
            {"expr", job.schedule.expr},
            {"tz", job.schedule.tz}
        };
        j["payload"] = QJsonObject{
            {"kind", job.payload.kind},
            {"message", job.payload.message},
            {"deliver", job.payload.deliver},
            {"channel", job.payload.channel},
            {"to", job.payload.to}
        };
        j["state"] = QJsonObject{
            {"nextRunAtMs", static_cast<double>(job.state.nextRunAtMs)},
            {"lastRunAtMs", static_cast<double>(job.state.lastRunAtMs)},
            {"lastStatus", job.state.lastStatus},
            {"lastError", job.state.lastError}
        };
        j["createdAtMs"] = static_cast<double>(job.createdAtMs);
        j["updatedAtMs"] = static_cast<double>(job.updatedAtMs);
        j["deleteAfterRun"] = job.deleteAfterRun;
        jobs.append(j);
    }

    QJsonObject root;
    root["version"] = _version;
    root["jobs"] = jobs;

    QFile file(_storePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    _lastMtimeUtc = QFileInfo(_storePath).lastModified().toUTC();
}

void CronService::recomputeNextRuns() {
    const qint64 current = nowMs();
    for (CronJob &job : _jobs) {
        if (job.enabled) {
            job.state.nextRunAtMs = computeNextRun(job.schedule, current);
        }
    }
}

qint64 CronService::getNextWakeMs() const {
    qint64 nextWake = -1;
    for (const CronJob &job : _jobs) {
        if (!job.enabled || job.state.nextRunAtMs <= 0) {
            continue;
        }
        if (nextWake < 0 || job.state.nextRunAtMs < nextWake) {
            nextWake = job.state.nextRunAtMs;
        }
    }
    return nextWake;
}

void CronService::armTimer() {
    if (!_running || !_timer) {
        return;
    }
    _timer->stop();

    const qint64 nextWake = getNextWakeMs();
    if (nextWake <= 0) {
        return;
    }

    const qint64 delayMs = std::max<qint64>(0, nextWake - nowMs());
    const int startMs = delayMs > std::numeric_limits<int>::max()
                        ? std::numeric_limits<int>::max()
                        : static_cast<int>(delayMs);
    _timer->start(startMs);
}

void CronService::executeJob(int index) {
    if (index < 0 || index >= _jobs.size()) {
        return;
    }

    CronJob &job = _jobs[index];
    const qint64 startedAt = nowMs();

    try {
        if (_onJob) {
            _onJob(job);
        }
        job.state.lastStatus = "ok";
        job.state.lastError.clear();
    } catch (const std::exception &e) {
        job.state.lastStatus = "error";
        job.state.lastError = QString::fromUtf8(e.what());
    } catch (...) {
        job.state.lastStatus = "error";
        job.state.lastError = "unknown error";
    }

    job.state.lastRunAtMs = startedAt;
    job.updatedAtMs = nowMs();

    if (job.schedule.kind == "at") {
        if (job.deleteAfterRun) {
            _jobs.remove(index);
        } else {
            job.enabled = false;
            job.state.nextRunAtMs = -1;
        }
        return;
    }

    job.state.nextRunAtMs = computeNextRun(job.schedule, nowMs());
}

void CronService::onTimer() {
    if (!_running) {
        return;
    }

    loadStore();
    const qint64 current = nowMs();
    QStringList dueIds;
    for (const CronJob &job : _jobs) {
        if (!job.enabled || job.state.nextRunAtMs <= 0) {
            continue;
        }
        if (current >= job.state.nextRunAtMs) {
            dueIds.append(job.id);
        }
    }

    for (const QString &id : dueIds) {
        int index = -1;
        for (int i = 0; i < _jobs.size(); ++i) {
            if (_jobs.at(i).id == id) {
                index = i;
                break;
            }
        }
        if (index >= 0) {
            executeJob(index);
        }
    }

    saveStore();
    armTimer();
}

QVector<CronJob> CronService::listJobs(bool includeDisabled) {
    loadStore();
    QVector<CronJob> jobs;
    for (const CronJob &job : _jobs) {
        if (includeDisabled || job.enabled) {
            jobs.append(job);
        }
    }
    std::sort(jobs.begin(), jobs.end(), [](const CronJob &a, const CronJob &b) {
        const qint64 av = a.state.nextRunAtMs <= 0 ? std::numeric_limits<qint64>::max() : a.state.nextRunAtMs;
        const qint64 bv = b.state.nextRunAtMs <= 0 ? std::numeric_limits<qint64>::max() : b.state.nextRunAtMs;
        return av < bv;
    });
    return jobs;
}

CronJob CronService::addJob(
    const QString &name,
    const CronSchedule &schedule,
    const QString &message,
    bool deliver,
    const QString &channel,
    const QString &to,
    bool deleteAfterRun,
    const QString &payloadKind
) {
    loadStore();

    QString error;
    CronJob empty;
    if (!validateScheduleForAdd(schedule, &error)) {
        empty.id = "error";
        empty.name = error;
        return empty;
    }

    const qint64 current = nowMs();
    CronJob job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    job.name = name;
    job.enabled = true;
    job.schedule = schedule;
    job.payload.kind = payloadKind.trimmed().isEmpty() ? QStringLiteral("agent_turn") : payloadKind.trimmed();
    job.payload.message = message;
    job.payload.deliver = deliver;
    job.payload.channel = channel;
    job.payload.to = to;
    job.state.nextRunAtMs = computeNextRun(schedule, current);
    job.createdAtMs = current;
    job.updatedAtMs = current;
    job.deleteAfterRun = deleteAfterRun;

    _jobs.append(job);
    saveStore();
    armTimer();
    return job;
}

bool CronService::removeJob(const QString &jobId) {
    loadStore();
    const int before = _jobs.size();
    _jobs.erase(std::remove_if(_jobs.begin(), _jobs.end(), [&jobId](const CronJob &job) {
        return job.id == jobId;
    }), _jobs.end());

    const bool removed = _jobs.size() < before;
    if (removed) {
        saveStore();
        armTimer();
    }
    return removed;
}

CronJob CronService::enableJob(const QString &jobId, bool enabled, bool *ok) {
    loadStore();
    for (CronJob &job : _jobs) {
        if (job.id != jobId) {
            continue;
        }
        job.enabled = enabled;
        job.updatedAtMs = nowMs();
        if (enabled) {
            job.state.nextRunAtMs = computeNextRun(job.schedule, nowMs());
        } else {
            job.state.nextRunAtMs = -1;
        }
        saveStore();
        armTimer();
        if (ok) *ok = true;
        return job;
    }

    if (ok) *ok = false;
    return CronJob();
}

bool CronService::runJob(const QString &jobId, bool force) {
    loadStore();
    for (int i = 0; i < _jobs.size(); ++i) {
        if (_jobs[i].id != jobId) {
            continue;
        }
        if (!force && !_jobs[i].enabled) {
            return false;
        }
        executeJob(i);
        saveStore();
        armTimer();
        return true;
    }
    return false;
}

CronStatus CronService::status() {
    loadStore();
    CronStatus st;
    st.enabled = _running;
    st.jobs = _jobs.size();
    st.nextWakeAtMs = getNextWakeMs();
    return st;
}

void CronService::setOnJobCallback(const std::function<QString(const CronJob &)> &callback) {
    _onJob = callback;
}

} // namespace yaos::runtime
