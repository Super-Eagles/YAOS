#include "EmailChannel.h"

#include <FastNet/FastNet.h>
#include <FastNet/TcpClient.h>

#include <cctype>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextCodec>
#include <QUuid>
#include <QUrl>

Q_LOGGING_CATEGORY(lcEmail, "yaos.channels.email")

namespace yaos::channels {

namespace {

struct MailHeaderValue {
    QString mediaType;
    QHash<QString, QString> params;
};

struct EmailAttachment {
    QString fileName;
    QString contentType;
    QByteArray payload;
};

struct ParsedEmailMessage {
    QHash<QString, QString> headers;
    QString plainText;
    QString htmlText;
    QList<EmailAttachment> attachments;
};

QString trimCrlf(QString value) {
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return value.simplified();
}

QString sanitizePathSegment(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) {
        return QStringLiteral("attachment");
    }
    for (QChar &ch : value) {
        if (!(ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_') || ch == QLatin1Char('.'))) {
            ch = QLatin1Char('_');
        }
    }
    return value;
}

QByteArray decodeQuotedPrintable(const QByteArray &input, bool underscoreAsSpace = false) {
    QByteArray out;
    out.reserve(input.size());
    for (int i = 0; i < input.size(); ++i) {
        const char ch = input.at(i);
        if (underscoreAsSpace && ch == '_') {
            out.append(' ');
            continue;
        }
        if (ch != '=') {
            out.append(ch);
            continue;
        }

        if (i + 1 < input.size() && input.at(i + 1) == '\n') {
            ++i;
            continue;
        }
        if (i + 2 < input.size() && input.at(i + 1) == '\r' && input.at(i + 2) == '\n') {
            i += 2;
            continue;
        }
        if (i + 2 < input.size()) {
            bool ok = false;
            const int value = input.mid(i + 1, 2).toInt(&ok, 16);
            if (ok) {
                out.append(static_cast<char>(value));
                i += 2;
                continue;
            }
        }
        out.append(ch);
    }
    return out;
}

QString decodeWithCharset(const QByteArray &payload, const QString &charset) {
    QTextCodec *codec = nullptr;
    if (!charset.trimmed().isEmpty()) {
        codec = QTextCodec::codecForName(charset.trimmed().toUtf8());
    }
    if (!codec) {
        codec = QTextCodec::codecForName("UTF-8");
    }
    QString text = codec ? codec->toUnicode(payload) : QString::fromUtf8(payload);
    if (text.isEmpty() && !payload.isEmpty()) {
        text = QString::fromLatin1(payload);
    }
    return text;
}

QString decodeHeaderValue(const QString &value) {
    static const QRegularExpression encodedWordPattern(
        QStringLiteral("=\\?([^?]+)\\?([bBqQ])\\?([^?]+)\\?="));

    QString decoded;
    int cursor = 0;
    QRegularExpressionMatchIterator it = encodedWordPattern.globalMatch(value);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        decoded += value.mid(cursor, match.capturedStart() - cursor);
        cursor = match.capturedEnd();

        const QString charset = match.captured(1).trimmed();
        const QString encoding = match.captured(2).trimmed().toUpper();
        const QByteArray encodedPayload = match.captured(3).toLatin1();

        QByteArray raw;
        if (encoding == QStringLiteral("B")) {
            raw = QByteArray::fromBase64(encodedPayload);
        } else {
            raw = decodeQuotedPrintable(encodedPayload, true);
        }
        decoded += decodeWithCharset(raw, charset);
    }
    decoded += value.mid(cursor);
    return decoded.trimmed();
}

QString normalizedEmailAddress(QString value) {
    value = decodeHeaderValue(value.trimmed());
    const QRegularExpression anglePattern(QStringLiteral("<([^>]+)>"));
    const QRegularExpressionMatch angleMatch = anglePattern.match(value);
    if (angleMatch.hasMatch()) {
        value = angleMatch.captured(1).trimmed();
    }

    value.remove(QLatin1Char('"'));
    const int commentIndex = value.indexOf(QLatin1Char('('));
    if (commentIndex >= 0) {
        value = value.left(commentIndex).trimmed();
    }
    return value.toLower();
}

QString imapQuoted(QString value) {
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(value);
}

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

std::string toStdString(const QString &value) {
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

QByteArray bufferToByteArray(const FastNet::Buffer &buffer) {
    if (buffer.empty()) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(buffer.data()),
                      static_cast<int>(buffer.size()));
}

struct MailSocketState {
    mutable std::mutex mutex;
    std::condition_variable cv;
    QByteArray buffer;
    QString error;
    bool connectDone = false;
    bool connectOk = false;
    bool tlsInProgress = false;
    bool tlsDone = false;
    bool tlsOk = false;
    bool closed = false;
};

class FastNetMailSocket {
public:
    FastNetMailSocket() = default;
    ~FastNetMailSocket() {
        disconnectFromHost();
    }

