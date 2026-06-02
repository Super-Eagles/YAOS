#ifndef YAOS_SESSION_SESSIONMANAGER_H
#define YAOS_SESSION_SESSIONMANAGER_H

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace yaos::session {

struct Session {
    QString key;
    QJsonArray messages;
    QDateTime createdAt = QDateTime::currentDateTime();
    QDateTime updatedAt = QDateTime::currentDateTime();
    QJsonObject metadata;
    int lastConsolidated = 0;

    void addMessage(const QString &role, const QJsonValue &content, const QJsonObject &extra = QJsonObject());
    QJsonArray getHistory(int maxMessages = 500) const;
    void clear();
};

struct SessionSummary {
    QString key;
    QDateTime createdAt;
    QDateTime updatedAt;
    QString path;
};

class SessionManager {
public:
    explicit SessionManager(const QString &workspace);

    Session getOrCreate(const QString &key);
    void save(const Session &session);
    void invalidate(const QString &key);
    QVector<SessionSummary> listSessions() const;

private:
    QString sessionPath(const QString &key) const;
    Session load(const QString &key, bool *ok = nullptr) const;
    static QString safeFileName(const QString &name);

private:
    QString _workspace;
    QString _sessionsDir;
    mutable QHash<QString, Session> _cache;
};

} // namespace yaos::session

#endif // YAOS_SESSION_SESSIONMANAGER_H
