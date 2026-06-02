#include "LocalDaemonProtocol.h"

#include <QCryptographicHash>
#include <QJsonDocument>

namespace yaos::daemon::protocol {

namespace {

QString endpointServerName(const QString &endpoint) {
    const QString trimmed = endpoint.trimmed();
    if (trimmed.startsWith("local://", Qt::CaseInsensitive)) {
        return trimmed.mid(QStringLiteral("local://").size()).trimmed();
    }
    if (trimmed.startsWith("ipc://", Qt::CaseInsensitive)) {
        return trimmed.mid(QStringLiteral("ipc://").size()).trimmed();
    }
    return QString();
}

} // namespace

QString defaultServerName(const config::Config &config) {
    const QString seed = QStringLiteral("%1|%2|%3")
                             .arg(config.workspacePath(),
                                  config.deployment.clusterId,
                                  config.deployment.nodeId);
    const QByteArray digest = QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("yaosd-%1").arg(QString::fromLatin1(digest.left(12)));
}

QString resolveServerName(const config::Config &config, const QString &explicitName) {
    const QString requested = explicitName.trimmed();
    if (!requested.isEmpty()) {
        return requested;
    }

    const QString endpointName = endpointServerName(config.runtime.endpoint);
    if (!endpointName.isEmpty()) {
        return endpointName;
    }
    return defaultServerName(config);
}

quint16 serverPort(const QString &serverName) {
    const QByteArray digest = QCryptographicHash::hash(serverName.trimmed().toUtf8(),
                                                       QCryptographicHash::Sha1);
    quint32 value = 0;
    for (int index = 0; index < 4 && index < digest.size(); ++index) {
        value = (value << 8) | static_cast<unsigned char>(digest.at(index));
    }
    return static_cast<quint16>(23000 + (value % 20000));
}

QJsonObject makeRequest(const QString &method, const QJsonObject &payload) {
    return QJsonObject{
        {"method", method},
        {"payload", payload}
    };
}

QByteArray encodeMessage(const QJsonObject &message) {
    QByteArray frame = QJsonDocument(message).toJson(QJsonDocument::Compact);
    frame.append('\n');
    return frame;
}

bool decodeMessage(const QByteArray &frame, QJsonObject *message, QString *error) {
    if (!message) {
        if (error) {
            *error = QStringLiteral("Decode target is null.");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(frame.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = parseError.errorString();
        }
        return false;
    }

    *message = doc.object();
    return true;
}

} // namespace yaos::daemon::protocol