    bool connectToHost(const QString &host, int port, bool encrypted, int timeoutMs = 15000) {
        disconnectFromHost();
        _state = std::make_shared<MailSocketState>();
        QString initError;
        if (!ensureFastNetInitialized(&initError)) {
            setError(initError);
            return false;
        }

        _client = std::make_shared<FastNet::TcpClient>(FastNet::getGlobalIoService());
        _client->setConnectTimeout(static_cast<uint32_t>(timeoutMs));
        _client->setReadTimeout(static_cast<uint32_t>(timeoutMs));
        _client->setWriteTimeout(static_cast<uint32_t>(timeoutMs));

        const std::shared_ptr<MailSocketState> state = _state;
        _client->setDataReceivedCallback([state](const FastNet::Buffer &data) {
            const QByteArray chunk = bufferToByteArray(data);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->buffer.append(chunk);
            }
            state->cv.notify_all();
        });
        _client->setDisconnectCallback([state](const std::string &reason) {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->closed = true;
                if (state->error.trimmed().isEmpty() && !reason.empty()) {
                    state->error = QString::fromStdString(reason);
                }
            }
            state->cv.notify_all();
        });
        _client->setErrorCallback([state](FastNet::ErrorCode, const std::string &message) {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->error = QString::fromStdString(message).trimmed().isEmpty()
                    ? QStringLiteral("FastNet mail socket error.")
                    : QString::fromStdString(message);
                state->closed = true;
                if (!state->connectDone) {
                    state->connectDone = true;
                    state->connectOk = false;
                }
                if (state->tlsInProgress && !state->tlsDone) {
                    state->tlsDone = true;
                    state->tlsOk = false;
                }
            }
            state->cv.notify_all();
        });

        FastNet::SSLConfig sslConfig;
        sslConfig.enableSSL = encrypted;
        if (encrypted) {
            sslConfig.hostnameVerification = toStdString(host);
        }

        const bool started = _client->connect(
            toStdString(host),
            static_cast<uint16_t>(port),
            [state](bool success, const std::string &message) {
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->connectDone = true;
                    state->connectOk = success;
                    if (!success) {
                        state->error = QString::fromStdString(message).trimmed().isEmpty()
                            ? QStringLiteral("FastNet mail socket connect failed.")
                            : QString::fromStdString(message);
                    }
                }
                state->cv.notify_all();
            },
            sslConfig);
        if (!started) {
            const FastNet::Error lastError = _client->getLastError();
            setError(lastError.isFailure()
                         ? QString::fromStdString(lastError.toString())
                         : QStringLiteral("FastNet mail socket connect failed."));
            return false;
        }

        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait_for(lock,
                           std::chrono::milliseconds(timeoutMs),
                           [&state]() { return state->connectDone; });
        if (!state->connectDone && state->error.trimmed().isEmpty()) {
            state->error = QStringLiteral("Timed out connecting mail socket.");
        }
        return state->connectDone && state->connectOk;
    }

    bool startTls(const QString &host, int timeoutMs = 15000) {
        if (!_client || !_state) {
            setError(QStringLiteral("Mail socket is not connected."));
            return false;
        }

        FastNet::SSLConfig sslConfig;
        sslConfig.enableSSL = true;
        sslConfig.hostnameVerification = toStdString(host);

        const std::shared_ptr<MailSocketState> state = _state;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->tlsInProgress = true;
            state->tlsDone = false;
            state->tlsOk = false;
        }

        const bool started = _client->startTls(
            sslConfig,
            [state](bool success, const std::string &message) {
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->tlsDone = true;
                    state->tlsOk = success;
                    state->tlsInProgress = false;
                    if (!success) {
                        state->error = QString::fromStdString(message).trimmed().isEmpty()
                            ? QStringLiteral("FastNet mail TLS negotiation failed.")
                            : QString::fromStdString(message);
                    }
                }
                state->cv.notify_all();
            });
        if (!started) {
            const FastNet::Error lastError = _client->getLastError();
            setError(lastError.isFailure()
                         ? QString::fromStdString(lastError.toString())
                         : QStringLiteral("FastNet mail TLS negotiation failed."));
            return false;
        }

        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait_for(lock,
                           std::chrono::milliseconds(timeoutMs),
                           [&state]() { return state->tlsDone; });
        if (!state->tlsDone && state->error.trimmed().isEmpty()) {
            state->error = QStringLiteral("Timed out negotiating mail TLS.");
        }
        return state->tlsDone && state->tlsOk;
    }

    qint64 write(const QByteArray &payload) {
        if (!_client || !_state) {
            setError(QStringLiteral("Mail socket is not connected."));
            return -1;
        }
        if (payload.isEmpty()) {
            return 0;
        }
        std::string data(payload.constData(), static_cast<size_t>(payload.size()));
        if (!_client->send(std::move(data))) {
            const FastNet::Error lastError = _client->getLastError();
            setError(lastError.isFailure()
                         ? QString::fromStdString(lastError.toString())
                         : QStringLiteral("Failed to write mail socket."));
            return -1;
        }
        return payload.size();
    }

    bool waitForBytesWritten(int) {
        return _client && _state && errorString().trimmed().isEmpty();
    }

    qint64 bytesAvailable() const {
        if (!_state) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(_state->mutex);
        return _state->buffer.size();
    }

    bool waitForReadyRead(int timeoutMs) {
        if (!_state) {
            return false;
        }
        std::unique_lock<std::mutex> lock(_state->mutex);
        _state->cv.wait_for(lock,
                            std::chrono::milliseconds(timeoutMs),
                            [this]() {
                                return !_state->buffer.isEmpty() ||
                                       !_state->error.trimmed().isEmpty() ||
                                       _state->closed;
                            });
        return !_state->buffer.isEmpty();
    }

    bool canReadLine() const {
        if (!_state) {
            return false;
        }
        std::lock_guard<std::mutex> lock(_state->mutex);
        return _state->buffer.contains('\n');
    }

    QByteArray readLine() {
        if (!_state) {
            return {};
        }
        std::lock_guard<std::mutex> lock(_state->mutex);
        const int newline = _state->buffer.indexOf('\n');
        if (newline < 0) {
            return {};
        }
        const QByteArray line = _state->buffer.left(newline + 1);
        _state->buffer.remove(0, newline + 1);
        return line;
    }

    QByteArray readAll() {
        if (!_state) {
            return {};
        }
        std::lock_guard<std::mutex> lock(_state->mutex);
        const QByteArray data = _state->buffer;
        _state->buffer.clear();
        return data;
    }

    QString errorString() const {
        if (!_state) {
            return {};
        }
        std::lock_guard<std::mutex> lock(_state->mutex);
        return _state->error;
    }

    void disconnectFromHost() {
        if (_client) {
            _client->disconnect();
            _client.reset();
        }
    }

