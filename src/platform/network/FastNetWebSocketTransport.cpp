#include "FastNetWebSocketTransport.h"

#include <FastNet/FastNet.h>
#include <FastNet/WebSocketClient.h>

#include <QMetaObject>
#include <QPointer>

#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace yaos::platform::network {

namespace {

bool ensureFastNetInitialized() {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, []() {
        const FastNet::ErrorCode result = FastNet::initialize(8);
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

FastNet::WebSocketHeaders toFastNetHeaders(const QMap<QByteArray, QByteArray> &headers) {
    FastNet::WebSocketHeaders result;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        result[toStdString(it.key())] = toStdString(it.value());
    }
    return result;
}

std::vector<std::string> toFastNetSubprotocols(const QStringList &subprotocols) {
    std::vector<std::string> result;
    result.reserve(static_cast<size_t>(subprotocols.size()));
    for (const QString &subprotocol : subprotocols) {
        const QString trimmed = subprotocol.trimmed();
        if (!trimmed.isEmpty()) {
            result.push_back(toStdString(trimmed));
        }
    }
    return result;
}

void invokeOnObject(const QPointer<FastNetWebSocketTransport> &self,
                    const std::function<void(FastNetWebSocketTransport *)> &fn) {
    if (!self) {
        return;
    }
    QMetaObject::invokeMethod(self.data(), [self, fn]() {
        if (self) {
            fn(self.data());
        }
    }, Qt::QueuedConnection);
}

} // namespace

FastNetWebSocketTransport::FastNetWebSocketTransport(QObject *parent)
    : QObject(parent) {
}

FastNetWebSocketTransport::~FastNetWebSocketTransport() {
    close();
}

void FastNetWebSocketTransport::open(const QUrl &url) {
    open(url, QMap<QByteArray, QByteArray>(), QStringList());
}

void FastNetWebSocketTransport::open(const QUrl &url,
                                     const QMap<QByteArray, QByteArray> &headers,
                                     const QStringList &subprotocols) {
    close();

    _lastError.clear();
    _connecting = true;
    _connected = false;
    const quint64 generation = ++_generation;

    if (!ensureFastNetInitialized()) {
        _connecting = false;
        _lastError = QStringLiteral("FastNet initialization failed.");
        emit errorMessage(_lastError);
        emit disconnected();
        return;
    }

    const QPointer<FastNetWebSocketTransport> self(this);
    auto client = std::make_shared<FastNet::WebSocketClient>(FastNet::getGlobalIoService());
    if (!headers.isEmpty()) {
        client->setHandshakeHeaders(toFastNetHeaders(headers));
    }
    if (!subprotocols.isEmpty()) {
        client->setSubprotocols(toFastNetSubprotocols(subprotocols));
    }
    client->setMessageCallback([self, generation](const std::string &message) {
        invokeOnObject(self, [generation, message](FastNetWebSocketTransport *transport) {
            if (transport->_generation != generation) {
                return;
            }
            emit transport->textMessageReceived(QString::fromStdString(message));
        });
    });
    client->setErrorCallback([self, generation](FastNet::ErrorCode, const std::string &message) {
        invokeOnObject(self, [generation, message](FastNetWebSocketTransport *transport) {
            if (transport->_generation != generation) {
                return;
            }
            transport->_lastError = QString::fromStdString(message);
            transport->_connecting = false;
            emit transport->errorMessage(transport->_lastError);
        });
    });
    client->setCloseCallback([self, generation](uint16_t, const std::string &reason) {
        invokeOnObject(self, [generation, reason](FastNetWebSocketTransport *transport) {
            if (transport->_generation != generation) {
                return;
            }
            transport->_connected = false;
            transport->_connecting = false;
            if (!reason.empty()) {
                transport->_lastError = QString::fromStdString(reason);
            }
            emit transport->disconnected();
        });
    });

    _client = client;
    const bool started = client->connect(toStdString(url.toString(QUrl::FullyEncoded)),
                                        [self, generation](bool success, const std::string &message) {
        invokeOnObject(self, [generation, success, message](FastNetWebSocketTransport *transport) {
            if (transport->_generation != generation) {
                return;
            }
            transport->_connecting = false;
            transport->_connected = success;
            if (success) {
                transport->_lastError.clear();
                emit transport->connected();
            } else {
                transport->_lastError = QString::fromStdString(message.empty()
                                                                   ? std::string("WebSocket connection failed.")
                                                                   : message);
                emit transport->errorMessage(transport->_lastError);
                emit transport->disconnected();
            }
        });
    });

    if (!started) {
        _connecting = false;
        _connected = false;
        _lastError = QStringLiteral("WebSocket connection failed to start.");
        emit errorMessage(_lastError);
        emit disconnected();
    }
}

void FastNetWebSocketTransport::close() {
    ++_generation;
    if (_client) {
        _client->close();
        _client.reset();
    }
    _connecting = false;
    _connected = false;
}

bool FastNetWebSocketTransport::sendTextMessage(const QString &message) {
    if (!_client || !_connected) {
        _lastError = QStringLiteral("WebSocket is not connected.");
        return false;
    }
    const bool ok = _client->sendText(toStdString(message));
    if (!ok) {
        _lastError = QStringLiteral("WebSocket send failed.");
    }
    return ok;
}

bool FastNetWebSocketTransport::isValid() const {
    return _connected && _client && _client->isConnected();
}

bool FastNetWebSocketTransport::isOpenOrConnecting() const {
    return _connecting || isValid();
}

QString FastNetWebSocketTransport::errorString() const {
    return _lastError;
}

} // namespace yaos::platform::network
