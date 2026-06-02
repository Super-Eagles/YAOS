#include "TelegramChannel.h"

#include "ChannelHttp.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include "../providers/OpenAICompatibleProvider.h"
#include "../providers/ProviderOAuth.h"
#include "../providers/ProviderRegistry.h"

Q_LOGGING_CATEGORY(lcTelegram, "yaos.channels.telegram")

namespace yaos::channels {

namespace {

struct TelegramMediaSpec {
    QString kind;
    QString fileId;
    QString preferredName;
    QString fallbackExtension;
};

QString sanitizePathSegment(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) {
        return QStringLiteral("media");
    }
    for (QChar &ch : value) {
        if (!(ch.isLetterOrNumber() ||
              ch == QLatin1Char('-') ||
              ch == QLatin1Char('_') ||
              ch == QLatin1Char('.'))) {
            ch = QLatin1Char('_');
        }
    }
    return value;
}

QString joinedPreferredName(const QString &baseName, const QString &fallbackExtension) {
    QString clean = sanitizePathSegment(baseName);
    if (clean.isEmpty() || clean == QStringLiteral("media")) {
        clean = QStringLiteral("attachment");
    }

    const QFileInfo info(clean);
    if (!fallbackExtension.trimmed().isEmpty() && info.suffix().trimmed().isEmpty()) {
        return clean + fallbackExtension.trimmed();
    }
    return clean;
}

TelegramMediaSpec mediaSpecFromObject(const QString &kind,
                                      const QJsonObject &object,
                                      const QString &preferredName,
                                      const QString &fallbackExtension) {
    TelegramMediaSpec spec;
    spec.kind = kind;
    spec.fileId = object.value(QStringLiteral("file_id")).toString().trimmed();
    spec.preferredName = preferredName.trimmed();
    spec.fallbackExtension = fallbackExtension.trimmed();
    return spec;
}

QList<TelegramMediaSpec> collectMediaSpecs(const QJsonObject &message) {
    QList<TelegramMediaSpec> specs;

    const QJsonArray photos = message.value(QStringLiteral("photo")).toArray();
    if (!photos.isEmpty()) {
        specs.append(mediaSpecFromObject(QStringLiteral("photo"),
                                         photos.last().toObject(),
                                         QStringLiteral("photo"),
                                         QStringLiteral(".jpg")));
    }

    const QJsonObject document = message.value(QStringLiteral("document")).toObject();
    if (!document.isEmpty()) {
        specs.append(mediaSpecFromObject(QStringLiteral("document"),
                                         document,
                                         document.value(QStringLiteral("file_name")).toString(),
                                         QString()));
    }

    const QJsonObject audio = message.value(QStringLiteral("audio")).toObject();
    if (!audio.isEmpty()) {
        specs.append(mediaSpecFromObject(QStringLiteral("audio"),
                                         audio,
                                         audio.value(QStringLiteral("file_name")).toString(),
                                         QStringLiteral(".mp3")));
    }

    const QJsonObject voice = message.value(QStringLiteral("voice")).toObject();
    if (!voice.isEmpty()) {
        specs.append(mediaSpecFromObject(QStringLiteral("voice"),
                                         voice,
                                         QStringLiteral("voice"),
                                         QStringLiteral(".ogg")));
    }

    const QJsonObject video = message.value(QStringLiteral("video")).toObject();
    if (!video.isEmpty()) {
        specs.append(mediaSpecFromObject(QStringLiteral("video"),
                                         video,
                                         video.value(QStringLiteral("file_name")).toString(),
                                         QStringLiteral(".mp4")));
    }

    const QJsonObject animation = message.value(QStringLiteral("animation")).toObject();
    if (!animation.isEmpty()) {
        specs.append(mediaSpecFromObject(QStringLiteral("animation"),
                                         animation,
                                         animation.value(QStringLiteral("file_name")).toString(),
                                         QStringLiteral(".gif")));
    }

    const QJsonObject videoNote = message.value(QStringLiteral("video_note")).toObject();
    if (!videoNote.isEmpty()) {
        specs.append(mediaSpecFromObject(QStringLiteral("video_note"),
                                         videoNote,
                                         QStringLiteral("video_note"),
                                         QStringLiteral(".mp4")));
    }

    const QJsonObject sticker = message.value(QStringLiteral("sticker")).toObject();
    if (!sticker.isEmpty()) {
        QString extension = QStringLiteral(".webp");
        if (sticker.value(QStringLiteral("is_animated")).toBool(false)) {
            extension = QStringLiteral(".tgs");
        } else if (sticker.value(QStringLiteral("is_video")).toBool(false)) {
            extension = QStringLiteral(".webm");
        }
        specs.append(mediaSpecFromObject(QStringLiteral("sticker"),
                                         sticker,
                                         QStringLiteral("sticker"),
                                         extension));
    }

    return specs;
}

QString normalizedProviderId(QString value) {
    value = value.trimmed().toLower();
    value.replace(QLatin1Char('-'), QLatin1Char('_'));
    if (value == QStringLiteral("azureopenai")) value = QStringLiteral("azure_openai");
    if (value == QStringLiteral("openaicodex")) value = QStringLiteral("openai_codex");
    if (value == QStringLiteral("githubcopilot")) value = QStringLiteral("github_copilot");
    return value;
}

QString defaultApiBaseForProvider(const QString &providerName) {
    const QString normalized = normalizedProviderId(providerName);
    const QString oauthApiBase = providers::defaultApiBaseForProvider(normalized);
    if (!oauthApiBase.trimmed().isEmpty()) {
        return oauthApiBase.trimmed();
    }

    const providers::ProviderSpec spec = providers::findProviderSpec(normalized);
    if (!spec.defaultApiBase.trimmed().isEmpty()) {
        return spec.defaultApiBase.trimmed();
    }
    if (normalized == QStringLiteral("vllm")) {
        return QStringLiteral("http://127.0.0.1:8000/v1");
    }
    return QString();
}

bool resolveTranscriptionAccess(const QString &providerName,
                                const config::ProviderConfig &providerConfig,
                                bool allowRefresh,
                                QString *apiKey,
                                QString *apiBase,
                                QHash<QString, QString> *extraHeaders,
                                QString *error) {
    const QString normalized = normalizedProviderId(providerName);
    const providers::ProviderOAuthResult resolved =
        providers::resolveProviderAccess(normalized, providerConfig, allowRefresh);

    const QString resolvedApiKey = resolved.apiKey.trimmed().isEmpty()
        ? providerConfig.apiKey.trimmed()
        : resolved.apiKey.trimmed();
    QString resolvedApiBase = resolved.apiBase.trimmed();
    if (resolvedApiBase.isEmpty()) {
        resolvedApiBase = providerConfig.apiBase.trimmed();
    }
    if (resolvedApiBase.isEmpty()) {
        resolvedApiBase = defaultApiBaseForProvider(normalized);
    }
    const QHash<QString, QString> resolvedHeaders =
        resolved.headers.isEmpty() ? providerConfig.extraHeaders : resolved.headers;

    const bool allowsBaseOnly = normalized == QStringLiteral("custom") ||
                                normalized == QStringLiteral("vllm");
    const bool requiresExplicitBase = allowsBaseOnly ||
                                      normalized == QStringLiteral("azure_openai");
    const bool hasCredential = !resolvedApiKey.isEmpty() || !resolvedHeaders.isEmpty();
    const bool ready = (allowsBaseOnly || hasCredential) &&
                       (!requiresExplicitBase || !resolvedApiBase.isEmpty());

    if (apiKey) *apiKey = resolvedApiKey;
    if (apiBase) *apiBase = resolvedApiBase;
    if (extraHeaders) *extraHeaders = resolvedHeaders;
    if (error) *error = resolved.error.trimmed();
    return ready;
}

} // namespace

