#include "DaemonRuntimeClient.h"

#include <FastNet/FastNet.h>
#include <FastNet/TcpClient.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "LocalDaemonProtocol.h"

namespace yaos::daemon {

namespace {

constexpr const char *kLoopbackHost = "127.0.0.1";

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

QStringList daemonProgramCandidates() {
    const QString currentProgram = QCoreApplication::applicationFilePath();
    QFileInfo currentInfo(currentProgram);
    QStringList programs;

    if (currentInfo.exists()) {
        const QString sibling = currentInfo.dir().filePath(
#ifdef Q_OS_WIN
            QStringLiteral("yaosd.exe")
#else
            QStringLiteral("yaosd")
#endif
        );
        if (QFileInfo::exists(sibling) && sibling != currentProgram) {
            programs << sibling;
        }
    }

    if (!currentProgram.trimmed().isEmpty()) {
        programs << currentProgram;
    }
    programs.removeDuplicates();
    return programs;
}

QStringList spawnArgsForProgram(const QString &program, const QString &serverName) {
    const QString baseName = QFileInfo(program).completeBaseName().toLower();
    if (baseName == QStringLiteral("yaosd")) {
        return QStringList{QStringLiteral("--server"), serverName};
    }
    return QStringList{
        QStringLiteral("daemon"),
        QStringLiteral("--server"),
        serverName
    };
}

QByteArray bufferToByteArray(const FastNet::Buffer &buffer) {
    if (buffer.empty()) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(buffer.data()),
                      static_cast<int>(buffer.size()));
}

struct RequestState {
    std::mutex mutex;
    std::condition_variable cv;
    QByteArray buffer;
    QByteArray responseFrame;
    QString error;
    bool done = false;
    bool ok = false;
    bool connected = false;
    bool sent = false;
};

struct RequestAttempt {
    bool ok = false;
    bool connected = false;
    bool sent = false;
    QByteArray responseFrame;
    QString error;
};

void finishRequest(const std::shared_ptr<RequestState> &state,
                   bool ok,
                   const QString &error = QString()) {
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->done) {
            return;
        }
        state->ok = ok;
        state->error = error;
        state->done = true;
    }
    state->cv.notify_all();
}

RequestAttempt sendRequestOnce(const QString &serverName, const QByteArray &request) {
    RequestAttempt attempt;
    const quint16 port = protocol::serverPort(serverName);
    auto state = std::make_shared<RequestState>();
    auto client = std::make_shared<FastNet::TcpClient>(FastNet::getGlobalIoService());
    std::weak_ptr<FastNet::TcpClient> weakClient(client);

    client->setConnectTimeout(1200);
    client->setReadTimeout(5000);
    client->setWriteTimeout(3000);
    client->setDataReceivedCallback([state, weakClient](const FastNet::Buffer &data) {
        const QByteArray chunk = bufferToByteArray(data);
        bool complete = false;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->done) {
                return;
            }
            state->buffer.append(chunk);
            const int newline = state->buffer.indexOf('\n');
            if (newline >= 0) {
                state->responseFrame = state->buffer.left(newline);
                state->ok = true;
                state->done = true;
                complete = true;
            }
        }
        if (complete) {
            if (auto lockedClient = weakClient.lock()) {
                lockedClient->disconnect();
            }
            state->cv.notify_all();
        }
    });
    client->setDisconnectCallback([state](const std::string &) {
        finishRequest(state,
                      false,
                      QStringLiteral("Disconnected before daemon response was received."));
    });
    client->setErrorCallback([state](FastNet::ErrorCode, const std::string &message) {
        finishRequest(state,
                      false,
                      QString::fromStdString(message).trimmed().isEmpty()
                          ? QStringLiteral("Local daemon connection failed.")
                          : QString::fromStdString(message));
    });

    const bool started = client->connect(
        kLoopbackHost,
        port,
        [state, weakClient, request](bool success, const std::string &message) {
            if (!success) {
                finishRequest(state,
                              false,
                              QString::fromStdString(message).trimmed().isEmpty()
                                  ? QStringLiteral("Unable to connect to local YAOS daemon.")
                                  : QString::fromStdString(message));
                return;
            }

            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->connected = true;
            }

            auto lockedClient = weakClient.lock();
            if (!lockedClient) {
                finishRequest(state, false, QStringLiteral("Local daemon client is no longer available."));
                return;
            }

            std::string payload(request.constData(), static_cast<size_t>(request.size()));
            const bool sent = lockedClient->send(std::move(payload));
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->sent = sent;
            }
            if (!sent) {
                finishRequest(state, false, QStringLiteral("Failed to write daemon request."));
            }
        });
    if (!started) {
        attempt.error = QStringLiteral("Unable to start local daemon connection.");
        return attempt;
    }

    {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait_for(lock,
                           std::chrono::milliseconds(6500),
                           [&state]() { return state->done; });
        if (!state->done) {
            state->error = QStringLiteral("Timed out waiting for daemon response.");
            state->done = true;
        }
        attempt.ok = state->ok;
        attempt.connected = state->connected;
        attempt.sent = state->sent;
        attempt.responseFrame = state->responseFrame;
        attempt.error = state->error;
    }

    client->disconnect();
    return attempt;
}

} // namespace

