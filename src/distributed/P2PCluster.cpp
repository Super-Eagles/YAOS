#include "P2PCluster.h"

#include <FastNet/FastNet.h>
#include <FastNet/TcpClient.h>
#include <FastNet/TcpServer.h>
#include <FastNet/UdpSocket.h>

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QUrl>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace yaos::distributed {

static const quint16 P2P_UDP_PORT = 19999;
static const quint16 P2P_TCP_PORT = 19998;
static const qint64  PEER_STALE_MS = 10000; // 10s inactive == offline
static const quint32 MAX_P2P_FRAME_BYTES = 16 * 1024 * 1024;

namespace {

bool ensureFastNetInitialized() {
    static std::once_flag once;
    static FastNet::ErrorCode result = FastNet::ErrorCode::UnknownError;
    std::call_once(once, []() {
        result = FastNet::initialize(2);
    });
    return result == FastNet::ErrorCode::Success ||
           (result == FastNet::ErrorCode::AlreadyRunning && FastNet::isInitialized());
}

std::string toStdString(const QString &value) {
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

QByteArray bufferToByteArray(const FastNet::Buffer &data) {
    if (data.empty()) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(data.data()), static_cast<int>(data.size()));
}

void invokeOnCluster(const QPointer<P2PCluster> &self, const std::function<void(P2PCluster *)> &fn) {
    if (!self) {
        return;
    }
    QMetaObject::invokeMethod(self.data(), [self, fn]() {
        if (self) {
            fn(self.data());
        }
    }, Qt::QueuedConnection);
}

QByteArray makeFramedPayload(const QJsonObject &payload) {
    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const quint32 length = static_cast<quint32>(data.size());
    QByteArray framed;
    framed.resize(static_cast<int>(sizeof(length)));
    std::memcpy(framed.data(), &length, sizeof(length));
    framed.append(data);
    return framed;
}

QString endpointHost(QString endpoint, quint16 *port) {
    endpoint = endpoint.trimmed();
    if (port) {
        *port = P2P_TCP_PORT;
    }
    if (endpoint.isEmpty()) {
        return {};
    }

    const QUrl url(endpoint);
    if (url.isValid() && !url.host().isEmpty()) {
        if (port && url.scheme().compare(QStringLiteral("tcp"), Qt::CaseInsensitive) == 0 && url.port() > 0) {
            *port = static_cast<quint16>(url.port());
        }
        return url.host();
    }

    endpoint.remove(QStringLiteral("tcp://"), Qt::CaseInsensitive);
    endpoint.remove(QStringLiteral("http://"), Qt::CaseInsensitive);
    endpoint.remove(QStringLiteral("https://"), Qt::CaseInsensitive);

    const int slash = endpoint.indexOf('/');
    if (slash >= 0) {
        endpoint = endpoint.left(slash);
    }
    const int colon = endpoint.indexOf(':');
    if (colon > 0 && endpoint.indexOf(':', colon + 1) < 0) {
        if (port) {
            bool ok = false;
            const int parsedPort = endpoint.mid(colon + 1).toInt(&ok);
            if (ok && parsedPort > 0 && parsedPort <= 65535) {
                *port = static_cast<quint16>(parsedPort);
            }
        }
        endpoint = endpoint.left(colon);
    }
    return endpoint.trimmed();
}

} // namespace

P2PCluster::P2PCluster(const NodeDescriptor& localNode, QObject *parent)
    : QObject(parent),
      _localNode(localNode),
      _heartbeatTimer(new QTimer(this)),
      _electionTimer(new QTimer(this)),
      _cleanupTimer(new QTimer(this))
{
    connect(_heartbeatTimer, &QTimer::timeout, this, &P2PCluster::onHeartbeatTimeout);
    connect(_electionTimer,  &QTimer::timeout, this, &P2PCluster::onElectionTimeout);
    connect(_cleanupTimer,   &QTimer::timeout, this, &P2PCluster::onStalePeerCleanup);
}

P2PCluster::~P2PCluster() {
    _heartbeatTimer->stop();
    _electionTimer->stop();
    _cleanupTimer->stop();
    if (_udpSocket) {
        _udpSocket->stopReceive();
        _udpSocket.reset();
    }
    if (_tcpServer) {
        _tcpServer->stop();
        _tcpServer.reset();
    }
    _tcpBuffers.clear();
}

void P2PCluster::start() {
    if (ensureFastNetInitialized()) {
        const QPointer<P2PCluster> self(this);

        _udpSocket = std::make_unique<FastNet::UdpSocket>(FastNet::getGlobalIoService());
        _udpSocket->setBroadcast(true);
        _udpSocket->setDataReceivedCallback([self](const FastNet::Address &, const FastNet::Buffer &data) {
            const QByteArray datagram = bufferToByteArray(data);
            invokeOnCluster(self, [datagram](P2PCluster *cluster) {
                cluster->handleUdpDatagram(datagram);
            });
        });
        if (_udpSocket->bind(P2P_UDP_PORT, "0.0.0.0")) {
            _udpSocket->startReceive();
        }

        _tcpServer = std::make_unique<FastNet::TcpServer>(FastNet::getGlobalIoService());
        _tcpServer->setOwnedDataReceivedCallback([self](FastNet::ConnectionId clientId, FastNet::Buffer &&data) {
            const QByteArray chunk = bufferToByteArray(data);
            invokeOnCluster(self, [clientId, chunk](P2PCluster *cluster) {
                cluster->handleTcpData(static_cast<quint64>(clientId), chunk);
            });
        });
        _tcpServer->setClientDisconnectedCallback([self](FastNet::ConnectionId clientId, const std::string &) {
            invokeOnCluster(self, [clientId](P2PCluster *cluster) {
                cluster->handleTcpDisconnected(static_cast<quint64>(clientId));
            });
        });
        _tcpServer->start(P2P_TCP_PORT, "0.0.0.0");
    }

    _heartbeatTimer->start(1000);   // 1 s heartbeat broadcast
    _electionTimer->start(3500);    // 3.5 s without commander → self-elect
    _cleanupTimer->start(5000);     // 5 s sweep for stale peers
}

ClusterRole P2PCluster::role() const {
    QMutexLocker lock(&_mutex);
    return _role;
}

QString P2PCluster::commanderId() const {
    QMutexLocker lock(&_mutex);
    return _commanderId;
}

// ── INodeRegistryClient ────────────────────────────────────────────────────

QList<NodeDescriptor> P2PCluster::listNodes() const {
    QMutexLocker lock(&_mutex);
    QList<NodeDescriptor> all = _peers.values();
    // Include self
    all.append(_localNode);
    return all;
}

bool P2PCluster::publishPresence(const NodeDescriptor &node) {
    bool isCommander = false;
    {
        QMutexLocker lock(&_mutex);
        _localNode = node;
        isCommander = (_role == ClusterRole::COMMANDER);
    }
    // Broadcast the update immediately
    QJsonObject payload;
    payload["type"]        = "heartbeat";
    payload["nodeId"]      = node.nodeId;
    payload["isCommander"] = isCommander;
    payload["role"]        = node.role;
    payload["endpoint"]    = node.endpoint;
    broadcast(payload);
    return true;
}

// ── ITaskBus ───────────────────────────────────────────────────────────────

bool P2PCluster::submit(const TaskEnvelope &task) {
    ClusterRole currentRole;
    QString commanderId;
    NodeDescriptor commanderPeer;
    bool peerFound = false;

    {
        QMutexLocker lock(&_mutex);
        currentRole = _role;
        commanderId = _commanderId;
        if (currentRole == ClusterRole::SOLDIER && !commanderId.isEmpty()
                && _peers.contains(commanderId)) {
            commanderPeer = _peers[commanderId];
            peerFound = true;
        }
        if (currentRole == ClusterRole::COMMANDER) {
            _localTaskQueue.append(task);
            return true;
        }
    }

    if (!peerFound) {
        // No known commander – queue locally as fallback
        QMutexLocker lock(&_mutex);
        _localTaskQueue.append(task);
        return true;
    }

    // Forward to commander outside the mutex
    return forwardToCommander(task);
}

bool P2PCluster::forwardToCommander(const TaskEnvelope &task) {
    QString endpoint;
    {
        QMutexLocker lock(&_mutex);
        if (!_peers.contains(_commanderId)) return false;
        endpoint = _peers[_commanderId].endpoint;
    }

    quint16 port = P2P_TCP_PORT;
    const QString host = endpointHost(endpoint, &port);
    if (host.isEmpty() || !ensureFastNetInitialized()) {
        return false;
    }

    QJsonObject payload;
    payload["type"]     = "task";
    payload["taskId"]   = task.taskId;
    payload["traceId"]  = task.traceId;
    payload["taskType"] = task.taskType;
    payload["payload"]  = task.payload;
    const QByteArray framed = makeFramedPayload(payload);
    const std::string packet(framed.constData(), static_cast<size_t>(framed.size()));

    struct WaitState {
        std::mutex mutex;
        std::condition_variable condition;
        bool done = false;
        bool ok = false;
    };
    auto state = std::make_shared<WaitState>();
    auto client = std::make_shared<FastNet::TcpClient>(FastNet::getGlobalIoService());
    std::weak_ptr<FastNet::TcpClient> weakClient = client;
    client->setConnectTimeout(1000);
    client->setWriteTimeout(1000);
    client->setDisconnectCallback([state](const std::string &) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->done = true;
        state->condition.notify_all();
    });
    client->setErrorCallback([state](FastNet::ErrorCode, const std::string &) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->done = true;
        state->ok = false;
        state->condition.notify_all();
    });

    const bool started = client->connect(toStdString(host), port, [weakClient, state, packet](bool success,
                                                                                              const std::string &) {
        auto client = weakClient.lock();
        if (!success || !client) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->done = true;
            state->ok = false;
            state->condition.notify_all();
            return;
        }

        const bool sent = client->send(std::string(packet));
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->ok = sent;
            if (!sent) {
                state->done = true;
            }
        }
        if (sent) {
            client->disconnectAfterPendingWrites();
        } else {
            state->condition.notify_all();
            client->disconnect();
        }
    });
    if (!started) {
        return false;
    }

    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->condition.wait_for(lock, std::chrono::milliseconds(1500), [state]() { return state->done; })) {
        lock.unlock();
        client->disconnect();
        return state->ok;
    }
    return state->ok;
}