TelegramChannel::TelegramChannel(const config::Config &appConfig,
                                 const QString &workspace,
                                 bus::MessageBus &bus,
                                 QObject *parent)
    : QObject(parent),
      _appConfig(appConfig),
      _config(appConfig.channels.telegram),
      _workspace(workspace),
      _bus(bus) {
    configureProxy();
}

QString TelegramChannel::name() const {
    return QStringLiteral("telegram");
}

bool TelegramChannel::start() {
    if (_running) return true;
    if (_config.token.trimmed().isEmpty()) {
        qWarning(lcTelegram) << "Telegram token is empty, channel disabled";
        return false;
    }
    _running = true;
    qDebug(lcTelegram) << "Starting Telegram long polling";
    QTimer::singleShot(0, this, &TelegramChannel::schedulePoll);
    return true;
}

void TelegramChannel::stop() {
    _running = false;
    if (_pendingPoll) {
        _pendingPoll->disconnect(this);
        _pendingPoll->deleteLater();
        _pendingPoll = nullptr;
    }
    qDebug(lcTelegram) << "Telegram channel stopped";
}

bool TelegramChannel::isAllowed(const QString &senderId) const {
    if (_config.allowFrom.contains(QStringLiteral("*"))) return true;
    if (_config.allowFrom.isEmpty()) return false;
    if (_config.allowFrom.contains(senderId)) return true;

    const QStringList pair = senderId.split(QLatin1Char('|'));
    if (pair.size() == 2) {
        return _config.allowFrom.contains(pair.at(0)) ||
               _config.allowFrom.contains(pair.at(1));
    }
    return false;
}

