#ifndef YAOS_DAEMON_LOCALDAEMONPROTOCOL_H
#define YAOS_DAEMON_LOCALDAEMONPROTOCOL_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include "../config/Config.h"

namespace yaos::daemon::protocol {

QString defaultServerName(const config::Config &config);
QString resolveServerName(const config::Config &config, const QString &explicitName = QString());
quint16 serverPort(const QString &serverName);

QJsonObject makeRequest(const QString &method, const QJsonObject &payload = QJsonObject());
QByteArray encodeMessage(const QJsonObject &message);
bool decodeMessage(const QByteArray &frame, QJsonObject *message, QString *error = nullptr);

} // namespace yaos::daemon::protocol

#endif // YAOS_DAEMON_LOCALDAEMONPROTOCOL_H
