#include "OpenAICompatibleProvider.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <FastNet/HttpCommon.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QSysInfo>
#include <QUuid>

namespace yaos::providers {

namespace {

bool isAzureEndpoint(const QString &baseOrEndpoint) {
    return baseOrEndpoint.contains(".openai.azure.com", Qt::CaseInsensitive);
}

QString headerAppName() {
    QString value = QCoreApplication::applicationName().trimmed();
    if (value.isEmpty()) {
        value = QFileInfo(QCoreApplication::applicationFilePath()).baseName().trimmed();
    }
    if (value.isEmpty()) {
        value = QStringLiteral("yaos");
    }
    value.replace(' ', '-');
    return value.toLower();
}

QString headerAppVersion() {
    const QString version = QCoreApplication::applicationVersion().trimmed();
    return version.isEmpty() ? QStringLiteral("0.0.0") : version;
}

QString copilotEditorVersionHeader() {
    return QStringLiteral("%1/%2").arg(headerAppName(), headerAppVersion());
}

QString copilotEditorPluginVersionHeader() {
    return QStringLiteral("%1-desktop/%2").arg(headerAppName(), headerAppVersion());
}

QString copilotSessionId() {
    static const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return sessionId;
}

QString copilotMachineId() {
    const QByteArray machineId = QSysInfo::machineUniqueId();
    if (!machineId.isEmpty()) {
        return QString::fromLatin1(machineId.toHex());
    }
    return QStringLiteral("yaos-%1").arg(headerAppName());
}

QString strippedKnownEndpointSuffix(QString base) {
    const QStringList suffixes = {
        QStringLiteral("/chat/completions"),
        QStringLiteral("/audio/transcriptions"),
        QStringLiteral("/models")
    };
    for (const QString &suffix : suffixes) {
        const int index = base.indexOf(suffix, 0, Qt::CaseInsensitive);
        if (index >= 0) {
            base = base.left(index);
            break;
        }
    }
    while (base.endsWith('/')) {
        base.chop(1);
    }
    return base;
}

QString providerErrorMessage(const QByteArray &payload, const QString &fallback) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error == QJsonParseError::NoError && doc.isObject()) {
        const QString message = doc.object().value("error").toObject().value("message").toString().trimmed();
        if (!message.isEmpty()) {
            return message;
        }
    }
    const QString text = QString::fromUtf8(payload).trimmed();
    return text.isEmpty() ? fallback : text;
}

QStringList githubCopilotFallbackModels() {
    // GitHub Copilot does not consistently expose a live /models catalog.
    // Keep a conservative built-in list so model sync can still seed usable choices.
    return QStringList{
        QStringLiteral("claude-haiku-4.5"),
        QStringLiteral("claude-sonnet-4"),
        QStringLiteral("claude-sonnet-4.5"),
        QStringLiteral("gpt-4.1"),
        QStringLiteral("gpt-4o"),
        QStringLiteral("gpt-5"),
        QStringLiteral("gpt-5-mini")
    };
}

bool isChatIncompatibleImageModel(const QString &modelName) {
    const QString normalized = modelName.trimmed().toLower();
    if (normalized.isEmpty()) {
        return false;
    }
    const QString local = normalized.contains(QLatin1Char('/'))
        ? normalized.section(QLatin1Char('/'), -1)
        : normalized;
    return local.startsWith(QStringLiteral("qwen-image")) ||
           local.startsWith(QStringLiteral("gpt-image-")) ||
           normalized.contains(QStringLiteral("image-generation"));
}

void appendReasoningText(QStringList &parts, const QString &text) {
    if (!text.trimmed().isEmpty()) {
        parts.append(text);
    }
}