private:
    void setError(const QString &error) {
        if (!_state) {
            _state = std::make_shared<MailSocketState>();
        }
        {
            std::lock_guard<std::mutex> lock(_state->mutex);
            _state->error = error;
        }
        _state->cv.notify_all();
    }

    std::shared_ptr<FastNet::TcpClient> _client;
    std::shared_ptr<MailSocketState> _state;
};

bool waitForSocketReady(FastNetMailSocket &socket, int timeoutMs) {
    if (socket.bytesAvailable() > 0) {
        return true;
    }
    return socket.waitForReadyRead(timeoutMs);
}

bool readSmtpResponse(FastNetMailSocket &socket,
                      QByteArray *response,
                      int *code,
                      int timeoutMs = 15000) {
    QByteArray data;
    int parsedCode = 0;

    while (true) {
        if (!socket.canReadLine() && !waitForSocketReady(socket, timeoutMs)) {
            return false;
        }

        while (socket.canReadLine()) {
            const QByteArray line = socket.readLine();
            data += line;
            if (line.size() < 4) {
                continue;
            }
            const bool digits = std::isdigit(static_cast<unsigned char>(line.at(0))) &&
                                std::isdigit(static_cast<unsigned char>(line.at(1))) &&
                                std::isdigit(static_cast<unsigned char>(line.at(2)));
            if (!digits) {
                continue;
            }
            parsedCode = line.left(3).toInt();
            if (line.at(3) == ' ') {
                if (response) {
                    *response = data;
                }
                if (code) {
                    *code = parsedCode;
                }
                return true;
            }
        }
    }
}

bool smtpWriteLine(FastNetMailSocket &socket, const QByteArray &line, int timeoutMs = 15000) {
    if (socket.write(line + "\r\n") < 0) {
        return false;
    }
    return socket.waitForBytesWritten(timeoutMs);
}

bool smtpExpectCode(FastNetMailSocket &socket,
                    const QByteArray &command,
                    int expectedCode,
                    QByteArray *response = nullptr,
                    int timeoutMs = 15000) {
    if (!command.isEmpty() && !smtpWriteLine(socket, command, timeoutMs)) {
        return false;
    }

    int code = 0;
    QByteArray localResponse;
    if (!readSmtpResponse(socket, &localResponse, &code, timeoutMs)) {
        return false;
    }
    if (response) {
        *response = localResponse;
    }
    return code == expectedCode;
}

QStringList smtpAuthMechanisms(const QByteArray &ehloResponse) {
    QStringList mechanisms;
    const QString text = QString::fromUtf8(ehloResponse);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")),
                                         Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.startsWith(QStringLiteral("250-"), Qt::CaseInsensitive)) {
            line = line.mid(4).trimmed();
        } else if (line.startsWith(QStringLiteral("250 "), Qt::CaseInsensitive)) {
            line = line.mid(4).trimmed();
        }
        if (!line.startsWith(QStringLiteral("AUTH "), Qt::CaseInsensitive)) {
            continue;
        }
        const QStringList parts = line.mid(5).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            mechanisms.append(part.trimmed().toUpper());
        }
    }
    mechanisms.removeDuplicates();
    return mechanisms;
}

bool smtpAuthenticate(FastNetMailSocket &socket,
                      const QString &username,
                      const QString &password,
                      const QStringList &mechanisms) {
    if (username.trimmed().isEmpty() && password.trimmed().isEmpty()) {
        return true;
    }

    if (mechanisms.contains(QStringLiteral("PLAIN"))) {
        const QByteArray payload = QByteArray("\0") + username.toUtf8() + QByteArray("\0") + password.toUtf8();
        return smtpExpectCode(socket,
                              QByteArray("AUTH PLAIN ") + payload.toBase64(),
                              235);
    }

    if (mechanisms.contains(QStringLiteral("LOGIN"))) {
        if (!smtpExpectCode(socket, "AUTH LOGIN", 334)) {
            return false;
        }
        if (!smtpExpectCode(socket, username.toUtf8().toBase64(), 334)) {
            return false;
        }
        return smtpExpectCode(socket, password.toUtf8().toBase64(), 235);
    }

    return username.trimmed().isEmpty() && password.trimmed().isEmpty();
}

QString smtpEnvelopeFrom(const config::EmailConfig &config) {
    if (!config.fromAddress.trimmed().isEmpty()) {
        return config.fromAddress.trimmed();
    }
    if (!config.smtpUsername.trimmed().isEmpty()) {
        return config.smtpUsername.trimmed();
    }
    return QString();
}

QString smtpSubjectFor(const bus::OutboundMessage &msg) {
    const QString subject = trimCrlf(msg.metadata.value(QStringLiteral("subject")).toString());
    return subject.isEmpty() ? QStringLiteral("YAOS reply") : subject;
}

QString normalizeCrlf(QString text) {
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    return text;
}

QString smtpBodyFor(const bus::OutboundMessage &msg) {
    QString body = msg.content.trimmed();
    if (body.isEmpty()) {
        body = QStringLiteral("(empty)");
    }
    return normalizeCrlf(body);
}

bool connectMailSocket(FastNetMailSocket &socket, const QString &host, int port, bool encrypted, int timeoutMs = 15000) {
    return socket.connectToHost(host, port, encrypted, timeoutMs);
}

QHash<QString, QString> parseMailHeaders(const QByteArray &rawHeaders) {
    QHash<QString, QString> headers;
    const QString text = QString::fromUtf8(rawHeaders);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));

    QString currentName;
    QString currentValue;
    auto commitHeader = [&]() {
        if (!currentName.isEmpty()) {
            headers.insert(currentName.toLower(), currentValue.trimmed());
        }
        currentName.clear();
        currentValue.clear();
    };

    for (const QString &line : lines) {
        if (line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'))) {
            if (!currentName.isEmpty()) {
                currentValue += QLatin1Char(' ') + line.trimmed();
            }
            continue;
        }

        const int separator = line.indexOf(QLatin1Char(':'));
        if (separator < 0) {
            continue;
        }

        commitHeader();
        currentName = line.left(separator).trimmed();
        currentValue = line.mid(separator + 1).trimmed();
    }
    commitHeader();
    return headers;
}

