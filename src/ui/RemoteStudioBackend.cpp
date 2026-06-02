#include "StudioBackend_p.h"

#include <QJsonObject>
#include <QJsonDocument>

namespace yaos::ui {

RemoteStudioBackend::RemoteStudioBackend(std::unique_ptr<runtime::IRuntimeFacade> facade,
                                         std::unique_ptr<distributed::IRuntimeClient> client,
                                         const QString &transportMode)
    : RuntimeFacadeStudioBackend(std::move(facade)),
      m_client(std::move(client)),
      m_transportMode(transportMode.trimmed().isEmpty()
                          ? QStringLiteral("remote")
                          : transportMode.trimmed().toLower()) {}

QVariantMap RemoteStudioBackend::status() {
    QVariantMap map = RuntimeFacadeStudioBackend::status();
    map.insert(QStringLiteral("studioBackend"), QStringLiteral("remote"));
    map.insert(QStringLiteral("studioBackendTransport"), m_transportMode);
    return map;
}

StudioConfigSaveResult RemoteStudioBackend::saveConfiguration(const config::Config &draftConfig,
                                                              const config::Config &liveConfig) {
    if (draftConfig.normalizedRuntimeMode() == QStringLiteral("embedded")) {
        return RuntimeFacadeStudioBackend::saveConfiguration(draftConfig, liveConfig);
    }

    StudioConfigSaveResult result;
    result.config = draftConfig;
    result.tone = QStringLiteral("warning");

    const QVariantMap response = invokeStudioMap(QStringLiteral("studio.saveConfiguration"),
                                                 QJsonObject{
                                                     {QStringLiteral("draftConfig"), draftConfig.toJson()},
                                                     {QStringLiteral("liveConfig"), liveConfig.toJson()}
                                                 });
    result.ok = response.value(QStringLiteral("ok")).toBool();
    result.saved = response.value(QStringLiteral("saved")).toBool();
    result.reloadOk = response.value(QStringLiteral("reloadOk")).toBool();
    result.configChanged = response.value(QStringLiteral("configChanged")).toBool();
    result.title = response.value(QStringLiteral("title")).toString();
    result.body = response.value(QStringLiteral("body")).toString();
    result.tone = response.value(QStringLiteral("tone"), QStringLiteral("warning")).toString();
    result.fallbackReason = response.value(QStringLiteral("fallbackReason")).toString();

    const QVariantMap configMap = response.value(QStringLiteral("config")).toMap();
    if (!configMap.isEmpty()) {
        result.config = config::Config::fromJson(QJsonObject::fromVariantMap(configMap));
    }

    if (result.saved || result.configChanged) {
        StudioBackendSelection selection = createStudioBackend(result.config);
        if (!selection.fallbackReason.isEmpty()) {
            result.fallbackReason = selection.fallbackReason;
        }
        if (selection.backend) {
            result.backend = std::move(selection.backend);
        } else if (result.ok) {
            result.ok = false;
            result.title = QStringLiteral("运行时切换失败");
            result.body = QStringLiteral("配置已保存,但新的 runtime facade 没有成功初始化.");
            result.tone = QStringLiteral("warning");
        }
    }

    return result;
}

QVariantMap RemoteStudioBackend::invokeStudioMap(const QString &method,
                                                 const QJsonObject &payload) const {
    if (!m_client) {
        return QVariantMap{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("remote studio client is not initialized")}
        };
    }

    const QJsonObject response = m_client->invoke(method, payload);
    QVariantMap map = response.toVariantMap();
    if (!map.contains(QStringLiteral("ok"))) {
        map.insert(QStringLiteral("ok"), false);
    }
    if (!map.value(QStringLiteral("ok")).toBool() &&
        !map.contains(QStringLiteral("error"))) {
        map.insert(QStringLiteral("error"), QStringLiteral("remote studio request failed"));
    }
    return map;
}

QVariantMap RemoteStudioBackend::invokeProviderOAuth(config::Config *config,
                                                     const QString &method,
                                                     const QString &providerId,
                                                     const QString &errorTitle) const {
    if (!config) {
        return operationError(errorTitle, QStringLiteral("config is not available"));
    }

    QVariantMap result = invokeStudioMap(method,
                                         QJsonObject{{QStringLiteral("providerId"), providerId}});
    if (result.value(QStringLiteral("configChanged")).toBool()) {
        const QVariantMap configMap = result.value(QStringLiteral("config")).toMap();
        if (!configMap.isEmpty()) {
            *config = config::Config::fromJson(QJsonObject::fromVariantMap(configMap));
        }
    }
    return result;
}

} // namespace yaos::ui
