#include "EchoProvider.h"

#include <QJsonObject>

namespace yaos::providers {

EchoProvider::EchoProvider(const QString &model)
    : _defaultModel(model) {}

agent::LLMResponse EchoProvider::chat(
    const QJsonArray &messages,
    const QJsonArray &tools,
    const QString &model,
    double temperature,
    int maxTokens
) {
    Q_UNUSED(tools);
    Q_UNUSED(model);
    Q_UNUSED(temperature);
    Q_UNUSED(maxTokens);

    QString userText;
    for (int i = messages.size() - 1; i >= 0; --i) {
        const QJsonObject msg = messages.at(i).toObject();
        if (msg.value("role").toString() == "user") {
            if (msg.value("content").isString()) {
                userText = msg.value("content").toString();
            } else {
                userText = "[non-text content]";
            }
            break;
        }
    }

    agent::LLMResponse res;
    res.content = "YAOS(Echo): " + userText;
    return res;
}

QString EchoProvider::defaultModel() const {
    return _defaultModel;
}

QString EchoProvider::backendName() const {
    return "echo";
}

bool EchoProvider::isFallback() const {
    return true;
}

} // namespace yaos::providers
