#include "OAuthLoopbackServer.h"

#include <FastNet/FastNet.h>
#include <FastNet/HttpServer.h>
#include <FastNet/TcpClient.h>

#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace yaos::ui {

namespace {

constexpr int kCancelRetryCount = 3;
constexpr int kCancelRetryDelayMs = 200;
constexpr int kCloseAfterResponseDelayMs = 250;

QString reasonPhraseFor(int statusCode) {
    switch (statusCode) {
    case 200: return QStringLiteral("OK");
    case 503: return QStringLiteral("Service Unavailable");
    default: return QStringLiteral("OK");
    }
}

bool ensureFastNetInitialized(QString *error) {
    static std::once_flag once;
    static FastNet::ErrorCode result = FastNet::ErrorCode::UnknownError;
    std::call_once(once, []() {
        result = FastNet::initialize(2);
    });

    if (result == FastNet::ErrorCode::Success ||
        (result == FastNet::ErrorCode::AlreadyRunning && FastNet::isInitialized())) {
        return true;
    }

    if (error) {
        *error = QStringLiteral("FastNet initialization failed.");
    }
    return false;
}

std::string toStdString(const QString &value) {
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

QString htmlTitle(const OAuthLoopbackResponse &response) {
    if (!response.title.trimmed().isEmpty()) {
        return response.title.trimmed();
    }
    return response.ok
        ? QStringLiteral("YAOS OAuth complete")
        : QStringLiteral("YAOS OAuth failed");
}

QString htmlBody(const OAuthLoopbackResponse &response) {
    if (!response.body.trimmed().isEmpty()) {
        return response.body.trimmed();
    }
    if (response.ok) {
        return QStringLiteral("You can close this page and return to YAOS.");
    }
    if (!response.error.trimmed().isEmpty()) {
        return response.error.trimmed();
    }
    return QStringLiteral("OAuth callback failed.");
}

QString requestTargetFor(const FastNet::HttpRequest &request) {
    QString target = QString::fromStdString(request.target).trimmed();
    if (target.isEmpty()) {
        target = QString::fromStdString(request.path).trimmed();
        const QString query = QString::fromStdString(request.queryString).trimmed();
        if (!query.isEmpty()) {
            target += QStringLiteral("?") + query;
        }
    }
    if (target.isEmpty()) {
        return QStringLiteral("/");
    }
    if (!target.startsWith(QLatin1Char('/'))) {
        target.prepend(QLatin1Char('/'));
    }
    return target;
}

QString pathOnly(const QString &target) {
    const int queryIndex = target.indexOf(QLatin1Char('?'));
    return queryIndex >= 0 ? target.left(queryIndex) : target;
}

void writeHtmlResponse(FastNet::HttpResponse &response,
                       int statusCode,
                       const QString &title,
                       const QString &body) {
    const QString html = QStringLiteral(
        "<html><body><h2>%1</h2><p>%2</p></body></html>")
            .arg(title.toHtmlEscaped(), body.toHtmlEscaped());
    const QByteArray payload = html.toUtf8();
    response.statusCode = statusCode;
    response.statusMessage = toStdString(reasonPhraseFor(statusCode));
    response.headers["Content-Type"] = "text/html; charset=utf-8";
    response.headers["Connection"] = "close";
    response.body.assign(payload.constData(), static_cast<size_t>(payload.size()));
}

struct CancelState {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool sent = false;
};

void finishCancelRequest(const std::shared_ptr<CancelState> &state, bool sent) {
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->sent = sent;
        state->done = true;
    }
    state->cv.notify_all();
}

bool sendCancelRequest(quint16 port) {
    if (port == 0) {
        return false;
    }

    auto state = std::make_shared<CancelState>();
    auto client = std::make_shared<FastNet::TcpClient>(FastNet::getGlobalIoService());
    std::weak_ptr<FastNet::TcpClient> weakClient(client);
    client->setConnectTimeout(500);
    client->setReadTimeout(500);
    client->setWriteTimeout(500);
    client->setErrorCallback([state](FastNet::ErrorCode, const std::string &) {
        finishCancelRequest(state, false);
    });

    const bool started = client->connect(
        "127.0.0.1",
        port,
        [state, weakClient](bool success, const std::string &) {
            if (!success) {
                finishCancelRequest(state, false);
                return;
            }

            auto lockedClient = weakClient.lock();
            if (!lockedClient) {
                finishCancelRequest(state, false);
                return;
            }

            std::string request =
                "GET /cancel HTTP/1.1\r\n"
                "Host: 127.0.0.1\r\n"
                "Connection: close\r\n\r\n";
            const bool sent = lockedClient->send(std::move(request));
            lockedClient->disconnectAfterPendingWrites();
            finishCancelRequest(state, sent);
        });
    if (!started) {
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait_for(lock,
                           std::chrono::milliseconds(800),
                           [&state]() { return state->done; });
    }
    const bool sent = state->sent;
    client->disconnect();
    return sent;
}

} // namespace

OAuthLoopbackServer::OAuthLoopbackServer(QObject *parent)
    : QObject(parent) {}

OAuthLoopbackServer::~OAuthLoopbackServer() {
    close();
}

