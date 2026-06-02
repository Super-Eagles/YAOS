#include "RemoteTaskBus.h"

#include <utility>

#include "ContractsJson.h"

namespace yaos::distributed {

RemoteTaskBus::RemoteTaskBus(QString endpoint,
                             int timeoutMs)
    : _client(std::move(endpoint), timeoutMs) {}

bool RemoteTaskBus::submit(const TaskEnvelope &task) {
    QString error;
    const QJsonObject response = _client.post(QStringLiteral("/v1/control/tasks/submit"),
                                              QJsonObject{{"task", json::toJson(task)}},
                                              &error);
    return error.isEmpty() && response.value(QStringLiteral("ok")).toBool(false);
}

bool RemoteTaskBus::publishResult(const TaskResultEnvelope &result) {
    QString error;
    const QJsonObject response = _client.post(QStringLiteral("/v1/control/tasks/result"),
                                              QJsonObject{{"result", json::toJson(result)}},
                                              &error);
    return error.isEmpty() && response.value(QStringLiteral("ok")).toBool(false);
}

bool RemoteTaskBus::cancel(const QString &taskId) {
    if (taskId.trimmed().isEmpty()) {
        return false;
    }

    QString error;
    const QJsonObject response = _client.post(QStringLiteral("/v1/control/tasks/cancel"),
                                              QJsonObject{{"taskId", taskId.trimmed()}},
                                              &error);
    return error.isEmpty() && response.value(QStringLiteral("ok")).toBool(false);
}

bool RemoteTaskBus::claim(const QString &taskId,
                          const QString &consumerNode) {
    if (taskId.trimmed().isEmpty()) {
        return false;
    }

    QString error;
    const QJsonObject response = _client.post(QStringLiteral("/v1/control/tasks/claim"),
                                              QJsonObject{
                                                  {"taskId", taskId.trimmed()},
                                                  {"consumerNode", consumerNode.trimmed()}
                                              },
                                              &error);
    return error.isEmpty() && response.value(QStringLiteral("ok")).toBool(false);
}

QList<TaskEnvelope> RemoteTaskBus::pendingTasks(const QString &targetNode,
                                                const QString &targetRole,
                                                int limit) const {
    QString error;
    const QJsonObject response = _client.post(QStringLiteral("/v1/control/tasks/pending"),
                                              QJsonObject{
                                                  {"targetNode", targetNode.trimmed()},
                                                  {"targetRole", targetRole.trimmed()},
                                                  {"limit", limit > 0 ? limit : 100}
                                              },
                                              &error);
    if (!error.isEmpty() || !response.value(QStringLiteral("ok")).toBool(false)) {
        return {};
    }
    return json::taskEnvelopesFromJson(response.value(QStringLiteral("tasks")));
}

QList<TaskResultEnvelope> RemoteTaskBus::recentResults(const QString &taskId,
                                                       const QString &traceId,
                                                       int limit) const {
    QString error;
    const QJsonObject response = _client.post(QStringLiteral("/v1/control/tasks/results"),
                                              QJsonObject{
                                                  {"taskId", taskId.trimmed()},
                                                  {"traceId", traceId.trimmed()},
                                                  {"limit", limit > 0 ? limit : 100}
                                              },
                                              &error);
    if (!error.isEmpty() || !response.value(QStringLiteral("ok")).toBool(false)) {
        return {};
    }
    return json::taskResultEnvelopesFromJson(response.value(QStringLiteral("results")));
}

bool RemoteTaskBus::isReady() const {
    return _client.isReady();
}

bool RemoteTaskBus::ping(QString *error) const {
    return _client.ping(error);
}

QString RemoteTaskBus::endpoint() const {
    return _client.endpoint();
}

} // namespace yaos::distributed