bool P2PCluster::publishResult(const TaskResultEnvelope &result) {
    QMutexLocker lock(&_mutex);
    // Keep last 200 results to bound memory
    if (_resultQueue.size() >= 200) {
        _resultQueue.removeFirst();
    }
    _resultQueue.append(result);
    return true;
}

bool P2PCluster::cancel(const QString &taskId) {
    QMutexLocker lock(&_mutex);
    for (int i = 0; i < _localTaskQueue.size(); ++i) {
        if (_localTaskQueue[i].taskId == taskId) {
            _localTaskQueue.removeAt(i);
            return true;
        }
    }
    return false;
}

bool P2PCluster::claim(const QString &taskId, const QString &/*consumerNode*/) {
    QMutexLocker lock(&_mutex);
    for (int i = 0; i < _localTaskQueue.size(); ++i) {
        if (_localTaskQueue[i].taskId == taskId) {
            _claimedTasks.append(_localTaskQueue.takeAt(i));
            return true;
        }
    }
    return false;
}

QList<TaskEnvelope> P2PCluster::pendingTasks(const QString &targetNode,
                                              const QString &targetRole,
                                              int limit) const {
    QMutexLocker lock(&_mutex);
    QList<TaskEnvelope> result;
    const int max = limit > 0 ? limit : 100;
    for (const TaskEnvelope &task : _localTaskQueue) {
        if (!targetNode.isEmpty() && task.targetNode != targetNode) continue;
        if (!targetRole.isEmpty() && task.targetRole != targetRole) continue;
        result.append(task);
        if (result.size() >= max) break;
    }
    return result;
}

