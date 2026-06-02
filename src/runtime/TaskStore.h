#ifndef YAOS_RUNTIME_TASKSTORE_H
#define YAOS_RUNTIME_TASKSTORE_H

#include <QDateTime>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QVector>

namespace yaos::runtime {

struct TaskRecord {
    QString id;
    QString traceId;
    QString parentTaskId;
    QString rootTaskId;
    QString originNode;
    QString targetNode;
    QString kind;
    QString title;
    QString sessionKey;
    QString channel;
    QString state;
    QString summary;
    QString resultPreview;
    QString error;
    int depth = 0;
    int childCount = 0;
    int descendantCount = 0;
    bool hasChildren = false;
    QDateTime createdAt;
    QDateTime startedAt;
    QDateTime finishedAt;
    QJsonObject metadata;
};

class TaskStore {
public:
    explicit TaskStore(const QString &workspace);

    QString createTask(const QString &kind,
                       const QString &title,
                       const QString &sessionKey,
                       const QString &channel,
                       const QJsonObject &metadata = QJsonObject());
    bool upsertTask(const TaskRecord &task);
    bool markRunning(const QString &taskId);
    bool markCompleted(const QString &taskId,
                       const QString &resultPreview,
                       const QString &summary = QString());
    bool markFailed(const QString &taskId,
                    const QString &error,
                    const QString &summary = QString());
    bool markCancelled(const QString &taskId,
                       const QString &summary = QString());
    QVector<TaskRecord> recentTasks(int limit = 20) const;
    int count() const;

private:
    static QJsonObject toJson(const TaskRecord &task);
    static TaskRecord fromJson(const QJsonObject &obj);
    static QString trimText(const QString &text, int maxLen);
    static QString metadataString(const QJsonObject &metadata,
                                  const char *primaryKey,
                                  const char *alternateKey = nullptr);
    QVector<TaskRecord> loadUnlocked() const;
    void saveUnlocked(const QVector<TaskRecord> &tasks) const;
    QString filePath() const;

private:
    QString _workspace;
    mutable QMutex _mutex;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_TASKSTORE_H
