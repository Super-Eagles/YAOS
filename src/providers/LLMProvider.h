#ifndef YAOS_PROVIDERS_LLMPROVIDER_H
#define YAOS_PROVIDERS_LLMPROVIDER_H

#include <functional>
#include <QStringList>
#include <QJsonArray>
#include <QString>

#include "../agent/Types.h"

namespace yaos::providers {

// Streaming chunk delivered to the caller during a streaming chat call.
// Both fields are incremental deltas (not cumulative).
struct LLMStreamChunk {
    QString contentDelta;
    QString thinkingDelta;
    bool done = false;       // true on the final (empty) chunk
};

using LLMStreamCallback = std::function<void(const LLMStreamChunk &chunk)>;

class LLMProvider {
public:
    virtual ~LLMProvider() = default;

    // Blocking, returns full response.
    virtual agent::LLMResponse chat(
        const QJsonArray &messages,
        const QJsonArray &tools,
        const QString &model,
        double temperature,
        int maxTokens
    ) = 0;

    // Streaming variant. Calls `callback` incrementally on the calling thread
    // (FastNet IO thread), then returns the assembled full response.
    // Default implementation falls back to the blocking `chat()`.
    virtual agent::LLMResponse chatStreaming(
        const QJsonArray &messages,
        const QJsonArray &tools,
        const QString &model,
        double temperature,
        int maxTokens,
        LLMStreamCallback callback
    ) {
        agent::LLMResponse response = chat(messages, tools, model, temperature, maxTokens);
        if (callback) {
            // Deliver the full content as a single chunk, then signal done.
            LLMStreamChunk chunk;
            chunk.contentDelta = response.content;
            chunk.thinkingDelta = response.thinking;
            chunk.done = false;
            callback(chunk);
            LLMStreamChunk done;
            done.done = true;
            callback(done);
        }
        return response;
    }

    virtual QString defaultModel() const = 0;
    virtual QString backendName() const = 0;
    virtual bool isFallback() const { return false; }
    virtual QStringList listModels() { return {}; }
};

} // namespace yaos::providers

#endif // YAOS_PROVIDERS_LLMPROVIDER_H