bool TelegramChannel::shouldTranscribeKind(const QString &kind) const {
    const QString normalized = kind.trimmed().toLower();
    if (normalized == QStringLiteral("voice")) return _config.transcribeVoice;
    if (normalized == QStringLiteral("audio")) return _config.transcribeAudio;
    return false;
}

void TelegramChannel::configureProxy() {
    if (_config.proxy.trimmed().isEmpty()) {
        return;
    }
    qInfo(lcTelegram) << "Telegram HTTP proxy is configured.";
}

QString TelegramChannel::botApiUrl(const QString &method) const {
    return QStringLiteral("https://api.telegram.org/bot%1/%2").arg(_config.token, method);
}

QString TelegramChannel::fileApiUrl(const QString &filePath) const {
    return QStringLiteral("https://api.telegram.org/file/bot%1/%2").arg(_config.token, filePath);
}

QString TelegramChannel::defaultTranscriptionProvider() const {
    const auto isReady = [this](const QString &providerName) {
        const config::ProviderConfig *provider = providerConfigFor(providerName);
        if (!provider) {
            return false;
        }
        return resolveTranscriptionAccess(providerName,
                                          *provider,
                                          false,
                                          nullptr,
                                          nullptr,
                                          nullptr,
                                          nullptr);
    };

    if (isReady(QStringLiteral("openai"))) return QStringLiteral("openai");
    if (isReady(QStringLiteral("openai_codex"))) return QStringLiteral("openai_codex");
    if (isReady(QStringLiteral("custom"))) return QStringLiteral("custom");
    if (isReady(QStringLiteral("vllm"))) return QStringLiteral("vllm");
    return QString();
}

QString TelegramChannel::resolvedTranscriptionProvider() const {
    const QString provider = normalizedProviderId(_config.transcriptionProvider);
    if (!provider.isEmpty() && provider != QStringLiteral("auto")) {
        return provider;
    }
    return defaultTranscriptionProvider();
}

QString TelegramChannel::resolvedTranscriptionModel() const {
    const QString model = _config.transcriptionModel.trimmed();
    return model.isEmpty() ? QStringLiteral("whisper-1") : model;
}

const config::ProviderConfig *TelegramChannel::providerConfigFor(const QString &providerName) const {
    const QString normalized = normalizedProviderId(providerName);
    if (normalized == QStringLiteral("custom")) return &_appConfig.providers.custom;
    if (normalized == QStringLiteral("azure_openai")) return &_appConfig.providers.azureOpenAI;
    if (normalized == QStringLiteral("anthropic")) return &_appConfig.providers.anthropic;
    if (normalized == QStringLiteral("openai")) return &_appConfig.providers.openai;
    if (normalized == QStringLiteral("openrouter")) return &_appConfig.providers.openrouter;
    if (normalized == QStringLiteral("deepseek")) return &_appConfig.providers.deepseek;
    if (normalized == QStringLiteral("groq")) return &_appConfig.providers.groq;
    if (normalized == QStringLiteral("zhipu")) return &_appConfig.providers.zhipu;
    if (normalized == QStringLiteral("dashscope")) return &_appConfig.providers.dashscope;
    if (normalized == QStringLiteral("vllm")) return &_appConfig.providers.vllm;
    if (normalized == QStringLiteral("gemini")) return &_appConfig.providers.gemini;
    if (normalized == QStringLiteral("moonshot")) return &_appConfig.providers.moonshot;
    if (normalized == QStringLiteral("minimax")) return &_appConfig.providers.minimax;
    if (normalized == QStringLiteral("aihubmix")) return &_appConfig.providers.aihubmix;
    if (normalized == QStringLiteral("siliconflow")) return &_appConfig.providers.siliconflow;
    if (normalized == QStringLiteral("volcengine")) return &_appConfig.providers.volcengine;
    if (normalized == QStringLiteral("openai_codex")) return &_appConfig.providers.openaiCodex;
    if (normalized == QStringLiteral("github_copilot")) return &_appConfig.providers.githubCopilot;
    return nullptr;
}

