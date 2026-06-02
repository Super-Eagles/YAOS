#ifndef YAOS_UI_OAUTHLOOPBACKSERVER_H
#define YAOS_UI_OAUTHLOOPBACKSERVER_H

#include <functional>
#include <memory>

#include <QObject>
#include <QString>

namespace FastNet {
class HttpServer;
struct HttpRequest;
struct HttpResponse;
}

namespace yaos::ui {

struct OAuthLoopbackResponse {
    bool ok = false;
    QString callbackUrl;
    QString title;
    QString body;
    QString error;
};

class OAuthLoopbackServer : public QObject {
    Q_OBJECT
public:
    using CallbackHandler = std::function<OAuthLoopbackResponse(const QString &callbackUrl)>;

    explicit OAuthLoopbackServer(QObject *parent = nullptr);
    ~OAuthLoopbackServer() override;

    bool listen(const CallbackHandler &handler,
                QString *redirectUri,
                QString *error = nullptr,
                quint16 preferredPort = 1455);
    void close();

    bool isListening() const;
    QString redirectUri() const;
    QString lastCallbackUrl() const;

private:
    void handleRequest(const FastNet::HttpRequest &request, FastNet::HttpResponse &response);
    OAuthLoopbackResponse processRequestTarget(const QString &target);
    void writeResponse(FastNet::HttpResponse &response, const OAuthLoopbackResponse &loopbackResponse) const;
    void queueCloseAfterResponse();

    std::unique_ptr<FastNet::HttpServer> m_server;
    CallbackHandler m_handler;
    QString m_redirectUri;
    QString m_lastCallbackUrl;
    quint16 m_listenPort = 0;
    bool m_closeQueued = false;
};

} // namespace yaos::ui

#endif // YAOS_UI_OAUTHLOOPBACKSERVER_H
