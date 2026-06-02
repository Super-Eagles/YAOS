#ifndef YAOS_RUNTIME_EVENTLOG_H
#define YAOS_RUNTIME_EVENTLOG_H

#include <QDateTime>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QVector>

namespace yaos::runtime {

struct EventRecord {
    QString id;
    QString level;
    QString category;
    QString message;
    QDateTime timestamp;
    QJsonObject metadata;
};

class EventLog {
public:
    explicit EventLog(const QString &workspace);

    void append(const QString &level,
                const QString &category,
                const QString &message,
                const QJsonObject &metadata = QJsonObject());
    QVector<EventRecord> recentEvents(int limit = 50) const;
    int count() const;

private:
    static QJsonObject toJson(const EventRecord &record);
    static EventRecord fromJson(const QJsonObject &obj);
    QString filePath() const;

private:
    QString _workspace;
    mutable QMutex _mutex;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_EVENTLOG_H
