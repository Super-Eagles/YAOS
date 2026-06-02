#include "RemoteControlClient.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <utility>

#include "../platform/network/FastNetHttpTransport.h"
#include "../runtime/StructuredLog.h"

namespace yaos::distributed {

namespace {

const char *kControlClientUserAgent = "YAOS-ControlClient/1.0";

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

RemoteControlClient::RemoteControlClient(QString endpoint,
                                         int timeoutMs)
    : _endpoint(normalizedEndpoint(std::move(endpoint))),
      _timeoutMs(timeoutMs > 0 ? timeoutMs : 3500) {}

bool RemoteControlClient::isReady() const {
    const QUrl url(_endpoint);
    return !_endpoint.isEmpty() && url.isValid() && !url.host().trimmed().isEmpty();
}

QString RemoteControlClient::endpoint() const {
    return _endpoint;
}

QJsonObject RemoteControlClient::get(const QString &path,
                                     QString *error) const {
    return request(QStringLiteral("GET"), path, nullptr, error);
}

QJsonObject RemoteControlClient::post(const QString &path,
                                      const QJsonObject &payload,
                                      QString *error) const {
    return request(QStringLiteral("POST"), path, &payload, error);
}

bool RemoteControlClient::ping(QString *error) const {
    QJsonObject response = get(QStringLiteral("/health"), error);
    if (!response.isEmpty() && response.value(QStringLiteral("ok")).toBool(false)) {
        return true;
    }
    response = get(QStringLiteral("/v1/control/health"), error);
    return !response.isEmpty() && response.value(QStringLiteral("ok")).toBool(false);
}

QJsonObject RemoteControlClient::request(const QString &method,
                                         const QString &path,
                                         const QJsonObject *payload,
                                         QString *error) const {
    if (error) {
        error->clear();
    }
    if (!isReady()) {
        if (error) {
            *error = "Control-plane endpoint is not configured.";
        }
        return {};
    }

    const QUrl url(buildUrl(path));
    if (!url.isValid() || url.host().trimmed().isEmpty()) {
        if (error) {
            *error = "Control-plane URL is invalid.";
        }
        return {};
    }

    const QString traceId = runtime::StructuredLog::ensureTraceId(traceIdFromPayload(payload));
    const runtime::ScopedTraceContext traceScope(traceId);

    runtime::StructuredLog::log(QStringLiteral("info"),
                                QStringLiteral("http.client"),
                                QStringLiteral("Outbound control HTTP request"),
                                QJsonObject{
                                    {QStringLiteral("component"), QStringLiteral("control")},
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
    httpRequest.headers.insert("User-Agent", kControlClientUserAgent);
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
        runtime::StructuredLog::log(QStringLiteral("warning"),
                                    QStringLiteral("http.client"),
                                    QStringLiteral("Control HTTP request failed"),
                                    QJsonObject{
                                        {QStringLiteral("component"), QStringLiteral("control")},
                                        {QStringLiteral("method"), method},
                                        {QStringLiteral("path"), path},
                                        {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)},
                                        {QStringLiteral("statusCode"), status},
                                        {QStringLiteral("error"), localError}
                                    });
        if (error) {
            if (!body.isEmpty()) {
                *error = localError + ": " + QString::fromUtf8(body);
            } else {
                *error = localError;
            }
        }
        return {};
    }

    runtime::StructuredLog::log(QStringLiteral("info"),
                                QStringLiteral("http.client"),
                                QStringLiteral("Control HTTP request completed"),
                                QJsonObject{
                                    {QStringLiteral("component"), QStringLiteral("control")},
                                    {QStringLiteral("method"), method},
                                    {QStringLiteral("path"), path},
                                    {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)},
                                    {QStringLiteral("statusCode"), status},
                                    {QStringLiteral("responseBytes"), body.size()}
                                });

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = "Control-plane returned invalid JSON.";
        }
        return {};
    }

    if (doc.isObject()) {
        return doc.object();
    }
    if (doc.isArray()) {
        QJsonObject obj;
        obj["items"] = doc.array();
        return obj;
    }

    if (error) {
        *error = "Control-plane returned an unsupported JSON payload.";
    }
    return {};
}

QString RemoteControlClient::buildUrl(const QString &path) const {
    if (path.startsWith('/')) {
        return _endpoint + path;
    }
    return _endpoint + "/" + path;
}

} // namespace yaos::distributed