void TelegramChannel::schedulePoll() {
    if (!_running) return;

    QJsonObject payload;
    payload.insert(QStringLiteral("offset"), static_cast<double>(_offset));
    payload.insert(QStringLiteral("timeout"), 30);
    payload.insert(QStringLiteral("limit"), 100);
    payload.insert(QStringLiteral("allowed_updates"), QJsonArray{QStringLiteral("message")});

    QMap<QByteArray, QByteArray> headers;
    headers.insert("Content-Type", "application/json; charset=utf-8");
    platform::network::HttpRequest request =
        http::makeRequest(QStringLiteral("POST"),
                          botApiUrl(QStringLiteral("getUpdates")),
                          headers,
                          QJsonDocument(payload).toJson(QJsonDocument::Compact),
                          40000);
    request.proxyUrl = _config.proxy.trimmed();

    auto *watcher = new QFutureWatcher<platform::network::HttpResponse>(this);
    _pendingPoll = watcher;
    connect(watcher,
            &QFutureWatcher<platform::network::HttpResponse>::finished,
            this,
            &TelegramChannel::onPollReply,
            Qt::UniqueConnection);
    watcher->setFuture(QtConcurrent::run([request]() {
        return platform::network::FastNetHttpTransport::send(request);
    }));
}

void TelegramChannel::onPollReply() {
    QObject *source = sender();
    if (!source) return;
    auto *watcher = static_cast<QFutureWatcher<platform::network::HttpResponse> *>(source);
    const platform::network::HttpResponse response = watcher->result();
    watcher->deleteLater();
    if (_pendingPoll == watcher) {
        _pendingPoll = nullptr;
    }

    if (!_running) return;

    if (!response.ok()) {
        qWarning(lcTelegram) << "Poll error:" << response.error;
        QTimer::singleShot(2000, this, &TelegramChannel::schedulePoll);
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QTimer::singleShot(1000, this, &TelegramChannel::schedulePoll);
        return;
    }

    const QJsonObject root = doc.object();
    if (!root.value(QStringLiteral("ok")).toBool(false)) {
        qWarning(lcTelegram) << "Telegram API error:"
                             << root.value(QStringLiteral("description")).toString();
        QTimer::singleShot(5000, this, &TelegramChannel::schedulePoll);
        return;
    }

    const QJsonArray updates = root.value(QStringLiteral("result")).toArray();
    for (const QJsonValue &v : updates) {
        const QJsonObject update = v.toObject();
        const qint64 updateId = update.value(QStringLiteral("update_id")).toVariant().toLongLong();
        if (updateId >= _offset) {
            _offset = updateId + 1;
        }
        processUpdate(update);
    }

    QTimer::singleShot(0, this, &TelegramChannel::schedulePoll);
}

