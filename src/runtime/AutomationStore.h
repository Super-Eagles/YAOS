#ifndef YAOS_RUNTIME_AUTOMATIONSTORE_H
#define YAOS_RUNTIME_AUTOMATIONSTORE_H

#include <QDateTime>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVector>

namespace yaos::runtime {

struct AutomationRecord {
    QString id;
    QString name;
    QString trigger = "manual";
    QString provider = "auto";
    QString model;
    QString prompt;
    QStringList tags;
    bool enabled = true;
    QString scheduleKind = "manual";
    QString scheduleValue;
    QString timeZone;
    QString cronJobId;
    QDateTime nextRunAt;
    QDateTime lastRunAt;
    QString lastStatus;
    QString lastError;
    QString lastResultPreview;
    int runCount = 0;
    QDateTime createdAt;
    QDateTime updatedAt;
    QJsonObject metadata;
};

struct AutomationRunRecord {
    QString id;
    QString automationId;
    QString automationName;
    QString triggerSource = "manual";
    QString sessionKey;
    QString provider;
    QString model;
    QString promptPreview;
    QString result;
    QString resultPreview;
    QString status = "ok";
    QString error;
    QDateTime createdAt;
    QDateTime finishedAt;
    QJsonObject metadata;
};

class AutomationStore {
public:
    explicit AutomationStore(const QString &workspace);

    QVector<AutomationRecord> list(int limit = 100) const;
    AutomationRecord get(const QString &id) const;
    QString save(const AutomationRecord &record, QString *error = nullptr);
    bool remove(const QString &id);
    int count() const;

private:
    static QJsonObject toJson(const AutomationRecord &record);
    static AutomationRecord fromJson(const QJsonObject &obj);
    static QString trimText(const QString &text, int maxLen);
    QVector<AutomationRecord> loadUnlocked() const;
    void saveUnlocked(const QVector<AutomationRecord> &records) const;
    QString filePath() const;

private:
    QString _workspace;
    mutable QMutex _mutex;
};

class AutomationRunStore {
public:
    explicit AutomationRunStore(const QString &workspace);

    QVector<AutomationRunRecord> list(int limit = 100, const QString &automationId = QString()) const;
    AutomationRunRecord latest(const QString &automationId) const;
    void append(const AutomationRunRecord &record);
    void removeForAutomation(const QString &automationId);
    int count(const QString &automationId = QString()) const;

private:
    static QJsonObject toJson(const AutomationRunRecord &record);
    static AutomationRunRecord fromJson(const QJsonObject &obj);
    static QString trimText(const QString &text, int maxLen);
    QVector<AutomationRunRecord> loadUnlocked() const;
    void saveUnlocked(const QVector<AutomationRunRecord> &records) const;
    QString filePath() const;

private:
    QString _workspace;
    mutable QMutex _mutex;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_AUTOMATIONSTORE_H
