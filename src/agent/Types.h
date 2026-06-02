#ifndef YAOS_AGENT_TYPES_H
#define YAOS_AGENT_TYPES_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace yaos::agent {

struct ToolCallRequest {
    QString id;
    QString name;
    QJsonObject arguments;
};

struct LLMResponse {
    QString content;
    QString thinking;   // extended thinking / reasoning content from the model
    QJsonArray reasoningDetails; // raw provider reasoning blocks needed by some tool-call continuations
    QVector<ToolCallRequest> toolCalls;
    QString finishReason = "stop";

    bool hasToolCalls() const { return !toolCalls.isEmpty(); }
};

} // namespace yaos::agent

#endif // YAOS_AGENT_TYPES_H
