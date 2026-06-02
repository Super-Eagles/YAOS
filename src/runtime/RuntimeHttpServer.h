#ifndef YAOS_RUNTIME_RUNTIMEHTTPSERVER_H
#define YAOS_RUNTIME_RUNTIMEHTTPSERVER_H

#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>
#include <memory>

#include "../distributed/Contracts.h"

namespace FastNet {
class HttpServer;
struct HttpRequest;
struct HttpResponse;
}

namespace yaos::runtime {

class RuntimeHttpServer : public QObject {
    Q_OBJECT
public:
    explicit RuntimeHttpServer(distributed::IRuntimeClient &client,
                               QObject *parent = nullptr);
    ~RuntimeHttpServer() override;

    bool start(const QString &host, quint16 port, QString *error = nullptr);
    void stop();

    QString listenAddress() const;
    quint16 listenPort() const;

private:
    void handleRequest(const FastNet::HttpRequest &request,
                       FastNet::HttpResponse &response);
    void writeJsonResponse(FastNet::HttpResponse &response,
                           int statusCode,
                           const QJsonObject &payload) const;
    void writeError(FastNet::HttpResponse &response,
                    int statusCode,
                    const QString &message) const;

    distributed::IRuntimeClient &_client;
    std::unique_ptr<FastNet::HttpServer> _server;
    QString _listenAddress;
    quint16 _listenPort = 0;
    QMutex _requestMutex;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_RUNTIMEHTTPSERVER_H
