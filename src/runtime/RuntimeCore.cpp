#if defined(_MSC_VER) && (_MSC_VER >= 1600)
#pragma execution_character_set("utf-8")
#endif

#include "RuntimeCore.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent>

#include <cmath>

#include "../config/ConfigLoader.h"
#include "../memory/MemoryServiceSupport.h"
#include "../distributed/P2PCluster.h"
#include "../distributed/ContractsJson.h"
#include "../distributed/RemoteControlClient.h"
#include "../distributed/RemoteNodeRegistryClient.h"
#include "../distributed/RemoteTaskBus.h"
#include "../providers/ProviderFactory.h"
#include "../providers/ProviderOAuth.h"
#include "../session/SessionManager.h"
#include "RuntimeServiceSupport.h"
#include "StructuredLog.h"
#include "Templates.h"

Q_LOGGING_CATEGORY(lcRuntime, "yaos.runtime")

namespace yaos::runtime {

namespace {

constexpr int kControlPlaneProbeTimeoutMs = 3500;
constexpr int kDelegationPollIntervalMs = 2200;
constexpr int kDelegationActivePollIntervalMs = 2200;
constexpr int kDelegationIdlePollIntervalMs = 15000;
constexpr int kDefaultNodeConcurrency = 4;
constexpr int kStatusRuntimeProbeTimeoutMs = 200;
constexpr int kStatusControlProbeTimeoutMs = 200;
constexpr int kStatusMemoryProbeTimeoutMs = 300;

QStringList enabledToolCapabilities(const config::Config &cfg) {
    QStringList out;
    if (cfg.tools.capabilities.web) out << "web";
    if (cfg.tools.capabilities.filesystem) out << "filesystem";
    if (cfg.tools.capabilities.exec) out << "exec";
    if (cfg.tools.capabilities.messaging) out << "message";
    if (cfg.tools.capabilities.spawn) out << "spawn";
    if (cfg.tools.capabilities.cron) out << "cron";
    if (cfg.tools.capabilities.mcp) out << "mcp";
    return out;
}

QStringList configuredChannels(const config::Config &cfg) {
    QStringList out;
    if (cfg.channels.telegram.enabled) out << "telegram";
    if (cfg.channels.slack.enabled) out << "slack";
    if (cfg.channels.whatsapp.enabled) out << "whatsapp";
    if (cfg.channels.feishu.enabled) out << "feishu";
    if (cfg.channels.dingtalk.enabled) out << "dingtalk";
    if (cfg.channels.discord.enabled) out << "discord";
    if (cfg.channels.mochat.enabled) out << "mochat";
    if (cfg.channels.matrix.enabled) out << "matrix";
    if (cfg.channels.email.enabled) out << "email";
    if (cfg.channels.qq.enabled) out << "qq";
    return out;
}

QStringList memoryBackendsForConfig(const config::Config &cfg) {
    QStringList out;
    const QString backend = cfg.normalizedMemoryBackend();
    if (!backend.isEmpty()) {
        out << backend;
    }
    const QString mode = cfg.normalizedMemoryMode();
    if (!mode.isEmpty() && !out.contains(mode)) {
        out << mode;
    }
    if (cfg.usesLayeredMemory()) {
        const QString conversationDriver = cfg.memory.conversation.driver.trimmed();
        const QString factDriver = cfg.memory.facts.driver.trimmed();
        const QString vectorDriver = cfg.memory.vector.driver.trimmed();
        if (!conversationDriver.isEmpty() && !out.contains(conversationDriver)) {
            out << conversationDriver;
        }
        if (!factDriver.isEmpty() && !out.contains(factDriver)) {
            out << factDriver;
        }
        if (!vectorDriver.isEmpty() && vectorDriver != "none" && !out.contains(vectorDriver)) {
            out << vectorDriver;
        }
    }
    return out;
}

QString nodeEndpointForConfig(const config::Config &cfg) {
    const QString runtimeMode = cfg.normalizedRuntimeMode();
    const QString endpoint = runtimeAdvertiseEndpoint(cfg);
    if (runtimeMode == "embedded") {
        return QStringLiteral("local://embedded");
    }
    if (!endpoint.isEmpty()) {
        return endpoint;
    }
    if (runtimeMode == "daemon") {
        return QStringLiteral("local://yaosd");
    }
    return QStringLiteral("remote://unconfigured");
}

QString effectiveNodeId(const config::Config &cfg) {
    QString nodeId = cfg.deployment.nodeId.trimmed();
    if (nodeId.isEmpty()) {
        nodeId = QStringLiteral("desktop-primary");
    }
    const QString runtimeMode = cfg.normalizedRuntimeMode();
    const QString suffix = QStringLiteral("-%1").arg(runtimeMode);
    if (!runtimeMode.isEmpty() && !nodeId.endsWith(suffix, Qt::CaseInsensitive)) {
        nodeId += suffix;
    }
    return nodeId;
}

QString newTraceId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

distributed::NodeDescriptor localNodeDescriptor(const config::Config &cfg,
                                                const QStringList &enabledChannels,
                                                bool online,
                                                int activeTaskCount,
                                                int queuedTaskCount,
                                                int maxConcurrencyHint) {
    distributed::NodeDescriptor node;
    node.nodeId = effectiveNodeId(cfg);
    node.clusterId = cfg.deployment.clusterId.trimmed().isEmpty()
        ? QStringLiteral("local")
        : cfg.deployment.clusterId.trimmed();
    node.displayName = node.nodeId;
    node.role = cfg.deployment.nodeRole.trimmed().isEmpty()
        ? QStringLiteral("desktop")
        : cfg.deployment.nodeRole.trimmed();
    node.endpoint = nodeEndpointForConfig(cfg);
    node.runtimeMode = cfg.normalizedRuntimeMode();
    node.tags = QStringList{
        cfg.normalizedDeploymentMode(),
        cfg.normalizedRuntimeMode(),
        cfg.normalizedMemoryBackend()
    };
    for (const QString &tag : cfg.deployment.nodeTags) {
        const QString normalizedTag = tag.trimmed();
        if (!normalizedTag.isEmpty() && !node.tags.contains(normalizedTag, Qt::CaseInsensitive)) {
            node.tags.append(normalizedTag);
        }
    }
    if (!node.role.trimmed().isEmpty() && !node.tags.contains(node.role.trimmed(), Qt::CaseInsensitive)) {
        node.tags.append(node.role.trimmed());
    }
    node.activeTaskCount = std::max(0, activeTaskCount);
    node.queuedTaskCount = std::max(0, queuedTaskCount);
    node.maxConcurrencyHint = std::max(1, maxConcurrencyHint);
    node.weight = 100;
    node.online = online;

    distributed::NodeCapability capability;
    capability.name = QStringLiteral("yaos-runtime");
    capability.version = QStringLiteral("phase4-local");
    capability.roles = QStringList{node.role};
    capability.tools = enabledToolCapabilities(cfg);
    capability.channels = enabledChannels.isEmpty() ? configuredChannels(cfg) : enabledChannels;
    capability.memoryBackends = memoryBackendsForConfig(cfg);
    capability.maxConcurrency = node.maxConcurrencyHint;
    capability.supportsDelegation = cfg.tools.capabilities.spawn;
    capability.supportsStreaming = true;
    node.capabilities = QList<distributed::NodeCapability>{capability};
    return node;
}

int nodeDeclaredConcurrency(const distributed::NodeDescriptor &node) {
    int declared = node.maxConcurrencyHint > 0 ? node.maxConcurrencyHint : 0;
    for (const distributed::NodeCapability &capability : node.capabilities) {
        declared = std::max(declared, capability.maxConcurrency);
    }
    return std::max(1, declared);
}

bool nodeHasAvailableCapacity(const distributed::NodeDescriptor &node) {
    return node.activeTaskCount < nodeDeclaredConcurrency(node);
}

double nodeSchedulingPressure(const distributed::NodeDescriptor &node) {
    const double denominator = static_cast<double>(nodeDeclaredConcurrency(node));
    return (static_cast<double>(node.activeTaskCount) +
            static_cast<double>(node.queuedTaskCount) * 0.5) / denominator;
}

bool expiresAtNearOrPast(const QString &expiresAt, int thresholdSeconds = 300) {
    const QString normalized = expiresAt.trimmed();
    if (normalized.isEmpty()) {
        return false;
    }

    const QDateTime parsed = QDateTime::fromString(normalized, Qt::ISODate);
    if (!parsed.isValid()) {
        return false;
    }

    return QDateTime::currentDateTimeUtc().secsTo(parsed.toUTC()) <= thresholdSeconds;
}

QJsonObject providerOAuthSummary(const QString &providerId,
                                 const config::ProviderConfig &providerConfig) {
    const providers::ProviderOAuthResult status =
        providers::resolveProviderAccess(providerId, providerConfig, false);
    return QJsonObject{
        {QStringLiteral("providerId"), status.providerId},
        {QStringLiteral("mode"), status.mode},
        {QStringLiteral("credentialMode"), status.credentialMode},
        {QStringLiteral("loggedIn"), status.loggedIn},
        {QStringLiteral("pending"), status.pending},
        {QStringLiteral("browserSupported"), status.browserSupported},
        {QStringLiteral("deviceSupported"), status.deviceSupported},
        {QStringLiteral("refreshSupported"), status.refreshSupported},
        {QStringLiteral("requiresClientId"), status.requiresClientId},
        {QStringLiteral("accountId"), status.accountId},
        {QStringLiteral("expiresAt"), status.expiresAt},
        {QStringLiteral("expiresSoon"), expiresAtNearOrPast(status.expiresAt)},
        {QStringLiteral("lastRefreshAt"), providerConfig.oauthLastRefreshAt},
        {QStringLiteral("lastError"), providerConfig.oauthLastError},
        {QStringLiteral("apiBase"), status.apiBase},
        {QStringLiteral("model"), providerConfig.model.trimmed()},
        {QStringLiteral("modelCount"), providerConfig.availableModels.size()},
        {QStringLiteral("hasApiKey"), !status.apiKey.trimmed().isEmpty()},
        {QStringLiteral("hasOAuthAccessToken"), !providerConfig.oauthAccessToken.trimmed().isEmpty()},
        {QStringLiteral("hasRefreshToken"), !providerConfig.oauthRefreshToken.trimmed().isEmpty()},
        {QStringLiteral("hasIdToken"), !providerConfig.oauthIdToken.trimmed().isEmpty()}
    };
}

QJsonArray providerOAuthStatuses(const config::Config &cfg) {
    QJsonArray statuses;
    statuses.append(providerOAuthSummary(QStringLiteral("codebuddy"), cfg.providers.codebuddy));
    statuses.append(providerOAuthSummary(QStringLiteral("openai_codex"), cfg.providers.openaiCodex));
    statuses.append(providerOAuthSummary(QStringLiteral("github_copilot"), cfg.providers.githubCopilot));
    return statuses;
}

void refreshDelegatedWorkerLeases(distributed::ITaskBus *taskBus,
                                  QSet<QString> *activeTaskIds,
                                  QMutex *mutex,
                                  const QString &consumerNode) {
    if (!taskBus || !activeTaskIds || !mutex) {
        return;
    }

    QStringList taskIds;
    {
        QMutexLocker locker(mutex);
        taskIds = activeTaskIds->values();
    }

    for (const QString &taskId : taskIds) {
        if (taskId.trimmed().isEmpty()) {
            continue;
        }
        taskBus->claim(taskId.trimmed(), consumerNode);
    }
}

distributed::TaskResultEnvelope taskResultEnvelopeForTurn(const ChatTurnResult &turn,
                                                          const QString &taskId,
                                                          const QString &traceId,
                                                          const QString &originNode,
                                                          const QString &sessionKey,
                                                          const QString &channel) {
    distributed::TaskResultEnvelope result;
    result.taskId = taskId;
    result.traceId = traceId;
    result.producerNode = originNode;
    result.status = turn.error ? QStringLiteral("error") : QStringLiteral("ok");
    result.message = turn.error ? turn.content : QStringLiteral("Task completed");
    result.output = QJsonObject{
        {"content", turn.content},
        {"sessionKey", sessionKey},
        {"channel", channel},
        {"provider", turn.provider},
        {"model", turn.model}
    };
    if (turn.error) {
        result.error = QJsonObject{{"message", turn.content}};
    }
    result.finishedAt = QDateTime::currentDateTimeUtc();
    return result;
}

QString taskTitleFromContent(const QString &content) {
    const QString simplified = content.simplified();
    if (simplified.size() <= 120) {
        return simplified;
    }
    return simplified.left(120) + "...";
}

QString normalizedPolicy(const QString &value) {
    const QString policy = value.trimmed().toLower();
    if (policy == "deny" || policy == "confirm" || policy == "allow") {
        return policy;
    }
    return "allow";
}

QString toolPolicyFor(const config::Config &cfg, const QString &toolName) {
    if (toolName == "web_search") return "allow";
    if (toolName == "web_fetch") return "allow";
    if (toolName == "read_file") return normalizedPolicy(cfg.security.toolPolicies.readFile);
    if (toolName == "write_file") return normalizedPolicy(cfg.security.toolPolicies.writeFile);
    if (toolName == "edit_file") return normalizedPolicy(cfg.security.toolPolicies.writeFile);
    if (toolName == "list_dir") return normalizedPolicy(cfg.security.toolPolicies.listDir);
    if (toolName == "exec") return normalizedPolicy(cfg.security.toolPolicies.exec);
    if (toolName == "message") return normalizedPolicy(cfg.security.toolPolicies.message);
    if (toolName == "spawn") return normalizedPolicy(cfg.security.toolPolicies.spawn);
    if (toolName == "cron") return normalizedPolicy(cfg.security.toolPolicies.cron);
    if (toolName == "mcp_call") return normalizedPolicy(cfg.security.toolPolicies.mcpCall);
    if (toolName.startsWith("mcp_")) return normalizedPolicy(cfg.security.toolPolicies.mcpCall);
    if (toolName.startsWith("plugin_")) return normalizedPolicy(cfg.security.toolPolicies.pluginCall);
    return "allow";
}

QString toolDisplayName(const QString &toolName) {
    if (toolName == "web_search") return "Web Search";
    if (toolName == "web_fetch") return "Web Fetch";
    if (toolName == "read_file") return "Read File";
    if (toolName == "write_file") return "Write File";
    if (toolName == "edit_file") return "Edit File";
    if (toolName == "list_dir") return "List Directory";
    if (toolName == "exec") return "Exec";
    if (toolName == "message") return "Message";
    if (toolName == "spawn") return "Spawn";
    if (toolName == "cron") return "Cron";
    if (toolName == "mcp_call") return "MCP Call";
    if (toolName.startsWith("mcp_")) return "MCP Call";
    if (toolName.startsWith("plugin_")) return "Plugin Call";
    return toolName;
}

QString compactJson(const QJsonObject &obj, int maxLen = 320) {
    QString text = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    if (text.size() > maxLen) {
        if (text.at(maxLen - 1).isHighSurrogate()) {
            maxLen--;
        }
        text = text.left(maxLen) + "...";
    }
    return text;
}

QString normalizedProviderOverride(const QString &providerName) {
    QString normalized = providerName.trimmed().toLower();
    normalized.replace('-', '_');
    if (normalized == "azureopenai") normalized = "azure_openai";
    if (normalized == "code_buddy") normalized = "codebuddy";
    if (normalized == "openaicodex") normalized = "openai_codex";
    if (normalized == "githubcopilot") normalized = "github_copilot";
    return normalized;
}

config::Config effectiveConfigForTurn(const config::Config &baseConfig,
                                      const QString &modelOverride,
                                      const QString &providerOverride) {
    config::Config cfg = baseConfig;
    if (!providerOverride.trimmed().isEmpty()) {
        cfg.agentDefaults.provider = normalizedProviderOverride(providerOverride);
    }
    if (!modelOverride.trimmed().isEmpty()) {
        cfg.agentDefaults.model = modelOverride.trimmed();
    }
    return cfg;
}

QString resolvedProviderForTurn(const config::Config &baseConfig,
                                const QString &modelOverride,
                                const QString &providerOverride) {
    const config::Config effective = effectiveConfigForTurn(baseConfig, modelOverride, providerOverride);
    QString provider = normalizedProviderOverride(effective.agentDefaults.provider);
    if (provider.isEmpty() || provider == "auto") {
        provider = normalizedProviderOverride(effective.matchedProviderName(effective.agentDefaults.model));
    }
    return provider;
}

struct SkillTurnOverrides {
    QString model;
    QString provider;
    QVector<SkillMatch> activeSkills;
};

SkillTurnOverrides resolveSkillTurnOverrides(const config::Config &cfg,
                                             const QString &workspace,
                                             const QString &content) {
    SkillTurnOverrides overrides;
    SkillRegistry registry(workspace);
    overrides.activeSkills = registry.select(cfg.extensions, content, 3);

    for (const SkillMatch &skill : overrides.activeSkills) {
        if (overrides.provider.isEmpty()) {
            const QString provider = normalizedProviderOverride(skill.profile.provider);
            if (!provider.isEmpty() && provider != "auto") {
                overrides.provider = provider;
            }
        }
        if (overrides.model.isEmpty() && !skill.profile.model.trimmed().isEmpty()) {
            overrides.model = skill.profile.model.trimmed();
        }
        if (!overrides.provider.isEmpty() && !overrides.model.isEmpty()) {
            break;
        }
    }

    return overrides;
}

bool eventBelongsToTurn(const EventRecord &event,
                        const QString &taskId,
                        const QString &sessionKey,
                        const QString &channel,
                        const QDateTime &startedAt) {
    if (event.timestamp.isValid() && event.timestamp < startedAt.addSecs(-1)) {
        return false;
    }

    const QJsonObject metadata = event.metadata;
    if (!taskId.isEmpty() && metadata.value("task_id").toString() == taskId) {
        return true;
    }

    if (metadata.value("session_key").toString() != sessionKey) {
        return false;
    }

    const QString eventChannel = metadata.value("channel").toString();
    if (!eventChannel.isEmpty() && eventChannel != channel) {
        return false;
    }

    return event.category == "task" ||
           event.category == "tool" ||
           event.category == "security";
}

QString automationPreview(const QString &text, int maxLen = 320) {
    const QString simplified = text.simplified();
    if (simplified.size() <= maxLen) {
        return simplified;
    }
    return simplified.left(maxLen) + "...";
}

QString providerOperationalError(const config::Config &cfg,
                                 const providers::LLMProvider *provider,
                                 const QString &modelOverride = QString(),
                                 const QString &providerOverride = QString()) {
    if (!provider) {
        return QStringLiteral("Runtime provider is not initialized.");
    }

    const QString requestedProvider = resolvedProviderForTurn(cfg, modelOverride, providerOverride);
    if (!provider->isFallback() || requestedProvider == QStringLiteral("echo")) {
        return {};
    }

    const QString fallbackProvider = provider->backendName().trimmed().isEmpty()
        ? QStringLiteral("echo")
        : provider->backendName().trimmed();
    return QStringLiteral(
               "Provider '%1' is not ready; YAOS refused to silently fall back to '%2'. "
               "Configure credentials/API base or use '--provider echo' explicitly.")
        .arg(requestedProvider.isEmpty() ? QStringLiteral("unknown") : requestedProvider,
             fallbackProvider);
}

bool usesRemoteTaskBus(const distributed::ITaskBus *taskBus) {
    return dynamic_cast<const distributed::RemoteTaskBus *>(taskBus) != nullptr;
}

QString normalizedAutomationProvider(const QString &providerName) {
    const QString normalized = normalizedProviderOverride(providerName);
    return normalized.isEmpty() ? QString("auto") : normalized;
}

QString normalizedAutomationScheduleKind(const QString &value, const QString &legacyTrigger = QString()) {
    QString kind = value.trimmed().toLower();
    if (kind == "at") {
        kind = "once";
    }
    if (kind == "scheduled") {
        kind = legacyTrigger.trimmed().toLower() == "cron" ? "cron" : "every";
    }
    if (kind == "manual" || kind == "once" || kind == "every" || kind == "cron") {
        return kind;
    }

    const QString trigger = legacyTrigger.trimmed().toLower();
    if (trigger == "once" || trigger == "every" || trigger == "cron") {
        return trigger;
    }
    if (trigger == "scheduled") {
        return "every";
    }
    return "manual";
}

bool automationUsesSchedule(const AutomationRecord &record) {
    return normalizedAutomationScheduleKind(record.scheduleKind, record.trigger) != "manual";
}

qint64 parseAutomationIntervalMs(const QString &rawValue, QString *error) {
    const QString raw = rawValue.trimmed().toLower();
    if (raw.isEmpty()) {
        if (error) {
            *error = "Interval automations require a value, for example 30m or 2h.";
        }
        return -1;
    }

    const QRegularExpression re(R"(^(\d+)\s*(ms|s|m|min|h|d)?$)");
    const QRegularExpressionMatch match = re.match(raw);
    if (!match.hasMatch()) {
        if (error) {
            *error = "Invalid interval. Use values like 15m, 2h, 1d, or a plain number of minutes.";
        }
        return -1;
    }

    bool ok = false;
    const qint64 value = match.captured(1).toLongLong(&ok);
    if (!ok || value <= 0) {
        if (error) {
            *error = "Interval must be a positive number.";
        }
        return -1;
    }

    const QString unit = match.captured(2);
    if (unit == "ms") return value;
    if (unit == "s") return value * 1000;
    if (unit == "h") return value * 60 * 60 * 1000;
    if (unit == "d") return value * 24 * 60 * 60 * 1000;
    return value * 60 * 1000;
}

CronSchedule cronScheduleForAutomation(const AutomationRecord &record, QString *error) {
    CronSchedule schedule;
    const QString scheduleKind = normalizedAutomationScheduleKind(record.scheduleKind, record.trigger);
    if (scheduleKind == "manual") {
        if (error) {
            *error = "Manual automations do not define a schedule.";
        }
        return schedule;
    }

    if (scheduleKind == "once") {
        const QDateTime at = QDateTime::fromString(record.scheduleValue.trimmed(), Qt::ISODate);
        if (!at.isValid()) {
            if (error) {
                *error = "Single-run automations require an ISO datetime like 2026-03-13T09:30:00.";
            }
            return CronSchedule();
        }
        schedule.kind = "at";
        schedule.atMs = at.toMSecsSinceEpoch();
        return schedule;
    }

    if (scheduleKind == "every") {
        schedule.kind = "every";
        schedule.everyMs = parseAutomationIntervalMs(record.scheduleValue, error);
        return schedule;
    }

    if (scheduleKind == "cron") {
        if (record.scheduleValue.trimmed().isEmpty()) {
            if (error) {
                *error = "Cron automations require a cron expression.";
            }
            return CronSchedule();
        }
        schedule.kind = "cron";
        schedule.expr = record.scheduleValue.trimmed();
        schedule.tz = record.timeZone.trimmed();
        return schedule;
    }

    if (error) {
        *error = "Unknown automation schedule kind.";
    }
    return CronSchedule();
}

QString automationSessionKey(const AutomationRecord &record, const QString &overrideSession) {
    return overrideSession.trimmed().isEmpty()
        ? QString("automation:%1").arg(record.id)
        : overrideSession.trimmed();
}

QString renderAutomationPrompt(const AutomationRecord &record,
                               const QString &triggerSource,
                               const QString &workspacePath,
                               int runNumber) {
    const QDateTime now = QDateTime::currentDateTime();
    QString prompt = record.prompt;
    const QHash<QString, QString> values{
        {"automation.id", record.id},
        {"automation.name", record.name},
        {"automation.provider", normalizedAutomationProvider(record.provider)},
        {"automation.model", record.model},
        {"automation.schedule", normalizedAutomationScheduleKind(record.scheduleKind, record.trigger)},
        {"run.source", triggerSource},
        {"run.count", QString::number(runNumber)},
        {"workspace", workspacePath},
        {"today", now.date().toString(Qt::ISODate)},
        {"time", now.time().toString("HH:mm:ss")},
        {"now", now.toString(Qt::ISODate)},
        {"timestamp", QString::number(now.toSecsSinceEpoch())}
    };

    for (auto it = values.begin(); it != values.end(); ++it) {
        prompt.replace(QString("{{%1}}").arg(it.key()), it.value(), Qt::CaseInsensitive);
    }
    return prompt;
}

QPair<QString, QString> pickHeartbeatTarget(const config::Config &cfg,
                                            channels::ChannelManager *channels) {
    const QStringList enabled = channels ? channels->enabledChannels() : QStringList();
    if (!enabled.isEmpty()) {
        session::SessionManager sessions(cfg.workspacePath());
        const QVector<session::SessionSummary> knownSessions = sessions.listSessions();
        for (const session::SessionSummary &entry : knownSessions) {
            const QString key = entry.key.trimmed();
            const int separator = key.indexOf(':');
            if (separator <= 0) {
                continue;
            }

            const QString channel = key.left(separator);
            const QString chatId = key.mid(separator + 1);
            if (channel == "cli" || channel == "system" || channel == "automation" ||
                channel == "subagent" || channel == "cron" || channel == "heartbeat") {
                continue;
            }
            if (enabled.contains(channel) && !chatId.trimmed().isEmpty()) {
                return qMakePair(channel, chatId);
            }
        }
    }

    return qMakePair(QString("cli"), QString("direct"));
}

QString configuredControlPlaneEndpoint(const config::Config &cfg) {
    return cfg.deployment.controlPlaneUrl.trimmed();
}

QString effectiveRegistryEndpoint(const config::Config &cfg) {
    const QString registryEndpoint = cfg.deployment.registryUrl.trimmed();
    if (!registryEndpoint.isEmpty()) {
        return registryEndpoint;
    }
    return configuredControlPlaneEndpoint(cfg);
}

bool shouldUseRemoteControlPlane(const config::Config &cfg) {
    return cfg.usesClusterDeployment() ||
           !configuredControlPlaneEndpoint(cfg).isEmpty() ||
           !cfg.deployment.registryUrl.trimmed().isEmpty();
}

bool probeControlPlaneEndpoint(const QString &endpoint,
                               int timeoutMs = kControlPlaneProbeTimeoutMs) {
    if (endpoint.trimmed().isEmpty()) {
        return false;
    }
    QString error;
    distributed::RemoteControlClient client(endpoint, timeoutMs > 0 ? timeoutMs : kControlPlaneProbeTimeoutMs);
    return client.isReady() && client.ping(&error);
}

QJsonObject fetchControlPlaneHealth(const QString &endpoint,
                                    bool *reachable = nullptr,
                                    int timeoutMs = kControlPlaneProbeTimeoutMs) {
    if (reachable) {
        *reachable = false;
    }
    if (endpoint.trimmed().isEmpty()) {
        return {};
    }

    QString error;
    distributed::RemoteControlClient client(endpoint, timeoutMs > 0 ? timeoutMs : kControlPlaneProbeTimeoutMs);
    if (!client.isReady()) {
        return {};
    }

    QJsonObject response = client.get(QStringLiteral("/health"), &error);
    if (response.isEmpty() || !response.value(QStringLiteral("ok")).toBool(false)) {
        response = client.get(QStringLiteral("/v1/control/health"), &error);
    }
    if (response.isEmpty() || !response.value(QStringLiteral("ok")).toBool(false)) {
        return {};
    }

    if (reachable) {
        *reachable = true;
    }
    return response;
}

bool containsAllTags(const QStringList &nodeTags, const QStringList &requiredTags) {
    for (const QString &tag : requiredTags) {
        if (!nodeTags.contains(tag.trimmed(), Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

bool capabilitySupportsRole(const distributed::NodeCapability &capability, const QString &role) {
    return role.trimmed().isEmpty() || capability.roles.contains(role.trimmed(), Qt::CaseInsensitive);
}

bool capabilitySupportsTool(const distributed::NodeCapability &capability, const QString &tool) {
    return tool.trimmed().isEmpty() || capability.tools.contains(tool.trimmed(), Qt::CaseInsensitive);
}

bool capabilitySupportsChannel(const distributed::NodeCapability &capability, const QString &channel) {
    return channel.trimmed().isEmpty() || capability.channels.contains(channel.trimmed(), Qt::CaseInsensitive);
}

bool capabilitySupportsMemory(const distributed::NodeCapability &capability, const QString &memoryBackend) {
    return memoryBackend.trimmed().isEmpty() ||
           capability.memoryBackends.contains(memoryBackend.trimmed(), Qt::CaseInsensitive);
}

bool nodeMatchesDelegation(const distributed::NodeDescriptor &node,
                           const QString &role,
                           const QStringList &targetTags,
                           const QString &requiredTool,
                           const QString &channel,
                           const QString &memoryBackend) {
    const QString normalizedRole = role.trimmed();
    if (!normalizedRole.isEmpty() &&
        node.role.trimmed().compare(normalizedRole, Qt::CaseInsensitive) != 0) {
        bool anyCapabilityRole = false;
        for (const distributed::NodeCapability &capability : node.capabilities) {
            if (capabilitySupportsRole(capability, normalizedRole)) {
                anyCapabilityRole = true;
                break;
            }
        }
        if (!anyCapabilityRole) {
            return false;
        }
    }

    if (!containsAllTags(node.tags, targetTags)) {
        return false;
    }

    for (const distributed::NodeCapability &capability : node.capabilities) {
        if (capabilitySupportsTool(capability, requiredTool) &&
            capabilitySupportsChannel(capability, channel) &&
            capabilitySupportsMemory(capability, memoryBackend)) {
            return true;
        }
    }
    return false;
}

bool nodeMatchesCluster(const distributed::NodeDescriptor &node, const QString &clusterId) {
    const QString normalizedClusterId = clusterId.trimmed();
    return normalizedClusterId.isEmpty() ||
           node.clusterId.trimmed().isEmpty() ||
           node.clusterId.trimmed().compare(normalizedClusterId, Qt::CaseInsensitive) == 0;
}

constexpr int kNodeEndpointProbeTimeoutMs = 900;
constexpr int kResolveProbeBudget = 12;
constexpr int kPreviewProbeBudget = 16;

distributed::NodeDescriptor withEndpointHealth(distributed::NodeDescriptor node,
                                               bool allowNetwork) {
    const RuntimeServiceEndpointHealth cached =
        runtimeServiceEndpointHealth(node.endpoint, kNodeEndpointProbeTimeoutMs, false);
    RuntimeServiceEndpointHealth health = cached;
    if (allowNetwork && cached.probeSupported && !cached.checked) {
        health = runtimeServiceEndpointHealth(node.endpoint, kNodeEndpointProbeTimeoutMs, true);
    }
    node.endpointProbeSupported = health.probeSupported;
    node.endpointHealthChecked = health.checked;
    node.endpointReachable = health.reachable;
    node.endpointHealthError = health.reachable ? QString() : health.error;
    return node;
}

void annotateEndpointHealth(QList<distributed::NodeDescriptor> *nodes,
                            bool allowNetwork,
                            int maxNetworkProbes) {
    if (!nodes) {
        return;
    }
    int probesUsed = 0;
    for (distributed::NodeDescriptor &node : *nodes) {
        const bool useNetwork = allowNetwork && (maxNetworkProbes < 0 || probesUsed < maxNetworkProbes);
        node = withEndpointHealth(node, useNetwork);
        if (useNetwork && node.endpointProbeSupported && node.endpointHealthChecked) {
            ++probesUsed;
        }
    }
}

int nodeReachabilityRank(const distributed::NodeDescriptor &node) {
    if (!node.endpointProbeSupported || !node.endpointHealthChecked) {
        return 1;
    }
    return node.endpointReachable ? 2 : 0;
}

QString nodeEndpointHealthText(const distributed::NodeDescriptor &node) {
    if (!node.endpointProbeSupported) {
        return QStringLiteral("local-only");
    }
    if (!node.endpointHealthChecked) {
        return QStringLiteral("unchecked");
    }
    return node.endpointReachable ? QStringLiteral("reachable") : QStringLiteral("unreachable");
}

QStringList stringListFromJsonValue(const QJsonValue &value) {
    QStringList out;
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        out.reserve(array.size());
        for (const QJsonValue &item : array) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty()) {
                out.append(text);
            }
        }
        return out;
    }
    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return out;
    }
    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString normalized = part.trimmed();
        if (!normalized.isEmpty()) {
            out.append(normalized);
        }
    }
    return out;
}

void sortDelegationNodes(QList<distributed::NodeDescriptor> *nodes, const QString &localNodeId) {
    if (!nodes) {
        return;
    }
    std::sort(nodes->begin(), nodes->end(), [localNodeId](const distributed::NodeDescriptor &left,
                                                          const distributed::NodeDescriptor &right) {
        const bool leftHasCapacity = nodeHasAvailableCapacity(left);
        const bool rightHasCapacity = nodeHasAvailableCapacity(right);
        if (leftHasCapacity != rightHasCapacity) {
            return leftHasCapacity && !rightHasCapacity;
        }
        const double leftPressure = nodeSchedulingPressure(left);
        const double rightPressure = nodeSchedulingPressure(right);
        if (std::abs(leftPressure - rightPressure) > 0.0001) {
            return leftPressure < rightPressure;
        }
        if (left.queuedTaskCount != right.queuedTaskCount) {
            return left.queuedTaskCount < right.queuedTaskCount;
        }
        const int leftReachability = nodeReachabilityRank(left);
        const int rightReachability = nodeReachabilityRank(right);
        if (leftReachability != rightReachability) {
            return leftReachability > rightReachability;
        }
        const bool leftIsLocal = left.nodeId.trimmed() == localNodeId;
        const bool rightIsLocal = right.nodeId.trimmed() == localNodeId;
        if (leftIsLocal != rightIsLocal) {
            return !leftIsLocal && rightIsLocal;
        }
        if (left.weight != right.weight) {
            return left.weight > right.weight;
        }
        return left.nodeId < right.nodeId;
    });
}

QStringList delegationFailureReasons(const distributed::NodeDescriptor &node,
                                     const QString &clusterId,
                                     const QString &role,
                                     const QStringList &targetTags,
                                     const QString &requiredTool,
                                     const QString &channel,
                                     const QString &memoryBackend,
                                     bool includeOffline) {
    QStringList reasons;
    if (!nodeMatchesCluster(node, clusterId)) {
        reasons.append(QStringLiteral("cluster mismatch"));
    }
    if (!includeOffline && !node.online) {
        reasons.append(QStringLiteral("offline"));
    }
    if (node.endpointProbeSupported && node.endpointHealthChecked && !node.endpointReachable) {
        reasons.append(QStringLiteral("runtime endpoint unreachable"));
    }

    const QString normalizedRole = role.trimmed();
    if (!normalizedRole.isEmpty() &&
        node.role.trimmed().compare(normalizedRole, Qt::CaseInsensitive) != 0) {
        bool anyCapabilityRole = false;
        for (const distributed::NodeCapability &capability : node.capabilities) {
            if (capabilitySupportsRole(capability, normalizedRole)) {
                anyCapabilityRole = true;
                break;
            }
        }
        if (!anyCapabilityRole) {
            reasons.append(QStringLiteral("role mismatch"));
        }
    }

    QStringList missingTags;
    for (const QString &tag : targetTags) {
        if (!node.tags.contains(tag.trimmed(), Qt::CaseInsensitive)) {
            missingTags.append(tag.trimmed());
        }
    }
    if (!missingTags.isEmpty()) {
        reasons.append(QStringLiteral("missing tags %1").arg(missingTags.join(",")));
    }

    if (node.capabilities.isEmpty() &&
        (!requiredTool.trimmed().isEmpty() || !channel.trimmed().isEmpty() || !memoryBackend.trimmed().isEmpty())) {
        reasons.append(QStringLiteral("no capabilities"));
        return reasons;
    }

    bool matchesAnyCapability = false;
    bool hasTool = requiredTool.trimmed().isEmpty();
    bool hasChannel = channel.trimmed().isEmpty();
    bool hasMemory = memoryBackend.trimmed().isEmpty();
    for (const distributed::NodeCapability &capability : node.capabilities) {
        hasTool = hasTool || capabilitySupportsTool(capability, requiredTool);
        hasChannel = hasChannel || capabilitySupportsChannel(capability, channel);
        hasMemory = hasMemory || capabilitySupportsMemory(capability, memoryBackend);
        if (capabilitySupportsTool(capability, requiredTool) &&
            capabilitySupportsChannel(capability, channel) &&
            capabilitySupportsMemory(capability, memoryBackend)) {
            matchesAnyCapability = true;
        }
    }

    if (!matchesAnyCapability) {
        if (!hasTool) {
            reasons.append(QStringLiteral("tool mismatch"));
        }
        if (!hasChannel) {
            reasons.append(QStringLiteral("channel mismatch"));
        }
        if (!hasMemory) {
            reasons.append(QStringLiteral("memory mismatch"));
        }
    }

    return reasons;
}

QJsonObject delegationDiagnosticNode(const distributed::NodeDescriptor &node,
                                     bool matched,
                                     int rank,
                                     const QStringList &reasons,
                                     const QString &localNodeId) {
    QJsonObject item;
    item.insert(QStringLiteral("node"), distributed::json::toJson(node));
    item.insert(QStringLiteral("nodeId"), node.nodeId);
    item.insert(QStringLiteral("matched"), matched);
    item.insert(QStringLiteral("rank"), rank);
    item.insert(QStringLiteral("reasons"), QJsonArray::fromStringList(reasons));
    item.insert(QStringLiteral("reasonText"),
                reasons.isEmpty()
                    ? QStringLiteral("matches current route filters")
                    : reasons.join(QStringLiteral("  |  ")));
    item.insert(QStringLiteral("hasCapacity"), nodeHasAvailableCapacity(node));
    item.insert(QStringLiteral("pressure"), nodeSchedulingPressure(node));
    item.insert(QStringLiteral("queuedTaskCount"), node.queuedTaskCount);
    item.insert(QStringLiteral("weight"), node.weight);
    item.insert(QStringLiteral("endpointStatus"), nodeEndpointHealthText(node));
    item.insert(QStringLiteral("isLocal"),
                !localNodeId.trimmed().isEmpty() &&
                    node.nodeId.trimmed().compare(localNodeId.trimmed(), Qt::CaseInsensitive) == 0);
    return item;
}

bool isDelegatedTask(const distributed::TaskEnvelope &task) {
    return task.taskType.trimmed().compare(QStringLiteral("delegated_subagent"), Qt::CaseInsensitive) == 0;
}

distributed::TaskContextRef makeContextRef(const QString &store,
                                           const QString &key,
                                           const QString &kind,
                                           const QString &summary) {
    distributed::TaskContextRef ref;
    ref.store = store.trimmed();
    ref.key = key.trimmed();
    ref.kind = kind.trimmed();
    ref.summary = summary.trimmed();
    return ref;
}

QList<distributed::TaskContextRef> delegatedContextRefsFor(const config::Config &cfg,
                                                           const QString &taskId,
                                                           const QString &traceId,
                                                           const QString &sessionKey,
                                                           const QString &originChannel,
                                                           const QString &originChatId,
                                                           const QString &targetNode,
                                                           const QString &targetRole,
                                                           const QStringList &targetTags,
                                                           const QString &requiredTool,
                                                           const QString &requiredMemoryBackend) {
    QList<distributed::TaskContextRef> refs;
    refs.append(makeContextRef(QStringLiteral("session"),
                               sessionKey,
                               QStringLiteral("conversation"),
                               QStringLiteral("Origin session for delegated task")));
    refs.append(makeContextRef(QStringLiteral("workspace"),
                               cfg.workspacePath(),
                               QStringLiteral("workspace"),
                               QStringLiteral("Shared workspace path")));
    refs.append(makeContextRef(QStringLiteral("task"),
                               taskId,
                               QStringLiteral("delegated_task"),
                               traceId.trimmed().isEmpty()
                                   ? QStringLiteral("Delegated task record")
                                   : QStringLiteral("Delegated task trace %1").arg(traceId)));
    refs.append(makeContextRef(QStringLiteral("reply"),
                               originChatId.trimmed().isEmpty()
                                   ? originChannel
                                   : QStringLiteral("%1:%2").arg(originChannel, originChatId),
                               QStringLiteral("reply_route"),
                               QStringLiteral("Reply route for async delegated result")));
    const QString memoryBackend = requiredMemoryBackend.trimmed().isEmpty()
        ? cfg.normalizedMemoryBackend().trimmed()
        : requiredMemoryBackend.trimmed();
    if (!memoryBackend.isEmpty()) {
        refs.append(makeContextRef(QStringLiteral("memory"),
                                   memoryBackend,
                                   QStringLiteral("memory_backend"),
                                   QStringLiteral("Memory backend expectation for the delegated worker")));
    }
    if (!targetNode.trimmed().isEmpty()) {
        refs.append(makeContextRef(QStringLiteral("node"),
                                   targetNode,
                                   QStringLiteral("target_node"),
                                   QStringLiteral("Resolved delegation target node")));
    }
    if (!targetRole.trimmed().isEmpty()) {
        refs.append(makeContextRef(QStringLiteral("role"),
                                   targetRole,
                                   QStringLiteral("target_role"),
                                   QStringLiteral("Requested worker role")));
    }
    if (!targetTags.isEmpty()) {
        refs.append(makeContextRef(QStringLiteral("routing"),
                                   targetTags.join(","),
                                   QStringLiteral("target_tags"),
                                   QStringLiteral("Requested worker tags")));
    }
    if (!requiredTool.trimmed().isEmpty()) {
        refs.append(makeContextRef(QStringLiteral("routing"),
                                   requiredTool.trimmed(),
                                   QStringLiteral("required_tool"),
                                   QStringLiteral("Capability required on the delegated worker")));
    }
    return refs;
}

QList<distributed::TaskContextRef> delegatedOutputRefsFor(const config::Config &cfg,
                                                          const QString &taskId,
                                                          const QString &traceId,
                                                          const QString &delegatedSession) {
    QList<distributed::TaskContextRef> refs;
    refs.append(makeContextRef(QStringLiteral("session"),
                               delegatedSession,
                               QStringLiteral("delegated_session"),
                               QStringLiteral("Worker-local delegated session")));
    refs.append(makeContextRef(QStringLiteral("task"),
                               taskId,
                               QStringLiteral("delegated_result"),
                               traceId.trimmed().isEmpty()
                                   ? QStringLiteral("Delegated task result")
                                   : QStringLiteral("Delegated result for trace %1").arg(traceId)));
    refs.append(makeContextRef(QStringLiteral("workspace"),
                               cfg.workspacePath(),
                               QStringLiteral("workspace"),
                               QStringLiteral("Workspace used during delegated execution")));
    return refs;
}

QString renderDelegatedPrompt(const distributed::TaskEnvelope &task) {
    const QString taskText = task.payload.value(QStringLiteral("task")).toString().trimmed();
    if (task.contextRefs.isEmpty()) {
        return taskText;
    }

    QStringList lines;
    lines << QStringLiteral("[Delegation Context]");
    for (const distributed::TaskContextRef &ref : task.contextRefs) {
        QString line = QStringLiteral("- %1:%2 [%3]").arg(ref.store, ref.key, ref.kind);
        if (!ref.summary.trimmed().isEmpty()) {
            line += QStringLiteral(" ") + ref.summary.trimmed();
        }
        lines << line;
    }
    lines << QString();
    lines << QStringLiteral("[Requested Task]");
    lines << taskText;
    return lines.join(QStringLiteral("\n"));
}

bool taskResultIsCancelled(const distributed::TaskResultEnvelope &result) {
    const QString status = result.status.trimmed().toLower();
    return status == QStringLiteral("cancelled") || status == QStringLiteral("canceled");
}

bool taskBusHasCancelledResult(distributed::ITaskBus *taskBus,
                               const QString &taskId) {
    if (!taskBus || taskId.trimmed().isEmpty()) {
        return false;
    }
    const QList<distributed::TaskResultEnvelope> results = taskBus->recentResults(taskId.trimmed(), QString(), 1);
    return !results.isEmpty() && taskResultIsCancelled(results.first());
}

} // namespace

