#ifndef YAOS_AGENT_AGENTLOOP_H
#define YAOS_AGENT_AGENTLOOP_H

#include <functional>
#include <memory>
#include <QObject>

#include "../bus/MessageBus.h"
#include "../config/Config.h"
#include "../providers/LLMProvider.h"
#include "../runtime/CronService.h"
#include "../runtime/MCPManager.h"
#include "../runtime/SubagentManager.h"
#include "../memory/MemoryBackend.h"
#include "../memory/MemoryRuntimeFactory.h"
#include "../session/SessionManager.h"
#include "ContextBuilder.h"
#include "MemoryStore.h"
#include "ToolRegistry.h"

namespace yaos::agent {

struct ToolExecutionContext {
    QString sessionKey;
    QString channel;
    QString chatId;
    QString messageId;
    QString taskId;
    QString traceId;
};

struct AgentTurnResult {
    QString content;
    QString thinking;
};

struct ToolGuardResult {
    bool allowed = true;
    QString message;
    QString policy = "allow";
    QString referenceId;
};

struct ToolAuditRecord {
    QString toolName;
    QJsonObject params;
    ToolExecutionContext context;
    bool allowed = true;
    QString result;
    QString policy;
    QString referenceId;
};

class AgentLoop : public QObject {
    Q_OBJECT
public:
    using ToolGuard = std::function<ToolGuardResult(const QString &, const QJsonObject &, const ToolExecutionContext &)>;
    using ToolAudit = std::function<void(const ToolAuditRecord &)>;
    // Called on the FastNet IO thread with incremental content/thinking deltas.
    // The bridge connects this to update the pending chat entry in real time.
    using StreamProgressCallback = std::function<void(const QString &contentDelta, const QString &thinkingDelta)>;

    AgentLoop(
        bus::MessageBus &bus,
        providers::LLMProvider &provider,
        const QString &workspace,
        const config::Config &config,
        runtime::CronService *cronService = nullptr,
        runtime::SubagentManager *subagentManager = nullptr,
        runtime::MCPManager *mcpManager = nullptr,
        QObject *parent = nullptr
    );

    void setToolGuard(const ToolGuard &guard);
    void setToolAudit(const ToolAudit &audit);
    void setStreamProgressCallback(const StreamProgressCallback &cb);
    QString processDirect(
        const QString &content,
        const QString &sessionKey,
        const QString &channel,
        const QString &chatId,
        const QString &modelOverride = QString(),
        const QString &providerOverride = QString(),
        const QJsonObject &runtimeMetadata = QJsonObject()
    );

    AgentTurnResult processDirectDetailed(
        const QString &content,
        const QString &sessionKey,
        const QString &channel,
        const QString &chatId,
        const QString &modelOverride = QString(),
        const QString &providerOverride = QString(),
        const QJsonObject &runtimeMetadata = QJsonObject()
    );

    bool setCronExecutionContext(bool active);

public slots:
    void onInboundMessage(const yaos::bus::InboundMessage &msg);
    void registerDefaultTools();

private:
    void setToolContext(const QString &channel,
                        const QString &chatId,
                        const QString &messageId = QString(),
                        const QString &taskId = QString(),
                        const QString &traceId = QString());
    bus::OutboundMessage processMessage(const bus::InboundMessage &msg,
                                       const QString &modelOverride = QString(),
                                       const QString &providerOverride = QString());
    QList<memory::ConversationMessage> saveTurn(session::Session &session,
                                                const QJsonArray &messages,
                                                int skipCount);

    bus::MessageBus &_bus;
    providers::LLMProvider &_provider;
    QString _workspace;
    config::Config _config;
    MemoryStore _memory;      // ✅ 唯一实例,由 ContextBuilder 引用
    ContextBuilder _context;  // ✅ 持有 _memory 的引用
    session::SessionManager _sessions;
    std::unique_ptr<memory::IConversationStore> _conversationStore;
    std::unique_ptr<memory::IFactStore> _factStore;
    std::unique_ptr<memory::IMemoryRetriever> _memoryRetriever;
    std::unique_ptr<memory::IMemoryExporter> _memoryExporter;
    std::unique_ptr<memory::IMemoryIngestor> _memoryIngestor;
    ToolRegistry _tools;
    runtime::CronService *_cronService;
    runtime::SubagentManager *_subagentManager;
    runtime::MCPManager *_mcpManager;
    ToolGuard _toolGuard;
    ToolAudit _toolAudit;
    StreamProgressCallback _streamProgressCallback;
};

} // namespace yaos::agent

#endif // YAOS_AGENT_AGENTLOOP_H
