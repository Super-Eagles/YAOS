#include "AnthropicProvider.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>

#include "platform/network/FastNetHttpTransport.h"

namespace yaos::providers {

namespace {

QString providerErrorMessage(const QByteArray &payload, const QString &fallback) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error == QJsonParseError::NoError && doc.isObject()) {
        const QString message = doc.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) {
            return message;
        }
    }
    const QString text = QString::fromUtf8(payload).trimmed();
    return text.isEmpty() ? fallback : text;
}

void appendAnthropicReasoningBlocks(QJsonArray &content, const QJsonObject &msg) {
    const QJsonArray blocks = msg.value(QStringLiteral("reasoning_details")).toArray();
    for (const QJsonValue &value : blocks) {
        const QJsonObject block = value.toObject();
        const QString type = block.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("thinking") || type == QStringLiteral("redacted_thinking")) {
            content.append(block);
        }
    }
}

} // namespace

AnthropicProvider::AnthropicProvider(
    const QString &apiKey,
    const QString &apiBase,
    const QString &defaultModel,
    const QString &reasoningEffort
) : _apiKey(apiKey),
    _apiBase(apiBase),
    _defaultModel(defaultModel),
    _reasoningEffort(reasoningEffort) {}

QString AnthropicProvider::defaultModel() const {
    return _defaultModel;
}

QString AnthropicProvider::backendName() const {
    return "anthropic";
}

QString AnthropicProvider::endpointForMessages() const {
    QString base = _apiBase.trimmed();
    if (base.isEmpty()) {
        base = "https://api.anthropic.com/v1";
    }
    if (base.endsWith('/')) {
        base.chop(1);
    }
    if (!base.endsWith("/messages")) {
        base += "/messages";
    }
    return base;
}

QString AnthropicProvider::stripProviderPrefix(const QString &model) {
    QString m = model.trimmed();
    if (m.startsWith("anthropic/")) {
        m = m.mid(QString("anthropic/").size());
    }
    return m;
}

QJsonObject AnthropicProvider::toAnthropicTool(const QJsonObject &openaiTool) {
    const QJsonObject fn = openaiTool.value("function").toObject();
    QJsonObject out;
    out["name"] = fn.value("name").toString();
    out["description"] = fn.value("description").toString();
    out["input_schema"] = fn.value("parameters").toObject();
    return out;
}

QJsonArray AnthropicProvider::toAnthropicMessages(const QJsonArray &messages, QString *systemPrompt) {
    QJsonArray out;
    QStringList systemParts;

    for (const QJsonValue &v : messages) {
        const QJsonObject msg = v.toObject();
        const QString role = msg.value("role").toString();

        if (role == "system") {
            const QString c = msg.value("content").toString();
            if (!c.trimmed().isEmpty()) {
                systemParts.append(c);
            }
            continue;
        }

        if (role == "tool") {
            QJsonArray content;
            content.append(QJsonObject{
                {"type", "tool_result"},
                {"tool_use_id", msg.value("tool_call_id").toString()},
                {"content", msg.value("content").toString()}
            });
            out.append(QJsonObject{
                {"role", "user"},
                {"content", content}
            });
            continue;
        }

        if (role == "assistant" && msg.contains("tool_calls")) {
            QJsonArray content;
            appendAnthropicReasoningBlocks(content, msg);
            const QString text = msg.value("content").toString();
            if (!text.trimmed().isEmpty()) {
                content.append(QJsonObject{
                    {"type", "text"},
                    {"text", text}
                });
            }
            const QJsonArray tcalls = msg.value("tool_calls").toArray();
            for (const QJsonValue &tv : tcalls) {
                const QJsonObject tc = tv.toObject();
                const QJsonObject fn = tc.value("function").toObject();
                QJsonParseError argErr;
                const QByteArray argBytes = fn.value("arguments").toString("{}").toUtf8();
                const QJsonDocument argDoc = QJsonDocument::fromJson(argBytes, &argErr);
                const QJsonObject args = (argErr.error == QJsonParseError::NoError && argDoc.isObject())
                                         ? argDoc.object()
                                         : QJsonObject();
                content.append(QJsonObject{
                    {"type", "tool_use"},
                    {"id", tc.value("id").toString()},
                    {"name", fn.value("name").toString()},
                    {"input", args}
                });
            }
            out.append(QJsonObject{
                {"role", "assistant"},
                {"content", content}
            });
            continue;
        }

        const QString contentText = msg.value("content").toString();
        if (role == "assistant" && msg.contains("reasoning_details")) {
            QJsonArray content;
            appendAnthropicReasoningBlocks(content, msg);
            if (!contentText.trimmed().isEmpty()) {
                content.append(QJsonObject{
                    {"type", "text"},
                    {"text", contentText}
                });
            }
            out.append(QJsonObject{
                {"role", "assistant"},
                {"content", content}
            });
            continue;
        }

        out.append(QJsonObject{
            {"role", role == "assistant" ? "assistant" : "user"},
            {"content", QJsonArray{QJsonObject{
                {"type", "text"},
                {"text", contentText}
            }}}
        });
    }

    if (systemPrompt) {
        *systemPrompt = systemParts.join("\n\n");
    }
    return out;
}