RuntimeCore::RuntimeCore() = default;

RuntimeCore::RuntimeCore(const config::Config &configOverride)
    : _configOverride(configOverride),
      _hasConfigOverride(true) {}

RuntimeCore::~RuntimeCore() {
    teardownRuntime();
}

StatusSnapshot RuntimeCore::statusSnapshot() {
    const config::Config cfg = activeConfig();
    StatusSnapshot snapshot;
    snapshot.configPath = config::ConfigLoader::defaultConfigPath();
    snapshot.configReady = config::ConfigLoader::isLoadable(snapshot.configPath);
    snapshot.workspacePath = cfg.workspacePath();
    snapshot.workspaceReady = QDir(snapshot.workspacePath).exists();
    snapshot.defaultModel = cfg.agentDefaults.model;
    snapshot.restrictToWorkspace = cfg.tools.restrictToWorkspace;
    snapshot.mcpServerCount = cfg.tools.mcpServers.size();
    snapshot.heartbeatEnabled = cfg.gateway.heartbeat.enabled;
    snapshot.heartbeatIntervalS = cfg.gateway.heartbeat.intervalS;
    snapshot.gatewayRunning = _gatewayRunning;
    snapshot.enabledToolCapabilities = enabledToolCapabilities(cfg);
    snapshot.runtimeMode = cfg.normalizedRuntimeMode();
    const RuntimeServiceAvailability runtimeService =
        ensureRuntimeService(cfg, false, kStatusRuntimeProbeTimeoutMs);
    snapshot.runtimeEndpoint = runtimeService.endpoint;
    snapshot.runtimeAdvertiseEndpoint = runtimeAdvertiseEndpoint(cfg);
    snapshot.runtimeServiceEnabled = runtimeService.enabled;
    snapshot.runtimeServiceReachable = runtimeService.reachable;
    snapshot.runtimeServiceAutoSpawn = cfg.runtime.autoSpawnLocalService;

    // Use the already-initialized provider if available, to avoid creating
    // a new provider instance on every status poll.
    if (_initialized && _provider) {
        snapshot.routedProvider = cfg.matchedProviderName(cfg.agentDefaults.model);
        snapshot.actualBackend = actualProviderLabel(*_provider);
        snapshot.backendFallback = _provider->isFallback();
    } else {
        QString routedProvider;
        std::unique_ptr<providers::LLMProvider> probe = providers::ProviderFactory::create(cfg, &routedProvider);
        snapshot.routedProvider = routedProvider;
        snapshot.actualBackend = probe ? actualProviderLabel(*probe) : "unknown";
        snapshot.backendFallback = probe && probe->isFallback();
    }
    snapshot.controlPlaneEndpoint = configuredControlPlaneEndpoint(cfg);
    snapshot.controlPlaneHealth = fetchControlPlaneHealth(snapshot.controlPlaneEndpoint,
                                                          &snapshot.controlPlaneReachable,
                                                          kStatusControlProbeTimeoutMs);
    if (snapshot.controlPlaneHealth.isEmpty() &&
        snapshot.controlPlaneEndpoint.trimmed().isEmpty() && _taskBus) {
        const int queued = _taskBus->pendingTasks(QString(), QString(), 1000).size();
        const int results = _taskBus->recentResults(QString(), QString(), 200).size();
        snapshot.controlPlaneHealth = QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("workspace"), snapshot.workspacePath},
            {QStringLiteral("taskBus"), QJsonObject{
                {QStringLiteral("queuedTaskCount"), queued},
                {QStringLiteral("recentResultCount"), results}
            }}
        };
    }
    snapshot.registryEndpoint = effectiveRegistryEndpoint(cfg);
    snapshot.registryReachable = snapshot.registryEndpoint == snapshot.controlPlaneEndpoint
        ? snapshot.controlPlaneReachable
        : probeControlPlaneEndpoint(snapshot.registryEndpoint, kStatusControlProbeTimeoutMs);
    const memory::MemoryServiceAvailability memoryService =
        memory::ensureMemoryService(cfg, false, kStatusMemoryProbeTimeoutMs);
    snapshot.memoryServiceEndpoint = memoryService.endpoint;
    snapshot.memoryServiceEnabled = memoryService.enabled;
    snapshot.memoryServiceReachable = memoryService.reachable;
    snapshot.memoryServiceAutoSpawn = cfg.memory.service.autoSpawnLocalService;
    snapshot.providerOAuthStatuses = providerOAuthStatuses(cfg);

    if (_initialized && _channels) {
        snapshot.enabledChannels = _channels->enabledChannels();
    } else {
        // Derive enabled channels directly from config to avoid creating a
        // temporary ChannelManager (which initializes network connections).
        QStringList enabled;
        const auto &ch = cfg.channels;
        if (ch.telegram.enabled)  enabled << QStringLiteral("telegram");
        if (ch.slack.enabled)     enabled << QStringLiteral("slack");
        if (ch.whatsapp.enabled)  enabled << QStringLiteral("whatsapp");
        if (ch.feishu.enabled)    enabled << QStringLiteral("feishu");
        if (ch.dingtalk.enabled)  enabled << QStringLiteral("dingtalk");
        if (ch.discord.enabled)   enabled << QStringLiteral("discord");
        if (ch.matrix.enabled)    enabled << QStringLiteral("matrix");
        if (ch.email.enabled)     enabled << QStringLiteral("email");
        if (ch.mochat.enabled)    enabled << QStringLiteral("mochat");
        if (ch.qq.enabled)        enabled << QStringLiteral("qq");
        snapshot.enabledChannels = enabled;
    }

    if (_initialized && _cron) {
        snapshot.cronJobCount = _cron->status().jobs;
    } else {
        CronService statusCron(QDir(snapshot.workspacePath).filePath("cron/jobs.json"));
        snapshot.cronJobCount = statusCron.status().jobs;
    }

    if (_initialized && _approvals && _taskStore && _eventLog &&
        _notifications && _automations && _plugins && _skills) {
        // Use already-loaded in-memory stores — avoids disk I/O on every poll.
        snapshot.taskCount = _taskStore->count();
        snapshot.eventCount = _eventLog->count();
        snapshot.pendingApprovalCount = _approvals->pendingCount();
        snapshot.unreadNotificationCount = _notifications->unreadCount();
        snapshot.automationCount = _automations->count();
        snapshot.pluginCount = _plugins->discover().size();
        snapshot.skillCount = _skills->discover().size();
        snapshot.resourceCount = _resources ? _resources->summary().totalCount : 0;
    } else {
        TaskStore taskStore(snapshot.workspacePath);
        ApprovalStore approvalStore(snapshot.workspacePath);
        AutomationStore automationStore(snapshot.workspacePath);
        EventLog eventLog(snapshot.workspacePath);
        NotificationCenter notificationCenter(snapshot.workspacePath);
        PluginRegistry pluginRegistry(snapshot.workspacePath);
        SkillRegistry skillRegistry(snapshot.workspacePath);
        ResourceCatalog resourceCatalog(snapshot.workspacePath);
        snapshot.taskCount = taskStore.count();
        snapshot.eventCount = eventLog.count();
        snapshot.pendingApprovalCount = approvalStore.pendingCount();
        snapshot.unreadNotificationCount = notificationCenter.unreadCount();
        snapshot.automationCount = automationStore.count();
        snapshot.pluginCount = pluginRegistry.discover().size();
        snapshot.skillCount = skillRegistry.discover().size();
        snapshot.resourceCount = resourceCatalog.summary().totalCount;
    }

    return snapshot;
}

