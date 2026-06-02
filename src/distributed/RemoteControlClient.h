#ifndef YAOS_DISTRIBUTED_REMOTECONTROLCLIENT_H
#define YAOS_DISTRIBUTED_REMOTECONTROLCLIENT_H

#include <QJsonObject>
#include <QString>

namespace yaos::distributed {

class RemoteControlClient {
public:
    explicit RemoteControlClient(QString endpoint,
                                 int timeoutMs = 3500);

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
    int _timeoutMs = 3500;
};

} // namespace yaos::distributed

#endif // YAOS_DISTRIBUTED_REMOTECONTROLCLIENT_H
