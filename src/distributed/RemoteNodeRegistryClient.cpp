#include "RemoteNodeRegistryClient.h"

#include <utility>

#include "ContractsJson.h"

namespace yaos::distributed {

RemoteNodeRegistryClient::RemoteNodeRegistryClient(QString endpoint,
                                                   int timeoutMs)
    : _client(std::move(endpoint), timeoutMs) {}

QList<NodeDescriptor> RemoteNodeRegistryClient::listNodes() const {
    QString error;
    const QJsonObject response = _client.post(QStringLiteral("/v1/control/nodes/list"),
                                              QJsonObject{{"onlineOnly", false}, {"limit", 256}},
                                              &error);
    if (!error.isEmpty() || !response.value(QStringLiteral("ok")).toBool(false)) {
        return {};
    }
    return json::nodeDescriptorsFromJson(response.value(QStringLiteral("nodes")));
}

bool RemoteNodeRegistryClient::publishPresence(const NodeDescriptor &node) {
    QString error;
    const QJsonObject response = _client.post(QStringLiteral("/v1/control/nodes/publish"),
                                              QJsonObject{{"node", json::toJson(node)}},
                                              &error);
    return error.isEmpty() && response.value(QStringLiteral("ok")).toBool(false);
}

bool RemoteNodeRegistryClient::isReady() const {
    return _client.isReady();
}

bool RemoteNodeRegistryClient::ping(QString *error) const {
    return _client.ping(error);
}

QString RemoteNodeRegistryClient::endpoint() const {
    return _client.endpoint();
}

} // namespace yaos::distributed