bool OAuthLoopbackServer::listen(const CallbackHandler &handler,
                                 QString *redirectUri,
                                 QString *error,
                                 quint16 preferredPort) {
    close();
    if (!ensureFastNetInitialized(error)) {
        return false;
    }

    m_handler = handler;

    QString message;
    const auto tryListen = [&](quint16 port) -> bool {
        m_server = std::make_unique<FastNet::HttpServer>(FastNet::getGlobalIoService());
        m_server->setMaxRequestSize(16 * 1024);
        QPointer<OAuthLoopbackServer> guard(this);
        m_server->setRequestHandler([guard](const FastNet::HttpRequest &request,
                                            FastNet::HttpResponse &response) {
            if (!guard) {
                writeHtmlResponse(response,
                                  503,
                                  QStringLiteral("YAOS OAuth unavailable"),
                                  QStringLiteral("The OAuth callback listener is no longer available."));
                return;
            }
            guard->handleRequest(request, response);
        });

        const FastNet::Error startError = m_server->start(port, "127.0.0.1");
        if (startError.isFailure()) {
            message = QString::fromStdString(startError.toString());
            m_server.reset();
            return false;
        }

        const FastNet::Address actual = m_server->getListenAddress();
        m_listenPort = actual.port != 0 ? actual.port : port;
        return m_listenPort != 0;
    };

    bool listenOk = false;
    if (preferredPort > 0) {
        for (int attempt = 0; attempt < kCancelRetryCount; ++attempt) {
            if (tryListen(preferredPort)) {
                listenOk = true;
                break;
            }
            if (!sendCancelRequest(preferredPort)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kCancelRetryDelayMs));
        }
    }

    if (!listenOk) {
        listenOk = tryListen(0);
    }

    if (!listenOk) {
        if (error) {
            *error = message.trimmed().isEmpty()
                ? QStringLiteral("Unable to start localhost callback listener.")
                : message;
        }
        m_handler = CallbackHandler();
        return false;
    }

    m_redirectUri = QStringLiteral("http://localhost:%1/auth/callback").arg(m_listenPort);
    if (redirectUri) {
        *redirectUri = m_redirectUri;
    }
    if (error) {
        error->clear();
    }
    return true;
}

void OAuthLoopbackServer::close() {
    m_redirectUri.clear();
    m_lastCallbackUrl.clear();
    m_handler = CallbackHandler();
    m_listenPort = 0;
    m_closeQueued = false;
    if (m_server) {
        m_server->stop();
        m_server.reset();
    }
}

bool OAuthLoopbackServer::isListening() const {
    return m_server && m_server->isRunning();
}

QString OAuthLoopbackServer::redirectUri() const {
    return m_redirectUri;
}

QString OAuthLoopbackServer::lastCallbackUrl() const {
    return m_lastCallbackUrl;
}

void OAuthLoopbackServer::handleRequest(const FastNet::HttpRequest &request,
                                        FastNet::HttpResponse &response) {
    const QString target = requestTargetFor(request);

    OAuthLoopbackResponse loopbackResponse;
    if (QThread::currentThread() == thread()) {
        loopbackResponse = processRequestTarget(target);
    } else {
        const bool invoked = QMetaObject::invokeMethod(
            this,
            [this, target, &loopbackResponse]() {
                loopbackResponse = processRequestTarget(target);
            },
            Qt::BlockingQueuedConnection);
        if (!invoked) {
            loopbackResponse.ok = false;
            loopbackResponse.title = QStringLiteral("YAOS OAuth failed");
            loopbackResponse.body = QStringLiteral("Unable to dispatch OAuth callback.");
            loopbackResponse.error = loopbackResponse.body;
        }
    }

    writeResponse(response, loopbackResponse);
    queueCloseAfterResponse();
}

OAuthLoopbackResponse OAuthLoopbackServer::processRequestTarget(const QString &target) {
    OAuthLoopbackResponse response;
    m_lastCallbackUrl = QStringLiteral("http://localhost:%1").arg(m_listenPort) + target;

    if (pathOnly(target) == QStringLiteral("/cancel")) {
        response.ok = false;
        response.callbackUrl = m_lastCallbackUrl;
        response.title = QStringLiteral("YAOS OAuth cancelled");
        response.body = QStringLiteral("The existing YAOS OAuth callback listener has been cancelled.");
        return response;
    }

    response.callbackUrl = m_lastCallbackUrl;
    if (m_handler) {
        response = m_handler(m_lastCallbackUrl);
        if (response.callbackUrl.trimmed().isEmpty()) {
            response.callbackUrl = m_lastCallbackUrl;
        }
    } else {
        response.ok = false;
        response.error = QStringLiteral("OAuth callback handler is not configured.");
    }
    return response;
}

void OAuthLoopbackServer::writeResponse(FastNet::HttpResponse &response,
                                        const OAuthLoopbackResponse &loopbackResponse) const {
    writeHtmlResponse(response, 200, htmlTitle(loopbackResponse), htmlBody(loopbackResponse));
}

void OAuthLoopbackServer::queueCloseAfterResponse() {
    QPointer<OAuthLoopbackServer> guard(this);
    QMetaObject::invokeMethod(
        this,
        [guard]() {
            if (!guard || guard->m_closeQueued) {
                return;
            }
            guard->m_closeQueued = true;
            QTimer::singleShot(kCloseAfterResponseDelayMs, guard.data(), [guard]() {
                if (guard) {
                    guard->close();
                }
            });
        },
        Qt::QueuedConnection);
}

} // namespace yaos::ui