agent::LLMResponse AnthropicProvider::chat(
    const QJsonArray &messages,
    const QJsonArray &tools,
    const QString &model,
    double temperature,
    int maxTokens
) {
    agent::LLMResponse out;

    QString systemPrompt;
    const QJsonArray anthropicMessages = toAnthropicMessages(messages, &systemPrompt);

    QJsonArray anthropicTools;
    for (const QJsonValue &v : tools) {
        anthropicTools.append(toAnthropicTool(v.toObject()));
    }

    QJsonObject payload;
    payload["model"] = stripProviderPrefix(model.isEmpty() ? _defaultModel : model);
    payload["messages"] = anthropicMessages;
    payload["max_tokens"] = maxTokens;
    payload["temperature"] = temperature;
    if (!systemPrompt.trimmed().isEmpty()) {
        payload["system"] = systemPrompt;
    }
    if (!anthropicTools.isEmpty()) {
        payload["tools"] = anthropicTools;
    }

    // Enable extended thinking when reasoningEffort is configured.
    // Anthropic requires budget_tokens >= 1024 and temperature == 1 when thinking is on.
    const QString effort = _reasoningEffort.trimmed().toLower();
    const bool thinkingEnabled = effort == "low" || effort == "medium" || effort == "high";
    if (thinkingEnabled) {
        int budgetTokens = 5000;
        if (effort == "medium") budgetTokens = 10000;
        else if (effort == "high") budgetTokens = 16000;
        payload["thinking"] = QJsonObject{
            {"type", "enabled"},
            {"budget_tokens", budgetTokens}
        };
        payload["temperature"] = 1.0; // required by Anthropic when thinking is enabled
    }

    const QString endpoint = endpointForMessages();
    platform::network::HttpRequest request;
    request.method = QStringLiteral("POST");
    request.url = endpoint;
    request.headers.insert("Content-Type", "application/json");
    request.headers.insert("x-api-key", _apiKey.toUtf8());
    request.headers.insert("anthropic-version", "2023-06-01");
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
    out.finishReason = root.value("stop_reason").toString("stop");
    const QJsonArray content = root.value("content").toArray();
    QStringList textParts;
    QStringList thinkingParts;
    for (const QJsonValue &v : content) {
        const QJsonObject block = v.toObject();
        const QString type = block.value("type").toString();
        if (type == "thinking") {
            thinkingParts.append(block.value("thinking").toString());
            out.reasoningDetails.append(block);
            continue;
        }
        if (type == "redacted_thinking") {
            out.reasoningDetails.append(block);
            continue;
        }
        if (type == "text") {
            textParts.append(block.value("text").toString());
            continue;
        }
        if (type == "tool_use") {
            agent::ToolCallRequest tc;
            tc.id = block.value("id").toString();
            tc.name = block.value("name").toString();
            tc.arguments = block.value("input").toObject();
            out.toolCalls.append(tc);
        }
    }
    out.content = textParts.join("\n");
    out.thinking = thinkingParts.join("\n");
    return out;
}

QStringList AnthropicProvider::listModels() {
    return {
        "claude-3-7-sonnet-20250219",
        "claude-3-5-sonnet-20241022",
        "claude-3-5-haiku-20241022",
        "claude-3-opus-20240229"
    };
}

