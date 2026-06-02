#ifndef YAOS_PLATFORM_NETWORK_FASTNETWEBSOCKETTRANSPORT_H
#define YAOS_PLATFORM_NETWORK_FASTNETWEBSOCKETTRANSPORT_H

#include <QObject>
#include <QMap>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <memory>

namespace FastNet {
class WebSocketClient;
}

namespace yaos::platform::network {

class FastNetWebSocketTransport : public QObject {
    Q_OBJECT
public:
    explicit FastNetWebSocketTransport(QObject *parent = nullptr);
    ~FastNetWebSocketTransport() override;

    void open(const QUrl &url);
    void open(const QUrl &url,
              const QMap<QByteArray, QByteArray> &headers,
              const QStringList &subprotocols = QStringList());
    void close();
    bool sendTextMessage(const QString &message);
    bool isValid() const;
    bool isOpenOrConnecting() const;
    QString errorString() const;

signals:
    void connected();
    void disconnected();
    void textMessageReceived(const QString &message);
    void errorMessage(const QString &message);

private:
    std::shared_ptr<FastNet::WebSocketClient> _client;
    quint64 _generation = 0;
    bool _connected = false;
    bool _connecting = false;
    QString _lastError;
};

} // namespace yaos::platform::network

#endif // YAOS_PLATFORM_NETWORK_FASTNETWEBSOCKETTRANSPORT_H
