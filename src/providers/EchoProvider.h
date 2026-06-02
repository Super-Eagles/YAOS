#ifndef YAOS_PROVIDERS_ECHOPROVIDER_H
#define YAOS_PROVIDERS_ECHOPROVIDER_H

#include "LLMProvider.h"

namespace yaos::providers {

class EchoProvider : public LLMProvider {
public:
    explicit EchoProvider(const QString &model = "echo/default");

    agent::LLMResponse chat(
        const QJsonArray &messages,
        const QJsonArray &tools,
        const QString &model,
        double temperature,
        int maxTokens
    ) override;

    QString defaultModel() const override;
    QString backendName() const override;
    bool isFallback() const override;

private:
    QString _defaultModel;
};

} // namespace yaos::providers

#endif // YAOS_PROVIDERS_ECHOPROVIDER_H