agent::LLMResponse AnthropicProvider::chatStreaming(
    const QJsonArray &messages,
    const QJsonArray &tools,
    const QString &model,
    double temperature,
    int maxTokens,
    providers::LLMStreamCallback callback
) {
    agent::LLMResponse out;

    QString systemPrompt;
    const QJsonArray anthropicMessages = toAnthropicMessages(messages, &systemPrompt);

    QJsonArray anthropicTools;
    for (const QJsonValue &v : tools) {
        anthropicTools.append(toAnthropicTool(v.toObject()));
    }

    QJsonObject payload;
    payload["model"]      = stripProviderPrefix(model.isEmpty() ? _defaultModel : model);
    payload["messages"]   = anthropicMessages;
    payload["max_tokens"] = maxTokens;
    payload["temperature"] = temperature;
    payload["stream"]     = true;   // enable SSE streaming
    if (!systemPrompt.trimmed().isEmpty()) {
        payload["system"] = systemPrompt;
    }
    if (!anthropicTools.isEmpty()) {
        payload["tools"] = anthropicTools;
    }

    const QString effort = _reasoningEffort.trimmed().toLower();
    const bool thinkingEnabled = effort == "low" || effort == "medium" || effort == "high";
    if (thinkingEnabled) {
        int budgetTokens = 5000;
        if (effort == "medium") budgetTokens = 10000;
        else if (effort == "high") budgetTokens = 16000;
        payload["thinking"] = QJsonObject{
            {"type", "enabled"},
            {"budget_tokens", budgetTokens}
        };
        payload["temperature"] = 1.0;
    }

    platform::network::HttpRequest request;
    request.method  = QStringLiteral("POST");
    request.url     = endpointForMessages();
    request.headers.insert("Content-Type", "application/json");
    request.headers.insert("x-api-key", _apiKey.toUtf8());
    request.headers.insert("anthropic-version", "2023-06-01");
    request.body    = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    request.timeoutMs = 300000;

    // Accumulate full content for the return value and for tool-call parsing.
    QString fullContent;
    QString fullThinking;
    QStringList toolCallIds, toolCallNames;
    QStringList toolCallArgBuffers;
    QMap<int, QJsonObject> thinkingBlocks;

    // SSE line buffer — FastNet may deliver partial lines across chunks.
    QByteArray lineBuffer;

    // Synchronisation: block until streaming completes.
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    QString streamError;

    auto onChunk = [&](const QByteArray &chunk) -> bool {
        lineBuffer.append(chunk);

        // Process all complete lines in the buffer.
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

            const QJsonObject ev = doc.object();
            const QString type  = ev.value("type").toString();

            if (type == "content_block_delta") {
                const QJsonObject delta = ev.value("delta").toObject();
                const QString deltaType = delta.value("type").toString();

                if (deltaType == "text_delta") {
                    const QString text = delta.value("text").toString();
                    fullContent += text;
                    if (callback) {
                        LLMStreamChunk c;
                        c.contentDelta = text;
                        callback(c);
                    }
                } else if (deltaType == "thinking_delta") {
                    const QString thinking = delta.value("thinking").toString();
                    fullThinking += thinking;
                    const int idx = ev.value("index").toInt();
                    QJsonObject block = thinkingBlocks.value(idx);
                    if (block.isEmpty()) {
                        block["type"] = "thinking";
                    }
                    block["thinking"] = block.value("thinking").toString() + thinking;
                    thinkingBlocks.insert(idx, block);
                    if (callback) {
                        LLMStreamChunk c;
                        c.thinkingDelta = thinking;
                        callback(c);
                    }
                } else if (deltaType == "signature_delta") {
                    const int idx = ev.value("index").toInt();
                    QJsonObject block = thinkingBlocks.value(idx);
                    if (block.isEmpty()) {
                        block["type"] = "thinking";
                    }
                    block["signature"] = block.value("signature").toString() +
                                         delta.value("signature").toString();
                    thinkingBlocks.insert(idx, block);
                } else if (deltaType == "input_json_delta") {
                    // Tool call argument streaming
                    const int idx = ev.value("index").toInt();
                    while (toolCallArgBuffers.size() <= idx) toolCallArgBuffers.append(QString());
                    toolCallArgBuffers[idx] += delta.value("partial_json").toString();
                }
            } else if (type == "content_block_start") {
                const QJsonObject block = ev.value("content_block").toObject();
                const QString blockType = block.value("type").toString();
                const int idx = ev.value("index").toInt();
                if (blockType == "thinking" || blockType == "redacted_thinking") {
                    thinkingBlocks.insert(idx, block);
                }
                if (blockType == "tool_use") {
                    toolCallIds.append(block.value("id").toString());
                    toolCallNames.append(block.value("name").toString());
                }
            } else if (type == "message_delta") {
                out.finishReason = ev.value("delta").toObject().value("stop_reason").toString("stop");
            }
        }
        // Keep the incomplete tail for the next chunk.
        lineBuffer = lineBuffer.mid(start);
        return true; // continue streaming
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
        out.content     = QStringLiteral("Error: failed to start streaming request");
        out.finishReason = "error";
        return out;
    }

    // Block until the stream finishes.
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

    out.content  = fullContent.trimmed();
    out.thinking = fullThinking;
    for (auto it = thinkingBlocks.constBegin(); it != thinkingBlocks.constEnd(); ++it) {
        out.reasoningDetails.append(it.value());
    }

    // Reconstruct tool calls from streamed argument buffers.
    for (int i = 0; i < toolCallIds.size(); ++i) {
        const QString argsStr = i < toolCallArgBuffers.size() ? toolCallArgBuffers.at(i) : QString();
        QJsonParseError argErr;
        const QJsonDocument argDoc = QJsonDocument::fromJson(argsStr.toUtf8(), &argErr);
        agent::ToolCallRequest tc;
        tc.id        = toolCallIds.at(i);
        tc.name      = i < toolCallNames.size() ? toolCallNames.at(i) : QString();
        tc.arguments = (argErr.error == QJsonParseError::NoError && argDoc.isObject())
                           ? argDoc.object()
                           : QJsonObject();
        out.toolCalls.append(tc);
    }

    if (out.finishReason.isEmpty()) {
        out.finishReason = out.hasToolCalls() ? "tool_use" : "stop";
    }

    // Signal done to the caller.
    if (callback) {
        LLMStreamChunk fin;
        fin.done = true;
        callback(fin);
    }

    return out;
}

} // namespace yaos::providers