void TelegramChannel::processUpdate(const QJsonObject &update) {
    const QJsonObject message = update.value(QStringLiteral("message")).toObject();
    if (message.isEmpty()) return;

    const QJsonObject from = message.value(QStringLiteral("from")).toObject();
    const QString uid = QString::number(from.value(QStringLiteral("id")).toVariant().toLongLong());
    const QString username = from.value(QStringLiteral("username")).toString();
    const QString senderId = username.isEmpty() ? uid : (uid + QLatin1Char('|') + username);

    if (!isAllowed(senderId)) {
        qDebug(lcTelegram) << "Ignoring message from unauthorized sender:" << senderId;
        return;
    }

    const QJsonObject chat = message.value(QStringLiteral("chat")).toObject();
    const QString chatId = QString::number(chat.value(QStringLiteral("id")).toVariant().toLongLong());
    const QString messageId = message.value(QStringLiteral("message_id")).toVariant().toString();

    QString content = message.value(QStringLiteral("text")).toString();
    if (content.trimmed().isEmpty()) {
        content = message.value(QStringLiteral("caption")).toString();
    }

    QStringList downloadedFiles;
    QJsonArray mediaMetadata = extractMedia(message, chatId, messageId, &downloadedFiles);
    const QStringList transcriptions = transcribeMedia(&mediaMetadata);
    if (!transcriptions.isEmpty()) {
        const QString transcriptionBlock =
            QStringLiteral("[Telegram audio transcription]\n%1")
                .arg(transcriptions.join(QStringLiteral("\n\n")).trimmed());
        content = content.trimmed().isEmpty()
            ? transcriptionBlock
            : QStringLiteral("%1\n\n%2").arg(content.trimmed(), transcriptionBlock);
    }
    if (content.trimmed().isEmpty() && downloadedFiles.isEmpty()) return;

    QJsonObject metadata;
    metadata.insert(QStringLiteral("message_id"), message.value(QStringLiteral("message_id")));
    metadata.insert(QStringLiteral("user_id"), from.value(QStringLiteral("id")));
    metadata.insert(QStringLiteral("username"), username);
    metadata.insert(QStringLiteral("first_name"), from.value(QStringLiteral("first_name")));
    metadata.insert(QStringLiteral("is_group"), chat.value(QStringLiteral("type")).toString() != QStringLiteral("private"));
    metadata.insert(QStringLiteral("message_thread_id"), message.value(QStringLiteral("message_thread_id")));
    if (!mediaMetadata.isEmpty()) {
        metadata.insert(QStringLiteral("telegram_media"), mediaMetadata);
    }
    if (!transcriptions.isEmpty()) {
        metadata.insert(QStringLiteral("telegram_transcriptions"), QJsonArray::fromStringList(transcriptions));
    }

    bus::InboundMessage inbound;
    inbound.channel = name();
    inbound.senderId = senderId;
    inbound.chatId = chatId;
    inbound.content = content;
    inbound.media = downloadedFiles;
    inbound.metadata = metadata;

    const QJsonValue threadId = message.value(QStringLiteral("message_thread_id"));
    if (!threadId.isUndefined() && !threadId.isNull() &&
        chat.value(QStringLiteral("type")).toString() != QStringLiteral("private")) {
        inbound.sessionKeyOverride = QStringLiteral("telegram:%1:topic:%2")
                                         .arg(chatId, threadId.toVariant().toString());
    }

    qDebug(lcTelegram) << "Inbound from" << senderId << ":" << content.left(60);
    _bus.publishInbound(inbound);
}

void TelegramChannel::send(const bus::OutboundMessage &msg) {
    if (_config.token.trimmed().isEmpty() || msg.chatId.trimmed().isEmpty()) return;

    QString text = msg.content;
    if (text.trimmed().isEmpty() && !msg.media.isEmpty()) {
        text = QStringLiteral("[attachments: %1]").arg(msg.media.size());
    }
    if (text.trimmed().isEmpty()) return;

    QStringList chunks;
    const int maxLen = 3900;
    for (int i = 0; i < text.size(); i += maxLen) {
        chunks.append(text.mid(i, maxLen));
    }

    for (const QString &chunk : chunks) {
        QJsonObject payload;
        payload.insert(QStringLiteral("chat_id"), msg.chatId);
        payload.insert(QStringLiteral("text"), chunk);
        if (_config.replyToMessage) {
            const QString replyId = msg.metadata.value(QStringLiteral("message_id")).toString();
            if (!replyId.isEmpty()) {
                payload.insert(QStringLiteral("reply_to_message_id"), replyId.toLongLong());
            }
        }
        const QJsonValue threadId = msg.metadata.value(QStringLiteral("message_thread_id"));
        if (!threadId.isUndefined() && !threadId.isNull()) {
            payload.insert(QStringLiteral("message_thread_id"), threadId);
        }
        bool ok = false;
        postJsonSync(QStringLiteral("sendMessage"), payload, &ok);
        if (!ok) {
            qWarning(lcTelegram) << "Failed to send message to" << msg.chatId;
            break;
        }
    }
}