QStringList reasoningTextParts(const QJsonValue &value) {
    QStringList parts;
    if (value.isString()) {
        appendReasoningText(parts, value.toString());
        return parts;
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array) {
            parts.append(reasoningTextParts(item));
        }
        return parts;
    }
    if (!value.isObject()) {
        return parts;
    }

    const QJsonObject obj = value.toObject();
    const QStringList textKeys = {
        QStringLiteral("text"),
        QStringLiteral("content"),
        QStringLiteral("reasoning"),
        QStringLiteral("thinking"),
        QStringLiteral("summary"),
        QStringLiteral("delta")
    };
    for (const QString &key : textKeys) {
        if (obj.contains(key)) {
            parts.append(reasoningTextParts(obj.value(key)));
        }
    }
    return parts;
}

QString joinReasoningParts(const QStringList &parts) {
    QStringList nonEmpty;
    for (const QString &part : parts) {
        if (!part.trimmed().isEmpty()) {
            nonEmpty.append(part);
        }
    }
    return nonEmpty.join(QStringLiteral("\n"));
}

QString reasoningFromContentBlocks(const QJsonValue &content) {
    if (!content.isArray()) {
        return QString();
    }

    QStringList parts;
    const QJsonArray blocks = content.toArray();
    for (const QJsonValue &blockValue : blocks) {
        const QJsonObject block = blockValue.toObject();
        const QString type = block.value(QStringLiteral("type")).toString().toLower();
        if (type.contains(QStringLiteral("reasoning")) ||
            type.contains(QStringLiteral("thinking"))) {
            parts.append(reasoningTextParts(block));
        }
    }
    return joinReasoningParts(parts);
}

QString reasoningFromObject(const QJsonObject &obj) {
    QStringList parts;
    const QStringList keys = {
        QStringLiteral("reasoning_content"),
        QStringLiteral("reasoningContent"),
        QStringLiteral("thinking_content"),
        QStringLiteral("thinkingContent"),
        QStringLiteral("reasoning"),
        QStringLiteral("thinking"),
        QStringLiteral("reasoning_summary"),
        QStringLiteral("reasoningSummary"),
        QStringLiteral("reasoning_details"),
        QStringLiteral("reasoningDetails"),
        QStringLiteral("thinking_delta"),
        QStringLiteral("thinkingDelta")
    };
    for (const QString &key : keys) {
        if (obj.contains(key)) {
            parts.append(reasoningTextParts(obj.value(key)));
        }
    }

    const QString blockReasoning = reasoningFromContentBlocks(obj.value(QStringLiteral("content")));
    if (!blockReasoning.isEmpty()) {
        parts.append(blockReasoning);
    }
    return joinReasoningParts(parts);
}

QString extractTaggedThinking(QString *content) {
    if (!content || content->isEmpty()) {
        return QString();
    }

    const QString source = *content;
    const QString lower = source.toLower();
    QString cleaned;
    QStringList thinkingParts;
    int pos = 0;

    while (true) {
        const int start = lower.indexOf(QStringLiteral("<think>"), pos);
        if (start < 0) {
            break;
        }
        const int bodyStart = start + 7;
        const int end = lower.indexOf(QStringLiteral("</think>"), bodyStart);
        if (end < 0) {
            return QString();
        }
        cleaned += source.mid(pos, start - pos);
        appendReasoningText(thinkingParts, source.mid(bodyStart, end - bodyStart));
        pos = end + 8;
    }

    if (thinkingParts.isEmpty()) {
        return QString();
    }

    cleaned += source.mid(pos);
    *content = cleaned.trimmed();
    return joinReasoningParts(thinkingParts);
}

QString normalizedReasoningEffort(const QString &value) {
    const QString effort = value.trimmed().toLower();
    const QStringList supported = {
        QStringLiteral("none"),
        QStringLiteral("minimal"),
        QStringLiteral("low"),
        QStringLiteral("medium"),
        QStringLiteral("high"),
        QStringLiteral("xhigh")
    };
    return supported.contains(effort) ? effort : QString();
}

