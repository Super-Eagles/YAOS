#ifndef YAOS_RUNTIME_NOTIFICATIONCENTER_H
#define YAOS_RUNTIME_NOTIFICATIONCENTER_H

#include <QDateTime>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QVector>

namespace yaos::runtime {

struct NotificationRecord {
    QString id;
    QString level;
    QString title;
    QString body;
    QString action;
    QString targetId;
    bool read = false;
    QDateTime createdAt;
    QJsonObject metadata;
};

class NotificationCenter {
public:
    explicit NotificationCenter(const QString &workspace);

    QString push(const QString &level,
                 const QString &title,
                 const QString &body,
                 const QString &action = QString(),
                 const QString &targetId = QString(),
                 const QJsonObject &metadata = QJsonObject());
    QVector<NotificationRecord> recentNotifications(int limit = 50, bool unreadOnly = false) const;
    int count() const;
    bool markRead(const QString &id, bool read = true);
    void markAllRead();
    int unreadCount() const;

private:
    static QJsonObject toJson(const NotificationRecord &record);
    static NotificationRecord fromJson(const QJsonObject &obj);
    static QString trimText(const QString &text, int maxLen);
    QVector<NotificationRecord> loadUnlocked() const;
    void saveUnlocked(const QVector<NotificationRecord> &records) const;
    QString filePath() const;

private:
    QString _workspace;
    mutable QMutex _mutex;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_NOTIFICATIONCENTER_H