bool RuntimeCore::initializeWorkspace(QString *message) {
    _config = activeConfig();
    const QString configPath = config::ConfigLoader::defaultConfigPath();
    if (!config::ConfigLoader::save(_config)) {
        if (message) {
            *message = QString::fromUtf8("YAOS 初始化失败：无法写入配置文件 %1").arg(configPath);
        }
        return false;
    }

    const QDir workspace(_config.workspacePath());
    if (!workspace.exists() && !QDir().mkpath(workspace.absolutePath())) {
        if (message) {
            *message = QString::fromUtf8("YAOS 初始化失败：无法创建工作空间 %1").arg(workspace.absolutePath());
        }
        return false;
    }
    syncWorkspaceTemplates(_config.workspacePath());
    ensureSystemStores(_config.workspacePath());
    logEvent("info", "system", "Workspace initialized", QJsonObject{
        {"workspace", _config.workspacePath()},
        {"config_path", configPath}
    });
    if (message) {
        *message = QString::fromUtf8("YAOS 初始化完成。\n配置: %1\n工作空间: %2")
                       .arg(configPath, _config.workspacePath());
    }
    return true;
}

bool RuntimeCore::reloadFromDisk(const QString &modelOverride, const QString &providerOverride) {
    const bool restartGateway = _gatewayRunning;
    teardownRuntime();
    if (!ensureInitialized(modelOverride, providerOverride)) {
        logEvent("error", "runtime", "Runtime reload failed");
        return false;
    }
    if (restartGateway) {
        const bool started = startGatewayServices();
        if (started) {
            logEvent("info", "runtime", "Runtime reloaded and gateway restored");
        }
        return started;
    }
    logEvent("info", "runtime", "Runtime reloaded");
    return true;
}

bool RuntimeCore::startGatewayServices() {
    if (!ensureInitialized()) {
        return false;
    }
    if (_gatewayRunning) {
        return true;
    }
    _cron->start();
    _heartbeat->start();
    if (_channels) {
        _channels->startAll();
    }
    _gatewayRunning = true;
    publishNodePresence(true);
    logEvent("info", "gateway", "Gateway services started", QJsonObject{
        {"heartbeat_enabled", _config.gateway.heartbeat.enabled},
        {"heartbeat_interval_s", _config.gateway.heartbeat.intervalS}
    });
    return true;
}

void RuntimeCore::stopGatewayServices() {
    if (_channels) {
        _channels->stopAll();
    }
    if (_heartbeat) {
        _heartbeat->stop();
    }
    if (_cron) {
        _cron->stop();
    }
    if (_gatewayRunning) {
        logEvent("info", "gateway", "Gateway services stopped");
    }
    _gatewayRunning = false;
}

bool RuntimeCore::gatewayRunning() const {
    return _gatewayRunning;
}

QVector<ApprovalRecord> RuntimeCore::recentApprovals(int limit, const QString &state) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    ApprovalStore store(cfg.workspacePath());
    return store.recentApprovals(limit, state);
}

bool RuntimeCore::resolveApproval(const QString &approvalId,
                                  const QString &decision,
                                  const QString &scope,
                                  const QString &note) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    ensureSystemStores(cfg.workspacePath());
    if (!_approvals || !_approvals->resolve(approvalId, decision, scope, note)) {
        return false;
    }
    logEvent("info", "security", "Approval updated", QJsonObject{
        {"approval_id", approvalId},
        {"decision", decision},
        {"scope", scope}
    });
    if (_notifications) {
        _notifications->push(
            "info",
            "Approval updated",
            QString("%1 -> %2 (%3)").arg(approvalId, decision, scope.isEmpty() ? "session" : scope),
            "security",
            approvalId
        );
    }
    return true;
}

QVector<NotificationRecord> RuntimeCore::recentNotifications(int limit, bool unreadOnly) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    NotificationCenter center(cfg.workspacePath());
    return center.recentNotifications(limit, unreadOnly);
}

void RuntimeCore::markAllNotificationsRead() {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    ensureSystemStores(cfg.workspacePath());
    if (_notifications) {
        _notifications->markAllRead();
    }
}

QVector<TaskRecord> RuntimeCore::recentTasks(int limit) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    TaskStore store(cfg.workspacePath());
    return store.recentTasks(limit);
}

QVector<EventRecord> RuntimeCore::recentEvents(int limit) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    EventLog log(cfg.workspacePath());
    return log.recentEvents(limit);
}

QVector<distributed::NodeDescriptor> RuntimeCore::recentNodes(int limit, bool onlineOnly) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    if (_initialized) {
        publishNodePresence(true, 5000, QStringLiteral("recentNodes"));
    }
    QList<distributed::NodeDescriptor> nodes;
    if (_nodeRegistry) {
        nodes = _nodeRegistry->listNodes();
    }

    QVector<distributed::NodeDescriptor> out;
    const int maxItems = limit > 0 ? limit : 64;
    out.reserve(qMin(maxItems, nodes.size()));
    for (const distributed::NodeDescriptor &node : nodes) {
        if (onlineOnly && !node.online) {
            continue;
        }
        if (!cfg.deployment.clusterId.trimmed().isEmpty() &&
            !node.clusterId.trimmed().isEmpty() &&
            node.clusterId.trimmed().compare(cfg.deployment.clusterId.trimmed(), Qt::CaseInsensitive) != 0) {
            continue;
        }
        out.append(node);
        if (out.size() >= maxItems) {
            break;
        }
    }
    QList<distributed::NodeDescriptor> annotated;
    annotated.reserve(out.size());
    for (const distributed::NodeDescriptor &node : out) {
        annotated.append(node);
    }
    annotateEndpointHealth(&annotated, false, 0);
    out.clear();
    out.reserve(annotated.size());
    for (const distributed::NodeDescriptor &node : annotated) {
        out.append(node);
    }
    return out;
}

