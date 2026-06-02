#include "StudioBackend_p.h"

#include "../config/DelegationTemplateExchange.h"
#include "../distributed/RemoteControlClient.h"
#include <QJsonDocument>
#include <QJsonArray>

namespace yaos::ui {

QString controlPlaneEndpoint(const config::Config &cfg) {
    const QString control = cfg.deployment.controlPlaneUrl.trimmed();
    if (!control.isEmpty()) {
        return control;
    }
    return cfg.deployment.registryUrl.trimmed();
}

QVariantMap RuntimeFacadeStudioBackend::pushDelegationTemplatesToControl(const config::Config &cfg,
                                                                         const QVariantList &records,
                                                                         bool replaceExisting) {
    const QString endpoint = controlPlaneEndpoint(cfg);
    if (endpoint.trimmed().isEmpty()) {
        const QString message = QStringLiteral("Control plane endpoint is not configured.");
        return operationError(QStringLiteral("同步失败"), message);
    }

    QList<config::DelegationTemplateConfig> normalized;
    QString error;
    const QJsonDocument document(QJsonArray::fromVariantList(records));
    if (!config::parseDelegationTemplateExchangeDocument(document, &normalized, &error)) {
        return operationError(QStringLiteral("同步失败"),
                              error.isEmpty()
                                  ? QStringLiteral("没有可推送的 delegation templates.")
                                  : error,
                              error);
    }

    distributed::RemoteControlClient client(endpoint, 5000);
    if (!client.ping(&error)) {
        return operationError(QStringLiteral("同步失败"),
                              error.isEmpty()
                                  ? QStringLiteral("当前 control plane 不可达.")
                                  : error,
                              error);
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("replace"), replaceExisting);
    payload.insert(QStringLiteral("envelope"),
                   config::delegationTemplateExchangeEnvelope(normalized,
                                                             config::ConfigLoader::defaultConfigPath(),
                                                             cfg.deployment.nodeId,
                                                             cfg.deployment.clusterId));

    QJsonObject response = client.post(QStringLiteral("/v1/control/delegation-templates/sync"),
                                       payload,
                                       &error);
    if (response.isEmpty() || !response.value(QStringLiteral("ok")).toBool(false)) {
        const QString message = !error.trimmed().isEmpty()
            ? error
            : response.value(QStringLiteral("error")).toString();
        return operationError(QStringLiteral("同步失败"),
                              message.isEmpty()
                                  ? QStringLiteral("无法推送 delegation templates.")
                                  : message,
                              message);
    }

    QVariantMap result = response.toVariantMap();
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("replace"), replaceExisting);
    result.insert(QStringLiteral("pushedCount"), normalized.size());
    result.insert(QStringLiteral("endpoint"), client.endpoint());
    result.insert(QStringLiteral("title"),
                  replaceExisting ? QStringLiteral("模板已替换推送")
                                  : QStringLiteral("模板已推送"));
    result.insert(QStringLiteral("body"),
                  QStringLiteral("已推送 %1 条 delegation templates.").arg(normalized.size()));
    result.insert(QStringLiteral("tone"), QStringLiteral("success"));
    return result;
}

QVariantMap RuntimeFacadeStudioBackend::pullDelegationTemplatesFromControl(const config::Config &cfg,
                                                                           bool replaceExisting) {
    const QString endpoint = controlPlaneEndpoint(cfg);
    if (endpoint.trimmed().isEmpty()) {
        const QString message = QStringLiteral("Control plane endpoint is not configured.");
        return operationError(QStringLiteral("拉取失败"), message);
    }

    distributed::RemoteControlClient client(endpoint, 5000);
    QString error;
    if (!client.ping(&error)) {
        return operationError(QStringLiteral("拉取失败"),
                              error.isEmpty()
                                  ? QStringLiteral("当前 control plane 不可达.")
                                  : error,
                              error);
    }

    QJsonObject response = client.post(QStringLiteral("/v1/control/delegation-templates/list"),
                                       QJsonObject{{QStringLiteral("limit"), 1024}},
                                       &error);
    if (response.isEmpty() || !response.value(QStringLiteral("ok")).toBool(false)) {
        const QString message = !error.trimmed().isEmpty()
            ? error
            : response.value(QStringLiteral("error")).toString();
        return operationError(QStringLiteral("拉取失败"),
                              message.isEmpty()
                                  ? QStringLiteral("无法从 control plane 拉取 delegation templates.")
                                  : message,
                              message);
    }

    const QJsonObject envelope = response.value(QStringLiteral("envelope")).toObject();
    QList<config::DelegationTemplateConfig> imported;
    if (!config::parseDelegationTemplateExchangeDocument(QJsonDocument(envelope.isEmpty() ? response : envelope),
                                                         &imported,
                                                         &error)) {
        return operationError(QStringLiteral("拉取失败"),
                              error.isEmpty()
                                  ? QStringLiteral("control plane 返回的模板内容无效.")
                                  : error,
                              error);
    }

    config::Config nextConfig = cfg;
    nextConfig.memory.delegationTemplates =
        config::mergeDelegationTemplateRecords(nextConfig.memory.delegationTemplates,
                                               imported,
                                               replaceExisting);
    if (!config::ConfigLoader::save(nextConfig)) {
        const QString message = QStringLiteral("Failed to save updated config.");
        return operationError(QStringLiteral("拉取失败"), message);
    }

    if (m_facade) {
        m_facade->reloadFromDisk();
    }

    return QVariantMap{
        {QStringLiteral("ok"), true},
        {QStringLiteral("replace"), replaceExisting},
        {QStringLiteral("pulledCount"), imported.size()},
        {QStringLiteral("totalTemplates"), nextConfig.memory.delegationTemplates.size()},
        {QStringLiteral("endpoint"), client.endpoint()},
        {QStringLiteral("configChanged"), true},
        {QStringLiteral("config"), nextConfig.toJson().toVariantMap()},
        {QStringLiteral("title"), replaceExisting ? QStringLiteral("模板已替换拉取")
                                                   : QStringLiteral("模板已拉取")},
        {QStringLiteral("body"), QStringLiteral("已拉取 %1 条 delegation templates.").arg(imported.size())},
        {QStringLiteral("tone"), QStringLiteral("success")}
    };
}

QVariantMap RemoteStudioBackend::pushDelegationTemplatesToControl(const config::Config &cfg,
                                                                  const QVariantList &records,
                                                                  bool replaceExisting) {
    return invokeStudioMap(QStringLiteral("studio.pushDelegationTemplatesToControl"),
                           QJsonObject{
                               {QStringLiteral("config"), cfg.toJson()},
                               {QStringLiteral("records"), QJsonArray::fromVariantList(records)},
                               {QStringLiteral("replace"), replaceExisting}
                           });
}

QVariantMap RemoteStudioBackend::pullDelegationTemplatesFromControl(const config::Config &cfg,
                                                                    bool replaceExisting) {
    return invokeStudioMap(QStringLiteral("studio.pullDelegationTemplatesFromControl"),
                           QJsonObject{
                               {QStringLiteral("config"), cfg.toJson()},
                               {QStringLiteral("replace"), replaceExisting}
                           });
}

} // namespace yaos::ui