QJsonObject TelegramChannel::postJsonSync(const QString &method,
                                          const QJsonObject &payload,
                                          bool *ok,
                                          int timeoutMs) {
    QMap<QByteArray, QByteArray> headers;
    headers.insert("Content-Type", "application/json; charset=utf-8");
    platform::network::HttpRequest request =
        http::makeRequest(QStringLiteral("POST"),
                          botApiUrl(method),
                          headers,
                          QJsonDocument(payload).toJson(QJsonDocument::Compact),
                          timeoutMs);
    request.proxyUrl = _config.proxy.trimmed();
    const platform::network::HttpResponse response =
        platform::network::FastNetHttpTransport::send(request);

    if (!response.ok()) {
        qWarning(lcTelegram) << "Telegram API request failed:" << method << response.error;
        if (ok) *ok = false;
        return {};
    }

    bool parsed = false;
    const QJsonObject root = http::parseJsonObject(response.body, &parsed);
    if (!parsed) {
        if (ok) *ok = false;
        return {};
    }

    if (ok) *ok = root.value(QStringLiteral("ok")).toBool(false);
    return root;
}

QString TelegramChannel::downloadTelegramFile(const QString &fileId,
                                              const QString &kind,
                                              const QString &chatId,
                                              const QString &messageId,
                                              const QString &preferredName,
                                              const QString &fallbackExtension,
                                              QJsonObject *mediaMetadata) {
    if (fileId.trimmed().isEmpty() || _workspace.trimmed().isEmpty()) {
        return QString();
    }

    bool fileOk = false;
    const QJsonObject fileResult = postJsonSync(QStringLiteral("getFile"),
                                                QJsonObject{{QStringLiteral("file_id"), fileId}},
                                                &fileOk,
                                                15000);
    if (!fileOk) {
        qWarning(lcTelegram) << "Failed to resolve Telegram file metadata for" << kind << fileId;
        return QString();
    }

    const QJsonObject fileObject = fileResult.value(QStringLiteral("result")).toObject();
    const QString filePath = fileObject.value(QStringLiteral("file_path")).toString().trimmed();
    if (filePath.isEmpty()) {
        qWarning(lcTelegram) << "Telegram getFile returned empty file_path for" << kind << fileId;
        return QString();
    }

    platform::network::HttpRequest request =
        http::makeRequest(QStringLiteral("GET"), fileApiUrl(filePath), {}, {}, 30000);
    request.proxyUrl = _config.proxy.trimmed();
    const platform::network::HttpResponse response =
        platform::network::FastNetHttpTransport::send(request);
    if (!response.ok()) {
        qWarning(lcTelegram) << "Failed to download Telegram file" << filePath << response.error;
        return QString();
    }

    const QByteArray payload = response.body;
    if (payload.isEmpty()) {
        qWarning(lcTelegram) << "Downloaded empty Telegram file" << filePath;
        return QString();
    }

    const QFileInfo remoteInfo(filePath);
    QString localName = preferredName.trimmed();
    if (localName.isEmpty()) {
        localName = remoteInfo.fileName();
    }
    if (localName.isEmpty()) {
        localName = QStringLiteral("%1-%2").arg(kind, fileId.left(12));
    }
    if (QFileInfo(localName).suffix().isEmpty() && !remoteInfo.suffix().isEmpty()) {
        localName += QLatin1Char('.') + remoteInfo.suffix();
    } else if (QFileInfo(localName).suffix().isEmpty() && !fallbackExtension.trimmed().isEmpty()) {
        localName += fallbackExtension;
    }

    const QString mediaDir = QDir(_workspace).filePath(
        QStringLiteral("runtime/telegram_media/%1").arg(sanitizePathSegment(chatId)));
    QDir().mkpath(mediaDir);

    const QString prefix = sanitizePathSegment(messageId.isEmpty() ? QStringLiteral("message") : messageId);
    const QString fileName = QStringLiteral("%1_%2").arg(prefix, joinedPreferredName(localName, fallbackExtension));
    const QString localPath = QDir(mediaDir).filePath(fileName);

    QSaveFile file(localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning(lcTelegram) << "Failed to create Telegram media file" << localPath;
        return QString();
    }
    if (file.write(payload) < 0 || !file.commit()) {
        qWarning(lcTelegram) << "Failed to persist Telegram media file" << localPath;
        return QString();
    }

    if (mediaMetadata) {
        mediaMetadata->insert(QStringLiteral("kind"), kind);
        mediaMetadata->insert(QStringLiteral("file_id"), fileId);
        mediaMetadata->insert(QStringLiteral("remote_path"), filePath);
        mediaMetadata->insert(QStringLiteral("local_path"), localPath);
        mediaMetadata->insert(QStringLiteral("size"), static_cast<qint64>(payload.size()));
    }
    return localPath;
}