QJsonObject RuntimeCore::previewDelegationRoute(const QJsonObject &request) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    const QString clusterId = cfg.deployment.clusterId.trimmed();
    const QString targetRole = request.value(QStringLiteral("targetRole"))
                                   .toString(request.value(QStringLiteral("role")).toString())
                                   .trimmed();
    const QStringList targetTags = stringListFromJsonValue(
        request.value(QStringLiteral("targetTags")).isUndefined()
            ? request.value(QStringLiteral("tags"))
            : request.value(QStringLiteral("targetTags")));
    const QString requiredTool = request.value(QStringLiteral("requiredTool"))
                                     .toString(request.value(QStringLiteral("tool")).toString())
                                     .trimmed();
    const QString requiredChannel = request.value(QStringLiteral("requiredChannel"))
                                        .toString(request.value(QStringLiteral("channel")).toString())
                                        .trimmed();
    QString requiredMemoryBackend = request.value(QStringLiteral("requiredMemoryBackend"))
                                        .toString(request.value(QStringLiteral("memoryBackend")).toString())
                                        .trimmed();
    if (requiredMemoryBackend.isEmpty()) {
        requiredMemoryBackend = cfg.normalizedMemoryBackend();
    }
    const bool includeOffline = request.value(QStringLiteral("includeOffline")).toBool(false);

    const QString originChannel = request.value(QStringLiteral("originChannel"))
                                      .toString(QStringLiteral("gui"))
                                      .trimmed()
                                      .isEmpty()
        ? QStringLiteral("gui")
        : request.value(QStringLiteral("originChannel")).toString(QStringLiteral("gui")).trimmed();
    const QString originChatId = request.value(QStringLiteral("originChatId"))
                                     .toString(QStringLiteral("desktop"))
                                     .trimmed()
                                     .isEmpty()
        ? QStringLiteral("desktop")
        : request.value(QStringLiteral("originChatId")).toString(QStringLiteral("desktop")).trimmed();
    const QString sceneKey = request.value(QStringLiteral("sessionKey")).toString().trimmed().isEmpty()
        ? QStringLiteral("%1:%2").arg(originChannel, originChatId)
        : request.value(QStringLiteral("sessionKey")).toString().trimmed();
    const QString taskId = request.value(QStringLiteral("taskId")).toString().trimmed().isEmpty()
        ? QStringLiteral("preview-delegation")
        : request.value(QStringLiteral("taskId")).toString().trimmed();
    const QString traceId = request.value(QStringLiteral("traceId")).toString().trimmed().isEmpty()
        ? QStringLiteral("preview-trace")
        : request.value(QStringLiteral("traceId")).toString().trimmed();
    const QString parentTaskId = request.value(QStringLiteral("parentTaskId")).toString().trimmed();
    const QString label = request.value(QStringLiteral("label")).toString().trimmed().isEmpty()
        ? QStringLiteral("Routing preview")
        : request.value(QStringLiteral("label")).toString().trimmed();
    const QString task = request.value(QStringLiteral("task")).toString().trimmed().isEmpty()
        ? QStringLiteral("Preview delegated task")
        : request.value(QStringLiteral("task")).toString().trimmed();

    if (_initialized) {
        publishNodePresence(true, 5000, QStringLiteral("previewDelegationRoute"));
    }

    QString resolutionSource;
    const QList<distributed::NodeDescriptor> candidates = resolveDelegationTargets(targetRole,
                                                                                   requiredChannel,
                                                                                   targetTags,
                                                                                   requiredTool,
                                                                                   requiredMemoryBackend,
                                                                                   24,
                                                                                   &resolutionSource);

    QList<distributed::NodeDescriptor> nodes;
    if (_nodeRegistry) {
        nodes = _nodeRegistry->listNodes();
    } else {
        const QString registryEndpoint = effectiveRegistryEndpoint(cfg);
        if (!registryEndpoint.isEmpty()) {
            distributed::RemoteNodeRegistryClient registry(registryEndpoint);
            nodes = registry.listNodes();
        }
    }
    if (nodes.isEmpty()) {
        nodes = candidates;
    }
    annotateEndpointHealth(&nodes, true, kPreviewProbeBudget);

    QList<distributed::NodeDescriptor> resolvedCandidates = candidates;
    if (resolvedCandidates.isEmpty()) {
        for (const distributed::NodeDescriptor &node : nodes) {
            if (!delegationFailureReasons(node,
                                          clusterId,
                                          targetRole,
                                          targetTags,
                                          requiredTool,
                                          requiredChannel,
                                          requiredMemoryBackend,
                                          includeOffline)
                     .isEmpty()) {
                continue;
            }
            resolvedCandidates.append(node);
        }
        if (!resolvedCandidates.isEmpty() && resolutionSource.isEmpty()) {
            resolutionSource = configuredControlPlaneEndpoint(cfg).trimmed().isEmpty()
                ? QStringLiteral("local_registry")
                : QStringLiteral("control_plane");
        }
    }

    for (const distributed::NodeDescriptor &candidate : resolvedCandidates) {
        bool replaced = false;
        for (distributed::NodeDescriptor &node : nodes) {
            if (node.nodeId.trimmed().compare(candidate.nodeId.trimmed(), Qt::CaseInsensitive) == 0) {
                node = candidate;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            nodes.append(candidate);
        }
    }

    QHash<QString, int> rankByNodeId;
    for (int index = 0; index < resolvedCandidates.size(); ++index) {
        rankByNodeId.insert(resolvedCandidates.at(index).nodeId.trimmed(), index + 1);
    }

    std::sort(nodes.begin(), nodes.end(), [&](const distributed::NodeDescriptor &left,
                                              const distributed::NodeDescriptor &right) {
        const bool leftMatched = rankByNodeId.contains(left.nodeId.trimmed());
        const bool rightMatched = rankByNodeId.contains(right.nodeId.trimmed());
        if (leftMatched != rightMatched) {
            return leftMatched && !rightMatched;
        }
        if (leftMatched && rightMatched) {
            return rankByNodeId.value(left.nodeId.trimmed()) < rankByNodeId.value(right.nodeId.trimmed());
        }
        const bool leftClusterMatch = nodeMatchesCluster(left, clusterId);
        const bool rightClusterMatch = nodeMatchesCluster(right, clusterId);
        if (leftClusterMatch != rightClusterMatch) {
            return leftClusterMatch && !rightClusterMatch;
        }
        if (left.online != right.online) {
            return left.online && !right.online;
        }
        const int leftReachability = nodeReachabilityRank(left);
        const int rightReachability = nodeReachabilityRank(right);
        if (leftReachability != rightReachability) {
            return leftReachability > rightReachability;
        }
        return left.nodeId < right.nodeId;
    });

    QJsonArray diagnosticNodes;
    const QString localNodeId = effectiveNodeId(cfg);
    for (const distributed::NodeDescriptor &node : nodes) {
        const bool matched = rankByNodeId.contains(node.nodeId.trimmed());
        QStringList reasons = delegationFailureReasons(node,
                                                       clusterId,
                                                       targetRole,
                                                       targetTags,
                                                       requiredTool,
                                                       requiredChannel,
                                                       requiredMemoryBackend,
                                                       includeOffline);
        if (matched) {
            reasons.clear();
        }
        diagnosticNodes.append(delegationDiagnosticNode(node,
                                                        matched,
                                                        rankByNodeId.value(node.nodeId.trimmed(), 0),
                                                        reasons,
                                                        localNodeId));
    }

    QJsonObject preview{
        {QStringLiteral("ok"), true},
        {QStringLiteral("resolved"), !resolvedCandidates.isEmpty()},
        {QStringLiteral("resolutionSource"),
         resolutionSource.isEmpty() ? QStringLiteral("local_registry") : resolutionSource},
        {QStringLiteral("targetRole"), targetRole},
        {QStringLiteral("targetTags"), QJsonArray::fromStringList(targetTags)},
        {QStringLiteral("requiredTool"), requiredTool},
        {QStringLiteral("requiredChannel"), requiredChannel},
        {QStringLiteral("requiredMemoryBackend"), requiredMemoryBackend},
        {QStringLiteral("originChannel"), originChannel},
        {QStringLiteral("originChatId"), originChatId},
        {QStringLiteral("sceneKey"), sceneKey},
        {QStringLiteral("candidateCount"), resolvedCandidates.size()},
        {QStringLiteral("nodes"), diagnosticNodes}
    };

    if (resolvedCandidates.isEmpty()) {
        QStringList requirements;
        if (!targetRole.isEmpty()) requirements << QStringLiteral("role '%1'").arg(targetRole);
        if (!targetTags.isEmpty()) requirements << QStringLiteral("tags [%1]").arg(targetTags.join(QStringLiteral(", ")));
        if (!requiredTool.isEmpty()) requirements << QStringLiteral("tool '%1'").arg(requiredTool);
        if (!requiredChannel.isEmpty()) requirements << QStringLiteral("channel '%1'").arg(requiredChannel);
        if (!requiredMemoryBackend.isEmpty()) requirements << QStringLiteral("memory '%1'").arg(requiredMemoryBackend);
        preview.insert(QStringLiteral("message"),
                       requirements.isEmpty()
                           ? QStringLiteral("No delegation target was resolved.")
                           : QStringLiteral("No online node matches %1.").arg(requirements.join(QStringLiteral(", "))));
        preview.insert(QStringLiteral("routeSummary"), QStringLiteral("unresolved"));
        preview.insert(QStringLiteral("labels"), QJsonArray());
        preview.insert(QStringLiteral("contextRefs"), QJsonArray());
        return preview;
    }

    const distributed::NodeDescriptor selected = resolvedCandidates.first();
    const QString resolvedTargetRole = targetRole.isEmpty() ? selected.role.trimmed() : targetRole;
    const QString resolvedTargetNode = selected.nodeId.trimmed();
    const QString replyTo = originChatId.trimmed().isEmpty()
        ? originChannel
        : QStringLiteral("%1:%2").arg(originChannel, originChatId);

    QStringList labels{
        QStringLiteral("delegated_subagent"),
        originChannel,
        cfg.normalizedDeploymentMode(),
        cfg.normalizedRuntimeMode()
    };
    for (const QString &tag : targetTags) {
        const QString normalizedTag = tag.trimmed();
        if (!normalizedTag.isEmpty()) {
            labels.append(QStringLiteral("tag:%1").arg(normalizedTag));
        }
    }
    if (!requiredTool.isEmpty()) {
        labels.append(QStringLiteral("tool:%1").arg(requiredTool));
    }
    if (!requiredChannel.isEmpty()) {
        labels.append(QStringLiteral("channel:%1").arg(requiredChannel));
    }
    if (!requiredMemoryBackend.isEmpty()) {
        labels.append(QStringLiteral("memory:%1").arg(requiredMemoryBackend));
    }

    const QList<distributed::TaskContextRef> contextRefs = delegatedContextRefsFor(cfg,
                                                                                    taskId,
                                                                                    traceId,
                                                                                    sceneKey,
                                                                                    originChannel,
                                                                                    originChatId,
                                                                                    resolvedTargetNode,
                                                                                    resolvedTargetRole,
                                                                                    targetTags,
                                                                                    requiredTool,
                                                                                    requiredMemoryBackend);

    QStringList routeSummary;
    routeSummary << QStringLiteral("target %1").arg(resolvedTargetNode);
    if (!resolvedTargetRole.isEmpty()) {
        routeSummary << QStringLiteral("role %1").arg(resolvedTargetRole);
    }
    if (!targetTags.isEmpty()) {
        routeSummary << QStringLiteral("tags %1").arg(targetTags.join(QStringLiteral(",")));
    }
    if (!requiredTool.isEmpty()) {
        routeSummary << QStringLiteral("tool %1").arg(requiredTool);
    }
    if (!requiredChannel.isEmpty()) {
        routeSummary << QStringLiteral("channel %1").arg(requiredChannel);
    }
    if (!requiredMemoryBackend.isEmpty()) {
        routeSummary << QStringLiteral("memory %1").arg(requiredMemoryBackend);
    }

    preview.insert(QStringLiteral("message"),
                   QStringLiteral("Suggested target %1 via %2.")
                       .arg(resolvedTargetNode,
                            resolutionSource.isEmpty() ? QStringLiteral("local registry") : resolutionSource));
    preview.insert(QStringLiteral("suggestedNodeId"), resolvedTargetNode);
    preview.insert(QStringLiteral("suggestedRole"), resolvedTargetRole);
    preview.insert(QStringLiteral("suggestedNode"), distributed::json::toJson(selected));
    preview.insert(QStringLiteral("replyTo"), replyTo);
    preview.insert(QStringLiteral("taskTitle"), taskTitleFromContent(task));
    preview.insert(QStringLiteral("label"), label);
    preview.insert(QStringLiteral("parentTaskId"), parentTaskId);
    preview.insert(QStringLiteral("routeSummary"), routeSummary.join(QStringLiteral("  |  ")));
    preview.insert(QStringLiteral("labels"), QJsonArray::fromStringList(labels));
    preview.insert(QStringLiteral("contextRefs"), distributed::json::toJson(contextRefs));
    return preview;
}

QJsonObject RuntimeCore::submitDelegationRequest(const QJsonObject &request) {
    if (!ensureInitialized()) {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Runtime is not initialized.")},
            {QStringLiteral("message"), QStringLiteral("Runtime is not initialized.")}
        };
    }
    if (!_subagents) {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Subagent manager is not initialized.")},
            {QStringLiteral("message"), QStringLiteral("Subagent manager is not initialized.")}
        };
    }
    if (!_config.tools.capabilities.spawn) {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Spawn capability is disabled in the current runtime configuration.")},
            {QStringLiteral("message"), QStringLiteral("Spawn capability is disabled in the current runtime configuration.")}
        };
    }

    const QString originChannel = request.value(QStringLiteral("originChannel"))
                                      .toString(QStringLiteral("gui"))
                                      .trimmed()
                                      .isEmpty()
        ? QStringLiteral("gui")
        : request.value(QStringLiteral("originChannel")).toString(QStringLiteral("gui")).trimmed();
    const QString originChatId = request.value(QStringLiteral("originChatId"))
                                     .toString(QStringLiteral("desktop"))
                                     .trimmed()
                                     .isEmpty()
        ? QStringLiteral("desktop")
        : request.value(QStringLiteral("originChatId")).toString(QStringLiteral("desktop")).trimmed();

    QString sessionKey = request.value(QStringLiteral("sessionKey")).toString().trimmed();
    if (sessionKey.isEmpty() || sessionKey == QStringLiteral("gui:preview")) {
        sessionKey = originChannel == QStringLiteral("gui")
            ? QStringLiteral("gui:primary")
            : QStringLiteral("%1:%2").arg(originChannel, originChatId);
    }

    const QString parentTaskId = request.value(QStringLiteral("parentTaskId")).toString().trimmed();
    const QString traceId = request.value(QStringLiteral("traceId")).toString().trimmed().isEmpty()
        ? newTraceId()
        : request.value(QStringLiteral("traceId")).toString().trimmed();
    const QString targetNode = request.value(QStringLiteral("targetNode")).toString().trimmed();
    const QString targetRole = request.value(QStringLiteral("targetRole"))
                                   .toString(request.value(QStringLiteral("role")).toString())
                                   .trimmed();
    const QStringList targetTags = stringListFromJsonValue(
        request.value(QStringLiteral("targetTags")).isUndefined()
            ? request.value(QStringLiteral("tags"))
            : request.value(QStringLiteral("targetTags")));
    const QString requiredTool = request.value(QStringLiteral("requiredTool"))
                                     .toString(request.value(QStringLiteral("tool")).toString())
                                     .trimmed();
    const QString requiredChannel = request.value(QStringLiteral("requiredChannel"))
                                        .toString(request.value(QStringLiteral("channel")).toString())
                                        .trimmed();
    const QString requiredMemoryBackend = request.value(QStringLiteral("requiredMemoryBackend"))
                                              .toString(request.value(QStringLiteral("memoryBackend")).toString())
                                              .trimmed();

    const auto hasDelegationSelector = [](const SubagentManager::SpawnRequest &item) {
        return !item.targetNode.trimmed().isEmpty() ||
               !item.targetRole.trimmed().isEmpty() ||
               !item.targetTags.isEmpty() ||
               !item.requiredTool.trimmed().isEmpty() ||
               !item.requiredChannel.trimmed().isEmpty() ||
               !item.requiredMemoryBackend.trimmed().isEmpty();
    };

    SubagentManager::SubmitResult submit;
    const QJsonArray tasks = request.value(QStringLiteral("tasks")).toArray();
    if (!tasks.isEmpty()) {
        QList<SubagentManager::SpawnRequest> items;
        items.reserve(tasks.size());
        bool anyDelegated = !targetNode.isEmpty() ||
                            !targetRole.isEmpty() ||
                            !targetTags.isEmpty() ||
                            !requiredTool.isEmpty() ||
                            !requiredChannel.isEmpty() ||
                            !requiredMemoryBackend.isEmpty();
        for (const QJsonValue &value : tasks) {
            if (!value.isObject()) {
                continue;
            }
            const QJsonObject obj = value.toObject();
            SubagentManager::SpawnRequest item;
            item.task = obj.value(QStringLiteral("task")).toString().trimmed();
            item.label = obj.value(QStringLiteral("label")).toString().trimmed();
            item.targetNode = obj.value(QStringLiteral("targetNode"))
                                  .toString(obj.value(QStringLiteral("target_node")).toString())
                                  .trimmed();
            item.targetRole = obj.value(QStringLiteral("targetRole"))
                                  .toString(obj.value(QStringLiteral("target_role")).toString())
                                  .trimmed();
            item.targetTags = stringListFromJsonValue(
                obj.value(QStringLiteral("targetTags")).isUndefined()
                    ? obj.value(QStringLiteral("target_tags"))
                    : obj.value(QStringLiteral("targetTags")));
            item.requiredTool = obj.value(QStringLiteral("requiredTool"))
                                    .toString(obj.value(QStringLiteral("required_tool")).toString())
                                    .trimmed();
            item.requiredChannel = obj.value(QStringLiteral("requiredChannel"))
                                       .toString(obj.value(QStringLiteral("required_channel")).toString())
                                       .trimmed();
            item.requiredMemoryBackend = obj.value(QStringLiteral("requiredMemoryBackend"))
                                             .toString(obj.value(QStringLiteral("required_memory_backend")).toString())
                                             .trimmed();
            if (item.task.isEmpty()) {
                continue;
            }
            anyDelegated = anyDelegated || hasDelegationSelector(item);
            items.append(item);
        }

        if (items.isEmpty()) {
            return QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("At least one batch task is required.")},
                {QStringLiteral("message"), QStringLiteral("At least one batch task is required.")}
            };
        }
        if (!anyDelegated) {
            return QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Delegation drafts must keep at least one route selector such as target node, role, tags, tool, channel, or memory backend.")},
                {QStringLiteral("message"), QStringLiteral("Delegation drafts must keep at least one route selector such as target node, role, tags, tool, channel, or memory backend.")}
            };
        }

        const QString groupLabel = request.value(QStringLiteral("groupLabel"))
                                       .toString(request.value(QStringLiteral("group_label")).toString())
                                       .trimmed();
        submit = _subagents->submitMany(items,
                                        groupLabel,
                                        originChannel,
                                        originChatId,
                                        sessionKey,
                                        parentTaskId,
                                        traceId,
                                        targetNode,
                                        targetRole,
                                        targetTags,
                                        requiredTool,
                                        requiredChannel,
                                        requiredMemoryBackend);
    } else {
        const QString task = request.value(QStringLiteral("task")).toString().trimmed();
        const QString label = request.value(QStringLiteral("label")).toString().trimmed();
        if (task.isEmpty()) {
            return QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Delegation task is required.")},
                {QStringLiteral("message"), QStringLiteral("Delegation task is required.")}
            };
        }

        SubagentManager::SpawnRequest item;
        item.task = task;
        item.label = label;
        item.targetNode = targetNode;
        item.targetRole = targetRole;
        item.targetTags = targetTags;
        item.requiredTool = requiredTool;
        item.requiredChannel = requiredChannel;
        item.requiredMemoryBackend = requiredMemoryBackend;
        if (!hasDelegationSelector(item)) {
            return QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Delegation drafts must keep at least one route selector such as target node, role, tags, tool, channel, or memory backend.")},
                {QStringLiteral("message"), QStringLiteral("Delegation drafts must keep at least one route selector such as target node, role, tags, tool, channel, or memory backend.")}
            };
        }

        submit = _subagents->submit(task,
                                    label,
                                    originChannel,
                                    originChatId,
                                    sessionKey,
                                    targetNode,
                                    targetRole,
                                    parentTaskId,
                                    traceId,
                                    targetTags,
                                    requiredTool,
                                    requiredChannel,
                                    requiredMemoryBackend);
    }

    QJsonArray taskIds;
    QJsonArray startedItems;
    for (const SubagentManager::SubmitItem &item : submit.started) {
        taskIds.append(item.taskId);
        startedItems.append(QJsonObject{
            {QStringLiteral("taskId"), item.taskId},
            {QStringLiteral("task"), item.task},
            {QStringLiteral("label"), item.label},
            {QStringLiteral("delegated"), item.delegated},
            {QStringLiteral("targetNode"), item.targetNode},
            {QStringLiteral("targetRole"), item.targetRole},
            {QStringLiteral("targetTags"), QJsonArray::fromStringList(item.targetTags)},
            {QStringLiteral("requiredTool"), item.requiredTool},
            {QStringLiteral("requiredChannel"), item.requiredChannel},
            {QStringLiteral("requiredMemoryBackend"), item.requiredMemoryBackend}
        });
    }

    if (submit.ok) {
        logEvent(QStringLiteral("info"),
                 QStringLiteral("cluster"),
                 submit.grouped
                     ? QStringLiteral("Delegation batch submitted from runtime draft")
                     : QStringLiteral("Delegation request submitted from runtime draft"),
                 QJsonObject{
                     {QStringLiteral("grouped"), submit.grouped},
                     {QStringLiteral("group_id"), submit.groupId},
                     {QStringLiteral("group_label"), submit.groupLabel},
                     {QStringLiteral("trace_id"), traceId},
                     {QStringLiteral("session_key"), sessionKey},
                     {QStringLiteral("origin_channel"), originChannel},
                     {QStringLiteral("origin_chat_id"), originChatId},
                     {QStringLiteral("target_node"), targetNode},
                     {QStringLiteral("target_role"), targetRole},
                     {QStringLiteral("target_tags"), QJsonArray::fromStringList(targetTags)},
                     {QStringLiteral("required_tool"), requiredTool},
                     {QStringLiteral("required_channel"), requiredChannel},
                     {QStringLiteral("required_memory_backend"), requiredMemoryBackend},
                     {QStringLiteral("submitted_count"), submit.started.size()},
                     {QStringLiteral("failure_count"), submit.failures.size()}
                 });
    }

    if (!submit.started.isEmpty() && _delegationPoller) {
        QMetaObject::invokeMethod(_delegationPoller.get(), [this]() {
            if (_delegationPoller) {
                _delegationPoller->setInterval(kDelegationActivePollIntervalMs);
                _delegationPoller->start();
                pollDelegatedTaskBus();
            }
        }, Qt::QueuedConnection);
    }

    return QJsonObject{
        {QStringLiteral("ok"), submit.ok},
        {QStringLiteral("grouped"), submit.grouped},
        {QStringLiteral("message"), submit.message},
        {QStringLiteral("error"), submit.ok ? QString() : submit.message},
        {QStringLiteral("groupId"), submit.groupId},
        {QStringLiteral("groupLabel"), submit.groupLabel},
        {QStringLiteral("sessionKey"), sessionKey},
        {QStringLiteral("traceId"), traceId},
        {QStringLiteral("submittedCount"), submit.started.size()},
        {QStringLiteral("failedCount"), submit.failures.size()},
        {QStringLiteral("taskIds"), taskIds},
        {QStringLiteral("tasks"), startedItems},
        {QStringLiteral("failures"), QJsonArray::fromStringList(submit.failures)}
    };
}

ResourceSummary RuntimeCore::resourceSummary() {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    ResourceCatalog catalog(cfg.workspacePath());
    return catalog.summary();
}

QVector<ResourceRecord> RuntimeCore::recentResources(int limit, const QString &kind) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    ResourceCatalog catalog(cfg.workspacePath());
    return catalog.recentResources(limit, kind);
}

QVector<AutomationRecord> RuntimeCore::automations(int limit) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    AutomationStore store(cfg.workspacePath());
    return hydrateAutomationRecords(store.list(limit));
}

QVector<AutomationRunRecord> RuntimeCore::automationRuns(int limit, const QString &automationId) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    AutomationRunStore store(cfg.workspacePath());
    return store.list(limit, automationId);
}

AutomationRecord RuntimeCore::automation(const QString &id) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    AutomationStore store(cfg.workspacePath());
    return hydrateAutomationRecord(store.get(id));
}

