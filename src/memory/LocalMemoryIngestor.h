#ifndef YAOS_MEMORY_LOCALMEMORYINGESTOR_H
#define YAOS_MEMORY_LOCALMEMORYINGESTOR_H

#include <QString>

#include "MemoryBackend.h"

namespace yaos::memory {

class LocalMemoryIngestor : public IMemoryIngestor {
public:
    LocalMemoryIngestor(const QString &workspace,
                        const IFactStore *factStore,
                        IMemoryExporter *exporter,
                        bool enableDailySummaries,
                        bool exportMarkdown);

    bool ingestTurn(const QString &workspaceId,
                    const QString &sessionKey,
                    const QList<ConversationMessage> &messages) override;
    bool buildDailySummary(const QString &workspaceId, const QDate &date) override;

private:
    QString _workspace;
    const IFactStore *_factStore = nullptr;
    IMemoryExporter *_exporter = nullptr;
    bool _enableDailySummaries = true;
    bool _exportMarkdown = true;
};

} // namespace yaos::memory

#endif // YAOS_MEMORY_LOCALMEMORYINGESTOR_H
