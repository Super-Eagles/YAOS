#include "RuntimeClientFacade.h"

#include <QJsonArray>
#include <QLoggingCategory>

#include "../distributed/ContractsJson.h"
#include "RuntimeSerialization.h"

Q_LOGGING_CATEGORY(lcRuntimeClientFacade, "yaos.runtime.client")

namespace yaos::runtime {

namespace {

QString responseError(const QJsonObject &response) {
    return response.value("error").toString();
}

bool responseOk(const QJsonObject &response) {
    return response.value("ok").toBool(false);
}

bool boolValue(const QJsonObject &response, const char *key, bool fallback = false) {
    const QJsonValue value = response.value(QLatin1String(key));
    return value.isBool() ? value.toBool() : fallback;
}

QString stringValue(const QJsonObject &response, const char *key, const QString &fallback = QString()) {
    const QJsonValue value = response.value(QLatin1String(key));
    return value.isString() ? value.toString() : fallback;
}

} // namespace

RuntimeClientFacade::RuntimeClientFacade(std::unique_ptr<distributed::IRuntimeClient> client)
    : _client(std::move(client)) {}

StatusSnapshot RuntimeClientFacade::statusSnapshot() {
    const QJsonObject response = invoke("statusSnapshot");
    return responseOk(response)
        ? serialization::statusSnapshotFromJson(response.value("status").toObject())
        : StatusSnapshot();
}

bool RuntimeClientFacade::initializeWorkspace(QString *message) {
    const QJsonObject response = invoke("initializeWorkspace");
    if (message) {
        *message = stringValue(response, "message");
    }
    return responseOk(response) && boolValue(response, "value");
}

bool RuntimeClientFacade::reloadFromDisk(const QString &modelOverride, const QString &providerOverride) {
    const QJsonObject response = invoke("reloadFromDisk", QJsonObject{
        {"modelOverride", modelOverride},
        {"providerOverride", providerOverride}
    });
    return responseOk(response) && boolValue(response, "value");
}

bool RuntimeClientFacade::startGatewayServices() {
    const QJsonObject response = invoke("startGatewayServices");
    return responseOk(response) && boolValue(response, "value");
}

void RuntimeClientFacade::stopGatewayServices() {
    invoke("stopGatewayServices");
}

bool RuntimeClientFacade::gatewayRunning() const {
    const QJsonObject response = invoke("gatewayRunning");
    return responseOk(response) && boolValue(response, "value");
}

QVector<ApprovalRecord> RuntimeClientFacade::recentApprovals(int limit, const QString &state) {
    const QJsonObject response = invoke("recentApprovals", QJsonObject{
        {"limit", limit},
        {"state", state}
    });
    return responseOk(response)
        ? serialization::approvalRecordsFromJson(response.value("items"))
        : QVector<ApprovalRecord>();
}

bool RuntimeClientFacade::resolveApproval(const QString &approvalId,
                                          const QString &decision,
                                          const QString &scope,
                                          const QString &note) {
    const QJsonObject response = invoke("resolveApproval", QJsonObject{
        {"approvalId", approvalId},
        {"decision", decision},
        {"scope", scope},
        {"note", note}
    });
    return responseOk(response) && boolValue(response, "value");
}

QVector<NotificationRecord> RuntimeClientFacade::recentNotifications(int limit, bool unreadOnly) {
    const QJsonObject response = invoke("recentNotifications", QJsonObject{
        {"limit", limit},
        {"unreadOnly", unreadOnly}
    });
    return responseOk(response)
        ? serialization::notificationRecordsFromJson(response.value("items"))
        : QVector<NotificationRecord>();
}

void RuntimeClientFacade::markAllNotificationsRead() {
    invoke("markAllNotificationsRead");
}

QVector<TaskRecord> RuntimeClientFacade::recentTasks(int limit) {
    const QJsonObject response = invoke("recentTasks", QJsonObject{{"limit", limit}});
    return responseOk(response)
        ? serialization::taskRecordsFromJson(response.value("items"))
        : QVector<TaskRecord>();
}

QVector<EventRecord> RuntimeClientFacade::recentEvents(int limit) {
    const QJsonObject response = invoke("recentEvents", QJsonObject{{"limit", limit}});
    return responseOk(response)
        ? serialization::eventRecordsFromJson(response.value("items"))
        : QVector<EventRecord>();
}

QVector<distributed::NodeDescriptor> RuntimeClientFacade::recentNodes(int limit, bool onlineOnly) {
    const QJsonObject response = invoke("recentNodes", QJsonObject{
        {"limit", limit},
        {"onlineOnly", onlineOnly}
    });
    if (!responseOk(response)) {
        return {};
    }
    const QList<distributed::NodeDescriptor> nodes =
        distributed::json::nodeDescriptorsFromJson(response.value("items"));
    return QVector<distributed::NodeDescriptor>(nodes.begin(), nodes.end());
}

QJsonObject RuntimeClientFacade::previewDelegationRoute(const QJsonObject &request) {
    const QJsonObject response = invoke("previewDelegationRoute", request);
    if (!responseOk(response)) {
        return QJsonObject{
            {"ok", false},
            {"error", responseError(response)},
            {"message", responseError(response)},
            {"resolved", false},
            {"nodes", QJsonArray()}
        };
    }
    return response.value("preview").toObject();
}

QJsonObject RuntimeClientFacade::submitDelegationRequest(const QJsonObject &request) {
    const QJsonObject response = invoke("submitDelegationRequest", request);
    if (!responseOk(response)) {
        return QJsonObject{
            {"ok", false},
            {"error", responseError(response)},
            {"message", responseError(response)}
        };
    }
    return response.value("result").toObject();
}

ResourceSummary RuntimeClientFacade::resourceSummary() {
    const QJsonObject response = invoke("resourceSummary");
    return responseOk(response)
        ? serialization::resourceSummaryFromJson(response.value("summary").toObject())
        : ResourceSummary();
}

QVector<ResourceRecord> RuntimeClientFacade::recentResources(int limit, const QString &kind) {
    const QJsonObject response = invoke("recentResources", QJsonObject{
        {"limit", limit},
        {"kind", kind}
    });
    return responseOk(response)
        ? serialization::resourceRecordsFromJson(response.value("items"))
        : QVector<ResourceRecord>();
}

QVector<AutomationRecord> RuntimeClientFacade::automations(int limit) {
    const QJsonObject response = invoke("automations", QJsonObject{{"limit", limit}});
    return responseOk(response)
        ? serialization::automationRecordsFromJson(response.value("items"))
        : QVector<AutomationRecord>();
}

QVector<AutomationRunRecord> RuntimeClientFacade::automationRuns(int limit, const QString &automationId) {
    const QJsonObject response = invoke("automationRuns", QJsonObject{
        {"limit", limit},
        {"automationId", automationId}
    });
    return responseOk(response)
        ? serialization::automationRunRecordsFromJson(response.value("items"))
        : QVector<AutomationRunRecord>();
}

AutomationRecord RuntimeClientFacade::automation(const QString &id) {
    const QJsonObject response = invoke("automation", QJsonObject{{"id", id}});
    return responseOk(response)
        ? serialization::automationRecordFromJson(response.value("record").toObject())
        : AutomationRecord();
}

QString RuntimeClientFacade::saveAutomation(const AutomationRecord &record, QString *error) {
    const QJsonObject response = invoke("saveAutomation", QJsonObject{
        {"record", serialization::toJson(record)}
    });
    if (error) {
        *error = stringValue(response, "error");
    }
    return responseOk(response) ? stringValue(response, "value") : QString();
}

bool RuntimeClientFacade::removeAutomation(const QString &id) {
    const QJsonObject response = invoke("removeAutomation", QJsonObject{{"id", id}});
    return responseOk(response) && boolValue(response, "value");
}

QString RuntimeClientFacade::runAutomation(const QString &id,
                                           QString *error,
                                           const QString &sessionKey) {
    const QJsonObject response = invoke("runAutomation", QJsonObject{
        {"id", id},
        {"sessionKey", sessionKey}
    });
    if (error) {
        *error = stringValue(response, "error");
    }
    return responseOk(response) ? stringValue(response, "value") : QString();
}

QVector<PluginRecord> RuntimeClientFacade::plugins() {
    const QJsonObject response = invoke("plugins");
    return responseOk(response)
        ? serialization::pluginRecordsFromJson(response.value("items"))
        : QVector<PluginRecord>();
}

QVector<SkillRecord> RuntimeClientFacade::skills() {
    const QJsonObject response = invoke("skills");
    return responseOk(response)
        ? serialization::skillRecordsFromJson(response.value("items"))
        : QVector<SkillRecord>();
}

ChatTurnResult RuntimeClientFacade::processMessageDetailed(const QString &content,
                                                           const QString &sessionKey,
                                                           const QString &channel,
                                                           const QString &chatId,
                                                           const QString &modelOverride,
                                                           const QString &providerOverride) {
    const QJsonObject response = invoke("processMessageDetailed", QJsonObject{
        {"content", content},
        {"sessionKey", sessionKey},
        {"channel", channel},
        {"chatId", chatId},
        {"modelOverride", modelOverride},
        {"providerOverride", providerOverride}
    });
    if (!responseOk(response)) {
        ChatTurnResult result;
        result.content = responseError(response);
        result.error = true;
        return result;
    }
    return serialization::chatTurnResultFromJson(response.value("turn").toObject());
}

QString RuntimeClientFacade::processMessage(const QString &content,
                                            const QString &sessionKey,
                                            const QString &channel,
                                            const QString &chatId,
                                            const QString &modelOverride,
                                            const QString &providerOverride) {
    const QJsonObject response = invoke("processMessage", QJsonObject{
        {"content", content},
        {"sessionKey", sessionKey},
        {"channel", channel},
        {"chatId", chatId},
        {"modelOverride", modelOverride},
        {"providerOverride", providerOverride}
    });
    return responseOk(response) ? stringValue(response, "value") : responseError(response);
}

QJsonObject RuntimeClientFacade::invoke(const QString &method, const QJsonObject &payload) const {
    if (!_client) {
        return QJsonObject{
            {"ok", false},
            {"error", QStringLiteral("Runtime client is not initialized.")}
        };
    }

    const QJsonObject response = _client->invoke(method, payload);
    if (!responseOk(response)) {
        qWarning(lcRuntimeClientFacade) << "Runtime client invoke failed:" << method << responseError(response);
    }
    return response;
}

} // namespace yaos::runtime