QString RuntimeCore::saveAutomation(const AutomationRecord &record, QString *error) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    ensureSystemStores(cfg.workspacePath());
    if (!_automations) {
        if (error) {
            *error = "Automation store is not available.";
        }
        return QString();
    }
    AutomationRecord next = record;
    AutomationRecord existing;
    if (!record.id.trimmed().isEmpty()) {
        existing = _automations->get(record.id);
        if (!existing.id.isEmpty()) {
            next.cronJobId = existing.cronJobId;
            next.nextRunAt = existing.nextRunAt;
            next.lastRunAt = existing.lastRunAt;
            next.lastStatus = existing.lastStatus;
            next.lastError = existing.lastError;
            next.lastResultPreview = existing.lastResultPreview;
            next.runCount = existing.runCount;
            if (next.metadata.isEmpty()) {
                next.metadata = existing.metadata;
            }
        }
    }

    next.provider = normalizedAutomationProvider(next.provider);
    next.scheduleKind = normalizedAutomationScheduleKind(next.scheduleKind, next.trigger);
    next.trigger = next.scheduleKind;
    if (next.scheduleKind == "manual") {
        next.scheduleValue.clear();
        next.timeZone.clear();
    } else {
        QString scheduleError;
        const CronSchedule schedule = cronScheduleForAutomation(next, &scheduleError);
        Q_UNUSED(schedule);
        if (!scheduleError.isEmpty()) {
            if (error) {
                *error = scheduleError;
            }
            return QString();
        }
    }

    const QString id = _automations->save(next, error);
    if (id.isEmpty()) {
        return QString();
    }
    next = _automations->get(id);
    QString scheduleError;
    if (!syncAutomationSchedule(next, &scheduleError)) {
        if (!existing.id.isEmpty()) {
            AutomationRecord restore = existing;
            QString restoreError;
            syncAutomationSchedule(restore, &restoreError);
            _automations->save(restore, nullptr);
        } else {
            _automations->remove(id);
        }
        if (error) {
            *error = scheduleError;
        }
        return QString();
    }
    _automations->save(next, nullptr);
    if (!id.isEmpty()) {
        logEvent("info", "automation", "Automation saved", QJsonObject{
            {"automation_id", id},
            {"name", next.name},
            {"schedule_kind", next.scheduleKind},
            {"provider", next.provider}
        });
    }
    return id;
}

bool RuntimeCore::removeAutomation(const QString &id) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    ensureSystemStores(cfg.workspacePath());
    if (!_automations) {
        return false;
    }

    const AutomationRecord record = _automations->get(id);
    if (!record.cronJobId.trimmed().isEmpty()) {
        CronService localCron(QDir(cfg.workspacePath()).filePath("cron/jobs.json"));
        CronService *cron = _cron ? _cron.get() : &localCron;
        cron->removeJob(record.cronJobId.trimmed());
    }
    if (_automationRuns) {
        _automationRuns->removeForAutomation(id);
    }
    if (!_automations->remove(id)) {
        return false;
    }
    logEvent("info", "automation", "Automation removed", QJsonObject{{"automation_id", id}});
    return true;
}

QString RuntimeCore::runAutomation(const QString &id, QString *error, const QString &sessionKey) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    ensureSystemStores(cfg.workspacePath());
    if (!_automations) {
        if (error) {
            *error = "Automation store is not available.";
        }
        return QString();
    }

    const AutomationRecord record = _automations->get(id);
    if (record.id.isEmpty()) {
        if (error) {
            *error = "Automation not found.";
        }
        return QString();
    }
    if (!record.enabled) {
        if (error) {
            *error = "Automation is disabled.";
        }
        return QString();
    }

    const ChatTurnResult turn = executeAutomationRecord(record, "manual", sessionKey);
    if (turn.error && error) {
        *error = turn.content;
    }
    return turn.content;
}

QVector<PluginRecord> RuntimeCore::plugins() {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    PluginRegistry registry(cfg.workspacePath());
    return registry.discover();
}

QVector<SkillRecord> RuntimeCore::skills() {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    SkillRegistry registry(cfg.workspacePath());
    return registry.discover();
}

ChatTurnResult RuntimeCore::executeAutomationRecord(const AutomationRecord &input,
                                                    const QString &triggerSource,
                                                    const QString &sessionKey,
                                                    const QString &cronJobId) {
    ChatTurnResult turn;
    AutomationRecord record = hydrateAutomationRecord(input);
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    ensureSystemStores(cfg.workspacePath());

    const QString providerOverride = normalizedAutomationProvider(record.provider);
    const QString modelOverride = record.model.trimmed();
    const QString effectiveSession = automationSessionKey(record, sessionKey);
    const int nextRunCount = _automationRuns ? (_automationRuns->count(record.id) + 1) : 1;
    const QString prompt = renderAutomationPrompt(record, triggerSource, cfg.workspacePath(), nextRunCount);

    AutomationRunRecord run;
    run.automationId = record.id;
    run.automationName = record.name;
    run.triggerSource = triggerSource;
    run.sessionKey = effectiveSession;
    run.provider = providerOverride;
    run.model = modelOverride;
    run.promptPreview = automationPreview(prompt, 240);
    run.createdAt = QDateTime::currentDateTime();
    run.metadata = QJsonObject{
        {"cron_job_id", cronJobId},
        {"schedule_kind", record.scheduleKind},
        {"workspace", cfg.workspacePath()}
    };

    if (!record.enabled) {
        turn.content = "Error: automation is disabled.";
        turn.error = true;
    } else if (!ensureModelReady(modelOverride, providerOverride)) {
        turn.content = "Error: failed to initialize automation runtime";
        turn.error = true;
    } else {
        QPointer<agent::AgentLoop> cronAgent;
        bool prevCron = false;
        if (triggerSource == "scheduled" && _agent) {
            cronAgent = _agent.get();
            if (cronAgent) {
                QMetaObject::invokeMethod(cronAgent.data(),
                    [cronAgent, &prevCron]() {
                        if (cronAgent) {
                            prevCron = cronAgent->setCronExecutionContext(true);
                        }
                    },
                    Qt::BlockingQueuedConnection);
            }
        }

        turn = processMessageDetailed(prompt,
                                      effectiveSession,
                                      "automation",
                                      record.id,
                                      modelOverride,
                                      providerOverride);

        if (cronAgent) {
            QMetaObject::invokeMethod(cronAgent.data(),
                [cronAgent, prevCron]() {
                    if (cronAgent) {
                        cronAgent->setCronExecutionContext(prevCron);
                    }
                },
                Qt::BlockingQueuedConnection);
        }
    }

    run.finishedAt = QDateTime::currentDateTime();
    run.provider = turn.provider.isEmpty() ? run.provider : turn.provider;
    run.model = turn.model.isEmpty() ? run.model : turn.model;
    run.result = turn.content;
    run.resultPreview = automationPreview(turn.content, 320);
    run.status = turn.error ? "error" : "ok";
    if (turn.error) {
        run.error = turn.content;
    }
    if (_automationRuns) {
        _automationRuns->append(run);
    }

    if (_automations) {
        AutomationRecord stored = _automations->get(record.id);
        if (!stored.id.isEmpty()) {
            stored.lastRunAt = run.finishedAt;
            stored.lastStatus = run.status;
            stored.lastError = run.error;
            stored.lastResultPreview = run.resultPreview;
            stored.runCount = _automationRuns ? _automationRuns->count(record.id) : (stored.runCount + 1);
            _automations->save(stored, nullptr);
        }
    }

    logEvent(turn.error ? "error" : "info",
             "automation",
             "Automation executed",
             QJsonObject{
                 {"automation_id", record.id},
                 {"name", record.name},
                 {"trigger_source", triggerSource},
                 {"cron_job_id", cronJobId},
                 {"provider", run.provider},
                 {"model", run.model},
                 {"session_key", effectiveSession}
             });

    return turn;
}

void RuntimeCore::setStreamProgressCallback(StreamProgressCallback cb) {
    if (_agent) {
        QMetaObject::invokeMethod(_agent.get(), [this, cb]() {
            if (_agent) {
                _agent->setStreamProgressCallback(cb);
            }
        }, Qt::QueuedConnection);
    }
}

ChatTurnResult RuntimeCore::processMessageDetailed(const QString &content,
                                                   const QString &sessionKey,
                                                   const QString &channel,
                                                   const QString &chatId,
                                                   const QString &modelOverride,
                                                   const QString &providerOverride) {
    ChatTurnResult turn;
    if (content.trimmed().isEmpty()) {
        return turn;
    }

    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    const QString normalizedExplicitProvider = normalizedProviderOverride(providerOverride);
    const bool hasExplicitProvider = !normalizedExplicitProvider.isEmpty() && normalizedExplicitProvider != "auto";
    const bool hasExplicitModel = !modelOverride.trimmed().isEmpty();
    const bool hasExplicitOverrides = hasExplicitModel || hasExplicitProvider;
    const SkillTurnOverrides skillOverrides = hasExplicitOverrides
        ? SkillTurnOverrides()
        : resolveSkillTurnOverrides(cfg, cfg.workspacePath(), content);
    const QString effectiveModelOverride = hasExplicitModel ? modelOverride.trimmed() : skillOverrides.model;
    const QString effectiveProviderOverride = hasExplicitProvider
        ? normalizedExplicitProvider
        : skillOverrides.provider;
    const bool hasTurnOverrides = !effectiveModelOverride.isEmpty() || !effectiveProviderOverride.isEmpty();
    ensureSystemStores(cfg.workspacePath());
    const QDateTime startedAt = QDateTime::currentDateTime();
    const QString traceId = StructuredLog::ensureTraceId();
    const ScopedTraceContext traceScope(traceId);
    const QString originNode = effectiveNodeId(cfg);
    const QString targetNode = originNode;
    const QJsonObject taskMetadata{
        {"chat_id", chatId},
        {"trace_id", traceId},
        {"origin_node", originNode},
        {"target_node", targetNode},
        {"target_role", cfg.deployment.nodeRole},
        {"runtime_mode", cfg.normalizedRuntimeMode()},
        {"deployment_mode", cfg.normalizedDeploymentMode()}
    };
    const QString taskId = _taskStore
        ? _taskStore->createTask("agent_turn", taskTitleFromContent(content), sessionKey, channel,
                                 taskMetadata)
        : QString();
    turn.taskId = taskId;
    turn.traceId = traceId;
    if (!taskId.isEmpty() && _taskBus) {
        distributed::TaskEnvelope task;
        task.taskId = taskId;
        task.traceId = traceId;
        task.originNode = originNode;
        task.targetNode = targetNode;
        task.targetRole = cfg.deployment.nodeRole;
        task.sceneKey = sessionKey;
        task.taskType = "agent_turn";
        task.replyTo = chatId.trimmed().isEmpty()
            ? channel
            : QString("%1:%2").arg(channel, chatId);
        task.labels = QStringList{
            channel,
            cfg.normalizedRuntimeMode(),
            cfg.normalizedDeploymentMode()
        };
        task.payload = QJsonObject{
            {"title", taskTitleFromContent(content)},
            {"chatId", chatId},
            {"channel", channel},
            {"sessionKey", sessionKey}
        };
        if (!_taskBus->submit(task)) {
            logEvent("warning", "cluster", "Local task bus submit failed", QJsonObject{
                {"task_id", taskId},
                {"trace_id", traceId}
            });
        }
    }
    if (!taskId.isEmpty() && _taskStore) {
        _taskStore->markRunning(taskId);
    }
    logEvent("info", "task", "Task started", QJsonObject{
        {"task_id", taskId},
        {"trace_id", traceId},
        {"origin_node", originNode},
        {"target_node", targetNode},
        {"session_key", sessionKey},
        {"channel", channel}
    });
    if (!ensureModelReady(effectiveModelOverride, effectiveProviderOverride)) {
        if (!taskId.isEmpty() && _taskStore) {
            _taskStore->markFailed(taskId, "failed to initialize agent runtime");
        }
        if (!taskId.isEmpty() && _taskBus) {
            ChatTurnResult failedTurn;
            failedTurn.taskId = taskId;
            failedTurn.traceId = traceId;
            failedTurn.content = "Error: failed to initialize agent runtime";
            failedTurn.error = true;
            failedTurn.model = effectiveModelOverride.isEmpty() ? cfg.agentDefaults.model : effectiveModelOverride;
            failedTurn.provider = effectiveProviderOverride;
            _taskBus->publishResult(taskResultEnvelopeForTurn(failedTurn,
                                                              taskId,
                                                              traceId,
                                                              originNode,
                                                              sessionKey,
                                                              channel));
        }
        logEvent("error", "task", "Task failed during runtime initialization", QJsonObject{
            {"task_id", taskId},
            {"trace_id", traceId},
            {"origin_node", originNode}
        });
        turn.content = "Error: failed to initialize agent runtime";
        turn.error = true;
    } else {
        turn.model = effectiveModelOverride.isEmpty() ? _config.agentDefaults.model : effectiveModelOverride;
        turn.provider = hasTurnOverrides
            ? resolvedProviderForTurn(cfg, effectiveModelOverride, effectiveProviderOverride)
            : (_provider ? actualProviderLabel(*_provider) : QString());

        if (!skillOverrides.activeSkills.isEmpty()) {
            QStringList skillIds;
            for (const SkillMatch &skill : skillOverrides.activeSkills) {
                skillIds.append(skill.record.id);
            }
            logEvent("info", "skill", "Skill context activated", QJsonObject{
                {"task_id", taskId},
                {"trace_id", traceId},
                {"session_key", sessionKey},
                {"skills", QJsonArray::fromStringList(skillIds)}
            });
        }

        const agent::AgentTurnResult agentResult = invokeProcessDirectDetailed(content,
                                                     sessionKey,
                                                     channel,
                                                     chatId,
                                                     effectiveModelOverride,
                                                     effectiveProviderOverride,
                                                     QJsonObject{
                                                         {"task_id", taskId},
                                                         {"trace_id", traceId}
                                                     });
        turn.content = agentResult.content;
        turn.thinking = agentResult.thinking;
        turn.error = agentResult.content.startsWith("Error:", Qt::CaseInsensitive);
        if (!taskId.isEmpty() && _taskStore) {
            if (turn.error) {
                _taskStore->markFailed(taskId, agentResult.content, taskTitleFromContent(content));
            } else {
                _taskStore->markCompleted(taskId, agentResult.content, taskTitleFromContent(content));
            }
        }
        if (!taskId.isEmpty() && _taskBus) {
            _taskBus->publishResult(taskResultEnvelopeForTurn(turn,
                                                              taskId,
                                                              traceId,
                                                              originNode,
                                                              sessionKey,
                                                              channel));
        }
        logEvent(turn.error ? "error" : "info",
                 "task",
                 turn.error ? "Task failed" : "Task completed",
                 QJsonObject{
                     {"task_id", taskId},
                     {"trace_id", traceId},
                     {"origin_node", originNode},
                     {"target_node", targetNode},
                     {"session_key", sessionKey},
                     {"channel", channel}
                 });
        if (_notifications && turn.error) {
            _notifications->push(
                "error",
                "Task failed",
                taskTitleFromContent(content),
                "task",
                taskId,
                QJsonObject{{"session_key", sessionKey}, {"channel", channel}}
            );
        }
    }

    if (_eventLog) {
        const QVector<EventRecord> recent = _eventLog->recentEvents(80);
        for (const EventRecord &event : recent) {
            if (eventBelongsToTurn(event, taskId, sessionKey, channel, startedAt)) {
                turn.trace.append(event);
            }
        }
    }

    if (turn.model.isEmpty()) {
        turn.model = effectiveModelOverride.isEmpty() ? cfg.agentDefaults.model : effectiveModelOverride;
    }
    if (turn.provider.isEmpty() && _provider) {
        turn.provider = hasTurnOverrides
            ? resolvedProviderForTurn(cfg, effectiveModelOverride, effectiveProviderOverride)
            : actualProviderLabel(*_provider);
    }
    return turn;
}

QString RuntimeCore::processMessage(const QString &content,
                                    const QString &sessionKey,
                                    const QString &channel,
                                    const QString &chatId,
                                    const QString &modelOverride,
                                    const QString &providerOverride) {
    return processMessageDetailed(content, sessionKey, channel, chatId, modelOverride, providerOverride).content;
}

QString RuntimeCore::actualProviderLabel(const providers::LLMProvider &provider) {
    QString label = provider.backendName().trimmed();
    if (label.isEmpty()) {
        label = "unknown";
    }
    return label;
}

QString RuntimeCore::invokeProcessDirect(const QString &content,
                                         const QString &sessionKey,
                                         const QString &channel,
                                         const QString &chatId,
                                         const QString &modelOverride,
                                         const QString &providerOverride,
                                         const QJsonObject &runtimeMetadata) {
    if (!_agent) return "Error: agent is not initialized";

    if (QThread::currentThread() == _agent->thread()) {
        return _agent->processDirect(content,
                                     sessionKey,
                                     channel,
                                     chatId,
                                     modelOverride,
                                     providerOverride,
                                     runtimeMetadata);
    }

    QString result;
    const bool invoked = QMetaObject::invokeMethod(
        _agent.get(),
        [this, &result, content, sessionKey, channel, chatId, modelOverride, providerOverride, runtimeMetadata]() {
            if (_agent) {
                result = _agent->processDirect(content,
                                               sessionKey,
                                               channel,
                                               chatId,
                                               modelOverride,
                                               providerOverride,
                                               runtimeMetadata);
            }
        },
        Qt::BlockingQueuedConnection
    );
    if (!invoked && result.isEmpty()) {
        return "Error: failed to dispatch to agent thread";
    }
    return result;
}

agent::AgentTurnResult RuntimeCore::invokeProcessDirectDetailed(const QString &content,
                                                                const QString &sessionKey,
                                                                const QString &channel,
                                                                const QString &chatId,
                                                                const QString &modelOverride,
                                                                const QString &providerOverride,
                                                                const QJsonObject &runtimeMetadata) {
    if (!_agent) {
        agent::AgentTurnResult err;
        err.content = "Error: agent is not initialized";
        return err;
    }

    if (QThread::currentThread() == _agent->thread()) {
        return _agent->processDirectDetailed(content,
                                             sessionKey,
                                             channel,
                                             chatId,
                                             modelOverride,
                                             providerOverride,
                                             runtimeMetadata);
    }

    agent::AgentTurnResult result;
    const bool invoked = QMetaObject::invokeMethod(
        _agent.get(),
        [this, &result, content, sessionKey, channel, chatId, modelOverride, providerOverride, runtimeMetadata]() {
            if (_agent) {
                result = _agent->processDirectDetailed(content,
                                                       sessionKey,
                                                       channel,
                                                       chatId,
                                                       modelOverride,
                                                       providerOverride,
                                                       runtimeMetadata);
            }
        },
        Qt::BlockingQueuedConnection
    );
    if (!invoked && result.content.isEmpty()) {
        result.content = "Error: failed to dispatch to agent thread";
    }
    return result;
}

