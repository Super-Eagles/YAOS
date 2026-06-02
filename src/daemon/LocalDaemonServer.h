#ifndef YAOS_DAEMON_LOCALDAEMONSERVER_H
#define YAOS_DAEMON_LOCALDAEMONSERVER_H

#include <memory>

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>

namespace FastNet {
class TcpServer;
}

namespace yaos::runtime {
class LocalRuntimeClient;
}

namespace yaos::daemon {

class LocalDaemonServer : public QObject {
    Q_OBJECT
public:
    explicit LocalDaemonServer(QObject *parent = nullptr);
    ~LocalDaemonServer() override;

    bool start(const QString &serverName, QString *error = nullptr);
    void stop();
    QString serverName() const;
    quint16 serverPort() const;

private:
    void handleData(quint64 clientId, const QByteArray &chunk);
    void handleDisconnected(quint64 clientId);

private:
    std::unique_ptr<FastNet::TcpServer> _server;
    std::unique_ptr<::yaos::runtime::LocalRuntimeClient> _client;
    QHash<quint64, QByteArray> _buffers;
    QString _serverName;
    quint16 _serverPort = 0;
    QMutex _requestMutex;
};

} // namespace yaos::daemon

#endif // YAOS_DAEMON_LOCALDAEMONSERVER_H