bool isOpenAIReasoningEffortModel(const QString &modelName) {
    QString local = modelName.trimmed().toLower();
    if (local.contains(QLatin1Char('/'))) {
        local = local.section(QLatin1Char('/'), -1);
    }
    return local.startsWith(QStringLiteral("o1")) ||
           local.startsWith(QStringLiteral("o3")) ||
           local.startsWith(QStringLiteral("o4")) ||
           local.startsWith(QStringLiteral("gpt-5")) ||
           local.contains(QStringLiteral("codex"));
}

void applyReasoningOptions(
    QJsonObject &payload,
    const QString &providerName,
    const QString &modelName,
    const QString &reasoningEffort
) {
    const QString effort = normalizedReasoningEffort(reasoningEffort);
    if (effort.isEmpty()) {
        return;
    }

    const QString provider = providerName.trimmed().isEmpty()
        ? QStringLiteral("openai")
        : providerName.trimmed().toLower();

    if (provider == QStringLiteral("openrouter")) {
        payload[QStringLiteral("reasoning")] = QJsonObject{
            {QStringLiteral("effort"), effort},
            {QStringLiteral("exclude"), false}
        };
        return;
    }

    if ((provider == QStringLiteral("openai") || provider == QStringLiteral("openai_codex")) &&
        effort != QStringLiteral("none") &&
        effort != QStringLiteral("xhigh") &&
        isOpenAIReasoningEffortModel(modelName)) {
        payload[QStringLiteral("reasoning_effort")] = effort;
    }
}

std::string toStdString(const QByteArray &value) {
    return std::string(value.constData(), static_cast<size_t>(value.size()));
}

std::string toStdString(const QString &value) {
    return toStdString(value.toUtf8());
}

} // namespace

OpenAICompatibleProvider::OpenAICompatibleProvider(
    const QString &apiKey,
    const QString &apiBase,
    const QString &defaultModel,
    const QString &providerName,
    const QString &reasoningEffort,
    const QHash<QString, QString> &extraHeaders
) : _apiKey(apiKey),
    _apiBase(apiBase),
    _defaultModel(defaultModel),
    _providerName(providerName),
    _reasoningEffort(reasoningEffort),
    _extraHeaders(extraHeaders) {}

QString OpenAICompatibleProvider::defaultModel() const {
    return _defaultModel;
}

QString OpenAICompatibleProvider::backendName() const {
    return _providerName.trimmed().isEmpty() ? "openai" : _providerName;
}

QString OpenAICompatibleProvider::normalizedApiBase() const {
    QString base = _apiBase.trimmed();
    if (base.isEmpty()) {
        if (_providerName == "azure_openai" || _providerName == "codebuddy") {
            return QString();
        }
        base = "https://api.openai.com/v1";
    }
    if (base.endsWith('/')) {
        base.chop(1);
    }
    return base;
}

QString OpenAICompatibleProvider::endpointForChat() const {
    QString base = strippedKnownEndpointSuffix(normalizedApiBase());
    if (base.isEmpty()) {
        return QString();
    }
    const bool isAzure = (_providerName == "azure_openai") || isAzureEndpoint(base);
    if (isAzure) {
        QString endpoint = base + "/chat/completions";
        if (!endpoint.contains("api-version=")) {
            endpoint += endpoint.contains('?') ? "&api-version=2024-10-21" : "?api-version=2024-10-21";
        }
        return endpoint;
    }

    return base + "/chat/completions";
}

QString OpenAICompatibleProvider::endpointForTranscription() const {
    QString base = strippedKnownEndpointSuffix(normalizedApiBase());
    if (base.isEmpty()) {
        return QString();
    }
    const bool isAzure = (_providerName == "azure_openai") || isAzureEndpoint(base);
    if (isAzure) {
        QString endpoint = base + "/audio/transcriptions";
        if (!endpoint.contains("api-version=")) {
            endpoint += endpoint.contains('?') ? "&api-version=2024-10-21" : "?api-version=2024-10-21";
        }
        return endpoint;
    }

    return base + "/audio/transcriptions";
}

