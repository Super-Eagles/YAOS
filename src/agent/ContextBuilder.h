#ifndef YAOS_AGENT_CONTEXTBUILDER_H
#define YAOS_AGENT_CONTEXTBUILDER_H

#include <QJsonArray>
#include <QString>

#include "../config/Config.h"
#include "../memory/MemoryBackend.h"
#include "MemoryStore.h"

namespace yaos::agent {

class ContextBuilder {
public:
    // 接受外部共享的 MemoryStore 引用,避免双实例问题
    ContextBuilder(const QString &workspace, const config::Config &config, MemoryStore &memory);

    QString buildSystemPrompt(const QString &currentMessage,
                             const QList<memory::MemoryRecallItem> &recallItems = {}) const;
    QJsonArray buildMessages(
        const QJsonArray &history,
        const QString &currentMessage,
        const QString &channel,
        const QString &chatId,
        const QList<memory::MemoryRecallItem> &recallItems = {}
    ) const;

    static QJsonArray addAssistantMessage(
        const QJsonArray &messages,
        const QString &content
    );

private:
    QString identity() const;
    QString loadBootstrapFiles() const;
    QString loadSkillPrompt(const QString &currentMessage) const;
    QString formatRecallItems(const QList<memory::MemoryRecallItem> &recallItems) const;
    static QString runtimeContext(const QString &channel, const QString &chatId);

    QString _workspace;
    config::Config _config;
    MemoryStore &_memory;  // ✅ 引用,不再持有独立副本
};

} // namespace yaos::agent

#endif // YAOS_AGENT_CONTEXTBUILDER_H
