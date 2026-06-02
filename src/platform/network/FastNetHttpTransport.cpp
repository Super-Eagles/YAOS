#include "FastNetHttpTransport.h"
#include <QDebug>

#include <FastNet/FastNet.h>
#include <FastNet/Timer.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace yaos::platform::network {

namespace {

bool ensureFastNetInitialized() {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, []() {
        // 2 threads is too few: Thread[0] is the IO poller, Thread[1] is the
        // only task worker. Under concurrent LLM streaming + WebSocket channels
        // + local HTTP servers all tasks queue up behind a single worker, causing
        // the system to slow down progressively over long runs.
        // Use hardware_concurrency, clamped to [4, 8].
        const size_t hw = static_cast<size_t>(std::thread::hardware_concurrency());
        const size_t threads = std::max<size_t>(4, std::min<size_t>(hw, 8));
        const FastNet::ErrorCode result = FastNet::initialize(threads);
        ok = result == FastNet::ErrorCode::Success ||
             result == FastNet::ErrorCode::AlreadyRunning ||
             FastNet::isInitialized();
    });
    return ok;
}

std::string toStdString(const QString &value) {
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

std::string toStdString(const QByteArray &value) {
    return std::string(value.constData(), static_cast<size_t>(value.size()));
}

FastNet::RequestHeaders toFastNetHeaders(const QMap<QByteArray, QByteArray> &headers) {
    FastNet::RequestHeaders result;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        result[toStdString(it.key())] = toStdString(it.value());
    }
    return result;
}

bool envFlagEnabled(const char *name) {
    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    return value == QStringLiteral("1") ||
           value == QStringLiteral("true") ||
           value == QStringLiteral("yes") ||
           value == QStringLiteral("on");
}

QString configuredCaFile(const HttpRequest &request) {
    if (!request.caFile.trimmed().isEmpty()) {
        return request.caFile.trimmed();
    }

    const QString yaosCa = qEnvironmentVariable("YAOS_CA_FILE").trimmed();
    if (!yaosCa.isEmpty()) {
        return yaosCa;
    }

    const QString sslCa = qEnvironmentVariable("SSL_CERT_FILE").trimmed();
    if (!sslCa.isEmpty()) {
        return sslCa;
    }

    const QString appCa = QCoreApplication::applicationDirPath() + QStringLiteral("/cacert.pem");
    return QFileInfo::exists(appCa) ? appCa : QString();
}

FastNet::SSLConfig sslConfigFor(const QUrl &url, const HttpRequest &request) {
    FastNet::SSLConfig sslCfg;
    sslCfg.enableSSL = true;
    sslCfg.verifyPeer = request.verifyTlsPeer && !envFlagEnabled("YAOS_TLS_INSECURE");
    sslCfg.hostnameVerification = toStdString(url.host());

    const QString caFile = configuredCaFile(request);
    if (!caFile.isEmpty()) {
        sslCfg.caFile = toStdString(caFile);
    }
    return sslCfg;
}

} // namespace