bool RuntimeCore::ensureInitialized(const QString &modelOverride, const QString &providerOverride) {
    if (_initialized) return true;

    _config = _hasConfigOverride ? _configOverride : config::ConfigLoader::load();
    if (!modelOverride.trimmed().isEmpty()) {
        _config.agentDefaults.model = modelOverride.trimmed();
    }
    if (!providerOverride.trimmed().isEmpty()) {
        _config.agentDefaults.provider = normalizedProviderOverride(providerOverride);
    }
    syncWorkspaceTemplates(_config.workspacePath());
    ensureSystemStores(_config.workspacePath());

    _bus = std::make_unique<bus::MessageBus>();
    const QString controlPlaneEndpoint = configuredControlPlaneEndpoint(_config);
    const QString registryEndpoint = effectiveRegistryEndpoint(_config);
    const bool remoteControlRequested = shouldUseRemoteControlPlane(_config);
    bool remoteRegistryActive = false;
    bool remoteTaskBusActive = false;
    std::unique_ptr<::yaos::distributed::RemoteNodeRegistryClient> remoteRegistryClient;
    std::unique_ptr<::yaos::distributed::RemoteTaskBus> remoteTaskBusClient;

    if (!registryEndpoint.isEmpty()) {
        auto remoteRegistry = std::make_unique<::yaos::distributed::RemoteNodeRegistryClient>(
            registryEndpoint,
            kControlPlaneProbeTimeoutMs);
        QString error;
        if (remoteRegistry->isReady() && remoteRegistry->ping(&error)) {
            remoteRegistryClient = std::move(remoteRegistry);
            remoteRegistryActive = true;
        } else {
            qWarning(lcRuntime) << "Registry endpoint unavailable, using local node registry"
                                << registryEndpoint << error;
        }
    }
    if (!controlPlaneEndpoint.isEmpty()) {
        auto remoteTaskBus = std::make_unique<::yaos::distributed::RemoteTaskBus>(
            controlPlaneEndpoint,
            kControlPlaneProbeTimeoutMs);
        QString error;
        if (remoteTaskBus->isReady() && remoteTaskBus->ping(&error)) {
            remoteTaskBusClient = std::move(remoteTaskBus);
            remoteTaskBusActive = true;
        } else {
            qWarning(lcRuntime) << "Control-plane endpoint unavailable, using local task bus"
                                << controlPlaneEndpoint << error;
        }
    }
    if (!remoteRegistryActive || !remoteTaskBusActive) {
        auto *p2p = new ::yaos::distributed::P2PCluster(
            localNodeDescriptor(_config, configuredChannels(_config), true, 0, 0, kDefaultNodeConcurrency));
        p2p->start();
        _p2pClusterOwner.reset(p2p);
        // Proxy wrappers take a no-op-deleter shared_ptr so the QObject lifetime
        // remains under QScopedPointer (_p2pClusterOwner), not shared_ptr.
        auto sharedNoDelete = QSharedPointer<::yaos::distributed::P2PCluster>(p2p, [](auto*){});
        _nodeRegistry = std::make_unique<::yaos::distributed::P2PRegistryProxy>(sharedNoDelete);
        _taskBus      = std::make_unique<::yaos::distributed::P2PTaskBusProxy>(sharedNoDelete);
    }
    if (remoteTaskBusClient) {
        _taskBus = std::move(remoteTaskBusClient);
    }
    if (remoteTaskBusActive && remoteRegistryClient) {
        _nodeRegistry = std::move(remoteRegistryClient);
    } else if (remoteRegistryClient && !remoteTaskBusActive) {
        qWarning(lcRuntime) << "Remote registry reachable but remote task bus unavailable;"
                            << "keeping local registry to avoid split delegation transport"
                            << registryEndpoint << controlPlaneEndpoint;
    }
    remoteRegistryActive = remoteTaskBusActive && remoteRegistryActive;

    if (remoteRegistryActive || remoteTaskBusActive) {
        logEvent("info", "cluster", "Remote control-plane transport enabled", QJsonObject{
            {"control_plane_endpoint", controlPlaneEndpoint},
            {"registry_endpoint", registryEndpoint},
            {"remote_registry", remoteRegistryActive},
            {"remote_task_bus", remoteTaskBusActive}
        });
    } else if (remoteControlRequested) {
        logEvent("warning", "cluster", "Control-plane unavailable, using local fallback", QJsonObject{
            {"control_plane_endpoint", controlPlaneEndpoint},
            {"registry_endpoint", registryEndpoint}
        });
    }

    QString selectedProvider;
    _provider = providers::ProviderFactory::create(_config, &selectedProvider);
    qDebug(lcRuntime) << "Using provider:" << selectedProvider;

    _cron = std::make_unique<CronService>(QDir(_config.workspacePath()).filePath("cron/jobs.json"));
    _mcp = std::make_unique<MCPManager>(_config.tools.mcpServers);
    _subagents = std::make_unique<SubagentManager>(*_bus);

    _agent = std::make_unique<agent::AgentLoop>(
        *_bus, *_provider, _config.workspacePath(), _config, _cron.get(), _subagents.get(), _mcp.get());
    _agent->setToolGuard([this](const QString &toolName,
                                const QJsonObject &params,
                                const agent::ToolExecutionContext &context) {
        agent::ToolGuardResult result;
        result.policy = toolPolicyFor(_config, toolName);
        const QString summary = QString("%1 · %2").arg(toolDisplayName(toolName), context.sessionKey);
        const QString paramsPreview = compactJson(params);

        if (result.policy == "deny") {
            result.allowed = false;
            result.message = QString("Action denied by security policy: %1").arg(toolDisplayName(toolName));
            if (_notifications && _config.security.notifyOnToolDenied) {
                _notifications->push(
                    "warning", "Tool blocked", result.message, "security", toolName,
                    QJsonObject{{"session_key", context.sessionKey}, {"channel", context.channel}});
            }
            return result;
        }

        if (result.policy == "confirm") {
            QString approvalId;
            if (_approvals && _approvals->consumeGrant(toolName, context.sessionKey, &approvalId)) {
                result.allowed = true;
                result.policy = "approved";
                result.referenceId = approvalId;
                return result;
            }

            if (_approvals) {
                approvalId = _approvals->createPending(
                    toolName,
                    context.sessionKey,
                    context.channel,
                    summary,
                    paramsPreview,
                    QJsonObject{{"chat_id", context.chatId}, {"message_id", context.messageId}});
                result.referenceId = approvalId;
            }

            result.allowed = false;
            result.message = QString("Action requires approval in Security Center: %1 (ID: %2)")
                                 .arg(toolDisplayName(toolName), result.referenceId.isEmpty() ? "-" : result.referenceId);
            if (_notifications && _config.security.notifyOnApprovalRequired) {
                _notifications->push(
                    "warning",
                    "Approval required",
                    result.message,
                    "security",
                    result.referenceId,
                    QJsonObject{{"tool", toolName},
                                {"session_key", context.sessionKey},
                                {"params", paramsPreview}});
            }
        }

        return result;
    });
    _agent->setToolAudit([this](const agent::ToolAuditRecord &record) {
        if (!_config.security.auditToolCalls && record.allowed) {
            return;
        }
        const QString level = record.allowed ? "info" : "warning";
        logEvent(level, "tool", record.allowed ? "Tool executed" : "Tool blocked", QJsonObject{
            {"tool", record.toolName},
            {"session_key", record.context.sessionKey},
            {"channel", record.context.channel},
            {"policy", record.policy},
            {"reference_id", record.referenceId},
            {"result_preview", record.result.left(220)},
            {"params", compactJson(record.params, 240)}
        });
    });

    _agentThread = std::make_unique<QThread>();
    _agentThread->setObjectName("AgentThread");
    _agent->moveToThread(_agentThread.get());
    _agentThread->start();
    QMetaObject::invokeMethod(_agent.get(), "registerDefaultTools", Qt::BlockingQueuedConnection);
    qDebug(lcRuntime) << "AgentLoop moved to dedicated thread";

    _subagents->setExecuteCallback([this](const QString &task,
                                          const QString &sessionKey,
                                          const QString &channel,
                                          const QString &chatId,
                                          const QString &taskId,
                                          const QString &traceId) -> QString {
        return invokeProcessDirect(task,
                                   sessionKey,
                                   channel,
                                   chatId,
                                   QString(),
                                   QString(),
                                   QJsonObject{
                                       {"task_id", taskId},
                                       {"trace_id", traceId}
                                   });
    });
    _subagents->setDelegateCallback([this](const QString &taskId,
                                           const QString &task,
                                           const QString &label,
                                           const QString &originChannel,
                                           const QString &originChatId,
                                           const QString &sessionKey,
                                           const QString &targetNode,
                                           const QString &targetRole,
                                           const QStringList &targetTags,
                                           const QString &requiredTool,
                                           const QString &requiredChannel,
                                           const QString &requiredMemoryBackend,
                                           const QString &parentTaskId,
                                           const QString &traceId) -> QString {
        return submitDelegatedSubagent(taskId,
                                       task,
                                       label,
                                       originChannel,
                                       originChatId,
                                       sessionKey,
                                       targetNode,
                                       targetRole,
                                       targetTags,
                                       requiredTool,
                                       requiredChannel,
                                       requiredMemoryBackend,
                                       parentTaskId,
                                       traceId);
    });
    _subagents->setCancelCallback([this](const QString &taskId,
                                         const QString &sessionKey,
                                         const QString &originChannel,
                                         const QString &originChatId) {
        cancelDelegatedSubagent(taskId, sessionKey, originChannel, originChatId);
    });

    _cron->setOnJobCallback([this](const CronJob &job) -> QString {
        if (!_agent || !_bus) return "Error: agent/bus not initialized";

        if (job.payload.kind == "automation") {
            const AutomationRecord record = automation(job.payload.message);
            if (record.id.isEmpty()) {
                return QString("Error: automation not found for scheduled job %1").arg(job.id);
            }
            return executeAutomationRecord(record,
                                           "scheduled",
                                           automationSessionKey(record, QString()),
                                           job.id).content;
        }

        const QString reminderNote = QString(
            "[Scheduled Task] Timer finished.\n\n"
            "Task '%1' has been triggered.\n"
            "Scheduled instruction: %2"
        ).arg(job.name, job.payload.message);

        const QPointer<agent::AgentLoop> cronAgent(_agent.get());
        const bool prevCron = [cronAgent]() {
            bool prev = false;
            if (cronAgent) {
                QMetaObject::invokeMethod(cronAgent.data(),
                    [cronAgent, &prev]() {
                        if (cronAgent) {
                            prev = cronAgent->setCronExecutionContext(true);
                        }
                    },
                    Qt::BlockingQueuedConnection);
            }
            return prev;
        }();

        const QString response = invokeProcessDirect(
            reminderNote,
            "cron:" + job.id,
            job.payload.channel.isEmpty() ? "cli" : job.payload.channel,
            job.payload.to.isEmpty() ? "direct" : job.payload.to
        );

        if (cronAgent) {
            QMetaObject::invokeMethod(cronAgent.data(),
                [cronAgent, prevCron]() {
                    if (cronAgent) {
                        cronAgent->setCronExecutionContext(prevCron);
                    }
                },
                Qt::BlockingQueuedConnection);
        }

        if (job.payload.deliver && !job.payload.to.isEmpty() && !response.trimmed().isEmpty()) {
            bus::OutboundMessage out;
            out.channel = job.payload.channel.isEmpty() ? "cli" : job.payload.channel;
            out.chatId = job.payload.to;
            out.content = response;
            _bus->publishOutbound(out);
        }
        return response;
    });

    _heartbeat = std::make_unique<HeartbeatService>(_config.workspacePath(), _config);
    _heartbeat->setEnabled(_config.gateway.heartbeat.enabled);
    _heartbeat->setIntervalSeconds(_config.gateway.heartbeat.intervalS);

    _heartbeat->setOnExecute([this](const QString &tasks) -> QString {
        const auto target = pickHeartbeatTarget(_config, _channels.get());
        return processMessageDetailed(tasks, QStringLiteral("heartbeat"), target.first, target.second).content;
    });
    _heartbeat->setOnNotify([this](const QString &response) {
        if (!_bus || response.trimmed().isEmpty()) return;
        const auto target = pickHeartbeatTarget(_config, _channels.get());
        logEvent("info",
                 "heartbeat",
                 "Heartbeat notification queued",
                 QJsonObject{
                     {"session_key", QStringLiteral("heartbeat")},
                     {"channel", target.first},
                     {"chat_id", target.second},
                     {"response_preview", automationPreview(response, 220)}
                 });
        if (_notifications) {
            _notifications->push(
                "info",
                "Heartbeat notification queued",
                automationPreview(response, 320),
                "heartbeat",
                QString(),
                QJsonObject{
                    {"session_key", QStringLiteral("heartbeat")},
                    {"channel", target.first},
                    {"chat_id", target.second}
                }
            );
        }
        bus::OutboundMessage msg;
        msg.channel = target.first;
        msg.chatId = target.second;
        msg.content = response;
        _bus->publishOutbound(msg);
    });

    _channels = std::make_unique<channels::ChannelManager>(_config, *_bus);
    QObject::connect(_bus.get(), &bus::MessageBus::outboundPublished,
                     [this](const bus::OutboundMessage &msg) {
        if (_channels) _channels->handleOutbound(msg);
    });
    startDelegationPolling();

    reconcileAutomationSchedules();
    publishNodePresence(true);

    _initialized = true;
    qDebug(lcRuntime) << "RuntimeCore initialized";
    logEvent("info", "runtime", "Runtime core initialized", QJsonObject{
        {"model", _config.agentDefaults.model},
        {"provider", _config.agentDefaults.provider}
    });
    return true;
}

bool RuntimeCore::ensureModelReady(const QString &modelOverride, const QString &providerOverride) {
    if (_initialized) {
        return true;
    }
    return ensureInitialized(modelOverride, providerOverride);
}

QJsonObject RuntimeCore::serviceHealth(const QString &modelOverride, const QString &providerOverride) {
    const bool initialized = ensureModelReady(modelOverride, providerOverride);
    const config::Config cfg = _initialized ? _config : activeConfig();
    const QString requestedProvider = resolvedProviderForTurn(cfg, modelOverride, providerOverride);
    const QString actualBackend = _provider ? actualProviderLabel(*_provider) : QStringLiteral("unknown");
    const QString error = !initialized
        ? QStringLiteral("Runtime initialization failed.")
        : providerOperationalError(cfg, _provider.get(), modelOverride, providerOverride);

    return QJsonObject{
        {QStringLiteral("ok"), error.isEmpty()},
        {QStringLiteral("initialized"), initialized},
        {QStringLiteral("requestedProvider"), requestedProvider},
        {QStringLiteral("actualBackend"), actualBackend},
        {QStringLiteral("backendFallback"), _provider && _provider->isFallback()},
        {QStringLiteral("error"), error}
    };
}

void RuntimeCore::ensureSystemStores(const QString &workspace) {
    if (!_approvals) _approvals = std::make_unique<ApprovalStore>(workspace);
    if (!_automations) _automations = std::make_unique<AutomationStore>(workspace);
    if (!_automationRuns) _automationRuns = std::make_unique<AutomationRunStore>(workspace);
    if (!_eventLog) _eventLog = std::make_unique<EventLog>(workspace);
    if (!_notifications) _notifications = std::make_unique<NotificationCenter>(workspace);
    if (!_plugins) _plugins = std::make_unique<PluginRegistry>(workspace);
    if (!_skills) _skills = std::make_unique<SkillRegistry>(workspace);
    if (!_resources) _resources = std::make_unique<ResourceCatalog>(workspace);
    if (!_taskStore) _taskStore = std::make_unique<TaskStore>(workspace);
}

QString RuntimeCore::submitDelegatedSubagent(const QString &taskId,
                                             const QString &task,
                                             const QString &label,
                                             const QString &originChannel,
                                             const QString &originChatId,
                                             const QString &sessionKey,
                                             const QString &targetNode,
                                             const QString &targetRole,
                                             const QStringList &targetTags,
                                             const QString &requiredTool,
                                             const QString &requiredChannel,
                                             const QString &requiredMemoryBackend,
                                             const QString &parentTaskId,
                                             const QString &traceId) {
    if (!_taskBus) {
        return QStringLiteral("Error: task bus is not initialized");
    }

    QString resolvedTargetNode = targetNode.trimmed();
    QString resolvedTargetRole = targetRole.trimmed();
    const QStringList resolvedTargetTags = targetTags;
    const QString effectiveRequiredTool = requiredTool.trimmed();
    const QString effectiveRequiredChannel = requiredChannel.trimmed();
    const QString effectiveRequiredMemoryBackend = requiredMemoryBackend.trimmed().isEmpty()
        ? _config.normalizedMemoryBackend()
        : requiredMemoryBackend.trimmed();
    const QString requestedTargetNode = resolvedTargetNode;
    QString failoverFromNode;
    QString failoverReason;

    const auto delegationRequirements = [&]() {
        QStringList requirements;
        if (!resolvedTargetRole.isEmpty()) requirements << QStringLiteral("role '%1'").arg(resolvedTargetRole);
        if (!resolvedTargetTags.isEmpty()) requirements << QStringLiteral("tags [%1]").arg(resolvedTargetTags.join(", "));
        if (!effectiveRequiredTool.isEmpty()) requirements << QStringLiteral("tool '%1'").arg(effectiveRequiredTool);
        if (!effectiveRequiredChannel.isEmpty()) requirements << QStringLiteral("channel '%1'").arg(effectiveRequiredChannel);
        if (!effectiveRequiredMemoryBackend.isEmpty()) requirements << QStringLiteral("memory '%1'").arg(effectiveRequiredMemoryBackend);
        return requirements;
    };

    const bool allowPinnedTargetFailover =
        !resolvedTargetNode.isEmpty() &&
        (!resolvedTargetRole.isEmpty() ||
         !resolvedTargetTags.isEmpty() ||
         !effectiveRequiredTool.isEmpty() ||
         !effectiveRequiredChannel.isEmpty() ||
         !requiredMemoryBackend.trimmed().isEmpty());

    QList<distributed::NodeDescriptor> candidates;
    if (resolvedTargetNode.isEmpty() || allowPinnedTargetFailover) {
        candidates = resolveDelegationTargets(resolvedTargetRole,
                                             effectiveRequiredChannel,
                                             resolvedTargetTags,
                                             effectiveRequiredTool,
                                             effectiveRequiredMemoryBackend);
    }

    if (resolvedTargetNode.isEmpty()) {
        if (candidates.isEmpty()) {
            const QStringList requirements = delegationRequirements();
            return requirements.isEmpty()
                ? QStringLiteral("Error: no delegation target was resolved")
                : QStringLiteral("Error: no online node matches %1").arg(requirements.join(", "));
        }
        resolvedTargetNode = candidates.first().nodeId.trimmed();
        if (resolvedTargetRole.isEmpty()) {
            resolvedTargetRole = candidates.first().role.trimmed();
        }
    } else if (allowPinnedTargetFailover) {
        QList<distributed::NodeDescriptor> knownNodes;
        const QString controlPlaneEndpoint = configuredControlPlaneEndpoint(_config);
        if (usesRemoteTaskBus(_taskBus.get()) && !controlPlaneEndpoint.isEmpty()) {
            distributed::RemoteNodeRegistryClient registry(controlPlaneEndpoint);
            if (registry.isReady()) {
                knownNodes = registry.listNodes();
            }
        }
        if (knownNodes.isEmpty() && _nodeRegistry) {
            knownNodes = _nodeRegistry->listNodes();
        }

        if (!knownNodes.isEmpty()) {
            std::stable_sort(knownNodes.begin(), knownNodes.end(),
                             [&](const distributed::NodeDescriptor &left,
                                 const distributed::NodeDescriptor &right) {
                const bool leftPreferred =
                    left.nodeId.trimmed().compare(requestedTargetNode, Qt::CaseInsensitive) == 0;
                const bool rightPreferred =
                    right.nodeId.trimmed().compare(requestedTargetNode, Qt::CaseInsensitive) == 0;
                if (leftPreferred != rightPreferred) {
                    return leftPreferred && !rightPreferred;
                }
                return false;
            });
            annotateEndpointHealth(&knownNodes, true, kResolveProbeBudget);
        }

        distributed::NodeDescriptor preferredNode;
        bool preferredNodeKnown = false;
        for (const distributed::NodeDescriptor &node : knownNodes) {
            if (node.nodeId.trimmed().compare(requestedTargetNode, Qt::CaseInsensitive) == 0) {
                preferredNode = node;
                preferredNodeKnown = true;
                break;
            }
        }

        const bool preferredNodeUnavailable =
            preferredNodeKnown &&
            (!preferredNode.online ||
             (preferredNode.endpointProbeSupported &&
              preferredNode.endpointHealthChecked &&
              !preferredNode.endpointReachable));
        if (preferredNodeUnavailable) {
            distributed::NodeDescriptor fallbackNode;
            bool foundFallback = false;
            for (const distributed::NodeDescriptor &candidate : candidates) {
                if (candidate.nodeId.trimmed().compare(requestedTargetNode, Qt::CaseInsensitive) == 0) {
                    continue;
                }
                fallbackNode = candidate;
                foundFallback = true;
                break;
            }

            if (!foundFallback) {
                const QStringList requirements = delegationRequirements();
                const QString preferredState = preferredNode.online
                    ? QStringLiteral("is unreachable")
                    : QStringLiteral("is offline");
                return requirements.isEmpty()
                    ? QStringLiteral("Error: target node '%1' %2 and no fallback candidate is available")
                          .arg(requestedTargetNode, preferredState)
                    : QStringLiteral("Error: target node '%1' %2 and no fallback node matches %3")
                          .arg(requestedTargetNode, preferredState, requirements.join(", "));
            }

            failoverFromNode = requestedTargetNode;
            failoverReason = preferredNode.online
                ? QStringLiteral("runtime endpoint unreachable")
                : QStringLiteral("node offline");
            resolvedTargetNode = fallbackNode.nodeId.trimmed();
            if (resolvedTargetRole.isEmpty()) {
                resolvedTargetRole = fallbackNode.role.trimmed();
            }
        }
    }

    distributed::TaskEnvelope delegated;
    delegated.taskId = taskId;
    delegated.traceId = traceId.trimmed().isEmpty() ? newTraceId() : traceId.trimmed();
    delegated.parentTaskId = parentTaskId.trimmed();
    delegated.originNode = effectiveNodeId(_config);
    delegated.targetNode = resolvedTargetNode;
    delegated.targetRole = resolvedTargetRole;
    delegated.targetTags = resolvedTargetTags;
    delegated.requiredTool = effectiveRequiredTool;
    delegated.requiredChannel = effectiveRequiredChannel;
    delegated.requiredMemoryBackend = effectiveRequiredMemoryBackend;
    delegated.sceneKey = sessionKey.trimmed().isEmpty() ? QString("%1:%2").arg(originChannel, originChatId) : sessionKey.trimmed();
    delegated.taskType = QStringLiteral("delegated_subagent");
    delegated.replyTo = originChatId.trimmed().isEmpty()
        ? originChannel
        : QString("%1:%2").arg(originChannel, originChatId);
    delegated.labels = QStringList{
        QStringLiteral("delegated_subagent"),
        originChannel,
        _config.normalizedDeploymentMode(),
        _config.normalizedRuntimeMode()
    };
    for (const QString &tag : delegated.targetTags) {
        const QString normalizedTag = tag.trimmed();
        if (!normalizedTag.isEmpty()) {
            delegated.labels.append(QStringLiteral("tag:%1").arg(normalizedTag));
        }
    }
    if (!delegated.requiredTool.isEmpty()) {
        delegated.labels.append(QStringLiteral("tool:%1").arg(delegated.requiredTool));
    }
    if (!delegated.requiredChannel.isEmpty()) {
        delegated.labels.append(QStringLiteral("channel:%1").arg(delegated.requiredChannel));
    }
    if (!delegated.requiredMemoryBackend.isEmpty()) {
        delegated.labels.append(QStringLiteral("memory:%1").arg(delegated.requiredMemoryBackend));
    }
    delegated.payload = QJsonObject{
        {"title", taskTitleFromContent(task)},
        {"label", label},
        {"task", task},
        {"originChannel", originChannel},
        {"originChatId", originChatId},
        {"sessionKey", delegated.sceneKey},
        {"targetTags", QJsonArray::fromStringList(delegated.targetTags)},
        {"requiredTool", delegated.requiredTool},
        {"requiredChannel", delegated.requiredChannel},
        {"requiredMemoryBackend", delegated.requiredMemoryBackend}
    };
    if (!failoverFromNode.isEmpty()) {
        delegated.payload.insert(QStringLiteral("requestedTargetNode"), failoverFromNode);
        delegated.payload.insert(QStringLiteral("failoverReason"), failoverReason);
    }
    delegated.contextRefs = delegatedContextRefsFor(_config,
                                                    delegated.taskId,
                                                    delegated.traceId,
                                                    delegated.sceneKey,
                                                    originChannel,
                                                    originChatId,
                                                    delegated.targetNode,
                                                    delegated.targetRole,
                                                    delegated.targetTags,
                                                    delegated.requiredTool,
                                                    delegated.requiredMemoryBackend);

    if (!_taskBus->submit(delegated)) {
        return QStringLiteral("Error: failed to submit delegated subagent task");
    }

    if (_taskStore) {
        QStringList routeSummary;
        if (!delegated.targetRole.trimmed().isEmpty()) {
            routeSummary.append(QStringLiteral("role %1").arg(delegated.targetRole.trimmed()));
        }
        if (!delegated.targetTags.isEmpty()) {
            routeSummary.append(QStringLiteral("tags %1").arg(delegated.targetTags.join(",")));
        }
        if (!delegated.requiredTool.isEmpty()) {
            routeSummary.append(QStringLiteral("tool %1").arg(delegated.requiredTool));
        }
        if (!delegated.requiredChannel.isEmpty()) {
            routeSummary.append(QStringLiteral("channel %1").arg(delegated.requiredChannel));
        }
        if (!delegated.requiredMemoryBackend.isEmpty()) {
            routeSummary.append(QStringLiteral("memory %1").arg(delegated.requiredMemoryBackend));
        }
        TaskRecord record;
        record.id = delegated.taskId;
        record.traceId = delegated.traceId;
        record.parentTaskId = delegated.parentTaskId;
        record.originNode = delegated.originNode;
        record.targetNode = delegated.targetNode;
        record.kind = QStringLiteral("delegated_subagent");
        record.title = taskTitleFromContent(task);
        record.sessionKey = delegated.sceneKey;
        record.channel = originChannel.trimmed().isEmpty() ? QStringLiteral("subagent") : originChannel.trimmed();
        record.state = QStringLiteral("queued");
        record.summary = delegated.targetNode.trimmed().isEmpty()
            ? QStringLiteral("Delegated to remote worker")
            : (!failoverFromNode.isEmpty()
                   ? QStringLiteral("Delegated to %1 (failover from %2)")
                         .arg(delegated.targetNode, failoverFromNode)
                   : QStringLiteral("Delegated to %1").arg(delegated.targetNode));
        if (!routeSummary.isEmpty()) {
            record.summary += QStringLiteral(" (%1)").arg(routeSummary.join("; "));
        }
        record.createdAt = QDateTime::currentDateTime();
        record.metadata = QJsonObject{
            {"chat_id", originChatId},
            {"trace_id", delegated.traceId},
            {"origin_node", delegated.originNode},
            {"target_node", delegated.targetNode},
            {"target_role", delegated.targetRole},
            {"target_tags", QJsonArray::fromStringList(delegated.targetTags)},
            {"required_tool", delegated.requiredTool},
            {"required_channel", delegated.requiredChannel},
            {"required_memory_backend", delegated.requiredMemoryBackend},
            {"parent_task_id", delegated.parentTaskId},
            {"reply_to", delegated.replyTo},
            {"context_ref_count", delegated.contextRefs.size()}
        };
        if (!failoverFromNode.isEmpty()) {
            record.metadata.insert(QStringLiteral("requested_target_node"), failoverFromNode);
            record.metadata.insert(QStringLiteral("failover_reason"), failoverReason);
        }
        _taskStore->upsertTask(record);
    }

    if (!failoverFromNode.isEmpty()) {
        logEvent("warning", "cluster", "Delegated subagent failover rerouted target", QJsonObject{
            {"task_id", delegated.taskId},
            {"trace_id", delegated.traceId},
            {"requested_target_node", failoverFromNode},
            {"resolved_target_node", delegated.targetNode},
            {"target_role", delegated.targetRole},
            {"target_tags", QJsonArray::fromStringList(delegated.targetTags)},
            {"required_tool", delegated.requiredTool},
            {"required_channel", delegated.requiredChannel},
            {"required_memory_backend", delegated.requiredMemoryBackend},
            {"reason", failoverReason}
        });
    }

    QJsonObject submitMetadata{
        {"task_id", delegated.taskId},
        {"trace_id", delegated.traceId},
        {"origin_node", delegated.originNode},
        {"target_node", delegated.targetNode},
        {"target_role", delegated.targetRole},
        {"target_tags", QJsonArray::fromStringList(delegated.targetTags)},
        {"required_tool", delegated.requiredTool},
        {"required_channel", delegated.requiredChannel},
        {"required_memory_backend", delegated.requiredMemoryBackend},
        {"parent_task_id", delegated.parentTaskId},
        {"session_key", delegated.sceneKey},
        {"context_ref_count", delegated.contextRefs.size()}
    };
    if (!failoverFromNode.isEmpty()) {
        submitMetadata.insert(QStringLiteral("requested_target_node"), failoverFromNode);
        submitMetadata.insert(QStringLiteral("failover_reason"), failoverReason);
    }
    logEvent("info", "cluster", "Delegated subagent submitted", submitMetadata);
    return delegated.targetNode;
}