DaemonRuntimeClient::DaemonRuntimeClient(config::Config config)
    : _config(std::move(config)) {}

bool DaemonRuntimeClient::ensureReady(QString *error) {
    const QJsonObject response = invoke(QStringLiteral("statusSnapshot"), QJsonObject());
    if (response.value("ok").toBool(false)) {
        return true;
    }
    if (error) {
        *error = response.value("error").toString();
    }
    return false;
}

QJsonObject DaemonRuntimeClient::invoke(const QString &method, const QJsonObject &payload) {
    const QByteArray request = protocol::encodeMessage(protocol::makeRequest(method, payload));
    QByteArray responseFrame;
    QString error;
    if (!sendRequest(request, &responseFrame, &error, true)) {
        return QJsonObject{
            {"ok", false},
            {"error", error}
        };
    }

    QJsonObject response;
    if (!protocol::decodeMessage(responseFrame, &response, &error)) {
        return QJsonObject{
            {"ok", false},
            {"error", error.isEmpty() ? QStringLiteral("Invalid daemon response.") : error}
        };
    }
    return response;
}

bool DaemonRuntimeClient::sendRequest(const QByteArray &request,
                                      QByteArray *responseFrame,
                                      QString *error,
                                      bool allowSpawn) const {
    if (!responseFrame) {
        if (error) {
            *error = QStringLiteral("Response target is null.");
        }
        return false;
    }
    if (!ensureFastNetInitialized(error)) {
        return false;
    }

    const QString serverName = resolvedServerName();
    RequestAttempt attempt = sendRequestOnce(serverName, request);
    if (attempt.ok) {
        *responseFrame = attempt.responseFrame;
        return true;
    }

    if (attempt.connected || attempt.sent || !allowSpawn || !_config.runtime.autoSpawnLocalDaemon) {
        if (error) {
            *error = attempt.error.isEmpty()
                ? QStringLiteral("Unable to connect to local YAOS daemon.")
                : attempt.error;
        }
        return false;
    }

    QString spawnError;
    if (!spawnDaemon(&spawnError)) {
        if (error) {
            *error = spawnError;
        }
        return false;
    }

    for (int retry = 0; retry < 20; ++retry) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        attempt = sendRequestOnce(serverName, request);
        if (attempt.ok) {
            *responseFrame = attempt.responseFrame;
            return true;
        }
        if (attempt.connected || attempt.sent) {
            break;
        }
    }

    if (error) {
        *error = attempt.error.isEmpty()
            ? QStringLiteral("Local YAOS daemon started but did not accept connections in time.")
            : attempt.error;
    }
    return false;
}

bool DaemonRuntimeClient::spawnDaemon(QString *error) const {
    const QString serverName = resolvedServerName();
    const QStringList programs = daemonProgramCandidates();
    if (programs.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Unable to determine YAOS daemon executable path.");
        }
        return false;
    }

    for (const QString &program : programs) {
        if (QProcess::startDetached(program, spawnArgsForProgram(program, serverName))) {
            return true;
        }
    }

    if (error) {
        *error = QStringLiteral("Failed to start local YAOS daemon process.");
    }
    return false;
}

QString DaemonRuntimeClient::resolvedServerName() const {
    return protocol::resolveServerName(_config);
}

} // namespace yaos::daemon
