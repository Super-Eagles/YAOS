#include "RemoteRuntimeClient.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QUrl>
#include <utility>

#include "../platform/network/FastNetHttpTransport.h"
#include "StructuredLog.h"

namespace yaos::runtime {

namespace {

const char *kRuntimeClientUserAgent = "YAOS-RemoteRuntimeClient/1.0";

QString normalizedEndpoint(QString endpoint) {
    endpoint = endpoint.trimmed();
    while (endpoint.endsWith('/')) {
        endpoint.chop(1);
    }
    if (!endpoint.contains("://")) {
        endpoint.prepend(QStringLiteral("http://"));
    }
    QUrl url(endpoint);
    const QString host = url.host().trimmed();
    if (host == QStringLiteral("0.0.0.0")) {
        url.setHost(QStringLiteral("127.0.0.1"));
        endpoint = url.toString(QUrl::FullyEncoded);
    } else if (host == QStringLiteral("::")) {
        url.setHost(QStringLiteral("::1"));
        endpoint = url.toString(QUrl::FullyEncoded);
    }
    return endpoint;
}

QString traceIdFromPayload(const QJsonObject *payload) {
    if (!payload) {
        return QString();
    }
    const QString traceId = payload->value(QStringLiteral("traceId")).toString().trimmed();
    if (!traceId.isEmpty()) {
        return traceId;
    }
    return payload->value(QStringLiteral("trace_id")).toString().trimmed();
}

} // namespace

RemoteRuntimeClient::RemoteRuntimeClient(config::Config config,
                                         int timeoutMs)
    : _config(std::move(config)),
      _endpoint(normalizedEndpoint(_config.runtime.endpoint)),
      _timeoutMs(timeoutMs > 0 ? timeoutMs : 6000) {}

bool RemoteRuntimeClient::ensureReady(QString *error) const {
    return ping(error);
}

QString RemoteRuntimeClient::endpoint() const {
    return _endpoint;
}

QJsonObject RemoteRuntimeClient::invoke(const QString &method, const QJsonObject &payload) {
    QString error;
    QJsonObject response = post(QStringLiteral("/v1/runtime/invoke"),
                                QJsonObject{
                                    {QStringLiteral("method"), method},
                                    {QStringLiteral("payload"), payload}
                                },
                                &error);
    if (!response.isEmpty()) {
        return response;
    }
    return QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"),
         error.isEmpty()
             ? QStringLiteral("Remote runtime request failed.")
             : error}
    };
}

QJsonObject RemoteRuntimeClient::get(const QString &path, QString *error) const {
    return request(QStringLiteral("GET"), path, nullptr, error);
}

QJsonObject RemoteRuntimeClient::post(const QString &path,
                                      const QJsonObject &payload,
                                      QString *error) const {
    return request(QStringLiteral("POST"), path, &payload, error);
}

bool RemoteRuntimeClient::ping(QString *error) const {
    QJsonObject response = get(QStringLiteral("/health"), error);
    if (!response.isEmpty() && response.value(QStringLiteral("ok")).toBool(false)) {
        return true;
    }
    response = get(QStringLiteral("/v1/runtime/health"), error);
    return !response.isEmpty() && response.value(QStringLiteral("ok")).toBool(false);
}

QString RemoteRuntimeClient::buildUrl(const QString &path) const {
    if (path.startsWith('/')) {
        return _endpoint + path;
    }
    return _endpoint + QStringLiteral("/") + path;
}

QJsonObject RemoteRuntimeClient::request(const QString &method,
                                         const QString &path,
                                         const QJsonObject *payload,
                                         QString *error) const {
    if (error) {
        error->clear();
    }
    const QUrl url(buildUrl(path));
    if (_endpoint.isEmpty() || !url.isValid() || url.host().trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Runtime endpoint is not configured.");
        }
        return {};
    }

    const QString traceId = StructuredLog::ensureTraceId(traceIdFromPayload(payload));
    const ScopedTraceContext traceScope(traceId);

    StructuredLog::log(QStringLiteral("info"),
                       QStringLiteral("http.client"),
                       QStringLiteral("Outbound runtime HTTP request"),
                       QJsonObject{
                           {QStringLiteral("component"), QStringLiteral("runtime")},
                           {QStringLiteral("method"), method},
                           {QStringLiteral("path"), path},
                           {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)},
                           {QStringLiteral("timeoutMs"), _timeoutMs}
                       });

    platform::network::HttpRequest httpRequest;
    httpRequest.method = method;
    httpRequest.url = url.toString(QUrl::FullyEncoded);
    httpRequest.timeoutMs = _timeoutMs;
    httpRequest.headers.insert("Accept", "application/json");
    httpRequest.headers.insert("Content-Type", "application/json; charset=utf-8");
    httpRequest.headers.insert("User-Agent", kRuntimeClientUserAgent);
    httpRequest.headers.insert("X-YAOS-Trace-Id", traceId.toUtf8());
    if (method != QStringLiteral("GET")) {
        const QJsonObject safePayload = payload ? *payload : QJsonObject();
        httpRequest.body = QJsonDocument(safePayload).toJson(QJsonDocument::Compact);
    }

    const platform::network::HttpResponse httpResponse =
        platform::network::FastNetHttpTransport::send(httpRequest);
    const QByteArray body = httpResponse.body;
    const int status = httpResponse.statusCode;
    const QString localError = httpResponse.error;

    if (!localError.isEmpty()) {
        StructuredLog::log(QStringLiteral("warning"),
                           QStringLiteral("http.client"),
                           QStringLiteral("Runtime HTTP request failed"),
                           QJsonObject{
                               {QStringLiteral("component"), QStringLiteral("runtime")},
                               {QStringLiteral("method"), method},
                               {QStringLiteral("path"), path},
                               {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)},
                               {QStringLiteral("statusCode"), status},
                               {QStringLiteral("error"), localError}
                           });
        if (error) {
            *error = body.isEmpty()
                ? localError
                : localError + QStringLiteral(": ") + QString::fromUtf8(body);
        }
        return {};
    }

    StructuredLog::log(QStringLiteral("info"),
                       QStringLiteral("http.client"),
                       QStringLiteral("Runtime HTTP request completed"),
                       QJsonObject{
                           {QStringLiteral("component"), QStringLiteral("runtime")},
                           {QStringLiteral("method"), method},
                           {QStringLiteral("path"), path},
                           {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)},
                           {QStringLiteral("statusCode"), status},
                           {QStringLiteral("responseBytes"), body.size()}
                       });

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("Runtime returned invalid JSON.");
        }
        return {};
    }
    if (document.isObject()) {
        return document.object();
    }

    if (error) {
        *error = QStringLiteral("Runtime returned an unsupported JSON payload.");
    }
    return {};
}

} // namespace yaos::runtime