MailHeaderValue parseHeaderValue(const QString &rawValue) {
    MailHeaderValue parsed;
    const QStringList parts = rawValue.split(QLatin1Char(';'), Qt::KeepEmptyParts);
    if (!parts.isEmpty()) {
        parsed.mediaType = parts.first().trimmed().toLower();
    }

    for (int i = 1; i < parts.size(); ++i) {
        const QString part = parts.at(i).trimmed();
        const int equals = part.indexOf(QLatin1Char('='));
        if (equals < 0) {
            continue;
        }
        QString key = part.left(equals).trimmed().toLower();
        QString value = part.mid(equals + 1).trimmed();
        if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')) && value.size() >= 2) {
            value = value.mid(1, value.size() - 2);
        }
        if (key.endsWith(QLatin1Char('*')) && value.contains(QStringLiteral("''"))) {
            value = value.section(QStringLiteral("''"), 1);
            value = QUrl::fromPercentEncoding(value.toUtf8());
            key.chop(1);
        }
        parsed.params.insert(key, decodeHeaderValue(value));
    }
    return parsed;
}

bool splitRawMessage(const QByteArray &raw,
                     QByteArray *rawHeaders,
                     QByteArray *rawBody) {
    int separator = raw.indexOf("\r\n\r\n");
    int separatorLength = 4;
    if (separator < 0) {
        separator = raw.indexOf("\n\n");
        separatorLength = 2;
    }
    if (separator < 0) {
        return false;
    }

    if (rawHeaders) {
        *rawHeaders = raw.left(separator);
    }
    if (rawBody) {
        *rawBody = raw.mid(separator + separatorLength);
    }
    return true;
}

QList<QByteArray> splitMultipartBody(const QByteArray &body, const QString &boundary) {
    QList<QByteArray> parts;
    if (boundary.trimmed().isEmpty()) {
        return parts;
    }

    const QByteArray marker = QByteArray("--") + boundary.toUtf8();
    const QByteArray closingMarker = marker + QByteArray("--");
    const QList<QByteArray> lines = body.split('\n');

    QByteArray current;
    bool inPart = false;
    for (QByteArray line : lines) {
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line == marker) {
            if (inPart && !current.trimmed().isEmpty()) {
                parts.append(current);
            }
            current.clear();
            inPart = true;
            continue;
        }
        if (line == closingMarker) {
            if (inPart && !current.trimmed().isEmpty()) {
                parts.append(current);
            }
            break;
        }
        if (inPart) {
            current += line;
            current += "\r\n";
        }
    }
    return parts;
}

QByteArray decodeTransferEncoded(const QByteArray &body, const QString &encodingHeader) {
    const QString encoding = encodingHeader.trimmed().toLower();
    if (encoding == QStringLiteral("base64")) {
        return QByteArray::fromBase64(body);
    }
    if (encoding == QStringLiteral("quoted-printable")) {
        return decodeQuotedPrintable(body);
    }
    return body;
}