QJsonArray OpenAICompatibleProvider::normalizeMessages(const QJsonArray &messages) const {
    QJsonArray out;
    const bool preserveReasoningContent =
        _providerName == QStringLiteral("deepseek") ||
        _providerName == QStringLiteral("vllm") ||
        _providerName == QStringLiteral("custom");
    const bool preserveReasoningDetails =
        _providerName == QStringLiteral("openrouter") ||
        _providerName == QStringLiteral("vllm") ||
        _providerName == QStringLiteral("custom");
    for (const QJsonValue &v : messages) {
        const QJsonObject src = v.toObject();
        QJsonObject clean;
        clean["role"] = src.value("role").toString();
        if (src.contains("content")) {
            clean["content"] = src.value("content");
        } else if (clean.value("role").toString() == "assistant" && src.contains("tool_calls")) {
            clean["content"] = QJsonValue(QJsonValue::Null);
        } else {
            clean["content"] = "(empty)";
        }
        if (src.contains("tool_calls")) clean["tool_calls"] = src.value("tool_calls");
        if (src.contains("tool_call_id")) clean["tool_call_id"] = src.value("tool_call_id");
        if (src.contains("name")) clean["name"] = src.value("name");
        if (clean.value("role").toString() == QStringLiteral("assistant")) {
            if (preserveReasoningContent) {
                if (src.contains("reasoning_content")) {
                    clean["reasoning_content"] = src.value("reasoning_content");
                } else if (src.contains("reasoning")) {
                    clean["reasoning_content"] = src.value("reasoning");
                }
            }
            if (preserveReasoningDetails && src.contains("reasoning_details")) {
                clean["reasoning_details"] = src.value("reasoning_details");
            }
            if (preserveReasoningDetails && src.contains("reasoning")) {
                clean["reasoning"] = src.value("reasoning");
            }
        }
        out.append(clean);
    }
    return out;
}