bool RuntimeCore::cancelDelegatedSubagent(const QString &taskId,
                                          const QString &sessionKey,
                                          const QString &originChannel,
                                          const QString &originChatId) {
    bool cancelled = false;
    if (_taskBus) {
        cancelled = _taskBus->cancel(taskId);
        const QString cancelledDelegatedSession = sessionKey.trimmed().isEmpty()
            ? QStringLiteral("delegated:%1").arg(taskId.trimmed())
            : QStringLiteral("delegated:%1:%2").arg(taskId.trimmed(), sessionKey.trimmed());

        distributed::TaskResultEnvelope result;
        result.taskId = taskId.trimmed();
        result.producerNode = effectiveNodeId(_config);
        result.status = QStringLiteral("cancelled");
        result.message = QStringLiteral("Delegated subagent cancelled by the origin session.");
        result.output = QJsonObject{
            {"content", QStringLiteral("Delegated task cancelled before completion.")},
            {"sessionKey", sessionKey},
            {"channel", originChannel},
            {"chatId", originChatId}
        };
        result.outputRefs = delegatedOutputRefsFor(_config, taskId.trimmed(), QString(), cancelledDelegatedSession);
        _taskBus->publishResult(result);
    }

    if (_taskStore) {
        _taskStore->markCancelled(taskId.trimmed(),
                                  QStringLiteral("Delegated subagent cancelled by /stop or session shutdown"));
    }

    logEvent("warning", "cluster", "Delegated subagent cancelled", QJsonObject{
        {"task_id", taskId},
        {"session_key", sessionKey},
        {"channel", originChannel},
        {"chat_id", originChatId},
        {"task_bus_cancelled", cancelled}
    });
    return cancelled;
}

QList<distributed::NodeDescriptor> RuntimeCore::resolveDelegationTargets(const QString &targetRole,
                                                                         const QString &channel,
                                                                         const QStringList &targetTags,
                                                                         const QString &requiredTool,
                                                                         const QString &requiredMemoryBackend,
                                                                         int limit,
                                                                         QString *resolutionSource) const {
    const QString normalizedRole = targetRole.trimmed();
    QList<distributed::NodeDescriptor> nodes;
    if (resolutionSource) {
        resolutionSource->clear();
    }

    const QString controlPlaneEndpoint = configuredControlPlaneEndpoint(_config);
    const QString registryEndpoint = effectiveRegistryEndpoint(_config);
    const bool remoteDelegationActive = usesRemoteTaskBus(_taskBus.get());
    const auto appendMatchingNodes = [&](const QList<distributed::NodeDescriptor> &listed) {
        for (const distributed::NodeDescriptor &node : listed) {
            if (!delegationFailureReasons(node,
                                          _config.deployment.clusterId.trimmed(),
                                          normalizedRole,
                                          targetTags,
                                          requiredTool,
                                          channel,
                                          requiredMemoryBackend,
                                          false)
                     .isEmpty()) {
                continue;
            }
            nodes.append(node);
        }
    };
    if (remoteDelegationActive && !controlPlaneEndpoint.isEmpty()) {
        QString error;
        distributed::RemoteControlClient client(controlPlaneEndpoint, kControlPlaneProbeTimeoutMs);
        if (client.isReady()) {
            const QJsonObject response = client.post(QStringLiteral("/v1/control/nodes/resolve"),
                                                     QJsonObject{
                                                         {"clusterId", _config.deployment.clusterId},
                                                         {"role", normalizedRole},
                                                         {"tags", QJsonArray::fromStringList(targetTags)},
                                                         {"tool", requiredTool},
                                                         {"channel", channel},
                                                         {"memoryBackend", requiredMemoryBackend},
                                                         {"limit", limit > 0 ? limit : 8}
                                                     },
                                                     &error);
            if (error.isEmpty() && response.value(QStringLiteral("ok")).toBool(false)) {
                nodes = distributed::json::nodeDescriptorsFromJson(response.value(QStringLiteral("nodes")));
                if (resolutionSource) {
                    *resolutionSource = QStringLiteral("control_plane");
                }
            }
        }
    }

    if (nodes.isEmpty() && remoteDelegationActive && !registryEndpoint.isEmpty()) {
        distributed::RemoteNodeRegistryClient remoteRegistry(registryEndpoint, kControlPlaneProbeTimeoutMs);
        if (remoteRegistry.isReady()) {
            appendMatchingNodes(remoteRegistry.listNodes());
            if (!nodes.isEmpty() && resolutionSource) {
                *resolutionSource = registryEndpoint.compare(controlPlaneEndpoint, Qt::CaseInsensitive) == 0
                    ? QStringLiteral("control_plane")
                    : QStringLiteral("remote_registry");
            }
        }
    }

    if (nodes.isEmpty() && _nodeRegistry) {
        appendMatchingNodes(_nodeRegistry->listNodes());
        if (!nodes.isEmpty() && resolutionSource) {
            *resolutionSource = controlPlaneEndpoint.isEmpty()
                ? QStringLiteral("local_registry")
                : QStringLiteral("local_registry_fallback");
        }
    }



    if (nodes.isEmpty() && _config.normalizedRuntimeMode() == QStringLiteral("embedded")) {
        const distributed::NodeDescriptor currentNode = localNodeDescriptor(_config,
                                                                           configuredChannels(_config),
                                                                           true,
                                                                           0,
                                                                           0,
                                                                           kDefaultNodeConcurrency);
        if (delegationFailureReasons(currentNode,
                                     _config.deployment.clusterId.trimmed(),
                                     normalizedRole,
                                     targetTags,
                                     requiredTool,
                                     channel,
                                     requiredMemoryBackend,
                                     false).isEmpty()) {
            nodes.append(currentNode);
            if (resolutionSource) {
                *resolutionSource = QStringLiteral("embedded_runtime");
            }
        }
    }

    const QString localNodeId = effectiveNodeId(_config);
    annotateEndpointHealth(&nodes, true, kResolveProbeBudget);
    sortDelegationNodes(&nodes, localNodeId);
    if (limit > 0 && nodes.size() > limit) {
        nodes = nodes.mid(0, limit);
    }
    return nodes;
}

void RuntimeCore::startDelegationPolling() {
    if (!_taskBus) {
        return;
    }
    if (!_delegationPoller) {
        _delegationPoller = std::make_unique<QTimer>();
        _delegationPoller->setInterval(kDelegationPollIntervalMs);
        QObject::connect(_delegationPoller.get(), &QTimer::timeout, [this]() {
            pollDelegatedTaskBus();
        });
    }
    if (!_delegationPoller->isActive()) {
        _delegationPoller->start();
    }
}

void RuntimeCore::stopDelegationPolling() {
    if (_delegationPoller && _delegationPoller->isActive()) {
        _delegationPoller->stop();
    }
}