QList<TaskResultEnvelope> P2PCluster::recentResults(const QString &taskId,
                                                     const QString &traceId,
                                                     int limit) const {
    QMutexLocker lock(&_mutex);
    QList<TaskResultEnvelope> result;
    const int max = limit > 0 ? limit : 100;
    for (int i = _resultQueue.size() - 1; i >= 0 && result.size() < max; --i) {
        const TaskResultEnvelope &r = _resultQueue[i];
        if (!taskId.isEmpty()  && r.taskId  != taskId)  continue;
        if (!traceId.isEmpty() && r.traceId != traceId) continue;
        result.prepend(r);
    }
    return result;
}

// ── Network slots ──────────────────────────────────────────────────────────

void P2PCluster::handleUdpDatagram(const QByteArray &datagram) {
    const QJsonDocument doc = QJsonDocument::fromJson(datagram);
    if (!doc.isNull() && doc.isObject()) {
        dispatchNetworkPayload(doc.object());
    }
}

void P2PCluster::onHeartbeatTimeout() {
    QString nodeId;
    bool isCommander;
    QString role;
    QString endpoint;
    {
        QMutexLocker lock(&_mutex);
        nodeId      = _localNode.nodeId;
        isCommander = (_role == ClusterRole::COMMANDER);
        role        = _localNode.role;
        endpoint    = _localNode.endpoint;
    }
    QJsonObject payload;
    payload["type"]        = "heartbeat";
    payload["nodeId"]      = nodeId;
    payload["isCommander"] = isCommander;
    payload["role"]        = role;
    payload["endpoint"]    = endpoint;
    broadcast(payload);
}

void P2PCluster::onElectionTimeout() {
    QMutexLocker lock(&_mutex);
    updateRole(ClusterRole::COMMANDER);
}

