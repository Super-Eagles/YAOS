#ifndef YAOS_MEMORY_LOCALMEMORYRETRIEVER_H
#define YAOS_MEMORY_LOCALMEMORYRETRIEVER_H

#include "MemoryBackend.h"

namespace yaos::memory {

class LocalMemoryRetriever : public IMemoryRetriever {
public:
    LocalMemoryRetriever(const IConversationStore *conversationStore,
                         const IFactStore *factStore);

    QList<MemoryRecallItem> recall(const MemoryQuery &query) const override;

private:
    const IConversationStore *_conversationStore = nullptr;
    const IFactStore *_factStore = nullptr;
};

} // namespace yaos::memory

#endif // YAOS_MEMORY_LOCALMEMORYRETRIEVER_H