QString stripHtmlTags(QString html) {
    html.replace(QRegularExpression(QStringLiteral("<\\s*br\\s*/?>"), QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\n"));
    html.replace(QRegularExpression(QStringLiteral("</\\s*(p|div|li|tr|table|h[1-6])\\s*>"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\n"));
    html.remove(QRegularExpression(QStringLiteral("<style[^>]*>.*?</style>"),
                                   QRegularExpression::CaseInsensitiveOption |
                                       QRegularExpression::DotMatchesEverythingOption));
    html.remove(QRegularExpression(QStringLiteral("<script[^>]*>.*?</script>"),
                                   QRegularExpression::CaseInsensitiveOption |
                                       QRegularExpression::DotMatchesEverythingOption));
    html.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    html.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    html.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    html.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    html.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    html.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    const QStringList rawLines = html.split(QRegularExpression(QStringLiteral("\\n+")),
                                            Qt::SkipEmptyParts);
    QStringList cleanedLines;
    for (const QString &line : rawLines) {
        const QString simplified = line.simplified();
        if (!simplified.isEmpty()) {
            cleanedLines.append(simplified);
        }
    }
    return cleanedLines.join(QStringLiteral("\n")).trimmed();
}

void appendTextSection(QString *target, const QString &text) {
    const QString cleaned = text.trimmed();
    if (cleaned.isEmpty()) {
        return;
    }
    if (!target->trimmed().isEmpty()) {
        *target += QStringLiteral("\n\n");
    }
    *target += cleaned;
}

void parseMimeEntity(const QHash<QString, QString> &headers,
                     const QByteArray &body,
                     ParsedEmailMessage *out);

void parseRawMimePart(const QByteArray &rawPart, ParsedEmailMessage *out) {
    QByteArray rawHeaders;
    QByteArray rawBody;
    if (!splitRawMessage(rawPart, &rawHeaders, &rawBody)) {
        return;
    }
    parseMimeEntity(parseMailHeaders(rawHeaders), rawBody, out);
}

void parseMimeEntity(const QHash<QString, QString> &headers,
                     const QByteArray &body,
                     ParsedEmailMessage *out) {
    if (!out) {
        return;
    }

    const MailHeaderValue contentType = parseHeaderValue(headers.value(QStringLiteral("content-type")));
    const MailHeaderValue disposition = parseHeaderValue(headers.value(QStringLiteral("content-disposition")));
    const QString mediaType = contentType.mediaType.isEmpty() ? QStringLiteral("text/plain")
                                                              : contentType.mediaType;

    if (mediaType.startsWith(QStringLiteral("multipart/"))) {
        const QString boundary = contentType.params.value(QStringLiteral("boundary")).trimmed();
        const QList<QByteArray> parts = splitMultipartBody(body, boundary);
        for (const QByteArray &part : parts) {
            parseRawMimePart(part, out);
        }
        return;
    }

    const QByteArray decodedPayload = decodeTransferEncoded(body, headers.value(QStringLiteral("content-transfer-encoding")));
    const QString fileName = disposition.params.value(QStringLiteral("filename"),
                                                      contentType.params.value(QStringLiteral("name")));
    const QString dispositionType = disposition.mediaType;
    const bool textual = mediaType.startsWith(QStringLiteral("text/plain")) ||
                         mediaType.startsWith(QStringLiteral("text/html"));
    const bool treatAsAttachment = !fileName.trimmed().isEmpty() ||
                                   dispositionType == QStringLiteral("attachment") ||
                                   (!textual && !decodedPayload.isEmpty());

    if (treatAsAttachment) {
        EmailAttachment attachment;
        attachment.fileName = fileName.trimmed().isEmpty()
            ? QStringLiteral("attachment.bin")
            : sanitizePathSegment(fileName);
        attachment.contentType = mediaType;
        attachment.payload = decodedPayload;
        out->attachments.append(attachment);
        return;
    }

    const QString charset = contentType.params.value(QStringLiteral("charset"));
    const QString decodedText = decodeWithCharset(decodedPayload, charset);
    if (mediaType.startsWith(QStringLiteral("text/html"))) {
        appendTextSection(&out->htmlText, decodedText);
    } else {
        appendTextSection(&out->plainText, decodedText);
    }
}

ParsedEmailMessage parseRawEmail(const QByteArray &rawMessage) {
    ParsedEmailMessage parsed;
    QByteArray rawHeaders;
    QByteArray rawBody;
    if (!splitRawMessage(rawMessage, &rawHeaders, &rawBody)) {
        return parsed;
    }
    parsed.headers = parseMailHeaders(rawHeaders);
    parseMimeEntity(parsed.headers, rawBody, &parsed);
    return parsed;
}

QString fallbackAttachmentName(const EmailAttachment &attachment) {
    QString fileName = sanitizePathSegment(attachment.fileName);
    if (!fileName.contains(QLatin1Char('.'))) {
        const QMimeDatabase mimeDatabase;
        const QString suffix =
            mimeDatabase.mimeTypeForName(attachment.contentType).preferredSuffix().trimmed();
        if (!suffix.isEmpty()) {
            fileName += QLatin1Char('.') + suffix;
        }
    }
    return fileName.isEmpty() ? QStringLiteral("attachment.bin") : fileName;
}

QJsonObject persistAttachment(const QString &workspace,
                              const QString &senderAddress,
                              quint64 uid,
                              const EmailAttachment &attachment) {
    if (workspace.trimmed().isEmpty() || attachment.payload.isEmpty()) {
        return {};
    }

    const QString safeSender = sanitizePathSegment(normalizedEmailAddress(senderAddress));
    const QString dirPath = QDir(workspace).filePath(
        QStringLiteral("runtime/email_media/%1").arg(safeSender.isEmpty() ? QStringLiteral("mailbox") : safeSender));
    QDir().mkpath(dirPath);

    const QString fileName = QStringLiteral("%1_%2")
                                 .arg(QString::number(uid),
                                      fallbackAttachmentName(attachment));
    const QString filePath = QDir(dirPath).filePath(fileName);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning(lcEmail) << "Failed to create email attachment file" << filePath;
        return {};
    }
    if (file.write(attachment.payload) < 0 || !file.commit()) {
        qWarning(lcEmail) << "Failed to persist email attachment file" << filePath;
        return {};
    }

    return QJsonObject{
        {QStringLiteral("name"), attachment.fileName},
        {QStringLiteral("content_type"), attachment.contentType},
        {QStringLiteral("local_path"), filePath},
        {QStringLiteral("size"), static_cast<qint64>(attachment.payload.size())}
    };
}

QList<EmailAttachment> loadOutboundAttachments(const QStringList &mediaPaths) {
    QList<EmailAttachment> attachments;
    const QMimeDatabase mimeDatabase;
    for (const QString &path : mediaPaths) {
        const QFileInfo info(path.trimmed());
        if (!info.exists() || !info.isFile()) {
            continue;
        }
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        EmailAttachment attachment;
        attachment.fileName = sanitizePathSegment(info.fileName());
        attachment.contentType = mimeDatabase.mimeTypeForFile(info).name().trimmed();
        if (attachment.contentType.isEmpty()) {
            attachment.contentType = QStringLiteral("application/octet-stream");
        }
        attachment.payload = file.readAll();
        if (!attachment.payload.isEmpty()) {
            attachments.append(attachment);
        }
    }
    return attachments;
}

QString foldBase64(const QByteArray &payload) {
    const QByteArray encoded = payload.toBase64();
    QStringList lines;
    for (int i = 0; i < encoded.size(); i += 76) {
        lines.append(QString::fromLatin1(encoded.mid(i, 76)));
    }
    return lines.join(QStringLiteral("\r\n"));
}

QString dotStuff(QString message) {
    QStringList lines = message.split(QStringLiteral("\r\n"));
    for (QString &line : lines) {
        if (line.startsWith(QLatin1Char('.'))) {
            line.prepend(QLatin1Char('.'));
        }
    }
    return lines.join(QStringLiteral("\r\n"));
}

QByteArray smtpMessagePayload(const config::EmailConfig &config,
                              const bus::OutboundMessage &msg,
                              const QString &sender,
                              const QString &recipient) {
    Q_UNUSED(config);
    const QString subject = smtpSubjectFor(msg);
    const QString body = smtpBodyFor(msg);
    const QList<EmailAttachment> attachments = loadOutboundAttachments(msg.media);

    QStringList headers;
    headers.append(QStringLiteral("From: <%1>").arg(sender));
    headers.append(QStringLiteral("To: <%1>").arg(recipient));
    headers.append(QStringLiteral("Subject: %1").arg(trimCrlf(subject)));
    headers.append(QStringLiteral("Date: %1")
                       .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("ddd, dd MMM yyyy hh:mm:ss +0000"))));
    headers.append(QStringLiteral("Message-ID: <%1@yaos.local>")
                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const QString inReplyTo = trimCrlf(msg.metadata.value(QStringLiteral("message_id")).toString());
    if (!inReplyTo.isEmpty()) {
        headers.append(QStringLiteral("In-Reply-To: %1").arg(inReplyTo));
        headers.append(QStringLiteral("References: %1").arg(inReplyTo));
    }
    headers.append(QStringLiteral("MIME-Version: 1.0"));

    QString bodySection;
    if (attachments.isEmpty()) {
        headers.append(QStringLiteral("Content-Type: text/plain; charset=UTF-8"));
        headers.append(QStringLiteral("Content-Transfer-Encoding: 8bit"));
        bodySection = body;
    } else {
        const QString boundary = QStringLiteral("yaos-%1").arg(
            QUuid::createUuid().toString(QUuid::WithoutBraces));
        headers.append(QStringLiteral("Content-Type: multipart/mixed; boundary=\"%1\"").arg(boundary));

        QStringList parts;
        parts.append(QStringLiteral("--%1").arg(boundary));
        parts.append(QStringLiteral("Content-Type: text/plain; charset=UTF-8"));
        parts.append(QStringLiteral("Content-Transfer-Encoding: 8bit"));
        parts.append(QString());
        parts.append(body);

        for (const EmailAttachment &attachment : attachments) {
            parts.append(QString());
            parts.append(QStringLiteral("--%1").arg(boundary));
            parts.append(QStringLiteral("Content-Type: %1; name=\"%2\"")
                             .arg(attachment.contentType,
                                  trimCrlf(attachment.fileName)));
            parts.append(QStringLiteral("Content-Disposition: attachment; filename=\"%1\"")
                             .arg(trimCrlf(attachment.fileName)));
            parts.append(QStringLiteral("Content-Transfer-Encoding: base64"));
            parts.append(QString());
            parts.append(foldBase64(attachment.payload));
        }
        parts.append(QString());
        parts.append(QStringLiteral("--%1--").arg(boundary));
        bodySection = parts.join(QStringLiteral("\r\n"));
    }

    const QString message = dotStuff(headers.join(QStringLiteral("\r\n")) +
                                     QStringLiteral("\r\n\r\n") +
                                     bodySection);
    return message.toUtf8() + QByteArray("\r\n.\r\n");
}

bool smtpSendMail(const config::EmailConfig &config, const bus::OutboundMessage &msg) {
    const QString recipient = normalizedEmailAddress(msg.chatId);
    const QString sender = normalizedEmailAddress(smtpEnvelopeFrom(config));
    if (recipient.isEmpty() || sender.isEmpty()) {
        return false;
    }

    FastNetMailSocket socket;
    const bool implicitTls = config.smtpUseTls && config.smtpPort == 465;
    if (!connectMailSocket(socket, config.smtpHost, config.smtpPort, implicitTls)) {
        qWarning(lcEmail) << "SMTP connect failed:" << socket.errorString();
        return false;
    }

    int code = 0;
    QByteArray response;
    if (!readSmtpResponse(socket, &response, &code) || code != 220) {
        qWarning(lcEmail) << "SMTP greeting failed";
        return false;
    }

    const QString ehloHost = config.smtpHost.trimmed().isEmpty() ? QStringLiteral("localhost")
                                                                 : config.smtpHost.trimmed();
    if (!smtpExpectCode(socket, QByteArray("EHLO ") + ehloHost.toUtf8(), 250, &response)) {
        qWarning(lcEmail) << "SMTP EHLO failed";
        return false;
    }

    QStringList mechanisms = smtpAuthMechanisms(response);
    if (config.smtpUseTls && !implicitTls) {
        if (!smtpExpectCode(socket, "STARTTLS", 220)) {
            qWarning(lcEmail) << "SMTP STARTTLS failed";
            return false;
        }
        if (!socket.startTls(config.smtpHost, 15000)) {
            qWarning(lcEmail) << "SMTP TLS negotiation failed:" << socket.errorString();
            return false;
        }
        if (!smtpExpectCode(socket, QByteArray("EHLO ") + ehloHost.toUtf8(), 250, &response)) {
            qWarning(lcEmail) << "SMTP EHLO after STARTTLS failed";
            return false;
        }
        mechanisms = smtpAuthMechanisms(response);
    }

    if (!smtpAuthenticate(socket, config.smtpUsername, config.smtpPassword, mechanisms)) {
        qWarning(lcEmail) << "SMTP authentication failed";
        return false;
    }

    if (!smtpExpectCode(socket, QByteArray("MAIL FROM:<") + sender.toUtf8() + '>', 250)) {
        qWarning(lcEmail) << "SMTP MAIL FROM failed";
        return false;
    }
    if (!smtpExpectCode(socket, QByteArray("RCPT TO:<") + recipient.toUtf8() + '>', 250)) {
        qWarning(lcEmail) << "SMTP RCPT TO failed";
        return false;
    }
    if (!smtpExpectCode(socket, "DATA", 354)) {
        qWarning(lcEmail) << "SMTP DATA failed";
        return false;
    }

    const QByteArray payload = smtpMessagePayload(config, msg, sender, recipient);
    if (socket.write(payload) < 0 || !socket.waitForBytesWritten(15000)) {
        qWarning(lcEmail) << "SMTP write failed:" << socket.errorString();
        return false;
    }

    if (!readSmtpResponse(socket, &response, &code) || code != 250) {
        qWarning(lcEmail) << "SMTP message submission failed";
        return false;
    }

    smtpExpectCode(socket, "QUIT", 221);
    socket.disconnectFromHost();
    return true;
}

bool readImapGreeting(FastNetMailSocket &socket, QByteArray *greeting, int timeoutMs = 15000) {
    QByteArray data;
    while (!data.contains("\r\n")) {
        if (!waitForSocketReady(socket, timeoutMs)) {
            return false;
        }
        data += socket.readAll();
    }
    if (greeting) {
        *greeting = data;
    }
    return true;
}

bool hasTaggedImapLine(const QByteArray &payload, const QByteArray &tag) {
    const QByteArray prefix = "\r\n" + tag + " ";
    int index = payload.indexOf(prefix);
    if (index < 0 && payload.startsWith(tag + " ")) {
        index = 0;
    }
    if (index < 0) {
        return false;
    }
    const int lineStart = index == 0 ? 0 : index + 2;
    return payload.indexOf("\r\n", lineStart) >= 0;
}

bool readImapTaggedResponse(FastNetMailSocket &socket,
                            const QByteArray &tag,
                            QByteArray *response,
                            int timeoutMs = 15000) {
    QByteArray data;
    while (!hasTaggedImapLine(data, tag)) {
        if (!waitForSocketReady(socket, timeoutMs)) {
            return false;
        }
        data += socket.readAll();
    }
    if (response) {
        *response = data;
    }
    return true;
}

bool imapResponseOk(const QByteArray &response, const QByteArray &tag) {
    const QString text = QString::fromUtf8(response);
    const QRegularExpression pattern(QStringLiteral("(^|\\r\\n)%1 OK\\b")
                                         .arg(QString::fromUtf8(tag)));
    return pattern.match(text).hasMatch();
}

QByteArray nextImapTag(int counter) {
    return QByteArray("A") + QByteArray::number(counter).rightJustified(4, '0');
}

bool imapCommand(FastNetMailSocket &socket,
                 int *tagCounter,
                 const QByteArray &command,
                 QByteArray *response,
                 int timeoutMs = 15000) {
    const QByteArray tag = nextImapTag((*tagCounter)++);
    if (socket.write(tag + ' ' + command + "\r\n") < 0 || !socket.waitForBytesWritten(timeoutMs)) {
        return false;
    }
    QByteArray localResponse;
    QByteArray *target = response ? response : &localResponse;
    if (!readImapTaggedResponse(socket, tag, target, timeoutMs)) {
        return false;
    }
    return imapResponseOk(*target, tag);
}

QList<quint64> parseImapSearchUids(const QByteArray &response) {
    QList<quint64> out;
    const QString text = QString::fromUtf8(response);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")),
                                         Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (!line.startsWith(QStringLiteral("* SEARCH "), Qt::CaseInsensitive)) {
            continue;
        }
        const QStringList parts = line.mid(9).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            bool ok = false;
            const quint64 value = part.toULongLong(&ok);
            if (ok) {
                out.append(value);
            }
        }
    }
    return out;
}

QByteArray extractImapLiteral(const QByteArray &response, const QByteArray &marker) {
    const int markerIndex = response.indexOf(marker);
    if (markerIndex < 0) {
        return {};
    }

    const int braceOpen = response.indexOf('{', markerIndex);
    const int braceClose = response.indexOf('}', braceOpen);
    if (braceOpen < 0 || braceClose < 0) {
        return {};
    }

    bool ok = false;
    const int length = response.mid(braceOpen + 1, braceClose - braceOpen - 1).toInt(&ok);
    if (!ok || length < 0) {
        return {};
    }

    const int literalStart = response.indexOf("\r\n", braceClose);
    if (literalStart < 0) {
        return {};
    }

    return response.mid(literalStart + 2, length);
}

QString buildInboundEmailContent(const QString &subject,
                                 const QString &body,
                                 bool hasAttachments) {
    const QString trimmedSubject = trimCrlf(subject);
    const QString trimmedBody = body.trimmed();
    if (!trimmedSubject.isEmpty() && !trimmedBody.isEmpty()) {
        return QStringLiteral("Subject: %1\n\n%2").arg(trimmedSubject, trimmedBody);
    }
    if (!trimmedBody.isEmpty()) {
        return trimmedBody;
    }
    if (!trimmedSubject.isEmpty()) {
        return hasAttachments
            ? QStringLiteral("Subject: %1\n\n[Email attachments included]").arg(trimmedSubject)
            : QStringLiteral("Subject: %1").arg(trimmedSubject);
    }
    return hasAttachments ? QStringLiteral("[Email attachments included]") : QString();
}

} // namespace