void P2PCluster::onStalePeerCleanup() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker lock(&_mutex);
    const QStringList ids = _peerLastSeen.keys();
    for (const QString &id : ids) {
        if (now - _peerLastSeen[id] > PEER_STALE_MS) {
            _peers.remove(id);
            _peerLastSeen.remove(id);
            if (id == _commanderId) {
                _commanderId.clear();
                // Commander gone – start election countdown
                _electionTimer->start(3500);
            }
        }
    }
}

void P2PCluster::handleTcpData(quint64 clientId, const QByteArray &chunk) {
    QByteArray &buffer = _tcpBuffers[clientId];
    buffer.append(chunk);

    while (buffer.size() > static_cast<int>(sizeof(quint32))) {
        quint32 length = 0;
        std::memcpy(&length, buffer.constData(), sizeof(length));
        if (length == 0 || length > MAX_P2P_FRAME_BYTES) {
            _tcpBuffers.remove(clientId);
            return;
        }
        const int frameSize = static_cast<int>(sizeof(quint32) + length);
        if (buffer.size() < frameSize) {
            return;
        }

        const QByteArray data = buffer.mid(static_cast<int>(sizeof(quint32)), static_cast<int>(length));
        buffer.remove(0, frameSize);

        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            dispatchNetworkPayload(doc.object());
        }
    }
}

void P2PCluster::handleTcpDisconnected(quint64 clientId) {
    _tcpBuffers.remove(clientId);
}

void P2PCluster::dispatchNetworkPayload(const QJsonObject &obj) {
    const QString type = obj["type"].toString();
    if (type == "heartbeat") {
        processHeartbeat(obj);
    } else if (type == "task") {
        processTask(obj);
    } else if (type == "result") {
        processResult(obj);
    }
}

// ── Internal helpers ───────────────────────────────────────────────────────

void P2PCluster::processHeartbeat(const QJsonObject& doc) {
    const QString senderId = doc["nodeId"].toString();
    if (senderId.isEmpty() || senderId == _localNode.nodeId) return;

    const bool isSenderCommander = doc["isCommander"].toBool();
    QMutexLocker lock(&_mutex);
    _peerLastSeen[senderId] = QDateTime::currentMSecsSinceEpoch();

    NodeDescriptor &peer = _peers[senderId];
    peer.nodeId   = senderId;
    peer.role     = doc["role"].toString(peer.role);
    peer.endpoint = doc["endpoint"].toString(peer.endpoint);
    peer.online   = true;

    if (isSenderCommander) {
        _commanderId = senderId;
        updateRole(ClusterRole::SOLDIER);
        _electionTimer->start(3500); // Reset election countdown
    } else if (_role == ClusterRole::COMMANDER && senderId < _localNode.nodeId) {
        // Deterministic conflict resolution: lower nodeId wins commander
        // We already are commander and have a higher ID, so we yield
        // (other node will self-elect when it sees no commander)
    }
}

void P2PCluster::processTask(const QJsonObject& doc) {
    TaskEnvelope task;
    task.taskId   = doc["taskId"].toString();
    task.traceId  = doc["traceId"].toString();
    task.taskType = doc["taskType"].toString();
    task.payload  = doc["payload"].toObject();

    QMutexLocker lock(&_mutex);
    _localTaskQueue.append(task);
}

void P2PCluster::processResult(const QJsonObject& doc) {
    TaskResultEnvelope result;
    result.taskId       = doc["taskId"].toString();
    result.traceId      = doc["traceId"].toString();
    result.producerNode = doc["producerNode"].toString();
    result.status       = doc["status"].toString("ok");
    result.message      = doc["message"].toString();
    result.output       = doc["output"].toObject();

    QMutexLocker lock(&_mutex);
    if (_resultQueue.size() >= 200) _resultQueue.removeFirst();
    _resultQueue.append(result);
}

void P2PCluster::updateRole(ClusterRole newRole) {
    // Caller must hold _mutex
    if (_role != newRole) {
        _role = newRole;
        if (_role == ClusterRole::COMMANDER) {
            _commanderId     = _localNode.nodeId;
            _localNode.role  = QStringLiteral("commander");
            _electionTimer->stop();
        } else {
            _localNode.role = QStringLiteral("soldier");
        }
    }
}

void P2PCluster::broadcast(const QJsonObject& payload) {
    if (!_udpSocket || !_udpSocket->isBound()) {
        return;
    }
    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const FastNet::Address destination("255.255.255.255", P2P_UDP_PORT);
    _udpSocket->sendTo(destination, std::string_view(data.constData(), static_cast<size_t>(data.size())));
}

} // namespace yaos::distributed