platform::network::HttpRequest OpenAICompatibleProvider::buildRequest(const QString &endpoint) const {
    platform::network::HttpRequest req;
    req.url = endpoint;

    const bool isAzure = (_providerName == "azure_openai") || endpoint.contains(".openai.azure.com", Qt::CaseInsensitive);
    if (isAzure) {
        if (!_apiKey.isEmpty()) {
            req.headers.insert("api-key", _apiKey.toUtf8());
        }
    } else if (_providerName == "codebuddy") {
        if (!_apiKey.isEmpty()) {
            req.headers.insert("X-Api-Key", _apiKey.toUtf8());
        }
    } else if (!_apiKey.isEmpty()) {
        req.headers.insert("Authorization", ("Bearer " + _apiKey).toUtf8());
    }

    if (_providerName == "github_copilot") {
        req.headers.insert("Accept", "application/json");
        req.headers.insert("Editor-Version", copilotEditorVersionHeader().toUtf8());
        req.headers.insert("Editor-Plugin-Version", copilotEditorPluginVersionHeader().toUtf8());
        req.headers.insert("Openai-Organization", "github-copilot");
        req.headers.insert("X-GitHub-Api-Version", "2025-05-01");
        req.headers.insert("X-Request-Id", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
        req.headers.insert("VScode-SessionId", copilotSessionId().toUtf8());
        req.headers.insert("VScode-MachineId", copilotMachineId().toUtf8());
    }

    for (auto it = _extraHeaders.begin(); it != _extraHeaders.end(); ++it) {
        req.headers.insert(it.key().toUtf8(), it.value().toUtf8());
    }
    return req;
}

agent::LLMResponse OpenAICompatibleProvider::chat(
    const QJsonArray &messages,
    const QJsonArray &tools,
    const QString &model,
    double temperature,
    int maxTokens
) {
    agent::LLMResponse out;
    const QString endpoint = endpointForChat();
    const QString effectiveModel = model.isEmpty() ? _defaultModel : model;
    if (endpoint.isEmpty()) {
        if (_providerName == "azure_openai") {
            out.content = "Error: azure_openai requires providers.azureOpenAI.apiBase";
        } else if (_providerName == "codebuddy") {
            out.content = "Error: codebuddy requires providers.codebuddy.apiBase";
        } else {
            out.content = "Error: provider API base is not configured";
        }
        out.finishReason = "error";
        return out;
    }
    if (isChatIncompatibleImageModel(effectiveModel)) {
        out.content = QStringLiteral("Error: model '%1' is an image-generation model and cannot be used for chat. Pick a text/chat model or pass --model explicitly.")
                          .arg(effectiveModel);
        out.finishReason = "error";
        return out;
    }
    if ((_providerName == "github_copilot" || _providerName == "openai_codex") &&
        _apiKey.trimmed().isEmpty() &&
        _extraHeaders.isEmpty()) {
        out.content = QStringLiteral("Error: %1 is not authorized yet.").arg(_providerName);
        out.finishReason = "error";
        return out;
    }
    QJsonObject payload;
    payload["model"] = effectiveModel;
    payload["messages"] = normalizeMessages(messages);
    payload["temperature"] = temperature;
    payload["max_tokens"] = maxTokens;
    applyReasoningOptions(payload, _providerName, effectiveModel, _reasoningEffort);
    if (!tools.isEmpty()) {
        payload["tools"] = tools;
    }

    platform::network::HttpRequest request = buildRequest(endpoint);
    request.method = QStringLiteral("POST");
    request.headers.insert("Content-Type", "application/json; charset=utf-8");
    request.body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    request.timeoutMs = 300000;

    const platform::network::HttpResponse response = platform::network::FastNetHttpTransport::send(request);
    if (!response.ok()) {
        const QString fallback = response.error.isEmpty()
                                     ? QStringLiteral("HTTP %1").arg(response.statusCode)
                                     : response.error;
        const QString message = providerErrorMessage(response.body, fallback);
        out.content = "Error: " + message;
        out.finishReason = "error";
        return out;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        out.content = "Error: invalid provider response";
        out.finishReason = "error";
        return out;
    }

    const QJsonObject root = doc.object();
    const QJsonArray choices = root.value("choices").toArray();
    if (choices.isEmpty()) {
        out.content = root.value("error").toObject().value("message").toString("Error: empty choices");
        out.finishReason = "error";
        return out;
    }

    const QJsonObject choice = choices.at(0).toObject();
    const QJsonObject msg = choice.value("message").toObject();
    out.finishReason = choice.value("finish_reason").toString("stop");

    if (msg.value("content").isString()) {
        out.content = msg.value("content").toString();
    } else if (msg.value("content").isArray()) {
        QStringList parts;
        const QJsonArray arr = msg.value("content").toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject obj = v.toObject();
            if (obj.value("type").toString() == "text") {
                parts.append(obj.value("text").toString());
            }
        }
        out.content = parts.join("\n");
    } else {
        out.content.clear();
    }

    out.thinking = reasoningFromObject(msg);
    out.reasoningDetails = msg.value(QStringLiteral("reasoning_details")).toArray();
    const QString choiceThinking = reasoningFromObject(choice);
    if (!choiceThinking.isEmpty()) {
        out.thinking = out.thinking.isEmpty()
            ? choiceThinking
            : out.thinking + QStringLiteral("\n") + choiceThinking;
    }
    const QString taggedThinking = extractTaggedThinking(&out.content);
    if (!taggedThinking.isEmpty()) {
        out.thinking = out.thinking.isEmpty()
            ? taggedThinking
            : out.thinking + QStringLiteral("\n") + taggedThinking;
    }

    const QJsonArray toolCalls = msg.value("tool_calls").toArray();
    for (const QJsonValue &v : toolCalls) {
        const QJsonObject tc = v.toObject();
        const QJsonObject fn = tc.value("function").toObject();
        QJsonParseError argErr;
        const QByteArray argRaw = fn.value("arguments").toString("{}").toUtf8();
        QJsonDocument argDoc = QJsonDocument::fromJson(argRaw, &argErr);
        QJsonObject args;
        if (argErr.error == QJsonParseError::NoError && argDoc.isObject()) {
            args = argDoc.object();
        }

        agent::ToolCallRequest call;
        call.id = tc.value("id").toString();
        call.name = fn.value("name").toString();
        call.arguments = args;
        out.toolCalls.append(call);
    }

    return out;
}