EmailChannel::EmailChannel(const config::EmailConfig &config,
                           const QString &workspace,
                           bus::MessageBus &bus,
                           QObject *parent)
    : QObject(parent),
      _config(config),
      _workspace(workspace),
      _bus(bus) {
    _pollTimer.setInterval(30000);
    connect(&_pollTimer, &QTimer::timeout, this, &EmailChannel::pollInbox);
}

QString EmailChannel::name() const {
    return QStringLiteral("email");
}

bool EmailChannel::hasImapConfig() const {
    return !_config.imapHost.trimmed().isEmpty() &&
           !_config.imapUsername.trimmed().isEmpty() &&
           !_config.imapPassword.trimmed().isEmpty();
}

bool EmailChannel::hasSmtpConfig() const {
    return !_config.smtpHost.trimmed().isEmpty() &&
           (!smtpEnvelopeFrom(_config).trimmed().isEmpty());
}

bool EmailChannel::isAllowed(const QString &senderAddress) const {
    if (_config.allowFrom.isEmpty() || _config.allowFrom.contains(QStringLiteral("*"))) {
        return true;
    }
    const QString normalized = normalizedEmailAddress(senderAddress);
    for (const QString &allowed : _config.allowFrom) {
        if (normalizedEmailAddress(allowed) == normalized) {
            return true;
        }
    }
    return false;
}

