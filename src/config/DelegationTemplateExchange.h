#ifndef YAOS_CONFIG_DELEGATIONTEMPLATEEXCHANGE_H
#define YAOS_CONFIG_DELEGATIONTEMPLATEEXCHANGE_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "Config.h"

namespace yaos::config {

QString normalizedDelegationTemplateKind(QString kind);
QJsonObject runtimeRequestFromDelegationTemplate(const DelegationTemplateConfig &record);
DelegationTemplateConfig normalizeDelegationTemplateRecord(const DelegationTemplateConfig &source,
                                                          int ordinal = 0);
QJsonObject delegationTemplateToExchangeJson(const DelegationTemplateConfig &record);
QJsonArray delegationTemplateExchangeArray(const QList<DelegationTemplateConfig> &records);
QJsonObject delegationTemplateExchangeEnvelope(const QList<DelegationTemplateConfig> &records,
                                               const QString &sourceConfigPath = QString(),
                                               const QString &sourceNodeId = QString(),
                                               const QString &clusterId = QString());
bool parseDelegationTemplateExchangeDocument(const QJsonDocument &document,
                                             QList<DelegationTemplateConfig> *records,
                                             QString *error = nullptr);
QList<DelegationTemplateConfig> mergeDelegationTemplateRecords(
    const QList<DelegationTemplateConfig> &existing,
    const QList<DelegationTemplateConfig> &incoming,
    bool replaceExisting);

} // namespace yaos::config

#endif // YAOS_CONFIG_DELEGATIONTEMPLATEEXCHANGE_H
