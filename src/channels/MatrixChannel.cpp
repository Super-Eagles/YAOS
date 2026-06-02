#include "MatrixChannel.h"

#include "ChannelHttp.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTimer>
#include <QUuid>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcMatrix, "yaos.channels.matrix")

namespace yaos::channels {

namespace {

QString normalizedHomeserver(QString homeserver) {
    homeserver = homeserver.trimmed();
    if (homeserver.endsWith('/')) {
        homeserver.chop(1);
    }

    const int clientPathIndex = homeserver.indexOf(QStringLiteral("/_matrix/client/"), 0, Qt::CaseInsensitive);
    if (clientPathIndex >= 0) {
        homeserver = homeserver.left(clientPathIndex);
    }
    while (homeserver.endsWith('/')) {
        homeserver.chop(1);
    }
    return homeserver;
}

QString matrixErrorMessage(const QByteArray &payload, const QString &fallback) {
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        const QString error = obj.value(QStringLiteral("error")).toString().trimmed();
        if (!error.isEmpty()) {
            return error;
        }
    }

    const QString text = QString::fromUtf8(payload).trimmed();
    return text.isEmpty() ? fallback : text;
}

} // namespace

MatrixChannel::MatrixChannel(const config::MatrixConfig &config,
                             bus::MessageBus &bus,
                             QObject *parent)
    : QObject(parent),
      _config(config),
      _bus(bus) {
}

QString MatrixChannel::name() const {
    return QStringLiteral("matrix");
}

bool MatrixChannel::start() {
    if (_running) {
        return true;
    }
    if (!_config.enabled) {
        return false;
    }
    if (_config.accessToken.trimmed().isEmpty()) {
        qWarning(lcMatrix) << "Matrix access token is empty, channel disabled";
        return false;
    }
    if (_config.homeserver.trimmed().isEmpty()) {
        qWarning(lcMatrix) << "Matrix homeserver is empty, channel disabled";
        return false;
    }

    _running = true;
    _initialized = false;
    _since.clear();
    QTimer::singleShot(0, this, &MatrixChannel::scheduleSync);
    return true;
}

void MatrixChannel::stop() {
    _running = false;
    if (_pendingSync) {
        _pendingSync->disconnect(this);
        _pendingSync->deleteLater();
        _pendingSync = nullptr;
    }
}

bool MatrixChannel::isAllowed(const QString &senderId) const {
    if (_config.allowFrom.isEmpty() || _config.allowFrom.contains(QStringLiteral("*"))) {
        return true;
    }
    return _config.allowFrom.contains(senderId);
}

QString MatrixChannel::clientApiBase() const {
    const QString homeserver = normalizedHomeserver(_config.homeserver);
    if (homeserver.isEmpty()) {
        return QString();
    }
    return homeserver + QStringLiteral("/_matrix/client/v3");
}

QMap<QByteArray, QByteArray> MatrixChannel::authorizedHeaders() const {
    QMap<QByteArray, QByteArray> headers;
    headers.insert("Authorization", ("Bearer " + _config.accessToken.trimmed()).toUtf8());
    headers.insert("Accept", "application/json");
    return headers;
}

