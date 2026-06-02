#ifndef YAOS_PROVIDERS_OPENAICOMPATIBLEPROVIDER_H
#define YAOS_PROVIDERS_OPENAICOMPATIBLEPROVIDER_H

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "LLMProvider.h"
#include "platform/network/FastNetHttpTransport.h"

namespace yaos::providers {

struct AudioTranscriptionResult {
    bool ok = false;
    QString text;
    QString error;
    QString model;
    QString language;
    QString endpoint;
    QJsonObject raw;
};

class OpenAICompatibleProvider : public LLMProvider {
public:
    OpenAICompatibleProvider(
        const QString &apiKey,
        const QString &apiBase,
        const QString &defaultModel,
        const QString &providerName = QString(),
        const QString &reasoningEffort = QString(),
        const QHash<QString, QString> &extraHeaders = QHash<QString, QString>()
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
        LLMStreamCallback callback
    ) override;

    QString defaultModel() const override;
    QString backendName() const override;
    QStringList listModels() override;
    AudioTranscriptionResult transcribeAudioFile(
        const QString &filePath,
        const QString &model = QString(),
        const QString &language = QString(),
        const QString &prompt = QString()
    ) const;

private:
    QString normalizedApiBase() const;
    QString endpointForChat() const;
    QString endpointForTranscription() const;
    QJsonArray normalizeMessages(const QJsonArray &messages) const;
    platform::network::HttpRequest buildRequest(const QString &endpoint) const;

private:
    QString _apiKey;
    QString _apiBase;
    QString _defaultModel;
    QString _providerName;
    QString _reasoningEffort;
    QHash<QString, QString> _extraHeaders;
};

} // namespace yaos::providers

#endif // YAOS_PROVIDERS_OPENAICOMPATIBLEPROVIDER_H