bool EmailChannel::start() {
    if (_running) {
        return true;
    }
    if (!_config.enabled) {
        return false;
    }
    if (!_config.consentGranted) {
        qWarning(lcEmail) << "Email channel requires consentGranted=true";
        return false;
    }
    if (!hasImapConfig() && !hasSmtpConfig()) {
        qWarning(lcEmail) << "Email channel is enabled but IMAP/SMTP config is incomplete";
        return false;
    }

    _running = true;
    if (hasImapConfig()) {
        _pollTimer.start();
        QTimer::singleShot(0, this, &EmailChannel::pollInbox);
    } else {
        qInfo(lcEmail) << "Email channel started in outbound-only mode";
    }
    return true;
}

void EmailChannel::stop() {
    _running = false;
    _pollTimer.stop();
}

void EmailChannel::send(const bus::OutboundMessage &msg) {
    if (!_config.enabled || !_config.consentGranted) {
        return;
    }
    if (!hasSmtpConfig()) {
        qWarning(lcEmail) << "SMTP config is incomplete, skipping outbound email";
        return;
    }
    if (!smtpSendMail(_config, msg)) {
        qWarning(lcEmail) << "Failed to send outbound email to" << msg.chatId;
    }
}

void EmailChannel::pollInbox() {
    if (!_running || !hasImapConfig()) {
        return;
    }

    FastNetMailSocket socket;
    if (!connectMailSocket(socket, _config.imapHost, _config.imapPort, _config.imapUseSsl)) {
        qWarning(lcEmail) << "IMAP connect failed:" << socket.errorString();
        return;
    }

    QByteArray greeting;
    if (!readImapGreeting(socket, &greeting)) {
        qWarning(lcEmail) << "IMAP greeting failed";
        return;
    }

    int tagCounter = 1;
    QByteArray response;
    const QByteArray loginCommand =
        "LOGIN " + imapQuoted(_config.imapUsername).toUtf8() + ' ' + imapQuoted(_config.imapPassword).toUtf8();
    if (!imapCommand(socket, &tagCounter, loginCommand, &response)) {
        qWarning(lcEmail) << "IMAP login failed";
        return;
    }
    if (!imapCommand(socket, &tagCounter, "SELECT INBOX", &response)) {
        qWarning(lcEmail) << "IMAP SELECT INBOX failed";
        imapCommand(socket, &tagCounter, "LOGOUT", &response);
        return;
    }

    if (!_initialized) {
        bool establishedWaterline = false;
        if (imapCommand(socket, &tagCounter, "UID SEARCH ALL", &response)) {
            const QList<quint64> uids = parseImapSearchUids(response);
            for (quint64 uid : uids) {
                _lastSeenUid = qMax(_lastSeenUid, uid);
            }
            establishedWaterline = true;
        }
        _initialized = establishedWaterline;
        imapCommand(socket, &tagCounter, "LOGOUT", &response);
        return;
    }

    const QByteArray searchCommand =
        QByteArray("UID SEARCH UID ") + QByteArray::number(_lastSeenUid + 1) + ":*";
    if (!imapCommand(socket, &tagCounter, searchCommand, &response)) {
        qWarning(lcEmail) << "IMAP UID SEARCH failed";
        imapCommand(socket, &tagCounter, "LOGOUT", &response);
        return;
    }

    const QList<quint64> uids = parseImapSearchUids(response);
    for (quint64 uid : uids) {
        QByteArray fetchResponse;
        const QByteArray fetchCommand =
            QByteArray("UID FETCH ") + QByteArray::number(uid) + " (UID BODY.PEEK[])";
        if (!imapCommand(socket, &tagCounter, fetchCommand, &fetchResponse)) {
            continue;
        }

        const QByteArray rawMessage = extractImapLiteral(fetchResponse, "BODY[]");
        if (rawMessage.isEmpty()) {
            _lastSeenUid = qMax(_lastSeenUid, uid);
            continue;
        }

        const ParsedEmailMessage parsed = parseRawEmail(rawMessage);
        const QString senderRaw = decodeHeaderValue(parsed.headers.value(QStringLiteral("from")));
        const QString sender = normalizedEmailAddress(senderRaw);
        if (sender.isEmpty() || !isAllowed(sender)) {
            _lastSeenUid = qMax(_lastSeenUid, uid);
            continue;
        }

        const QString subject = decodeHeaderValue(parsed.headers.value(QStringLiteral("subject")));
        QString body = parsed.plainText.trimmed();
        if (body.isEmpty() && !parsed.htmlText.trimmed().isEmpty()) {
            body = stripHtmlTags(parsed.htmlText);
        }
        body = body.left(16000);

        QStringList downloadedFiles;
        QJsonArray attachments;
        for (const EmailAttachment &attachment : parsed.attachments) {
            const QJsonObject attachmentMeta = persistAttachment(_workspace, sender, uid, attachment);
            const QString localPath = attachmentMeta.value(QStringLiteral("local_path")).toString().trimmed();
            if (!localPath.isEmpty()) {
                downloadedFiles.append(localPath);
                attachments.append(attachmentMeta);
            }
        }

        const QString content = buildInboundEmailContent(subject, body, !downloadedFiles.isEmpty());
        if (content.trimmed().isEmpty() && downloadedFiles.isEmpty()) {
            _lastSeenUid = qMax(_lastSeenUid, uid);
            continue;
        }

        bus::InboundMessage inbound;
        inbound.channel = name();
        inbound.chatId = sender;
        inbound.senderId = sender;
        inbound.content = content;
        inbound.media = downloadedFiles;
        inbound.metadata = QJsonObject{
            {QStringLiteral("uid"), QString::number(uid)},
            {QStringLiteral("subject"), subject},
            {QStringLiteral("from"), senderRaw},
            {QStringLiteral("to"), decodeHeaderValue(parsed.headers.value(QStringLiteral("to")))},
            {QStringLiteral("message_id"), decodeHeaderValue(parsed.headers.value(QStringLiteral("message-id")))},
            {QStringLiteral("date"), decodeHeaderValue(parsed.headers.value(QStringLiteral("date")))}
        };
        if (!attachments.isEmpty()) {
            inbound.metadata.insert(QStringLiteral("attachments"), attachments);
        }

        _bus.publishInbound(inbound);
        _lastSeenUid = qMax(_lastSeenUid, uid);
    }

    imapCommand(socket, &tagCounter, "LOGOUT", &response);
}

} // namespace yaos::channels
