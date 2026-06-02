#ifndef YAOS_CONFIG_CONFIG_H
#define YAOS_CONFIG_CONFIG_H

#include <QHash>
#include <QList>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace yaos::config {

struct WhatsAppConfig {
    bool enabled = false;
    QString bridgeUrl = "ws://localhost:3001";
    QString bridgeToken;
    QStringList allowFrom;
};

struct TelegramConfig {
    bool enabled = false;
    QString token;
    QStringList allowFrom;
    QString proxy;
    bool replyToMessage = false;
    bool transcribeVoice = false;
    bool transcribeAudio = false;
    QString transcriptionProvider = "auto";
    QString transcriptionModel = "whisper-1";
    QString transcriptionLanguage;
    QString transcriptionPrompt;
};

struct SlackDMConfig {
    bool enabled = true;
    QString policy = "open";
    QStringList allowFrom;
};

struct SlackConfig {
    bool enabled = false;
    QString mode = "socket";
    QString botToken;
    QString appToken;
    QString reactEmoji = "eyes";
    QString groupPolicy = "mention";
    QStringList groupAllowFrom;
    SlackDMConfig dm;
    bool replyInThread = true;
};

struct FeishuConfig {
    bool enabled = false;
    QString appId;
    QString appSecret;
    QString encryptKey;
    QString verificationToken;
    QStringList allowFrom;
};

struct DingTalkConfig {
    bool enabled = false;
    QString clientId;
    QString clientSecret;
    QStringList allowFrom;
};

struct DiscordConfig {
    bool enabled = false;
    QString token;
    QStringList allowFrom;
};

struct MatrixConfig {
    bool enabled = false;
    QString homeserver = "https://matrix.org";
    QString accessToken;
    QString userId;
    QString deviceId;
    QStringList allowFrom;
};

struct EmailConfig {
    bool enabled = false;
    bool consentGranted = false;
    QString imapHost;
    int imapPort = 993;
    bool imapUseSsl = true;
    QString imapUsername;
    QString imapPassword;
    QString smtpHost;
    int smtpPort = 587;
    bool smtpUseTls = true;
    QString smtpUsername;
    QString smtpPassword;
    QString fromAddress;
    QStringList allowFrom;
};

struct MochatConfig {
    bool enabled = false;
    QString baseUrl = "https://mochat.io";
    QString clawToken;
    QString agentUserId;
};

struct QQConfig {
    bool enabled = false;
    QString appId;
    QString secret;
    QStringList allowFrom;
};

struct ChannelsConfig {
    bool sendProgress = true;
    bool sendToolHints = false;
    TelegramConfig telegram;
    SlackConfig slack;
    WhatsAppConfig whatsapp;
    FeishuConfig feishu;
    DingTalkConfig dingtalk;
    DiscordConfig discord;
    MatrixConfig matrix;
    EmailConfig email;
    MochatConfig mochat;
    QQConfig qq;
};



struct AgentDefaults {
    QString workspace = "~/.yaos/workspace";
    QString model = "anthropic/claude-opus-4-5";
    QString provider = "auto";
    int maxTokens = 8192;
    double temperature = 0.1;
    int maxToolIterations = 40;
    int memoryWindow = 100;
    QString reasoningEffort;
};

struct ExecToolConfig {
    int timeout = 60;
    QString pathAppend;
};

struct WebSearchConfig {
    QString provider = "brave";
    QString apiKey;
    QString baseUrl;
    int maxResults = 5;
};

struct WebToolsConfig {
    QString proxy;
    WebSearchConfig search;
};

struct MCPServerConfig {
    QString type;
    QString command;
    QStringList args;
    QHash<QString, QString> env;
    QString url;
    QHash<QString, QString> headers;
    int toolTimeout = 30;
};

struct ToolCapabilitiesConfig {
    bool web = true;
    bool filesystem = true;
    bool exec = false;
    bool messaging = true;
    bool spawn = true;
    bool cron = true;
    bool mcp = true;
};

struct ToolPoliciesConfig {
    QString readFile = "allow";
    QString writeFile = "confirm";
    QString listDir = "allow";
    QString exec = "deny";
    QString message = "allow";
    QString spawn = "confirm";
    QString cron = "confirm";
    QString mcpCall = "confirm";
    QString pluginCall = "confirm";
};

struct SecurityConfig {
    ToolPoliciesConfig toolPolicies;
    bool auditToolCalls = true;
    bool notifyOnApprovalRequired = true;
    bool notifyOnToolDenied = true;
};

struct ToolsConfig {
    WebToolsConfig web;
    ExecToolConfig exec;
    bool restrictToWorkspace = true;
    ToolCapabilitiesConfig capabilities;
    QHash<QString, MCPServerConfig> mcpServers;
};

struct ProviderConfig {
    QString apiKey;
    QString apiBase;
    QString model;
    QStringList availableModels;
    QStringList enabledModels;
    QHash<QString, QString> extraHeaders;
    QString oauthIssuer;
    QString oauthClientId;
    QString oauthScope;
    QString oauthAccessToken;
    QString oauthRefreshToken;
    QString oauthIdToken;
    QString oauthTokenType;
    QString oauthAccountId;
    QString oauthExpiresAt;
    QString oauthLastRefreshAt;
    QString oauthDeviceCode;
    QString oauthDeviceAuthId;
    QString oauthUserCode;
    QString oauthVerificationUrl;
    QString oauthLastError;
    int oauthIntervalSec = 5;
};

