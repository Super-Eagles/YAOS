#ifndef YAOS_UI_STUDIOBACKEND_P_H
#define YAOS_UI_STUDIOBACKEND_P_H

#include "StudioBackend.h"
#include "StudioBackendTypes.h"
#include "../config/ConfigLoader.h"
#include "../distributed/Contracts.h"
#include "../runtime/ExtensionCatalog.h"
#include "../runtime/RuntimeFacade.h"

#include <memory>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace yaos::ui {

// DTO functions
QVariantList stringListToVariant(const QStringList &values);
QVariantMap statusToVariant(const runtime::StatusSnapshot &snapshot);
QVariantMap taskToVariant(const runtime::TaskRecord &task);
QVariantMap nodeToVariant(const distributed::NodeDescriptor &node);
QVariantMap eventToVariant(const runtime::EventRecord &event);
QVariantMap approvalToVariant(const runtime::ApprovalRecord &record);
QVariantMap notificationToVariant(const runtime::NotificationRecord &record);
QVariantMap resourceToVariant(const runtime::ResourceRecord &record);
QVariantMap automationToVariant(const runtime::AutomationRecord &record);
QVariantMap automationRunToVariant(const runtime::AutomationRunRecord &record);
QVariantMap pluginToVariant(const runtime::PluginRecord &record);
QVariantMap skillToVariant(const runtime::SkillRecord &record);
QVariantMap extensionCatalogToVariant(const runtime::ExtensionCatalogEntry &record);
QVariantMap summaryToVariant(const runtime::ResourceSummary &summary);
runtime::AutomationRecord automationFromVariant(const QVariantMap &recordMap);
StudioChatTurnResult chatTurnToStudioResult(const runtime::ChatTurnResult &turn);

template <typename Record, typename Convert>
QVariantList recordsToVariant(const QVector<Record> &records, Convert convert) {
    QVariantList out;
    out.reserve(records.size());
    for (const Record &record : records) {
        out.append(convert(record));
    }
    return out;
}

// Provider Helpers — delegate to Config::providerById (single authoritative implementation)
inline config::ProviderConfig *providerConfigById(config::Config &config, const QString &providerId) {
    return config.providerById(providerId);
}
inline const config::ProviderConfig *providerConfigById(const config::Config &config, const QString &providerId) {
    return config.providerById(providerId);
}
QString localModelForProvider(const QString &providerId, const QString &model);
QString routedModelForProvider(const QString &providerId, const QString &model);
QString preferredModelForProvider(const config::Config &config, const QString &providerId);
QStringList fallbackModelCatalogForProvider(const QString &providerId);
QString defaultApiBaseForProvider(const QString &providerId);
QString resolvedApiBaseForProvider(const QString &providerId, const QString &apiBase);
QVariantMap providerModelError(const QString &providerId, const QString &title, const QString &body, const QString &tone = QStringLiteral("warning"));
void appendProviderWarning(QVariantList *warnings, const QString &title, const QString &body, const QString &tone = QStringLiteral("warning"));

// OAuth Helpers
void preserveOAuthRuntimeFields(config::ProviderConfig *target, const config::ProviderConfig &liveProvider);
void preserveOAuthDefaults(config::ProviderConfig *target, const config::ProviderConfig &liveProvider);
void preserveLiveOAuthProviderState(config::Config *targetConfig, const config::Config &liveConfig, const QString &providerId);
void preserveLiveOAuthState(config::Config *targetConfig, const config::Config &liveConfig);
void copyResolvedOAuthRuntimeState(config::ProviderConfig *target, const config::ProviderConfig &resolvedProvider);

// Control Helpers
QString controlPlaneEndpoint(const config::Config &cfg);

// Generic Error Helpers
QVariantMap operationError(const QString &title, const QString &body, const QString &error = QString(), const QString &tone = QStringLiteral("warning"));

} // namespace yaos::ui

#endif // YAOS_UI_STUDIOBACKEND_P_H
