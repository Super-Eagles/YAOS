#ifndef YAOS_MEMORY_MEMORYHTTPSERVER_H
#define YAOS_MEMORY_MEMORYHTTPSERVER_H

#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>
#include <memory>

#include "MemoryServiceCore.h"

namespace FastNet {
class HttpServer;
struct HttpRequest;
struct HttpResponse;
}

namespace yaos::memory {

class MemoryHttpServer : public QObject {
    Q_OBJECT
public:
    explicit MemoryHttpServer(MemoryServiceCore &core,
                              QString apiKey = QString(),
                              QObject *parent = nullptr);
    ~MemoryHttpServer() override;

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

    MemoryServiceCore &_core;
    QString _apiKey;
    std::unique_ptr<FastNet::HttpServer> _server;
    QString _listenAddress;
    quint16 _listenPort = 0;
    QMutex _requestMutex;
};

} // namespace yaos::memory

#endif // YAOS_MEMORY_MEMORYHTTPSERVER_H