AudioTranscriptionResult OpenAICompatibleProvider::transcribeAudioFile(
    const QString &filePath,
    const QString &model,
    const QString &language,
    const QString &prompt
) const {
    AudioTranscriptionResult out;
    out.model = model.trimmed().isEmpty() ? _defaultModel.trimmed() : model.trimmed();
    if (out.model.isEmpty()) {
        out.model = QStringLiteral("whisper-1");
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        out.error = QStringLiteral("Audio file does not exist: %1").arg(filePath);
        return out;
    }

    const QString endpoint = endpointForTranscription();
    out.endpoint = endpoint;
    if (endpoint.isEmpty()) {
        out.error = QStringLiteral("Audio transcription endpoint is not configured");
        return out;
    }
    FastNet::HttpMultipartBuilder multipart;
    multipart.addField("model", toStdString(out.model));

    if (!language.trimmed().isEmpty()) {
        multipart.addField("language", toStdString(language.trimmed()));
    }

    if (!prompt.trimmed().isEmpty()) {
        multipart.addField("prompt", toStdString(prompt.trimmed()));
    }

    multipart.addField("response_format", "verbose_json");

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        out.error = QStringLiteral("Failed to open audio file for transcription: %1").arg(filePath);
        return out;
    }
    const QByteArray fileBytes = file.readAll();

    QMimeDatabase mimeDatabase;
    const QString mimeType = mimeDatabase.mimeTypeForFile(fileInfo).name();
    multipart.addFile("file",
                      toStdString(fileInfo.fileName()),
                      toStdString(fileBytes),
                      toStdString(mimeType.trimmed().isEmpty()
                                      ? QStringLiteral("application/octet-stream")
                                      : mimeType));

    platform::network::HttpRequest request = buildRequest(endpoint);
    request.method = QStringLiteral("POST");
    request.headers.insert("Content-Type", QByteArray::fromStdString(multipart.contentType()));
    const std::string body = multipart.build();
    request.body = QByteArray(body.data(), static_cast<int>(body.size()));
    request.timeoutMs = 300000;

    const platform::network::HttpResponse response = platform::network::FastNetHttpTransport::send(request);
    const QByteArray payload = response.body;
    if (!response.ok()) {
        const QString fallback = response.error.isEmpty()
                                     ? QStringLiteral("HTTP %1").arg(response.statusCode)
                                     : response.error;
        out.error = providerErrorMessage(payload, fallback);
        return out;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error == QJsonParseError::NoError && doc.isObject()) {
        out.raw = doc.object();
        out.text = out.raw.value(QStringLiteral("text")).toString().trimmed();
        out.language = out.raw.value(QStringLiteral("language")).toString().trimmed();
        out.ok = !out.text.isEmpty();
        if (!out.ok) {
            out.error = QStringLiteral("Audio transcription returned empty text");
        }
        return out;
    }

    out.text = QString::fromUtf8(payload).trimmed();
    out.ok = !out.text.isEmpty();
    if (!out.ok) {
        out.error = QStringLiteral("Invalid audio transcription response");
    }
    return out;
}

QStringList OpenAICompatibleProvider::listModels() {
    const QString base = strippedKnownEndpointSuffix(normalizedApiBase());
    if (base.isEmpty()) {
        return _providerName == "github_copilot" ? githubCopilotFallbackModels() : QStringList{};
    }
    const QString endpoint = base + "/models";

    QStringList out;
    platform::network::HttpRequest request = buildRequest(endpoint);
    request.method = QStringLiteral("GET");
    request.timeoutMs = 10000;

    const platform::network::HttpResponse response = platform::network::FastNetHttpTransport::send(request);
    if (response.ok()) {
        const QJsonDocument doc = QJsonDocument::fromJson(response.body);
        const QJsonArray data = doc.object().value("data").toArray();
        for (const QJsonValue &v : data) {
            out.append(v.toObject().value("id").toString());
        }
    }
    if (out.isEmpty() && _providerName == "github_copilot") {
        return githubCopilotFallbackModels();
    }
    return out;
}

