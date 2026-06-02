#ifndef YAOS_PLATFORM_NETWORK_FASTNETHTTPTRANSPORT_H
#define YAOS_PLATFORM_NETWORK_FASTNETHTTPTRANSPORT_H

#include <functional>
#include <QByteArray>
#include <QMap>
#include <QString>

namespace yaos::platform::network {

struct HttpRequest {
    QString method;
    QString url;
    QMap<QByteArray, QByteArray> headers;
    QByteArray body;
    QString proxyUrl;
    QString caFile;
    int timeoutMs = 6000;
    bool verifyTlsPeer = true;
};

struct HttpResponse {
    int statusCode = 0;
    QByteArray body;
    QMap<QByteArray, QByteArray> headers;
    QString error;

    bool ok() const {
        return error.isEmpty() && statusCode >= 200 && statusCode < 300;
    }
};

// Streaming callbacks:
//   onChunk  - called for each SSE/body chunk; return false to abort
//   onDone   - called once when the stream completes (statusCode, error)
using StreamChunkCallback    = std::function<bool(const QByteArray &chunk)>;
using StreamCompleteCallback = std::function<void(int statusCode, const QString &error)>;

class FastNetHttpTransport {
public:
    static HttpResponse send(const HttpRequest &request);

    // Non-blocking streaming send. Callbacks are invoked on a FastNet IO thread.
    // Returns false immediately if the connection could not be started.
    static bool sendStreaming(const HttpRequest &request,
                              StreamChunkCallback onChunk,
                              StreamCompleteCallback onDone);
};

} // namespace yaos::platform::network

#endif // YAOS_PLATFORM_NETWORK_FASTNETHTTPTRANSPORT_H