void RuntimeCore::pollDelegatedTaskBus() {
    if (!_taskBus) {
        return;
    }

    publishNodePresence(true, 15000, QStringLiteral("pollDelegatedTaskBus"));

    const QString localNodeId = effectiveNodeId(_config);
    refreshDelegatedWorkerLeases(_taskBus.get(),
                                 &_activeDelegatedWorkerTasks,
                                 &_delegatedWorkerLeaseMutex,
                                 localNodeId);

    if (_subagents) {
        const QStringList delegatedIds = _subagents->delegatedTaskIds();
        for (const QString &delegatedId : delegatedIds) {
            const QList<distributed::TaskResultEnvelope> results = _taskBus->recentResults(delegatedId, QString(), 1);
            if (!results.isEmpty()) {
                const distributed::TaskResultEnvelope &result = results.first();
                if (_taskStore) {
                    const QString resultText = result.output.value(QStringLiteral("content")).toString(result.message);
                    if (taskResultIsCancelled(result)) {
                        _taskStore->markCancelled(delegatedId,
                                                  result.message.trimmed().isEmpty()
                                                      ? QStringLiteral("Delegated subagent cancelled")
                                                      : result.message.trimmed());
                    } else if (result.status.trimmed().compare(QStringLiteral("ok"), Qt::CaseInsensitive) == 0 ||
                               result.status.trimmed().compare(QStringLiteral("success"), Qt::CaseInsensitive) == 0 ||
                               result.status.trimmed().compare(QStringLiteral("succeeded"), Qt::CaseInsensitive) == 0) {
                        _taskStore->markCompleted(delegatedId, resultText, QStringLiteral("Delegated subagent completed"));
                    } else {
                        _taskStore->markFailed(delegatedId,
                                               result.error.value(QStringLiteral("message")).toString(result.message),
                                               QStringLiteral("Delegated subagent failed"));
                    }
                }
                _subagents->handleDelegatedResult(results.first());
            }
        }
    }

    const QString localRole = _config.deployment.nodeRole.trimmed();
    QHash<QString, distributed::TaskEnvelope> pending;
    const auto appendPending = [&pending](const QList<distributed::TaskEnvelope> &tasks) {
        for (const distributed::TaskEnvelope &task : tasks) {
            pending.insert(task.taskId, task);
        }
    };

    appendPending(_taskBus->pendingTasks(localNodeId, QString(), 16));
    if (!localRole.isEmpty()) {
        appendPending(_taskBus->pendingTasks(QString(), localRole, 16));
    }

    for (const distributed::TaskEnvelope &task : pending) {
        if (!isDelegatedTask(task)) {
            continue;
        }
        if (!task.targetNode.trimmed().isEmpty() &&
            task.targetNode.trimmed().compare(localNodeId, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (task.targetNode.trimmed().isEmpty() &&
            !task.targetRole.trimmed().isEmpty() &&
            task.targetRole.trimmed().compare(localRole, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (!_taskBus->claim(task.taskId, localNodeId)) {
            continue;
        }
        executeDelegatedTask(task);
    }

    // Adaptive Polling Interval Adjustment
    bool isCurrentlyActive = false;
    if (_subagents && _subagents->runningCount() > 0) {
        isCurrentlyActive = true;
    }
    {
        QMutexLocker locker(&_delegatedWorkerLeaseMutex);
        if (!_activeDelegatedWorkerTasks.isEmpty()) {
            isCurrentlyActive = true;
        }
    }

    if (_delegationPoller) {
        const int targetInterval = isCurrentlyActive ? kDelegationActivePollIntervalMs : kDelegationIdlePollIntervalMs;
        if (_delegationPoller->interval() != targetInterval) {
            _delegationPoller->setInterval(targetInterval);
            if (isCurrentlyActive && _delegationPoller->isActive()) {
                _delegationPoller->start();
            }
        }
    }
}

void RuntimeCore::executeDelegatedTask(distributed::TaskEnvelope task) {
    QtConcurrent::run([this, task]() {
        const QString leasedTaskId = task.taskId.trimmed();
        {
            QMutexLocker locker(&_delegatedWorkerLeaseMutex);
            if (!leasedTaskId.isEmpty()) {
                _activeDelegatedWorkerTasks.insert(leasedTaskId);
            }
        }
        const auto releaseDelegatedLease = [this, leasedTaskId]() {
            if (leasedTaskId.isEmpty()) {
                return;
            }
            QMutexLocker locker(&_delegatedWorkerLeaseMutex);
            _activeDelegatedWorkerTasks.remove(leasedTaskId);
        };

        const QString delegatedTask = task.payload.value(QStringLiteral("task")).toString().trimmed();
        const QString delegatedLabel =
            task.payload.value(QStringLiteral("label")).toString(task.payload.value(QStringLiteral("title")).toString());
        const QString delegatedSessionBase =
            task.payload.value(QStringLiteral("sessionKey")).toString(task.sceneKey.trimmed());
        const QString delegatedSession = delegatedSessionBase.trimmed().isEmpty()
            ? QStringLiteral("delegated:%1").arg(task.taskId)
            : QStringLiteral("delegated:%1:%2").arg(task.taskId, delegatedSessionBase);
        const QString workerNodeId = effectiveNodeId(_config);

        if (_taskStore) {
            TaskRecord record;
            record.id = task.taskId;
            record.traceId = task.traceId;
            record.parentTaskId = task.parentTaskId;
            record.originNode = task.originNode;
            record.targetNode = workerNodeId;
            record.kind = QStringLiteral("delegated_subagent_worker");
            record.title = delegatedLabel.trimmed().isEmpty() ? taskTitleFromContent(delegatedTask) : delegatedLabel.trimmed();
            record.sessionKey = delegatedSession;
            record.channel = QStringLiteral("subagent");
            record.state = QStringLiteral("queued");
            record.summary = QStringLiteral("Claimed delegated subagent task");
            record.createdAt = task.createdAt.isValid() ? task.createdAt.toLocalTime() : QDateTime::currentDateTime();
            record.metadata = QJsonObject{
                {"trace_id", task.traceId},
                {"origin_node", task.originNode},
                {"target_node", workerNodeId},
                {"target_role", task.targetRole},
                {"target_tags", QJsonArray::fromStringList(task.targetTags)},
                {"required_tool", task.requiredTool},
                {"required_channel", task.requiredChannel},
                {"required_memory_backend", task.requiredMemoryBackend},
                {"reply_to", task.replyTo},
                {"context_ref_count", task.contextRefs.size()}
            };
            _taskStore->upsertTask(record);
            _taskStore->markRunning(task.taskId);
        }

        if (taskBusHasCancelledResult(_taskBus.get(), task.taskId)) {
            if (_taskStore) {
                _taskStore->markCancelled(task.taskId,
                                          QStringLiteral("Delegated task was cancelled before worker execution"));
            }
            logEvent("warning", "cluster", "Delegated task skipped because it was cancelled", QJsonObject{
                {"task_id", task.taskId},
                {"trace_id", task.traceId},
                {"target_node", workerNodeId}
            });
            releaseDelegatedLease();
            return;
        }

        ChatTurnResult turn;
        turn.taskId = task.taskId;
        turn.traceId = task.traceId;
        if (delegatedTask.isEmpty()) {
            turn.content = QStringLiteral("Error: delegated task payload is empty");
            turn.error = true;
        } else {
            turn.content = invokeProcessDirect(renderDelegatedPrompt(task),
                                               delegatedSession,
                                               QStringLiteral("subagent"),
                                               task.originNode.isEmpty() ? QStringLiteral("delegated") : task.originNode,
                                               QString(),
                                               QString(),
                                               QJsonObject{
                                                   {"task_id", task.taskId},
                                                   {"trace_id", task.traceId}
                                               });
            turn.error = turn.content.startsWith(QStringLiteral("Error:"), Qt::CaseInsensitive);
        }

        turn.model = _config.agentDefaults.model;
        turn.provider = _provider ? actualProviderLabel(*_provider) : QString();

        if (taskBusHasCancelledResult(_taskBus.get(), task.taskId)) {
            if (_taskStore) {
                _taskStore->markCancelled(task.taskId,
                                          QStringLiteral("Delegated task was cancelled during worker execution"));
            }
            logEvent("warning", "cluster", "Delegated task result suppressed because it was cancelled", QJsonObject{
                {"task_id", task.taskId},
                {"trace_id", task.traceId},
                {"target_node", workerNodeId}
            });
            releaseDelegatedLease();
            return;
        }

        bool publishedResult = false;
        if (_taskBus) {
            distributed::TaskResultEnvelope result = taskResultEnvelopeForTurn(turn,
                                                                               task.taskId,
                                                                               task.traceId,
                                                                               workerNodeId,
                                                                               delegatedSession,
                                                                               QStringLiteral("subagent"));
            result.outputRefs = delegatedOutputRefsFor(_config, task.taskId, task.traceId, delegatedSession);
            result.output.insert(QStringLiteral("workerNode"), workerNodeId);
            result.output.insert(QStringLiteral("targetRole"), task.targetRole);
            result.output.insert(QStringLiteral("replyTo"), task.replyTo);
            publishedResult = _taskBus->publishResult(result);
        }

        if (_taskStore) {
            if (!publishedResult) {
                _taskStore->markCancelled(task.taskId,
                                          QStringLiteral("Delegated worker result was suppressed after lease failover"));
            } else if (turn.error) {
                _taskStore->markFailed(task.taskId, turn.content, QStringLiteral("Delegated worker execution failed"));
            } else {
                _taskStore->markCompleted(task.taskId, turn.content, QStringLiteral("Delegated worker execution completed"));
            }
        }

        if (!publishedResult) {
            logEvent("warning",
                     "cluster",
                     "Delegated task result suppressed after lease failover",
                     QJsonObject{
                         {"task_id", task.taskId},
                         {"trace_id", task.traceId},
                         {"label", delegatedLabel},
                         {"origin_node", task.originNode},
                         {"target_node", workerNodeId},
                         {"target_role", task.targetRole},
                         {"context_ref_count", task.contextRefs.size()}
                     });
        } else {
            logEvent(turn.error ? "error" : "info",
                     "cluster",
                     turn.error ? "Delegated task failed" : "Delegated task completed",
                     QJsonObject{
                         {"task_id", task.taskId},
                         {"trace_id", task.traceId},
                         {"label", delegatedLabel},
                         {"origin_node", task.originNode},
                         {"target_node", workerNodeId},
                         {"target_role", task.targetRole},
                         {"context_ref_count", task.contextRefs.size()}
                     });
        }
        releaseDelegatedLease();
    });
}

void RuntimeCore::publishNodePresence(bool online,
                                      int minIntervalMs,
                                      const QString &reason) {
    if (!_nodeRegistry) {
        return;
    }

    const QString trimmedReason = reason.trimmed();
    const bool suppressRoutineRecentNodesLogs =
        trimmedReason.compare(QStringLiteral("recentNodes"), Qt::CaseInsensitive) == 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (minIntervalMs > 0) {
        QMutexLocker locker(&_presencePublishMutex);
        if (_lastNodePresencePublishAtMs > 0) {
            const qint64 ageMs = nowMs - _lastNodePresencePublishAtMs;
            if (ageMs >= 0 && ageMs < minIntervalMs) {
                if (!trimmedReason.isEmpty() && !suppressRoutineRecentNodesLogs) {
                    qInfo().noquote()
                        << QStringLiteral("RuntimeCore publishNodePresence skipped reason=%1 lastAgeMs=%2 minIntervalMs=%3")
                               .arg(trimmedReason, QString::number(ageMs), QString::number(minIntervalMs));
                }
                return;
            }
        }
        _lastNodePresencePublishAtMs = nowMs;
    } else {
        QMutexLocker locker(&_presencePublishMutex);
        _lastNodePresencePublishAtMs = nowMs;
    }

    const bool verbose = !trimmedReason.isEmpty() && !suppressRoutineRecentNodesLogs;
    QElapsedTimer totalTimer;
    QElapsedTimer phaseTimer;
    if (verbose) {
        totalTimer.start();
        phaseTimer.start();
        const QString registryTransport = _nodeRegistry ? QStringLiteral("p2p") : QStringLiteral("none");
        const QString taskBusTransport   = _taskBus      ? QStringLiteral("p2p") : QStringLiteral("none");
        qInfo().noquote()
            << QStringLiteral("RuntimeCore publishNodePresence start reason=%1 online=%2 registry=%3 taskBus=%4")
                   .arg(trimmedReason,
                        online ? QStringLiteral("true") : QStringLiteral("false"),
                        registryTransport,
                        taskBusTransport);
    } else if (suppressRoutineRecentNodesLogs) {
        totalTimer.start();
    }

    const QString localNodeId = effectiveNodeId(_config);
    const QString localRole = _config.deployment.nodeRole.trimmed();
    int queuedTaskCount = 0;
    if (_taskBus) {
        QSet<QString> pendingIds;
        const auto appendPending = [&pendingIds](const QList<distributed::TaskEnvelope> &tasks) {
            for (const distributed::TaskEnvelope &task : tasks) {
                pendingIds.insert(task.taskId.trimmed());
            }
        };
        appendPending(_taskBus->pendingTasks(localNodeId, QString(), 64));
        if (!localRole.isEmpty()) {
            appendPending(_taskBus->pendingTasks(QString(), localRole, 64));
        }
        pendingIds.remove(QString());
        queuedTaskCount = pendingIds.size();
        if (verbose) {
            qInfo().noquote()
                << QStringLiteral("RuntimeCore publishNodePresence pending tasks reason=%1 queued=%2 elapsedMs=%3")
                       .arg(trimmedReason,
                            QString::number(queuedTaskCount),
                            QString::number(phaseTimer.elapsed()));
            phaseTimer.restart();
        }
    }

    int activeTaskCount = _subagents ? _subagents->runningCount() : 0;
    if (_taskStore) {
        const QVector<TaskRecord> recent = _taskStore->recentTasks(160);
        for (const TaskRecord &task : recent) {
            if (task.state.trimmed().compare(QStringLiteral("running"), Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (task.kind.trimmed().compare(QStringLiteral("delegated_subagent_worker"), Qt::CaseInsensitive) != 0) {
                continue;
            }
            const bool matchesLocalTarget = task.targetNode.trimmed().compare(localNodeId, Qt::CaseInsensitive) == 0;
            const bool matchesLocalRole = !localRole.isEmpty() &&
                task.metadata.value(QStringLiteral("target_role")).toString().trimmed().compare(localRole, Qt::CaseInsensitive) == 0;
            if (matchesLocalTarget || matchesLocalRole) {
                ++activeTaskCount;
            }
        }
    }
    if (verbose) {
        qInfo().noquote()
            << QStringLiteral("RuntimeCore publishNodePresence active tasks reason=%1 active=%2 elapsedMs=%3")
                   .arg(trimmedReason,
                        QString::number(activeTaskCount),
                        QString::number(phaseTimer.elapsed()));
        phaseTimer.restart();
    }

    const QStringList channels = _channels ? _channels->enabledChannels() : configuredChannels(_config);
    const distributed::NodeDescriptor node = localNodeDescriptor(_config,
                                                                 channels,
                                                                 online,
                                                                 activeTaskCount,
                                                                 queuedTaskCount,
                                                                 kDefaultNodeConcurrency);
    const bool published = _nodeRegistry->publishPresence(node);
    if (!published) {
        logEvent("warning", "cluster", "Failed to publish local node presence", QJsonObject{
            {"node_id", node.nodeId},
            {"online", online},
            {"active_task_count", node.activeTaskCount},
            {"queued_task_count", node.queuedTaskCount}
        });
        qWarning().noquote()
            << QStringLiteral("RuntimeCore publishNodePresence failed reason=%1 online=%2 queued=%3 active=%4 elapsedMs=%5")
                   .arg(trimmedReason.isEmpty() ? QStringLiteral("unspecified") : trimmedReason,
                        online ? QStringLiteral("true") : QStringLiteral("false"),
                        QString::number(queuedTaskCount),
                        QString::number(activeTaskCount),
                        QString::number(totalTimer.isValid() ? totalTimer.elapsed() : 0));
    }
    if (verbose) {
        qInfo().noquote()
            << QStringLiteral("RuntimeCore publishNodePresence finished reason=%1 published=%2 elapsedMs=%3 publishStepMs=%4")
                   .arg(trimmedReason,
                        published ? QStringLiteral("true") : QStringLiteral("false"),
                        QString::number(totalTimer.elapsed()),
                        QString::number(phaseTimer.elapsed()));
    } else if (suppressRoutineRecentNodesLogs && totalTimer.elapsed() >= 80) {
        qInfo().noquote()
            << QStringLiteral("RuntimeCore publishNodePresence slow reason=%1 online=%2 queued=%3 active=%4 elapsedMs=%5")
                   .arg(trimmedReason,
                        online ? QStringLiteral("true") : QStringLiteral("false"),
                        QString::number(queuedTaskCount),
                        QString::number(activeTaskCount),
                        QString::number(totalTimer.elapsed()));
    }
}

void RuntimeCore::logEvent(const QString &level,
                           const QString &category,
                           const QString &message,
                           const QJsonObject &metadata) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    ensureSystemStores(cfg.workspacePath());
    QJsonObject effectiveMetadata = metadata;
    const QString traceId = StructuredLog::currentTraceId();
    if (!traceId.isEmpty() &&
        effectiveMetadata.value(QStringLiteral("traceId")).toString().trimmed().isEmpty() &&
        effectiveMetadata.value(QStringLiteral("trace_id")).toString().trimmed().isEmpty()) {
        effectiveMetadata.insert(QStringLiteral("trace_id"), traceId);
    }
    if (_eventLog) {
        _eventLog->append(level, category, message, effectiveMetadata);
    }
    StructuredLog::log(level, category, message, effectiveMetadata);
}

AutomationRecord RuntimeCore::hydrateAutomationRecord(const AutomationRecord &input) {
    AutomationRecord record = input;
    if (record.id.isEmpty()) {
        return record;
    }

    record.provider = normalizedAutomationProvider(record.provider);
    record.scheduleKind = normalizedAutomationScheduleKind(record.scheduleKind, record.trigger);
    record.trigger = record.scheduleKind;

    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    CronService localCron(QDir(cfg.workspacePath()).filePath("cron/jobs.json"));
    CronService *cron = _cron ? _cron.get() : &localCron;
    const QVector<CronJob> jobs = cron->listJobs(true);
    for (const CronJob &job : jobs) {
        if (job.id != record.cronJobId) {
            continue;
        }
        record.nextRunAt = job.state.nextRunAtMs > 0
            ? QDateTime::fromMSecsSinceEpoch(job.state.nextRunAtMs)
            : QDateTime();
        if (job.state.lastRunAtMs > 0 && !record.lastRunAt.isValid()) {
            record.lastRunAt = QDateTime::fromMSecsSinceEpoch(job.state.lastRunAtMs);
        }
        if (record.lastStatus.isEmpty()) {
            record.lastStatus = job.state.lastStatus;
        }
        if (record.lastError.isEmpty()) {
            record.lastError = job.state.lastError;
        }
        break;
    }

    AutomationRunStore runStore(cfg.workspacePath());
    const AutomationRunRecord latestRun = runStore.latest(record.id);
    if (!latestRun.id.isEmpty()) {
        record.lastRunAt = latestRun.finishedAt.isValid() ? latestRun.finishedAt : latestRun.createdAt;
        record.lastStatus = latestRun.status;
        record.lastError = latestRun.error;
        record.lastResultPreview = latestRun.resultPreview;
    }
    record.runCount = runStore.count(record.id);
    return record;
}

QVector<AutomationRecord> RuntimeCore::hydrateAutomationRecords(const QVector<AutomationRecord> &records) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    CronService localCron(QDir(cfg.workspacePath()).filePath("cron/jobs.json"));
    CronService *cron = _cron ? _cron.get() : &localCron;
    QHash<QString, CronJob> jobsById;
    const QVector<CronJob> jobs = cron->listJobs(true);
    for (const CronJob &job : jobs) {
        jobsById.insert(job.id, job);
    }

    AutomationRunStore runStore(cfg.workspacePath());
    QHash<QString, AutomationRunRecord> latestRuns;
    QHash<QString, int> runCounts;
    const QVector<AutomationRunRecord> runs = runStore.list(0);
    for (const AutomationRunRecord &run : runs) {
        if (run.automationId.isEmpty()) {
            continue;
        }
        runCounts.insert(run.automationId, runCounts.value(run.automationId, 0) + 1);
        const bool hasLatest = latestRuns.contains(run.automationId);
        const QDateTime runAt = run.finishedAt.isValid() ? run.finishedAt : run.createdAt;
        const QDateTime latestAt = hasLatest
            ? (latestRuns.value(run.automationId).finishedAt.isValid()
                ? latestRuns.value(run.automationId).finishedAt
                : latestRuns.value(run.automationId).createdAt)
            : QDateTime();
        if (!hasLatest || runAt > latestAt) {
            latestRuns.insert(run.automationId, run);
        }
    }

    QVector<AutomationRecord> out;
    out.reserve(records.size());
    for (const AutomationRecord &record : records) {
        AutomationRecord item = record;
        item.provider = normalizedAutomationProvider(item.provider);
        item.scheduleKind = normalizedAutomationScheduleKind(item.scheduleKind, item.trigger);
        item.trigger = item.scheduleKind;

        if (jobsById.contains(item.cronJobId)) {
            const CronJob job = jobsById.value(item.cronJobId);
            item.nextRunAt = job.state.nextRunAtMs > 0
                ? QDateTime::fromMSecsSinceEpoch(job.state.nextRunAtMs)
                : QDateTime();
            if (job.state.lastRunAtMs > 0 && !item.lastRunAt.isValid()) {
                item.lastRunAt = QDateTime::fromMSecsSinceEpoch(job.state.lastRunAtMs);
            }
            if (item.lastStatus.isEmpty()) {
                item.lastStatus = job.state.lastStatus;
            }
            if (item.lastError.isEmpty()) {
                item.lastError = job.state.lastError;
            }
        }

        if (latestRuns.contains(item.id)) {
            const AutomationRunRecord latest = latestRuns.value(item.id);
            item.lastRunAt = latest.finishedAt.isValid() ? latest.finishedAt : latest.createdAt;
            item.lastStatus = latest.status;
            item.lastError = latest.error;
            item.lastResultPreview = latest.resultPreview;
        }
        item.runCount = runCounts.value(item.id, item.runCount);
        out.append(item);
    }
    return out;
}

bool RuntimeCore::syncAutomationSchedule(AutomationRecord &record, QString *error) {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    CronService localCron(QDir(cfg.workspacePath()).filePath("cron/jobs.json"));
    CronService *cron = _cron ? _cron.get() : &localCron;

    record.provider = normalizedAutomationProvider(record.provider);
    record.scheduleKind = normalizedAutomationScheduleKind(record.scheduleKind, record.trigger);
    record.trigger = record.scheduleKind;

    const QString existingCronJobId = record.cronJobId.trimmed();
    if (!record.enabled || !automationUsesSchedule(record)) {
        if (!existingCronJobId.isEmpty()) {
            cron->removeJob(existingCronJobId);
        }
        record.cronJobId.clear();
        record.nextRunAt = QDateTime();
        return true;
    }

    QString scheduleError;
    const CronSchedule schedule = cronScheduleForAutomation(record, &scheduleError);
    if (!scheduleError.isEmpty()) {
        if (error) {
            *error = scheduleError;
        }
        return false;
    }

    if (!existingCronJobId.isEmpty()) {
        cron->removeJob(existingCronJobId);
    }

    const CronJob job = cron->addJob(record.name,
                                     schedule,
                                     record.id,
                                     false,
                                     QString(),
                                     QString(),
                                     schedule.kind == "at",
                                     QStringLiteral("automation"));
    if (job.id.isEmpty() || job.id == "error") {
        if (error) {
            *error = job.name.isEmpty() ? QStringLiteral("Failed to create automation schedule.") : job.name;
        }
        return false;
    }

    record.cronJobId = job.id;
    record.nextRunAt = job.state.nextRunAtMs > 0
        ? QDateTime::fromMSecsSinceEpoch(job.state.nextRunAtMs)
        : QDateTime();
    return true;
}

void RuntimeCore::reconcileAutomationSchedules() {
    if (!_automations || !_cron) {
        return;
    }

    QVector<AutomationRecord> records = _automations->list(0);
    const QVector<CronJob> jobs = _cron->listJobs(true);
    QSet<QString> referencedJobIds;
    bool changed = false;

    for (AutomationRecord &record : records) {
        record.provider = normalizedAutomationProvider(record.provider);
        record.scheduleKind = normalizedAutomationScheduleKind(record.scheduleKind, record.trigger);
        record.trigger = record.scheduleKind;

        const bool shouldSchedule = record.enabled && automationUsesSchedule(record);
        bool hasJob = false;
        if (!record.cronJobId.trimmed().isEmpty()) {
            for (const CronJob &job : jobs) {
                if (job.id == record.cronJobId.trimmed()) {
                    hasJob = true;
                    referencedJobIds.insert(job.id);
                    break;
                }
            }
        }

        if (!shouldSchedule) {
            if (!record.cronJobId.trimmed().isEmpty()) {
                _cron->removeJob(record.cronJobId.trimmed());
                record.cronJobId.clear();
                record.nextRunAt = QDateTime();
                _automations->save(record, nullptr);
                changed = true;
            }
            continue;
        }

        if (!hasJob) {
            QString error;
            AutomationRecord next = record;
            if (syncAutomationSchedule(next, &error)) {
                _automations->save(next, nullptr);
                if (!next.cronJobId.isEmpty()) {
                    referencedJobIds.insert(next.cronJobId);
                }
                changed = true;
            } else {
                logEvent("warning",
                         "automation",
                         "Automation schedule could not be reconciled",
                         QJsonObject{{"automation_id", record.id}, {"error", error}});
            }
        }
    }

    for (const CronJob &job : jobs) {
        if (job.payload.kind == "automation" && !referencedJobIds.contains(job.id)) {
            _cron->removeJob(job.id);
            changed = true;
        }
    }

    if (changed) {
        logEvent("info", "automation", "Automation schedules reconciled");
    }
}

void RuntimeCore::teardownRuntime() {
    stopGatewayServices();
    stopDelegationPolling();
    publishNodePresence(false);
    {
        QMutexLocker locker(&_presencePublishMutex);
        _lastNodePresencePublishAtMs = 0;
    }

    if (_bus && _agent) {
        QObject::disconnect(_bus.get(), nullptr, _agent.get(), nullptr);
    }

    if (_agent) {
        agent::AgentLoop *agent = _agent.release();
        const bool destroyOnAgentThread = agent->thread() && agent->thread() != QThread::currentThread();
        if (destroyOnAgentThread) {
            const bool destroyed = QMetaObject::invokeMethod(
                agent,
                [agent]() { delete agent; },
                Qt::BlockingQueuedConnection
            );
            if (!destroyed) {
                delete agent;
            }
        } else {
            delete agent;
        }
    }

    if (_agentThread && _agentThread->isRunning()) {
        _agentThread->requestInterruption();
        _agentThread->quit();
        if (!_agentThread->wait(8000)) {
            qWarning(lcRuntime) << "Agent thread did not stop in time, forcing termination";
            _agentThread->terminate();
            _agentThread->wait(2000);
        }
    }
    _channels.reset();
    _heartbeat.reset();
    _cron.reset();
    _mcp.reset();
    _subagents.reset();
    _provider.reset();
    _bus.reset();
    _agentThread.reset();
    _approvals.reset();
    _automations.reset();
    _automationRuns.reset();
    _eventLog.reset();
    _notifications.reset();
    _plugins.reset();
    _skills.reset();
    _resources.reset();
    _nodeRegistry.reset();
    _taskBus.reset();
    _taskStore.reset();
    _delegationPoller.reset();
    {
        QMutexLocker locker(&_delegatedWorkerLeaseMutex);
        _activeDelegatedWorkerTasks.clear();
    }
    _initialized = false;
}

config::Config RuntimeCore::activeConfig() const {
    if (_initialized) {
        return _config;
    }
    return _hasConfigOverride ? _configOverride : config::ConfigLoader::load();
}

CronStatus RuntimeCore::cronStatus() const {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    if (_cron) {
        return _cron->status();
    }
    CronService localCron(QDir(cfg.workspacePath()).filePath("cron/jobs.json"));
    return localCron.status();
}

QStringList RuntimeCore::enabledChannels() const {
    const config::Config cfg = _initialized ? _config : config::ConfigLoader::load();
    if (_channels) {
        return _channels->enabledChannels();
    }
    bus::MessageBus statusBus;
    channels::ChannelManager statusChannels(cfg, statusBus);
    return statusChannels.enabledChannels();
}

QString RuntimeCore::agentThreadName() const {
    return _agentThread ? _agentThread->objectName() : QStringLiteral("AgentThread");
}

} // namespace yaos::runtime