HttpResponse FastNetHttpTransport::send(const HttpRequest &request) {
    HttpResponse output;
    if (!ensureFastNetInitialized()) {
        output.error = QStringLiteral("FastNet initialization failed.");
        return output;
    }

    const QUrl url(request.url);
    if (request.url.trimmed().isEmpty() || !url.isValid() || url.host().trimmed().isEmpty()) {
        output.error = QStringLiteral("HTTP URL is invalid.");
        return output;
    }

    auto &ioService = FastNet::getGlobalIoService();
    auto client = std::make_shared<FastNet::HttpClient>(ioService);
    const uint32_t timeoutMs = request.timeoutMs > 0 ? static_cast<uint32_t>(request.timeoutMs) : 6000U;
    client->setConnectTimeout(timeoutMs);
    client->setRequestTimeout(timeoutMs);
    client->setReadTimeout(timeoutMs);

    if (url.scheme().toLower() == QStringLiteral("https")) {
        client->setSSLConfig(sslConfigFor(url, request));
    }
    if (!request.proxyUrl.trimmed().isEmpty() && !client->setProxyUrl(toStdString(request.proxyUrl.trimmed()))) {
        output.error = QStringLiteral("HTTP proxy URL is invalid or unsupported by FastNet.");
        return output;
    }

    struct WaitState {
        std::mutex mutex;
        std::condition_variable condition;
        bool done = false;
        HttpResponse response;
    };
    auto state = std::make_shared<WaitState>();

    const std::string urlText = toStdString(url.toString(QUrl::FullyEncoded));
    const std::string methodText = toStdString(request.method.trimmed().isEmpty()
                                                  ? QStringLiteral("GET")
                                                  : request.method.trimmed().toUpper());
    const FastNet::RequestHeaders headers = toFastNetHeaders(request.headers);
    const std::string body = toStdString(request.body);

    std::weak_ptr<FastNet::HttpClient> weakClient = client;
    const bool connectStarted = client->connect(urlText, [weakClient, methodText, headers, body, state](bool success,
                                                                                                     const std::string &message) {
        auto client = weakClient.lock();
        if (!client) {
            return;
        }

        if (!success) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->response.error = QString::fromStdString(message.empty() ? std::string("HTTP connection failed.") : message);
            state->done = true;
            state->condition.notify_all();
            return;
        }

        std::weak_ptr<FastNet::HttpClient> weakClient2 = client;
        const bool requestStarted = client->request(methodText,
                                                    "",
                                                    headers,
                                                    body,
                                                    [weakClient2, state](const FastNet::HttpResponse &response) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->response.statusCode = response.statusCode;
            state->response.body = QByteArray(response.body.data(), static_cast<int>(response.body.size()));
            for (const auto &header : response.headers) {
                state->response.headers.insert(QByteArray::fromStdString(header.first),
                                               QByteArray::fromStdString(header.second));
            }
            if (response.statusCode == 0) {
                state->response.error = QString::fromStdString(response.statusMessage);
            } else if (response.statusCode < 200 || response.statusCode >= 300) {
                state->response.error = QStringLiteral("HTTP %1").arg(response.statusCode);
            }
            state->done = true;
            state->condition.notify_all();
            if (auto client = weakClient2.lock()) {
                client->disconnect();
            }
        });

        if (!requestStarted) {
            const FastNet::Error error = client->getLastError();
            std::lock_guard<std::mutex> lock(state->mutex);
            state->response.error = QString::fromStdString(error.isFailure()
                                                              ? error.toString()
                                                              : std::string("HTTP request failed to start."));
            state->done = true;
            state->condition.notify_all();
            client->disconnect();
        }
    });

    if (!connectStarted) {
        const FastNet::Error error = client->getLastError();
        output.error = QString::fromStdString(error.isFailure()
                                                  ? error.toString()
                                                  : std::string("HTTP connection failed to start."));
        return output;
    }

    std::unique_lock<std::mutex> lock(state->mutex);
    const auto timeout = std::chrono::milliseconds(timeoutMs + 1000U);
    if (!state->condition.wait_for(lock, timeout, [state]() { return state->done; })) {
        client->cancelRequest();
        output.error = QStringLiteral("HTTP request timed out.");
        return output;
    }
    return state->response;
}