agent::LLMResponse OpenAICompatibleProvider::chatStreaming(
    const QJsonArray &messages,
    const QJsonArray &tools,
    const QString &model,
    double temperature,
    int maxTokens,
    LLMStreamCallback callback
) {
    agent::LLMResponse out;
    const QString endpoint = endpointForChat();
    const QString effectiveModel = model.isEmpty() ? _defaultModel : model;

    if (endpoint.isEmpty() || isChatIncompatibleImageModel(effectiveModel)) {
        // Fall back to blocking for edge cases
        return chat(messages, tools, model, temperature, maxTokens);
    }

    QJsonObject payload;
    payload["model"]       = effectiveModel;
    payload["messages"]    = normalizeMessages(messages);
    payload["temperature"] = temperature;
    payload["max_tokens"]  = maxTokens;
    payload["stream"]      = true;
    applyReasoningOptions(payload, _providerName, effectiveModel, _reasoningEffort);
    if (!tools.isEmpty()) {
        payload["tools"] = tools;
    }

    platform::network::HttpRequest request = buildRequest(endpoint);
    request.method = QStringLiteral("POST");
    request.headers.insert("Content-Type", "application/json; charset=utf-8");
    request.body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    request.timeoutMs = 300000;

    QString fullContent;
    QString fullThinking;
    QJsonArray fullReasoningDetails;
    // Tool call accumulation: index -> {id, name, argBuffer}
    QHash<int, QString> tcIds, tcNames, tcArgBuffers;

    QByteArray lineBuffer;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    QString streamError;

    auto onChunk = [&](const QByteArray &chunk) -> bool {
        lineBuffer.append(chunk);
        int start = 0;
        while (true) {
            const int nl = lineBuffer.indexOf('\n', start);
            if (nl < 0) break;
            const QByteArray line = lineBuffer.mid(start, nl - start).trimmed();
            start = nl + 1;

            if (!line.startsWith("data:")) continue;
            const QByteArray jsonBytes = line.mid(5).trimmed();
            if (jsonBytes.isEmpty()) continue;
            if (jsonBytes == "[DONE]") continue;

            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;

            const QJsonObject root = doc.object();
            const QJsonArray choices = root.value("choices").toArray();
            if (choices.isEmpty()) continue;

            const QJsonObject choice = choices.at(0).toObject();
            out.finishReason = choice.value("finish_reason").toString(out.finishReason);

            const QJsonObject delta = choice.value("delta").toObject();

            const QString thinkingDelta = reasoningFromObject(delta);
            if (!thinkingDelta.isEmpty()) {
                fullThinking += thinkingDelta;
                if (callback) {
                    LLMStreamChunk c;
                    c.thinkingDelta = thinkingDelta;
                    callback(c);
                }
            }
            const QJsonArray reasoningDetails = delta.value(QStringLiteral("reasoning_details")).toArray();
            for (const QJsonValue &detail : reasoningDetails) {
                fullReasoningDetails.append(detail);
            }

            // Text delta
            if (delta.contains("content") && !delta.value("content").isNull()) {
                const QString text = delta.value("content").toString();
                if (!text.isEmpty()) {
                    fullContent += text;
                    if (callback) {
                        LLMStreamChunk c;
                        c.contentDelta = text;
                        callback(c);
                    }
                }
            }

            // Tool call deltas
            const QJsonArray tcDeltas = delta.value("tool_calls").toArray();
            for (const QJsonValue &tv : tcDeltas) {
                const QJsonObject tc = tv.toObject();
                const int idx = tc.value("index").toInt();
                if (tc.contains("id"))   tcIds[idx]   = tc.value("id").toString();
                if (tc.contains("function")) {
                    const QJsonObject fn = tc.value("function").toObject();
                    if (fn.contains("name"))      tcNames[idx]      = fn.value("name").toString();
                    if (fn.contains("arguments")) tcArgBuffers[idx] += fn.value("arguments").toString();
                }
            }
        }
        lineBuffer = lineBuffer.mid(start);
        return true;
    };

    auto onDone = [&](int statusCode, const QString &error) {
        if (statusCode != 0 && (statusCode < 200 || statusCode >= 300)) {
            streamError = error.isEmpty() ? QStringLiteral("HTTP %1").arg(statusCode) : error;
        } else if (!error.isEmpty() && statusCode == 0) {
            streamError = error;
        }
        std::lock_guard<std::mutex> lock(mtx);
        done = true;
        cv.notify_all();
    };

    const bool started = platform::network::FastNetHttpTransport::sendStreaming(request, onChunk, onDone);
    if (!started) {
        out.content      = QStringLiteral("Error: failed to start streaming request");
        out.finishReason = "error";
        return out;
    }

    std::unique_lock<std::mutex> lock(mtx);
    const int waitMs = (request.timeoutMs > 0 ? request.timeoutMs : 300000) + 10000;
    if (!cv.wait_for(lock, std::chrono::milliseconds(waitMs), [&] { return done; })) {
        out.content      = QStringLiteral("Error: HTTP stream timed out");
        out.finishReason = "error";
        return out;
    }

    if (!streamError.isEmpty()) {
        out.content      = "Error: " + streamError;
        out.finishReason = "error";
        return out;
    }

    if (fullContent.isEmpty() && !lineBuffer.trimmed().isEmpty()) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(lineBuffer, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject root = doc.object();
            const QJsonArray choices = root.value("choices").toArray();
            if (!choices.isEmpty()) {
                const QJsonObject choice = choices.at(0).toObject();
                out.finishReason = choice.value("finish_reason").toString("stop");
                const QJsonObject msg = choice.value("message").toObject();
                if (msg.value("content").isString()) {
                    fullContent = msg.value("content").toString();
                }
                fullThinking = reasoningFromObject(msg);
                const QString choiceThinking = reasoningFromObject(choice);
                if (!choiceThinking.isEmpty()) {
                    fullThinking = fullThinking.isEmpty()
                        ? choiceThinking
                        : fullThinking + QStringLiteral("\n") + choiceThinking;
                }
                const QJsonArray reasoningDetails = msg.value(QStringLiteral("reasoning_details")).toArray();
                for (const QJsonValue &detail : reasoningDetails) {
                    fullReasoningDetails.append(detail);
                }
            }
        }
    }

    const QString taggedThinking = extractTaggedThinking(&fullContent);
    if (!taggedThinking.isEmpty()) {
        fullThinking = fullThinking.isEmpty()
            ? taggedThinking
            : fullThinking + QStringLiteral("\n") + taggedThinking;
    }
    out.content = fullContent.trimmed();
    out.thinking = fullThinking.trimmed();
    out.reasoningDetails = fullReasoningDetails;

    for (auto it = tcIds.constBegin(); it != tcIds.constEnd(); ++it) {
        const int idx = it.key();
        const QString argsStr = tcArgBuffers.value(idx);
        QJsonParseError argErr;
        const QJsonDocument argDoc = QJsonDocument::fromJson(argsStr.toUtf8(), &argErr);
        agent::ToolCallRequest tc;
        tc.id        = it.value();
        tc.name      = tcNames.value(idx);
        tc.arguments = (argErr.error == QJsonParseError::NoError && argDoc.isObject())
                           ? argDoc.object()
                           : QJsonObject();
        out.toolCalls.append(tc);
    }

    if (out.finishReason.isEmpty()) {
        out.finishReason = out.hasToolCalls() ? "tool_calls" : "stop";
    }

    if (callback) {
        LLMStreamChunk fin;
        fin.done = true;
        callback(fin);
    }

    return out;
}

} // namespace yaos::providers
