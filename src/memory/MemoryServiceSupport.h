#ifndef YAOS_MEMORY_MEMORYSERVICESUPPORT_H
#define YAOS_MEMORY_MEMORYSERVICESUPPORT_H

#include <memory>

#include "../config/Config.h"

namespace yaos::memory {

class RemoteMemoryClient;

struct MemoryServiceAvailability {
    QString endpoint;
    bool preferred = false;
    bool enabled = false;
    bool localEndpoint = false;
    bool reachable = false;
    bool usedAutoSpawn = false;
    QString error;
    std::shared_ptr<RemoteMemoryClient> client;
};

bool prefersRemoteMemoryService(const config::Config &config);
bool remoteMemoryServiceEnabled(const config::Config &config);
MemoryServiceAvailability ensureMemoryService(const config::Config &config,
                                              bool allowAutoSpawn,
                                              int probeTimeoutMs = 0);

} // namespace yaos::memory

#endif // YAOS_MEMORY_MEMORYSERVICESUPPORT_H
