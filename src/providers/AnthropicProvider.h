#ifndef YAOS_PROVIDERS_ANTHROPICPROVIDER_H
#define YAOS_PROVIDERS_ANTHROPICPROVIDER_H

#include <QString>
#include <QStringList>

#include "LLMProvider.h"

namespace yaos::providers {

class AnthropicProvider : public LLMProvider {
public:
    AnthropicProvider(
        const QString &apiKey,
        const QString &apiBase,
        const QString &defaultModel,
        const QString &reasoningEffort = QString()
    );

    agent::LLMResponse chat(
        const QJsonArray &messages,
        const QJsonArray &tools,
        const QString &model,
        double temperature,
        int maxTokens
    ) override;

    agent::LLMResponse chatStreaming(
        const QJsonArray &messages,
        const QJsonArray &tools,
        const QString &model,
        double temperature,
        int maxTokens,
        providers::LLMStreamCallback callback
    ) override;

    QString defaultModel() const override;
    QString backendName() const override;
    QStringList listModels() override;

private:
    QString endpointForMessages() const;
    static QString stripProviderPrefix(const QString &model);
    static QJsonObject toAnthropicTool(const QJsonObject &openaiTool);
    static QJsonArray toAnthropicMessages(const QJsonArray &messages, QString *systemPrompt);

private:
    QString _apiKey;
    QString _apiBase;
    QString _defaultModel;
    QString _reasoningEffort;
};

} // namespace yaos::providers

#endif // YAOS_PROVIDERS_ANTHROPICPROVIDER_H
