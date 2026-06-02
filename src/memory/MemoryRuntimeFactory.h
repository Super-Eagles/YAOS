#ifndef YAOS_MEMORY_MEMORYRUNTIMEFACTORY_H
#define YAOS_MEMORY_MEMORYRUNTIMEFACTORY_H

#include <memory>

#include "../config/Config.h"
#include "MemoryBackend.h"

namespace yaos::memory {

class MemoryRuntimeFactory {
public:
    static std::unique_ptr<IConversationStore> createConversationStore(const QString &workspace,
                                                                       const config::Config &config);
    static std::unique_ptr<IFactStore> createFactStore(const QString &workspace,
                                                       const config::Config &config);
    static std::unique_ptr<IMemoryRetriever> createRetriever(const config::Config &config,
                                                             const IConversationStore *conversationStore,
                                                             const IFactStore *factStore);
    static std::unique_ptr<IMemoryExporter> createExporter(const QString &workspace,
                                                           const config::Config &config,
                                                           const IFactStore *factStore);
    static std::unique_ptr<IMemoryIngestor> createIngestor(const QString &workspace,
                                                           const config::Config &config,
                                                           const IFactStore *factStore,
                                                           IMemoryExporter *exporter);
};

} // namespace yaos::memory

#endif // YAOS_MEMORY_MEMORYRUNTIMEFACTORY_H
