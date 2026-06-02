#ifndef YAOS_RUNTIME_RUNTIMESERVICESUPPORT_H
#define YAOS_RUNTIME_RUNTIMESERVICESUPPORT_H

#include <QString>

#include "../config/Config.h"

namespace yaos::runtime {

struct RuntimeServiceAvailability {
    QString endpoint;
    bool preferred = false;
    bool enabled = false;
    bool localEndpoint = false;
    bool reachable = false;
    bool usedAutoSpawn = false;
    QString error;
};

struct RuntimeServiceEndpointHealth {
    QString endpoint;
    bool probeSupported = false;
    bool checked = false;
    bool reachable = false;
    QString error;
};

bool prefersRemoteRuntimeService(const config::Config &config);
bool remoteRuntimeServiceEnabled(const config::Config &config);
QString normalizedRuntimeServiceEndpoint(QString endpoint);
QString runtimeAdvertiseEndpoint(const config::Config &config);
RuntimeServiceEndpointHealth runtimeServiceEndpointHealth(QString endpoint,
                                                         int timeoutMs = 1500,
                                                         bool allowNetwork = true);
config::Config runtimeServiceConfig(const config::Config &baseConfig,
                                    const QString &listenEndpoint,
                                    const QString &advertiseEndpoint = QString());
RuntimeServiceAvailability ensureRuntimeService(const config::Config &config,
                                                bool allowAutoSpawn,
                                                int probeTimeoutMs = 0);

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_RUNTIMESERVICESUPPORT_H
