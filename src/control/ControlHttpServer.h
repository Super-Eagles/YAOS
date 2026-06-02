#ifndef YAOS_CONTROL_CONTROLHTTPSERVER_H
#define YAOS_CONTROL_CONTROLHTTPSERVER_H

#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>
#include <memory>

#include "ControlServiceCore.h"

namespace FastNet {
class HttpServer;
struct HttpRequest;
struct HttpResponse;
}

namespace yaos::control {

class ControlHttpServer : public QObject {
    Q_OBJECT
public:
    explicit ControlHttpServer(ControlServiceCore &core,
                               QObject *parent = nullptr);
    ~ControlHttpServer() override;

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

    ControlServiceCore &_core;
    std::unique_ptr<FastNet::HttpServer> _server;
    QString _listenAddress;
    quint16 _listenPort = 0;
    QMutex _requestMutex;
};

} // namespace yaos::control

#endif // YAOS_CONTROL_CONTROLHTTPSERVER_H