void MatrixChannel::scheduleSync() {
    if (!_running) {
        return;
    }

    const QString apiBase = clientApiBase();
    if (apiBase.isEmpty()) {
        qWarning(lcMatrix) << "Matrix API base is empty";
        QTimer::singleShot(5000, this, &MatrixChannel::scheduleSync);
        return;
    }

    QUrl url(apiBase + QStringLiteral("/sync"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("timeout"), _initialized ? QStringLiteral("30000")
                                                               : QStringLiteral("0"));
    if (!_since.trimmed().isEmpty()) {
        query.addQueryItem(QStringLiteral("since"), _since);
    }
    url.setQuery(query);

    const int timeoutMs = _initialized ? 40000 : 15000;
    const platform::network::HttpRequest request =
        http::makeRequest(QStringLiteral("GET"),
                          url.toString(QUrl::FullyEncoded),
                          authorizedHeaders(),
                          QByteArray(),
                          timeoutMs);
    auto *watcher = new QFutureWatcher<platform::network::HttpResponse>(this);
    _pendingSync = watcher;
    connect(watcher,
            &QFutureWatcher<platform::network::HttpResponse>::finished,
            this,
            &MatrixChannel::onSyncReply,
            Qt::UniqueConnection);
    watcher->setFuture(QtConcurrent::run([request]() {
        return platform::network::FastNetHttpTransport::send(request);
    }));
}

void MatrixChannel::onSyncReply() {
    auto *watcher = static_cast<QFutureWatcher<platform::network::HttpResponse> *>(sender());
    if (!watcher) {
        return;
    }
    const platform::network::HttpResponse response = watcher->result();
    watcher->deleteLater();
    if (_pendingSync == watcher) {
        _pendingSync = nullptr;
    }

    if (!_running) {
        return;
    }

    const QByteArray payload = response.body;
    if (!response.ok()) {
        qWarning(lcMatrix) << "Matrix sync failed:" << matrixErrorMessage(payload, response.error);
        QTimer::singleShot(2000, this, &MatrixChannel::scheduleSync);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        qWarning(lcMatrix) << "Matrix sync returned invalid JSON";
        QTimer::singleShot(2000, this, &MatrixChannel::scheduleSync);
        return;
    }

    const QJsonObject root = doc.object();
    const QString nextBatch = root.value(QStringLiteral("next_batch")).toString().trimmed();
    if (nextBatch.isEmpty()) {
        qWarning(lcMatrix) << "Matrix sync returned empty next_batch";
        QTimer::singleShot(2000, this, &MatrixChannel::scheduleSync);
        return;
    }

    if (!_initialized) {
        _since = nextBatch;
        _initialized = true;
        QTimer::singleShot(0, this, &MatrixChannel::scheduleSync);
        return;
    }

    processSyncResponse(root);
    _since = nextBatch;
    QTimer::singleShot(0, this, &MatrixChannel::scheduleSync);
}

void MatrixChannel::processSyncResponse(const QJsonObject &root) {
    const QJsonObject rooms = root.value(QStringLiteral("rooms")).toObject();
    const QJsonObject joinedRooms = rooms.value(QStringLiteral("join")).toObject();

    for (auto it = joinedRooms.begin(); it != joinedRooms.end(); ++it) {
        const QString roomId = it.key();
        const QJsonObject roomState = it.value().toObject();
        const QJsonArray events = roomState.value(QStringLiteral("timeline"))
                                      .toObject()
                                      .value(QStringLiteral("events"))
                                      .toArray();
        for (const QJsonValue &eventValue : events) {
            if (!eventValue.isObject()) {
                continue;
            }
            processRoomEvent(roomId, eventValue.toObject());
        }
    }
}

void MatrixChannel::processRoomEvent(const QString &roomId, const QJsonObject &event) {
    if (event.value(QStringLiteral("type")).toString() != QStringLiteral("m.room.message")) {
        return;
    }

    const QString senderId = event.value(QStringLiteral("sender")).toString().trimmed();
    if (senderId.isEmpty()) {
        return;
    }
    if (!_config.userId.trimmed().isEmpty() && senderId == _config.userId.trimmed()) {
        return;
    }
    if (!isAllowed(senderId)) {
        return;
    }

    const QJsonObject content = event.value(QStringLiteral("content")).toObject();
    const QString msgType = content.value(QStringLiteral("msgtype")).toString().trimmed();
    if (!msgType.isEmpty() && msgType != QStringLiteral("m.text") && msgType != QStringLiteral("m.notice")) {
        return;
    }

    QString text = content.value(QStringLiteral("body")).toString();
    if (text.trimmed().isEmpty()) {
        text = content.value(QStringLiteral("formatted_body")).toString();
    }
    if (text.trimmed().isEmpty()) {
        return;
    }

    bus::InboundMessage inbound;
    inbound.channel = name();
    inbound.chatId = roomId;
    inbound.senderId = senderId;
    inbound.content = text.trimmed();
    inbound.metadata = QJsonObject{
        {QStringLiteral("event_id"), event.value(QStringLiteral("event_id"))},
        {QStringLiteral("room_id"), roomId},
        {QStringLiteral("msgtype"), msgType},
        {QStringLiteral("raw"), event}
    };

    _bus.publishInbound(inbound);
}

void MatrixChannel::send(const bus::OutboundMessage &msg) {
    if (_config.accessToken.trimmed().isEmpty() || msg.chatId.trimmed().isEmpty()) {
        return;
    }

    QString text = msg.content.trimmed();
    if (text.isEmpty() && !msg.media.isEmpty()) {
        text = QStringLiteral("[attachments: %1]").arg(msg.media.size());
    }
    if (text.isEmpty()) {
        return;
    }

    const QString roomId = QString::fromUtf8(QUrl::toPercentEncoding(msg.chatId.trimmed()));
    const QString txnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QUrl url(clientApiBase() + QStringLiteral("/rooms/%1/send/m.room.message/%2").arg(roomId, txnId));

    QJsonObject content;
    content.insert(QStringLiteral("msgtype"), QStringLiteral("m.text"));
    content.insert(QStringLiteral("body"), text);

    QMap<QByteArray, QByteArray> headers = authorizedHeaders();
    headers.insert("Content-Type", "application/json; charset=utf-8");
    http::sendDetached(http::makeRequest(QStringLiteral("PUT"),
                                         url.toString(QUrl::FullyEncoded),
                                         headers,
                                         QJsonDocument(content).toJson(QJsonDocument::Compact),
                                         15000),
                       QStringLiteral("Failed to send Matrix message:"));
}

} // namespace yaos::channels
