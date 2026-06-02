#ifndef YAOS_CHANNELS_CHANNELHTTP_H
#define YAOS_CHANNELS_CHANNELHTTP_H

#include "../platform/network/FastNetHttpTransport.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QtConcurrent/QtConcurrentRun>

namespace yaos::channels::http {

inline QMap<QByteArray, QByteArray> withHeader(QMap<QByteArray, QByteArray> headers,
                                               const QByteArray &name,
                                               const QByteArray &value) {
    if (!name.trimmed().isEmpty()) {
        headers.insert(name, value);
    }
    return headers;
}

inline platform::network::HttpRequest makeRequest(const QString &method,
                                                  const QString &url,
                                                  const QMap<QByteArray, QByteArray> &headers = {},
                                                  const QByteArray &body = {},
                                                  int timeoutMs = 15000) {
    platform::network::HttpRequest request;
    request.method = method;
    request.url = url;
    request.headers = headers;
    request.body = body;
    request.timeoutMs = timeoutMs;
    return request;
}

inline platform::network::HttpResponse sendJson(const QString &method,
                                                const QString &url,
                                                const QJsonObject &payload,
                                                QMap<QByteArray, QByteArray> headers = {},
                                                int timeoutMs = 15000) {
    headers.insert("Content-Type", "application/json; charset=utf-8");
    return platform::network::FastNetHttpTransport::send(
        makeRequest(method,
                    url,
                    headers,
                    QJsonDocument(payload).toJson(QJsonDocument::Compact),
                    timeoutMs));
}

inline platform::network::HttpResponse sendBody(const QString &method,
                                                const QString &url,
                                                const QByteArray &body,
                                                QMap<QByteArray, QByteArray> headers = {},
                                                int timeoutMs = 15000) {
    return platform::network::FastNetHttpTransport::send(
        makeRequest(method, url, headers, body, timeoutMs));
}

inline platform::network::HttpResponse sendForm(const QString &url,
                                                const QByteArray &body,
                                                QMap<QByteArray, QByteArray> headers = {},
                                                int timeoutMs = 15000) {
    headers.insert("Content-Type", "application/x-www-form-urlencoded");
    return sendBody(QStringLiteral("POST"), url, body, headers, timeoutMs);
}

inline platform::network::HttpResponse sendGet(const QString &url,
                                               QMap<QByteArray, QByteArray> headers = {},
                                               int timeoutMs = 15000) {
    return sendBody(QStringLiteral("GET"), url, QByteArray(), headers, timeoutMs);
}

inline QJsonObject parseJsonObject(const QByteArray &payload, bool *ok = nullptr) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    const bool parsed = parseError.error == QJsonParseError::NoError && document.isObject();
    if (ok) {
        *ok = parsed;
    }
    return parsed ? document.object() : QJsonObject();
}

inline void sendDetached(platform::network::HttpRequest request,
                         const QString &warningContext = QString()) {
    QtConcurrent::run([request, warningContext]() {
        const platform::network::HttpResponse response =
            platform::network::FastNetHttpTransport::send(request);
        if (!warningContext.trimmed().isEmpty() && !response.ok()) {
            qWarning().noquote() << warningContext << response.error;
        }
    });
}

} // namespace yaos::channels::http

#endif // YAOS_CHANNELS_CHANNELHTTP_H
