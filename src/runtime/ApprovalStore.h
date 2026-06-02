#ifndef YAOS_RUNTIME_APPROVALSTORE_H
#define YAOS_RUNTIME_APPROVALSTORE_H

#include <QDateTime>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QVector>

namespace yaos::runtime {

struct ApprovalRecord {
    QString id;
    QString toolName;
    QString sessionKey;
    QString channel;
    QString state;
    QString scope;
    int remainingUses = 0;
    QString summary;
    QString paramsPreview;
    QString note;
    QDateTime createdAt;
    QDateTime updatedAt;
    QJsonObject metadata;
};

class ApprovalStore {
public:
    explicit ApprovalStore(const QString &workspace);

    QString createPending(const QString &toolName,
                          const QString &sessionKey,
                          const QString &channel,
                          const QString &summary,
                          const QString &paramsPreview,
                          const QJsonObject &metadata = QJsonObject());
    bool resolve(const QString &approvalId,
                 const QString &decision,
                 const QString &scope = QString(),
                 const QString &note = QString());
    bool consumeGrant(const QString &toolName,
                      const QString &sessionKey,
                      QString *approvalId = nullptr);
    QVector<ApprovalRecord> recentApprovals(int limit = 50, const QString &state = QString()) const;
    int count() const;
    int pendingCount() const;

private:
    static QJsonObject toJson(const ApprovalRecord &record);
    static ApprovalRecord fromJson(const QJsonObject &obj);
    static QString trimText(const QString &text, int maxLen);
    static bool matchesScope(const ApprovalRecord &record, const QString &sessionKey);
    QVector<ApprovalRecord> loadUnlocked() const;
    void saveUnlocked(const QVector<ApprovalRecord> &records) const;
    QString filePath() const;

private:
    QString _workspace;
    mutable QMutex _mutex;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_APPROVALSTORE_H
