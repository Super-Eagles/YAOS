#include "ProviderFactory.h"

#include <QDebug>

#include "../config/ConfigLoader.h"
#include "AnthropicProvider.h"
#include "EchoProvider.h"
#include "OpenAICompatibleProvider.h"
#include "ProviderOAuth.h"
#include "ProviderRegistry.h"

namespace yaos::providers {

namespace {

config::ProviderConfig providerConfigByName(const config::Config &config, const QString &name) {
    if (name == "custom") return config.providers.custom;
    if (name == "azure_openai") return config.providers.azureOpenAI;
    if (name == "anthropic") return config.providers.anthropic;
    if (name == "openai") return config.providers.openai;
    if (name == "codebuddy") return config.providers.codebuddy;
    if (name == "openrouter") return config.providers.openrouter;
    if (name == "deepseek") return config.providers.deepseek;
    if (name == "groq") return config.providers.groq;
    if (name == "zhipu") return config.providers.zhipu;
    if (name == "dashscope") return config.providers.dashscope;
    if (name == "vllm") return config.providers.vllm;
    if (name == "gemini") return config.providers.gemini;
    if (name == "moonshot") return config.providers.moonshot;
    if (name == "minimax") return config.providers.minimax;
    if (name == "aihubmix") return config.providers.aihubmix;
    if (name == "siliconflow") return config.providers.siliconflow;
    if (name == "volcengine") return config.providers.volcengine;
    if (name == "openai_codex") return config.providers.openaiCodex;
    if (name == "github_copilot") return config.providers.githubCopilot;
    return config::ProviderConfig();
}

config::ProviderConfig *providerConfigByName(config::Config &config, const QString &name) {
    if (name == "custom") return &config.providers.custom;
    if (name == "azure_openai") return &config.providers.azureOpenAI;
    if (name == "anthropic") return &config.providers.anthropic;
    if (name == "openai") return &config.providers.openai;
    if (name == "codebuddy") return &config.providers.codebuddy;
    if (name == "openrouter") return &config.providers.openrouter;
    if (name == "deepseek") return &config.providers.deepseek;
    if (name == "groq") return &config.providers.groq;
    if (name == "zhipu") return &config.providers.zhipu;
    if (name == "dashscope") return &config.providers.dashscope;
    if (name == "vllm") return &config.providers.vllm;
    if (name == "gemini") return &config.providers.gemini;
    if (name == "moonshot") return &config.providers.moonshot;
    if (name == "minimax") return &config.providers.minimax;
    if (name == "aihubmix") return &config.providers.aihubmix;
    if (name == "siliconflow") return &config.providers.siliconflow;
    if (name == "volcengine") return &config.providers.volcengine;
    if (name == "openai_codex") return &config.providers.openaiCodex;
    if (name == "github_copilot") return &config.providers.githubCopilot;
    return nullptr;
}

void persistResolvedProviderConfig(const QString &name, const config::ProviderConfig &providerConfig) {
    config::Config latest = config::ConfigLoader::load();
    config::ProviderConfig *target = providerConfigByName(latest, name);
    if (!target) {
        qWarning() << "Failed to persist refreshed OAuth config for unknown provider" << name;
        return;
    }
    *target = providerConfig;
    if (!config::ConfigLoader::save(latest)) {
        qWarning() << "Failed to persist refreshed OAuth config for provider" << name;
    }
}

} // namespace

std::unique_ptr<LLMProvider> ProviderFactory::create(const config::Config &config, QString *selectedProvider) {
    const QString model = config.agentDefaults.model;
    QString providerName = config.matchedProviderName(model).toLower();
    providerName.replace('-', '_');
    if (providerName == "azureopenai") providerName = "azure_openai";
    if (providerName == "code_buddy") providerName = "codebuddy";
    if (providerName == "openaicodex") providerName = "openai_codex";
    if (providerName == "githubcopilot") providerName = "github_copilot";
    if (providerName == "echo") {
        if (selectedProvider) {
            *selectedProvider = providerName;
        }
        const QString echoModel = model.trimmed().isEmpty() ? QStringLiteral("echo") : model.trimmed();
        return std::make_unique<EchoProvider>(echoModel);
    }
    if (providerName.isEmpty()) {
        providerName = "openai";
    }
    const ProviderSpec spec = findProviderSpec(providerName);
    config::ProviderConfig pCfg = providerConfigByName(config, providerName);

    QString routedModel = model;
    if (!spec.name.isEmpty()) {
        routedModel = routeModelForProvider(spec, model);
    }
    QString apiBase = pCfg.apiBase;
    if (apiBase.trimmed().isEmpty() && !spec.defaultApiBase.isEmpty()) {
        apiBase = spec.defaultApiBase;
    }

    const ProviderOAuthResult auth = resolveProviderAccess(providerName, pCfg, true);
    if (auth.changed) {
        pCfg = auth.config;
        persistResolvedProviderConfig(providerName, pCfg);
    }
    if (!auth.apiBase.trimmed().isEmpty()) {
        apiBase = auth.apiBase;
    }
    const QString resolvedApiKey = auth.apiKey.trimmed().isEmpty()
        ? (spec.isOAuth ? QString() : pCfg.apiKey)
        : auth.apiKey;
    const QHash<QString, QString> resolvedHeaders =
        auth.headers.isEmpty() ? pCfg.extraHeaders : auth.headers;

    if (selectedProvider) {
        *selectedProvider = providerName;
    }

    const bool hasApiKey = !resolvedApiKey.trimmed().isEmpty();
    const bool hasExtraHeaders = !resolvedHeaders.isEmpty();
    const bool allowsEmptyApiKey = providerName == "custom" || providerName == "vllm";

    if (providerName == "anthropic") {
        if (!hasApiKey && !spec.isOAuth) {
            return std::make_unique<EchoProvider>(routedModel);
        }
        return std::make_unique<AnthropicProvider>(
            resolvedApiKey, apiBase, routedModel, config.agentDefaults.reasoningEffort);
    }

    if (!hasApiKey && !hasExtraHeaders && !allowsEmptyApiKey && !spec.isOAuth) {
        return std::make_unique<EchoProvider>(routedModel);
    }

    return std::make_unique<OpenAICompatibleProvider>(
        resolvedApiKey,
        apiBase,
        routedModel,
        providerName,
        config.agentDefaults.reasoningEffort,
        resolvedHeaders
    );
}

} // namespace yaos::providers
