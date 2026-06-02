#ifndef YAOS_PROVIDERS_PROVIDERREGISTRY_H
#define YAOS_PROVIDERS_PROVIDERREGISTRY_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace yaos::providers {

struct ProviderSpec {
    QString name;
    QStringList keywords;
    QString defaultApiBase;
    QString litellmPrefix;
    QStringList skipPrefixes;
    bool stripModelPrefix = false;
    bool isGateway = false;
    bool isLocal = false;
    bool isOAuth = false;
};

const QVector<ProviderSpec> &providerSpecs();
ProviderSpec findProviderSpec(const QString &name);
QString routeModelForProvider(const ProviderSpec &spec, const QString &model);

} // namespace yaos::providers

#endif // YAOS_PROVIDERS_PROVIDERREGISTRY_H
