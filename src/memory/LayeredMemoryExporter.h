#ifndef YAOS_MEMORY_LAYEREDMEMORYEXPORTER_H
#define YAOS_MEMORY_LAYEREDMEMORYEXPORTER_H

#include <QString>

#include "MemoryBackend.h"

namespace yaos::memory {

class LayeredMemoryExporter : public IMemoryExporter {
public:
    LayeredMemoryExporter(const QString &workspace,
                          const IFactStore *factStore);

    bool exportLegacyFiles(const QString &workspacePath) override;

private:
    QString _workspace;
    const IFactStore *_factStore = nullptr;
};

} // namespace yaos::memory

#endif // YAOS_MEMORY_LAYEREDMEMORYEXPORTER_H
