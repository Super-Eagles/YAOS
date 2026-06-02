#ifndef YAOS_DISTRIBUTED_LOCALNODEREGISTRY_H
#define YAOS_DISTRIBUTED_LOCALNODEREGISTRY_H

#include <QMutex>
#include <QString>

#include "Contracts.h"

namespace yaos::distributed {

class LocalNodeRegistry : public INodeRegistryClient {
public:
    explicit LocalNodeRegistry(const QString &workspace, int staleAfterSeconds = 300);

    QList<NodeDescriptor> listNodes() const override;
    bool publishPresence(const NodeDescriptor &node) override;

private:
    QString filePath() const;

private:
    QString _workspace;
    int _staleAfterSeconds = 300;
    mutable QMutex _mutex;
};

} // namespace yaos::distributed

#endif // YAOS_DISTRIBUTED_LOCALNODEREGISTRY_H
