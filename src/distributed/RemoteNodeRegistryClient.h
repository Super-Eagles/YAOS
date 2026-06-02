#ifndef YAOS_DISTRIBUTED_REMOTENODEREGISTRYCLIENT_H
#define YAOS_DISTRIBUTED_REMOTENODEREGISTRYCLIENT_H

#include "Contracts.h"
#include "RemoteControlClient.h"

namespace yaos::distributed {

class RemoteNodeRegistryClient : public INodeRegistryClient {
public:
    explicit RemoteNodeRegistryClient(QString endpoint,
                                      int timeoutMs = 3500);

    QList<NodeDescriptor> listNodes() const override;
    bool publishPresence(const NodeDescriptor &node) override;

    bool isReady() const;
    bool ping(QString *error = nullptr) const;
    QString endpoint() const;

private:
    RemoteControlClient _client;
};

} // namespace yaos::distributed

#endif // YAOS_DISTRIBUTED_REMOTENODEREGISTRYCLIENT_H