bool FastNetHttpTransport::sendStreaming(const HttpRequest &request,
                                        StreamChunkCallback onChunk,
                                        StreamCompleteCallback onDone) {
    if (!ensureFastNetInitialized()) {
        if (onDone) onDone(0, QStringLiteral("FastNet initialization failed."));
        return false;
    }

    const QUrl url(request.url);
    if (request.url.trimmed().isEmpty() || !url.isValid() || url.host().trimmed().isEmpty()) {
        if (onDone) onDone(0, QStringLiteral("HTTP URL is invalid."));
        return false;
    }

    auto &ioService = FastNet::getGlobalIoService();
    auto client = std::make_shared<FastNet::HttpClient>(ioService);
    const uint32_t timeoutMs = request.timeoutMs > 0 ? static_cast<uint32_t>(request.timeoutMs) : 6000U;
    client->setConnectTimeout(timeoutMs);
    client->setRequestTimeout(timeoutMs);
    client->setReadTimeout(timeoutMs);

    if (url.scheme().toLower() == QStringLiteral("https")) {
        client->setSSLConfig(sslConfigFor(url, request));
    }
    if (!request.proxyUrl.trimmed().isEmpty() && !client->setProxyUrl(toStdString(request.proxyUrl.trimmed()))) {
        if (onDone) onDone(0, QStringLiteral("HTTP proxy URL is invalid or unsupported by FastNet."));
        return false;
    }

    const std::string urlText    = toStdString(url.toString(QUrl::FullyEncoded));
    const std::string methodText = toStdString(request.method.trimmed().isEmpty()
                                                   ? QStringLiteral("GET")
                                                   : request.method.trimmed().toUpper());
    const FastNet::RequestHeaders headers = toFastNetHeaders(request.headers);
    const std::string body = toStdString(request.body);

    struct StreamState {
        std::mutex mutex;
        bool completed = false;
        std::shared_ptr<FastNet::HttpClient> client;
        std::shared_ptr<FastNet::Timer> watchdog;
        StreamChunkCallback onChunk;
        StreamCompleteCallback onDone;
    };
    auto state = std::make_shared<StreamState>();
    state->client = client;
    state->watchdog = std::make_shared<FastNet::Timer>(ioService);
    state->onChunk = std::move(onChunk);
    state->onDone = std::move(onDone);

    auto completeOnce = [state](int statusCode, const QString &error) {
        StreamCompleteCallback doneCallback;
        std::shared_ptr<FastNet::Timer> watchdog;
        std::shared_ptr<FastNet::HttpClient> clientToReset;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->completed) {
                return;
            }
            state->completed = true;
            doneCallback = state->onDone;
            watchdog = state->watchdog;
            clientToReset = state->client;

            // Clear strong references inside StreamState to break cyclic references
            state->client.reset();
            state->watchdog.reset();
            state->onChunk = nullptr;
            state->onDone = nullptr;
        }
        if (watchdog) {
            watchdog->stop();
        }
        if (doneCallback) {
            doneCallback(statusCode, error);
        }
        if (clientToReset) {
            clientToReset->disconnect();
        }
    };

    std::weak_ptr<StreamState> weakState = state;
    state->watchdog->start(std::chrono::milliseconds(timeoutMs + 5000U), [weakState, completeOnce]() {
        auto state = weakState.lock();
        if (!state) {
            return;
        }
        std::shared_ptr<FastNet::HttpClient> activeClient;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->completed) {
                return;
            }
            activeClient = state->client;
        }
        if (activeClient) {
            activeClient->cancelRequest();
            activeClient->disconnect();
        }
        completeOnce(0, QStringLiteral("HTTP stream timed out."));
    });

    // qDebug() << "[FastNetHttpTransport] Starting connect to" << QString::fromStdString(urlText);
    std::weak_ptr<FastNet::HttpClient> weakClient = client;
    std::weak_ptr<StreamState> weakState2 = state;
    const bool connectStarted = client->connect(
        urlText,
        [weakClient, methodText, headers, body, weakState2, completeOnce, urlText](bool success, const std::string &message) {
            auto client = weakClient.lock();
            auto state = weakState2.lock();
            if (!client || !state) {
                return;
            }

            // qDebug() << "[FastNetHttpTransport] Connect callback: success =" << success << "msg =" << QString::fromStdString(message);
            if (!success) {
                completeOnce(0, QString::fromStdString(message.empty() ? "HTTP connection failed." : message));
                return;
            }

            // qDebug() << "[FastNetHttpTransport] Starting streamRequest, path=\"\", body_size =" << body.size();
            std::weak_ptr<FastNet::HttpClient> weakClient2 = client;
            std::weak_ptr<StreamState> weakState3 = state;
            const bool started = client->streamRequest(
                methodText, "", headers, body,
                // headersCallback — not used
                [](const FastNet::HttpResponse &) {},
                // dataCallback — called per chunk; return false to abort
                [weakState3](std::string_view chunk) -> bool {
                    auto state = weakState3.lock();
                    if (!state) {
                        return false;
                    }
                    StreamChunkCallback chunkCb;
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        if (state->completed || !state->onChunk) {
                            return !state->completed;
                        }
                        chunkCb = state->onChunk;
                    }
                    // Call onChunk WITHOUT holding state->mutex.
                    // completeStreamingResponse() may fire synchronously inside
                    // the FastNet call stack (e.g. when Content-Length bytes are
                    // fully consumed), which triggers completeCallback→completeOnce
                    // that also needs state->mutex — holding it here would deadlock.
                    return chunkCb(QByteArray(chunk.data(), static_cast<int>(chunk.size())));
                },
                // completeCallback
                [weakClient2, completeOnce](const FastNet::HttpResponse &response) {
                    // qDebug() << "[FastNetHttpTransport] Stream complete callback, status =" << response.statusCode << "msg =" << QString::fromStdString(response.statusMessage);
                    QString error;
                    if (response.statusCode == 0) {
                        error = QString::fromStdString(response.statusMessage);
                    } else if (response.statusCode < 200 || response.statusCode >= 300) {
                        error = QStringLiteral("HTTP %1").arg(response.statusCode);
                    }
                    completeOnce(response.statusCode, error);
                    if (auto client = weakClient2.lock()) {
                        client->disconnect();
                    }
                }
            );

            if (!started) {
                const FastNet::Error err = client->getLastError();
                qDebug() << "[FastNetHttpTransport] streamRequest failed to start:" << QString::fromStdString(err.toString());
                completeOnce(0, QString::fromStdString(err.isFailure() ? err.toString() : "HTTP stream request failed to start."));
                client->disconnect();
            }
        }
    );

    if (!connectStarted) {
        const FastNet::Error err = client->getLastError();
        qWarning() << "[FastNetHttpTransport] Connect failed to start immediately. Error:" << QString::fromStdString(err.toString());
        completeOnce(0, QString::fromStdString(err.isFailure() ? err.toString() : "HTTP connection failed to start."));
    }
    return connectStarted;
}

} // namespace yaos::platform::network
