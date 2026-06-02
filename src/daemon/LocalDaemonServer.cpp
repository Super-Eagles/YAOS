#include "LocalDaemonServer.h"

#include <FastNet/FastNet.h>
#include <FastNet/TcpServer.h>

#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>

#include <mutex>
#include <string>

#include "../runtime/LocalRuntimeClient.h"
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

QByteArray bufferToByteArray(const FastNet::Buffer &buffer) {
    if (buffer.empty()) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(buffer.data()),
                      static_cast<int>(buffer.size()));
}

} // namespace

LocalDaemonServer::LocalDaemonServer(QObject *parent)
    : QObject(parent),
      _client(std::make_unique<::yaos::runtime::LocalRuntimeClient>()) {}

LocalDaemonServer::~LocalDaemonServer() {
    stop();
}

bool LocalDaemonServer::start(const QString &serverName, QString *error) {
    stop();
    if (!ensureFastNetInitialized(error)) {
        return false;
    }

    _serverName = serverName.trimmed();
    if (_serverName.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Local daemon server name is empty.");
        }
        return false;
    }

    _serverPort = protocol::serverPort(_serverName);
    _server = std::make_unique<FastNet::TcpServer>(FastNet::getGlobalIoService());

    QPointer<LocalDaemonServer> guard(this);
    _server->setDataReceivedCallback([guard](FastNet::ConnectionId clientId,
                                             const FastNet::Buffer &data) {
        if (!guard) {
            return;
        }
        const QByteArray chunk = bufferToByteArray(data);
        QMetaObject::invokeMethod(guard.data(),
                                  [guard, clientId, chunk]() {
                                      if (guard) {
                                          guard->handleData(static_cast<quint64>(clientId), chunk);
                                      }
                                  },
                                  Qt::QueuedConnection);
    });
    _server->setClientDisconnectedCallback([guard](FastNet::ConnectionId clientId,
                                                  const std::string &) {
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(),
                                  [guard, clientId]() {
                                      if (guard) {
                                          guard->handleDisconnected(static_cast<quint64>(clientId));
                                      }
                                  },
                                  Qt::QueuedConnection);
    });

    const FastNet::Error startError = _server->start(_serverPort, kLoopbackHost);
    if (startError.isFailure()) {
        if (error) {
            *error = QStringLiteral("Failed to start local YAOS daemon on %1:%2: %3")
                         .arg(QString::fromLatin1(kLoopbackHost))
                         .arg(_serverPort)
                         .arg(QString::fromStdString(startError.toString()));
        }
        _server.reset();
        _serverName.clear();
        _serverPort = 0;
        return false;
    }

    return true;
}

void LocalDaemonServer::stop() {
    if (_server) {
        for (const FastNet::ConnectionId clientId : _server->getClientIds()) {
            _server->disconnectClient(clientId);
        }
        _server->stop();
        _server.reset();
    }

    _buffers.clear();
    _serverName.clear();
    _serverPort = 0;
}

QString LocalDaemonServer::serverName() const {
    return _serverName;
}

quint16 LocalDaemonServer::serverPort() const {
    return _serverPort;
}

void LocalDaemonServer::handleData(quint64 clientId, const QByteArray &chunk) {
    if (!_server || !_client) {
        return;
    }

    QByteArray &buffer = _buffers[clientId];
    buffer.append(chunk);

    const int newline = buffer.indexOf('\n');
    if (newline < 0) {
        return;
    }

    const QByteArray frame = buffer.left(newline);
    buffer.remove(0, newline + 1);

    QJsonObject request;
    QString error;
    QJsonObject response;
    if (!protocol::decodeMessage(frame, &request, &error)) {
        response = QJsonObject{
            {"ok", false},
            {"error", error.isEmpty() ? QStringLiteral("Invalid daemon request.") : error}
        };
    } else {
        QMutexLocker locker(&_requestMutex);
        response = _client->invoke(request.value("method").toString(),
                                   request.value("payload").toObject());
    }

    const QByteArray encoded = protocol::encodeMessage(response);
    std::string payload(encoded.constData(), static_cast<size_t>(encoded.size()));
    _server->sendToClient(static_cast<FastNet::ConnectionId>(clientId),
                          std::move(payload));
    _server->closeClientAfterPendingWrites(static_cast<FastNet::ConnectionId>(clientId));
}

void LocalDaemonServer::handleDisconnected(quint64 clientId) {
    _buffers.remove(clientId);
}

} // namespace yaos::daemon
