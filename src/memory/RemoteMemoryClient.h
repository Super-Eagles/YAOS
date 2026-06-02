#ifndef YAOS_MEMORY_REMOTEMEMORYCLIENT_H
#define YAOS_MEMORY_REMOTEMEMORYCLIENT_H

#include <QJsonObject>
#include <QString>

namespace yaos::memory {

class RemoteMemoryClient {
public:
    RemoteMemoryClient(QString endpoint,
                       QString apiKey,
                       int timeoutMs);

    bool isReady() const;
    QString endpoint() const;
    QJsonObject get(const QString &path,
                    QString *error = nullptr) const;
    QJsonObject post(const QString &path,
                     const QJsonObject &payload,
                     QString *error = nullptr) const;
    bool ping(QString *error = nullptr) const;

private:
    QString buildUrl(const QString &path) const;
    QJsonObject request(const QString &method,
                        const QString &path,
                        const QJsonObject *payload,
                        QString *error) const;

    QString _endpoint;
    QString _apiKey;
    int _timeoutMs = 12000;
};

} // namespace yaos::memory

#endif // YAOS_MEMORY_REMOTEMEMORYCLIENT_H
