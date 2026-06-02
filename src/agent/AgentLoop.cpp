#include "AgentLoop.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QUuid>
#include <algorithm>

#include "../providers/ProviderFactory.h"
#include "../providers/LLMProvider.h"
#include "../runtime/StructuredLog.h"
#include "tools/CronTool.h"
#include "tools/ExecTool.h"
#include "tools/FileTools.h"
#include "tools/WebTools.h"
#include "tools/MCPCallTool.h"
#include "tools/MCPProxyTool.h"
#include "tools/MessageTool.h"
#include "tools/PluginTool.h"
#include "tools/SpawnTool.h"
#include "../runtime/PluginRegistry.h"

Q_LOGGING_CATEGORY(lcAgent, "yaos.agent")

namespace yaos::agent {

namespace {

QString normalizedProviderId(const QString &providerId) {
    QString normalized = providerId.trimmed().toLower();
    normalized.replace('-', '_');
    if (normalized == "azureopenai") normalized = "azure_openai";
    if (normalized == "openaicodex") normalized = "openai_codex";
    if (normalized == "githubcopilot") normalized = "github_copilot";
    return normalized;
}

QString resolvedProviderId(const config::Config &config) {
    QString provider = normalizedProviderId(config.agentDefaults.provider);
    if (provider.isEmpty() || provider == "auto") {
        provider = normalizedProviderId(config.matchedProviderName(config.agentDefaults.model));
    }
    return provider;
}

QString providerFallbackErrorMessage(const QString &requestedProvider,
                                     const QString &fallbackProvider) {
    const QString requested = normalizedProviderId(requestedProvider);
    const QString fallback = normalizedProviderId(fallbackProvider);
    return QStringLiteral(
               "Error: provider '%1' is not ready; YAOS refused to silently fall back to '%2'. "
               "Configure credentials/API base or use '--provider echo' explicitly.")
        .arg(requested.isEmpty() ? QStringLiteral("unknown") : requested,
             fallback.isEmpty() ? QStringLiteral("echo") : fallback);
}

config::Config effectiveConfigForTurn(const config::Config &baseConfig,
                                      const QString &modelOverride,
                                      const QString &providerOverride) {
    config::Config cfg = baseConfig;
    if (!providerOverride.trimmed().isEmpty()) {
        cfg.agentDefaults.provider = normalizedProviderId(providerOverride);
    }
    if (!modelOverride.trimmed().isEmpty()) {
        cfg.agentDefaults.model = modelOverride.trimmed();
    }
    return cfg;
}

QString conversationMessageIdFor(const QJsonObject &entry, int index) {
    const QString directId = entry.value("message_id").toString().trimmed();
    if (!directId.isEmpty()) {
        return directId;
    }
    const QString toolCallId = entry.value("tool_call_id").toString().trimmed();
    if (!toolCallId.isEmpty()) {
        return toolCallId;
    }
    const QString timestamp = entry.value("timestamp").toString().trimmed();
    if (!timestamp.isEmpty()) {
        return QString("%1:%2").arg(timestamp, QString::number(index));
    }
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString renderInboundPromptContent(const bus::InboundMessage &msg) {
    QString content = msg.content.trimmed();
    if (msg.media.isEmpty()) {
        return content;
    }

    QStringList lines;
    if (!content.isEmpty()) {
        lines.append(content);
        lines.append(QString());
    } else {
        lines.append(QStringLiteral("[Inbound attachments]"));
        lines.append(QString());
    }
    lines.append(QStringLiteral("[Attachments]"));
    for (const QString &path : msg.media) {
        const QString clean = path.trimmed();
        if (!clean.isEmpty()) {
            lines.append(QStringLiteral("- %1").arg(clean));
        }
    }
    return lines.join(QLatin1Char('\n')).trimmed();
}

QString normalizedFactValue(QString value) {
    value = value.trimmed();
    while (!value.isEmpty() && QStringLiteral(".！!？?,,;;:.").contains(value.back())) {
        value.chop(1);
    }
    return value.trimmed();
}

QList<memory::MemoryFact> extractHeuristicFacts(const QString &sessionKey,
                                                const QList<memory::ConversationMessage> &messages) {
    QList<memory::MemoryFact> facts;
    for (const memory::ConversationMessage &message : messages) {
        if (message.role != "user") {
            continue;
        }

        const QString text = message.content.trimmed();
        if (text.isEmpty() || text.startsWith('/')) {
            continue;
        }

        const QString lower = text.toLower();
        auto appendFact = [&](const QString &predicate,
                              const QString &value,
                              double confidence,
                              const QStringList &tags) {
            const QString normalizedValue = normalizedFactValue(value);
            if (normalizedValue.isEmpty()) {
                return;
            }
            memory::MemoryFact fact;
            fact.scope = QString("session:%1").arg(sessionKey);
            fact.subject = "user";
            fact.predicate = predicate;
            fact.value = normalizedValue;
            fact.confidence = confidence;
            fact.tags = tags;
            fact.updatedAt = message.createdAt;
            facts.append(fact);
        };

        if (text.startsWith(QStringLiteral("我叫"))) {
            appendFact("identity", text.mid(2), 0.9, {"heuristic", "identity"});
        } else if (text.startsWith(QStringLiteral("我是"))) {
            appendFact("identity", text.mid(2), 0.75, {"heuristic", "identity"});
        } else if (lower.startsWith("my name is ")) {
            appendFact("identity", text.mid(QString("my name is ").size()), 0.9, {"heuristic", "identity"});
        } else if (lower.startsWith("i am ")) {
            appendFact("identity", text.mid(QString("i am ").size()), 0.7, {"heuristic", "identity"});
        }

        if (text.contains(QStringLiteral("喜欢")) || text.contains(QStringLiteral("偏好")) ||
            lower.contains(" i prefer ") || lower.startsWith("i prefer ") ||
            lower.contains(" i like ") || lower.startsWith("i like ")) {
            appendFact("preference", text, 0.65, {"heuristic", "preference"});
        }

        if (text.startsWith(QStringLiteral("记住")) || lower.startsWith("remember ")) {
            appendFact("instruction", text, 0.6, {"heuristic", "instruction"});
        }

        if (text.contains(QStringLiteral("项目")) || text.contains(QStringLiteral("仓库")) ||
            lower.contains("project") || lower.contains("repo")) {
            appendFact("project_context", text, 0.55, {"heuristic", "project"});
        }
    }

    return facts;
}

} // namespace

AgentLoop::AgentLoop(
    bus::MessageBus &bus,
    providers::LLMProvider &provider,
    const QString &workspace,
    const config::Config &config,
    runtime::CronService *cronService,
    runtime::SubagentManager *subagentManager,
    runtime::MCPManager *mcpManager,
    QObject *parent
) : QObject(parent),
    _bus(bus),
    _provider(provider),
    _workspace(workspace),
    _config(config),
    _memory(workspace),                   // ✅ 构造唯一的 MemoryStore
    _context(workspace, config, _memory), // ✅ 传引用,不再创建副本
    _sessions(workspace),
    _conversationStore(memory::MemoryRuntimeFactory::createConversationStore(workspace, config)),
    _factStore(memory::MemoryRuntimeFactory::createFactStore(workspace, config)),
    _memoryRetriever(memory::MemoryRuntimeFactory::createRetriever(config,
                                                                   _conversationStore.get(),
                                                                   _factStore.get())),
    _memoryExporter(memory::MemoryRuntimeFactory::createExporter(workspace,
                                                                 config,
                                                                 _factStore.get())),
    _memoryIngestor(memory::MemoryRuntimeFactory::createIngestor(workspace,
                                                                 config,
                                                                 _factStore.get(),
                                                                 _memoryExporter.get())),
    _cronService(cronService),
    _subagentManager(subagentManager),
    _mcpManager(mcpManager)
{
    // ✅ 注入真正的 LLM 摘要函数
    _memory.setSummarizeFunc([this](const QString &rawConversation) -> QString {
        const QString prompt =
            "Please summarize the following conversation into concise bullet points. "
            "Focus on key facts, decisions made, and important context. "
            "Write in third person, present tense. Be brief.\n\n"
            + rawConversation;

        const QJsonArray messages = {
            QJsonObject{{"role", "user"}, {"content", prompt}}
        };
        const auto response = _provider.chat(
            messages, {}, _provider.defaultModel(), 0.1, 1024
        );
        return response.content;
    });

    connect(&_bus, &bus::MessageBus::inboundPublished,
            this, &AgentLoop::onInboundMessage,
            Qt::QueuedConnection);
}

void AgentLoop::setToolGuard(const ToolGuard &guard) {
    _toolGuard = guard;
}

void AgentLoop::setToolAudit(const ToolAudit &audit) {
    _toolAudit = audit;
}

void AgentLoop::setStreamProgressCallback(const StreamProgressCallback &cb) {
    _streamProgressCallback = cb;
}

void AgentLoop::registerDefaultTools() {
    const QString allowedDir = _config.tools.restrictToWorkspace ? _workspace : QString();

    if (_config.tools.capabilities.web) {
        _tools.registerTool(QSharedPointer<tools::WebSearchTool>::create(_config.tools.web.search,
                                                                         _config.tools.web.proxy));
        _tools.registerTool(QSharedPointer<tools::WebFetchTool>::create(_config.tools.web.search,
                                                                        _config.tools.web.proxy));
    }
    if (_config.tools.capabilities.filesystem) {
        _tools.registerTool(QSharedPointer<tools::ReadFileTool>::create(_workspace, allowedDir));
        _tools.registerTool(QSharedPointer<tools::WriteFileTool>::create(_workspace, allowedDir));
        _tools.registerTool(QSharedPointer<tools::EditFileTool>::create(_workspace, allowedDir));
        _tools.registerTool(QSharedPointer<tools::ListDirTool>::create(_workspace, allowedDir));
    }
    if (_config.tools.capabilities.exec) {
        _tools.registerTool(QSharedPointer<tools::ExecTool>::create(
            _workspace,
            _config.tools.exec.timeout,
            _config.tools.exec.pathAppend,
            allowedDir
        ));
    }

    if (_config.tools.capabilities.messaging) {
        auto messageTool = QSharedPointer<tools::MessageTool>::create(
            [this](const bus::OutboundMessage &msg) { _bus.publishOutbound(msg); }
        );
        _tools.registerTool(messageTool);
    }

    if (_config.tools.capabilities.spawn && _subagentManager) {
        _tools.registerTool(QSharedPointer<tools::SpawnTool>::create(*_subagentManager));
    }
    if (_config.tools.capabilities.cron && _cronService) {
        _tools.registerTool(QSharedPointer<tools::CronTool>::create(*_cronService));
    }
    if (_config.tools.capabilities.mcp && _mcpManager && !_mcpManager->servers().isEmpty()) {
        _tools.registerTool(QSharedPointer<tools::MCPCallTool>::create(*_mcpManager));
        const QVector<runtime::MCPRemoteTool> mcpTools = _mcpManager->listAllTools();
        for (const runtime::MCPRemoteTool &tool : mcpTools) {
            if (tool.name.trimmed().isEmpty()) {
                continue;
            }
            _tools.registerTool(QSharedPointer<tools::MCPProxyTool>::create(*_mcpManager, tool));
        }
    }

    runtime::PluginRegistry pluginRegistry(_workspace);
    const QVector<runtime::PluginRecord> plugins = pluginRegistry.discover();
    for (const runtime::PluginRecord &plugin : plugins) {
        const config::ExtensionProfileConfig profile = _config.extensions.plugins.value(plugin.id);
        if (!profile.enabled || plugin.state != "ready") {
            continue;
        }
        _tools.registerTool(QSharedPointer<tools::PluginTool>::create(_workspace, _config, plugin));
    }
}

void AgentLoop::setToolContext(const QString &channel,
                               const QString &chatId,
                               const QString &messageId,
                               const QString &taskId,
                               const QString &traceId) {
    if (auto tool = _tools.get("message").dynamicCast<tools::MessageTool>()) {
        tool->setContext(channel, chatId, messageId);
    }
    if (auto tool = _tools.get("spawn").dynamicCast<tools::SpawnTool>()) {
        tool->setContext(channel, chatId, taskId, traceId);
    }
    if (auto tool = _tools.get("cron").dynamicCast<tools::CronTool>()) {
        tool->setContext(channel, chatId);
    }
}

void AgentLoop::onInboundMessage(const yaos::bus::InboundMessage &msg) {
    if (msg.channel == "cli" && msg.content == "exit") {
        return;
    }
    const QString displayContent = renderInboundPromptContent(msg);
    qDebug(lcAgent) << "Inbound message from" << msg.channel << "/" << msg.chatId
                    << ":" << displayContent.left(80);
    const bus::OutboundMessage response = processMessage(msg);
    if (!response.content.isEmpty() || !response.media.isEmpty()) {
        _bus.publishOutbound(response);
    }
}

QString AgentLoop::processDirect(
    const QString &content,
    const QString &sessionKey,
    const QString &channel,
    const QString &chatId,
    const QString &modelOverride,
    const QString &providerOverride,
    const QJsonObject &runtimeMetadata
) {
    bus::InboundMessage msg;
    msg.channel = channel;
    msg.senderId = "user";
    msg.chatId = chatId;
    msg.content = content;
    msg.sessionKeyOverride = sessionKey;
    msg.metadata = runtimeMetadata;

    const bus::OutboundMessage out = processMessage(msg, modelOverride, providerOverride);
    return out.content;
}

AgentTurnResult AgentLoop::processDirectDetailed(
    const QString &content,
    const QString &sessionKey,
    const QString &channel,
    const QString &chatId,
    const QString &modelOverride,
    const QString &providerOverride,
    const QJsonObject &runtimeMetadata
) {
    bus::InboundMessage msg;
    msg.channel = channel;
    msg.senderId = "user";
    msg.chatId = chatId;
    msg.content = content;
    msg.sessionKeyOverride = sessionKey;
    msg.metadata = runtimeMetadata;

    const bus::OutboundMessage out = processMessage(msg, modelOverride, providerOverride);
    AgentTurnResult result;
    result.content = out.content;
    result.thinking = out.metadata.value(QStringLiteral("thinking")).toString();
    return result;
}

bool AgentLoop::setCronExecutionContext(bool active) {
    if (auto tool = _tools.get("cron").dynamicCast<tools::CronTool>()) {
        return tool->setCronContext(active);
    }
    return false;
}

QList<memory::ConversationMessage> AgentLoop::saveTurn(session::Session &session,
                                                       const QJsonArray &messages,
                                                       int skipCount) {
    QList<memory::ConversationMessage> saved;
    for (int i = skipCount; i < messages.size(); ++i) {
        const QJsonObject msg = messages.at(i).toObject();
        const QString role = msg.value("role").toString();
        QString content = msg.value("content").toVariant().toString();

        if (role == "assistant" && content.trimmed().isEmpty() && !msg.contains("tool_calls")) {
            continue;
        }
        if (role == "tool" && content.size() > 4000) {
            content = content.left(4000) + "\n... (truncated)";
        }
        if (role == "user" && content.startsWith("[Runtime Context")) {
            const int split = content.indexOf("\n\n");
            if (split >= 0) {
                content = content.mid(split + 2);
            }
        }

        QJsonObject entry = msg;
        entry["content"] = content;
        if (!entry.contains("timestamp")) {
            entry["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        }
        session.messages.append(entry);

        memory::ConversationMessage conversationMessage;
        conversationMessage.sessionKey = session.key;
        conversationMessage.messageId = conversationMessageIdFor(entry, i);
        conversationMessage.role = role;
        conversationMessage.content = content;
        conversationMessage.meta = entry;
        conversationMessage.createdAt = QDateTime::fromString(entry.value("timestamp").toString(), Qt::ISODate);
        if (!conversationMessage.createdAt.isValid()) {
            conversationMessage.createdAt = QDateTime::currentDateTimeUtc();
        }
        saved.append(conversationMessage);
    }
    session.updatedAt = QDateTime::currentDateTime();
    return saved;
}

bus::OutboundMessage AgentLoop::processMessage(const bus::InboundMessage &msg,
                                               const QString &modelOverride,
                                               const QString &providerOverride) {
    bus::OutboundMessage out;
    out.channel = msg.channel;
    out.chatId = msg.chatId;
    QJsonObject metadata = msg.metadata;
    const QString traceId = runtime::StructuredLog::ensureTraceId(
        metadata.value("trace_id").toString(metadata.value("traceId").toString()));
    if (!traceId.isEmpty() &&
        metadata.value("trace_id").toString().trimmed().isEmpty() &&
        metadata.value("traceId").toString().trimmed().isEmpty()) {
        metadata.insert("trace_id", traceId);
    }
    const runtime::ScopedTraceContext traceScope(traceId);
    out.metadata = metadata;
    const QString userPromptContent = renderInboundPromptContent(msg);

    QString routeChannel = msg.channel;
    QString routeChatId = msg.chatId;
    QString key = msg.sessionKey();
    if (msg.channel == "system" && msg.chatId.contains(':')) {
        const QStringList parts = msg.chatId.split(':');
        if (parts.size() >= 2) {
            routeChannel = parts.at(0);
            routeChatId = msg.chatId.mid(routeChannel.size() + 1);
            out.channel = routeChannel;
            out.chatId = routeChatId;
            key = routeChannel + ":" + routeChatId;
        }
    }

    session::Session session = _sessions.getOrCreate(key);

    const QString cmd = msg.content.trimmed().toLower();
    if (cmd == "/help") {
        out.content = "YAOS 命令:\n/new  开始新会话\n/stop 停止当前任务\n/help 显示帮助";
        return out;
    }
    if (cmd == "/new") {
        _memory.consolidate(session, _config.agentDefaults.memoryWindow, true);
        session.clear();
        _sessions.save(session);
        _sessions.invalidate(session.key);
        out.content = "已开始新的会话.";
        return out;
    }
    if (cmd == "/stop") {
        int stopped = 0;
        if (_subagentManager) {
            stopped += _subagentManager->cancelBySession(key);
        }
        out.content = stopped > 0 ? QString("已停止 %1 个任务.").arg(stopped) : "没有正在运行的任务.";
        return out;
    }

    // ✅ 修复触发阈值：提前到 80% 时整合,而非 100% 时才触发
    const int unconsolidated = session.messages.size() - session.lastConsolidated;
    const int consolidateThreshold = static_cast<int>(_config.agentDefaults.memoryWindow * 0.8);
    if (unconsolidated >= consolidateThreshold) {
        qDebug(lcAgent) << "Triggering memory consolidation at" << unconsolidated << "messages";
        _memory.consolidate(session, _config.agentDefaults.memoryWindow, false);
    }

    setToolContext(routeChannel,
                   routeChatId,
                   metadata.value("message_id").toString(),
                   metadata.value("task_id").toString(metadata.value("taskId").toString()),
                   traceId);
    if (auto messageTool = _tools.get("message").dynamicCast<tools::MessageTool>()) {
        messageTool->startTurn();
    }
    const ToolExecutionContext toolContext{
        key,
        routeChannel,
        routeChatId,
        metadata.value("message_id").toString(),
        metadata.value("task_id").toString(metadata.value("taskId").toString()),
        traceId
    };

    const QJsonArray history = session.getHistory(_config.agentDefaults.memoryWindow);
    QList<memory::MemoryRecallItem> recallItems;
    if (_memoryRetriever) {
        memory::MemoryQuery query;
        query.workspaceId = _workspace;
        query.sessionKey = key;
        query.sceneKey = routeChannel + ":" + routeChatId;
        query.text = msg.content.trimmed().isEmpty() ? userPromptContent : msg.content;
        query.recentWindow = std::max(1, _config.memory.recentWindow);
        query.topK = std::max(1, _config.memory.retrievalTopK);
        recallItems = _memoryRetriever->recall(query);
    }

    QJsonArray messages = _context.buildMessages(history,
                                                 userPromptContent,
                                                 routeChannel,
                                                 routeChatId,
                                                 recallItems);
    const int skipCount = 1 + history.size();

    std::unique_ptr<providers::LLMProvider> overrideProvider;
    providers::LLMProvider *activeProvider = &_provider;
    config::Config turnConfig = effectiveConfigForTurn(_config, modelOverride, providerOverride);
    const QString currentProviderId = resolvedProviderId(_config);
    const QString targetProviderId = resolvedProviderId(turnConfig);
    QString activeModel = turnConfig.agentDefaults.model.trimmed().isEmpty()
        ? _provider.defaultModel()
        : turnConfig.agentDefaults.model.trimmed();

    if (!targetProviderId.isEmpty() && targetProviderId != currentProviderId) {
        overrideProvider = providers::ProviderFactory::create(turnConfig);
        if (overrideProvider) {
            activeProvider = overrideProvider.get();
            if (activeModel.isEmpty()) {
                activeModel = activeProvider->defaultModel();
            }
        }
    }

    if (activeProvider && activeProvider->isFallback() &&
        !targetProviderId.isEmpty() && targetProviderId != QStringLiteral("echo")) {
        const QString fallbackProvider = activeProvider->backendName().trimmed();
        qWarning(lcAgent) << "Refusing silent provider fallback"
                          << targetProviderId
                          << "->"
                          << fallbackProvider;
        out.content = providerFallbackErrorMessage(targetProviderId, fallbackProvider);
        return out;
    }

    QString finalContent;
    QStringList thinkingParts;
    for (int iteration = 0; iteration < _config.agentDefaults.maxToolIterations; ++iteration) {
        qDebug(lcAgent) << "LLM call iteration" << iteration;

        const LLMResponse response = activeProvider->chatStreaming(
            messages,
            _tools.definitions(),
            activeModel,
            turnConfig.agentDefaults.temperature,
            turnConfig.agentDefaults.maxTokens,
            _streamProgressCallback
                ? providers::LLMStreamCallback([this](const providers::LLMStreamChunk &chunk) {
                      if (!chunk.done && _streamProgressCallback) {
                          _streamProgressCallback(chunk.contentDelta, chunk.thinkingDelta);
                      }
                  })
                : providers::LLMStreamCallback{}
        );

        // Collect thinking content from every iteration
        if (!response.thinking.isEmpty()) {
            thinkingParts.append(response.thinking);
        }

        if (response.finishReason == "error") {
            qWarning(lcAgent) << "LLM error:" << response.content;
            finalContent = response.content;
            break;
        }

        if (response.hasToolCalls()) {
            QJsonArray toolCalls;
            for (const ToolCallRequest &tc : response.toolCalls) {
                toolCalls.append(QJsonObject{
                    {"id", tc.id},
                    {"type", "function"},
                    {"function", QJsonObject{
                        {"name", tc.name},
                        {"arguments", QString::fromUtf8(QJsonDocument(tc.arguments).toJson(QJsonDocument::Compact))}
                    }}
                });
            }

            QJsonObject assistantMsg;
            assistantMsg["role"] = "assistant";
            assistantMsg["content"] = response.content;
            assistantMsg["tool_calls"] = toolCalls;
            if (!response.thinking.isEmpty()) {
                assistantMsg["reasoning"] = response.thinking;
                assistantMsg["reasoning_content"] = response.thinking;
            }
            if (!response.reasoningDetails.isEmpty()) {
                assistantMsg["reasoning_details"] = response.reasoningDetails;
            }
            messages.append(assistantMsg);

            for (const ToolCallRequest &tc : response.toolCalls) {
                qDebug(lcAgent) << "Executing tool:" << tc.name;
                ToolGuardResult decision;
                if (_toolGuard) {
                    decision = _toolGuard(tc.name, tc.arguments, toolContext);
                }

                QString result;
                if (!decision.allowed) {
                    result = decision.message.isEmpty()
                        ? QString("Error: tool '%1' blocked by policy.").arg(tc.name)
                        : decision.message;
                } else {
                    result = _tools.execute(tc.name, tc.arguments);
                }

                if (_toolAudit) {
                    _toolAudit(ToolAuditRecord{
                        tc.name,
                        tc.arguments,
                        toolContext,
                        decision.allowed,
                        result,
                        decision.policy,
                        decision.referenceId
                    });
                }
                messages.append(QJsonObject{
                    {"role", "tool"},
                    {"tool_call_id", tc.id},
                    {"name", tc.name},
                    {"content", result}
                });
            }
            continue;
        }

        finalContent = response.content.trimmed();
        QJsonObject assistantMsg;
        assistantMsg["role"] = "assistant";
        assistantMsg["content"] = finalContent;
        if (!response.thinking.isEmpty()) {
            assistantMsg["reasoning"] = response.thinking;
            assistantMsg["reasoning_content"] = response.thinking;
        }
        if (!response.reasoningDetails.isEmpty()) {
            assistantMsg["reasoning_details"] = response.reasoningDetails;
        }
        messages.append(assistantMsg);
        break;
    }

    if (finalContent.isEmpty()) {
        finalContent = "处理已完成,但没有可返回的内容.";
    }

    const QList<memory::ConversationMessage> appendedTurn = saveTurn(session, messages, skipCount);
    _sessions.save(session);
    if (_conversationStore && !appendedTurn.isEmpty()) {
        if (!_conversationStore->appendTurn(session.key, appendedTurn)) {
            qWarning(lcAgent) << "Failed to persist conversation turn into layered conversation store for"
                              << session.key;
        }
    }
    if (_factStore && !appendedTurn.isEmpty()) {
        const QList<memory::MemoryFact> facts = extractHeuristicFacts(session.key, appendedTurn);
        if (!facts.isEmpty() && !_factStore->upsertFacts(_workspace, facts)) {
            qWarning(lcAgent) << "Failed to persist heuristic facts for" << session.key;
        }
    }
    if (_memoryIngestor && !appendedTurn.isEmpty()) {
        if (!_memoryIngestor->ingestTurn(_workspace, session.key, appendedTurn)) {
            qWarning(lcAgent) << "Failed to run layered memory ingestor for" << session.key;
        }
    }

    if (auto messageTool = _tools.get("message").dynamicCast<tools::MessageTool>()) {
        if (messageTool->sentInTurn()) {
            out.content.clear();
            return out;
        }
    }

    out.content = finalContent;
    if (!thinkingParts.isEmpty()) {
        QJsonObject meta = out.metadata;
        meta.insert(QStringLiteral("thinking"), thinkingParts.join(QStringLiteral("\n\n---\n\n")));
        out.metadata = meta;
    }
    return out;
}

} // namespace yaos::agent