QJsonArray TelegramChannel::extractMedia(const QJsonObject &message,
                                         const QString &chatId,
                                         const QString &messageId,
                                         QStringList *downloadedFiles) {
    QJsonArray media;
    const QList<TelegramMediaSpec> specs = collectMediaSpecs(message);
    for (const TelegramMediaSpec &spec : specs) {
        if (spec.fileId.isEmpty()) {
            continue;
        }

        QJsonObject mediaEntry;
        const QString localPath = downloadTelegramFile(spec.fileId,
                                                       spec.kind,
                                                       chatId,
                                                       messageId,
                                                       spec.preferredName,
                                                       spec.fallbackExtension,
                                                       &mediaEntry);
        if (localPath.isEmpty()) {
            continue;
        }
        media.append(mediaEntry);
        if (downloadedFiles) {
            downloadedFiles->append(localPath);
        }
    }
    return media;
}

QStringList TelegramChannel::transcribeMedia(QJsonArray *mediaMetadata) const {
    QStringList transcriptions;
    if (!mediaMetadata || mediaMetadata->isEmpty()) {
        return transcriptions;
    }

    const QString providerName = resolvedTranscriptionProvider();
    if (providerName.isEmpty()) {
        return transcriptions;
    }

    const config::ProviderConfig *providerConfig = providerConfigFor(providerName);
    if (!providerConfig) {
        qWarning(lcTelegram) << "Telegram transcription provider is unknown:" << providerName;
        return transcriptions;
    }

    QString apiKey;
    QString apiBase;
    QHash<QString, QString> extraHeaders;
    QString accessError;
    if (!resolveTranscriptionAccess(providerName,
                                    *providerConfig,
                                    true,
                                    &apiKey,
                                    &apiBase,
                                    &extraHeaders,
                                    &accessError)) {
        if (!accessError.trimmed().isEmpty()) {
            qWarning(lcTelegram) << "Telegram transcription provider is not ready:" << accessError;
        }
        return transcriptions;
    }

    const QString model = resolvedTranscriptionModel();
    providers::OpenAICompatibleProvider provider(apiKey,
                                                 apiBase,
                                                 model,
                                                 providerName,
                                                 QString(),
                                                 extraHeaders);

    for (int i = 0; i < mediaMetadata->size(); ++i) {
        QJsonObject item = mediaMetadata->at(i).toObject();
        const QString kind = item.value(QStringLiteral("kind")).toString();
        if (!shouldTranscribeKind(kind)) {
            continue;
        }

        const QString localPath = item.value(QStringLiteral("local_path")).toString().trimmed();
        if (localPath.isEmpty()) {
            continue;
        }

        const providers::AudioTranscriptionResult result =
            provider.transcribeAudioFile(localPath,
                                         model,
                                         _config.transcriptionLanguage,
                                         _config.transcriptionPrompt);
        item.insert(QStringLiteral("transcription_provider"), providerName);
        item.insert(QStringLiteral("transcription_model"), result.model);
        if (result.ok) {
            item.insert(QStringLiteral("transcription"), result.text);
            if (!result.language.trimmed().isEmpty()) {
                item.insert(QStringLiteral("transcription_language"), result.language);
            }
            transcriptions.append(result.text);
        } else {
            item.insert(QStringLiteral("transcription_error"), result.error);
            qWarning(lcTelegram) << "Telegram media transcription failed:" << result.error;
        }
        mediaMetadata->replace(i, item);
    }

    return transcriptions;
}

} // namespace yaos::channels