struct ExtensionProfileConfig {
    bool enabled = true;
    QString provider = "auto";
    QString model;
    QString note;
    QStringList triggers;
};

struct ExtensionsConfig {
    QHash<QString, ExtensionProfileConfig> plugins;
    QHash<QString, ExtensionProfileConfig> skills;
};

struct ProvidersConfig {
    ProviderConfig custom;
    ProviderConfig azureOpenAI;
    ProviderConfig anthropic;
    ProviderConfig openai;
    ProviderConfig codebuddy;
    ProviderConfig openrouter;
    ProviderConfig deepseek;
    ProviderConfig groq;
    ProviderConfig zhipu;
    ProviderConfig dashscope;
    ProviderConfig vllm;
    ProviderConfig gemini;
    ProviderConfig moonshot;
    ProviderConfig minimax;
    ProviderConfig aihubmix;
    ProviderConfig siliconflow;
    ProviderConfig volcengine;
    ProviderConfig openaiCodex;
    ProviderConfig githubCopilot;
};

struct HeartbeatConfig {
    bool enabled = true;
    int intervalS = 1800;
};

struct GatewayConfig {
    QString host = "0.0.0.0";
    int port = 18790;
    HeartbeatConfig heartbeat;
};

struct DeploymentConfig {
    QString mode = "standalone";
    QString clusterId = "local";
    QString nodeId = "desktop-primary";
    QString nodeRole = "desktop";
    QStringList nodeTags;
    QString gatewayUrl;
    QString controlPlaneUrl;
    QString registryUrl;
};

struct RuntimeConfig {
    QString mode = "embedded";
    QString endpoint = "http://127.0.0.1:18890";
    QString advertiseEndpoint;
    bool preferLocal = true;
    bool autoSpawnLocalDaemon = true;
    bool autoSpawnLocalService = true;
};

struct ConversationStoreConfig {
    QString driver = "auto";
    QString path;
    QString connectionString;
};

struct FactStoreConfig {
    QString driver = "auto";
    QString path;
    QString connectionString;
};

struct VectorStoreConfig {
    QString driver = "none";
    QString endpoint;
    QString apiKey;
    QString collection = "yaos_memory_chunks";
    int topK = 8;
};

struct HotStoreConfig {
    QString driver = "none";
    QString endpoint;
    QString keyPrefix = "yaos:";
    int ttlS = 21600;
};

struct EmbeddingConfig {
    QString provider = "openai";
    QString model = "text-embedding-3-small";
    QString apiBase;
    QString apiKey;
    int batchSize = 16;
    int dimensions = 1536;
};

struct MemoryServiceConfig {
    bool enabled = false;
    QString endpoint = "http://127.0.0.1:18891";
    QString apiKey;
    int timeoutMs = 12000;
    bool autoSpawnLocalService = true;
};

struct MemoryExportConfig {
    bool writeMarkdown = true;
    bool writeSessionJsonl = true;
};

struct DelegationTemplateConfig {
    QString id;
    QString name;
    QString kind = "single";
    QString note;
    QString updatedAt;
    QJsonObject request;
};

struct MemoryConfig {
    QString mode = "legacy";
    QString backend = "legacy";
    int recentWindow = 24;
    int retrievalTopK = 8;
    bool enableDailySummaries = true;
    ConversationStoreConfig conversation;
    FactStoreConfig facts;
    VectorStoreConfig vector;
    HotStoreConfig hot;
    EmbeddingConfig embedding;
    MemoryServiceConfig service;
    MemoryExportConfig exports;
    QList<DelegationTemplateConfig> delegationTemplates;
};

struct Config {
    AgentDefaults agentDefaults;
    ToolsConfig tools;
    SecurityConfig security;
    ChannelsConfig channels;
    ProvidersConfig providers;
    ExtensionsConfig extensions;
    GatewayConfig gateway;
    DeploymentConfig deployment;
    RuntimeConfig runtime;
    MemoryConfig memory;

    QString workspacePath() const;

    QJsonObject toJson() const;
    static Config fromJson(const QJsonObject &obj);

    ProviderConfig matchedProvider(const QString &model = QString()) const;
    QString matchedProviderName(const QString &model = QString()) const;

    ProviderConfig *providerById(const QString &providerId);
    const ProviderConfig *providerById(const QString &providerId) const;

    // Canonical provider ID normalization: toLower, replace '-' with '_',
    // plus legacy alias resolution. Single authoritative implementation.
    static QString normalizeProviderId(const QString &providerId);
    QString normalizedDeploymentMode() const;
    QString normalizedRuntimeMode() const;
    QString normalizedMemoryMode() const;
    QString normalizedMemoryBackend() const;
    bool usesClusterDeployment() const;
    bool usesRemoteRuntime() const;
    bool usesLayeredMemory() const;
};

} // namespace yaos::config

#endif // YAOS_CONFIG_CONFIG_H
