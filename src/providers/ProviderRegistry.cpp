#include "ProviderRegistry.h"

namespace yaos::providers {

const QVector<ProviderSpec> &providerSpecs() {
    static const QVector<ProviderSpec> specs = {
        {"custom", {}, "", "", {}, false, false, false, false},
        {"azure_openai", {"azure", "azure-openai"}, "", "", {}, false, false, false, false},
        {"openrouter", {"openrouter"}, "https://openrouter.ai/api/v1", "openrouter", {}, false, true, false, false},
        {"aihubmix", {"aihubmix"}, "https://aihubmix.com/v1", "openai", {}, true, true, false, false},
        {"siliconflow", {"siliconflow"}, "https://api.siliconflow.cn/v1", "openai", {}, false, true, false, false},
        {"volcengine", {"volcengine", "volces", "ark"}, "https://ark.cn-beijing.volces.com/api/v3", "volcengine", {}, false, true, false, false},
        {"anthropic", {"anthropic", "claude"}, "https://api.anthropic.com/v1", "", {}, false, false, false, false},
        {"openai", {"openai", "gpt"}, "https://api.openai.com/v1", "", {}, false, false, false, false},
        {"codebuddy", {"codebuddy"}, "", "", {}, false, false, false, false},
        {"openai_codex", {"openai-codex"}, "https://api.openai.com/v1", "", {}, false, false, false, true},
        {"github_copilot", {"github_copilot", "copilot"}, "https://api.githubcopilot.com", "github_copilot", {"github_copilot/"}, false, false, false, true},
        {"deepseek", {"deepseek"}, "https://api.deepseek.com/v1", "deepseek", {"deepseek/"}, false, false, false, false},
        {"gemini", {"gemini"}, "https://generativelanguage.googleapis.com/v1beta/openai", "gemini", {"gemini/"}, false, false, false, false},
        {"zhipu", {"zhipu", "glm", "zai"}, "https://open.bigmodel.cn/api/paas/v4", "zai", {"zai/", "zhipu/"}, false, false, false, false},
        {"dashscope", {"qwen", "dashscope"}, "https://dashscope.aliyuncs.com/compatible-mode/v1", "dashscope", {"dashscope/"}, false, false, false, false},
        {"moonshot", {"moonshot", "kimi"}, "https://api.moonshot.ai/v1", "moonshot", {"moonshot/"}, false, false, false, false},
        {"minimax", {"minimax"}, "https://api.minimax.io/v1", "minimax", {"minimax/"}, false, false, false, false},
        {"vllm", {"vllm"}, "", "hosted_vllm", {}, false, false, true, false},
        {"groq", {"groq"}, "https://api.groq.com/openai/v1", "groq", {"groq/"}, false, false, false, false}
    };
    return specs;
}

ProviderSpec findProviderSpec(const QString &name) {
    const QString normalized = name.trimmed().toLower();
    for (const ProviderSpec &spec : providerSpecs()) {
        if (spec.name == normalized) {
            return spec;
        }
    }
    return ProviderSpec();
}

QString routeModelForProvider(const ProviderSpec &spec, const QString &model) {
    QString routed = model.trimmed();
    if (routed.isEmpty()) {
        return routed;
    }

    if (spec.stripModelPrefix && routed.contains('/')) {
        routed = routed.section('/', 1);
    }
    if (spec.litellmPrefix.isEmpty()) {
        return routed;
    }

    for (const QString &skip : spec.skipPrefixes) {
        if (!skip.isEmpty() && routed.startsWith(skip)) {
            return routed;
        }
    }

    const QString prefix = spec.litellmPrefix + "/";
    if (!routed.startsWith(prefix)) {
        routed = prefix + routed;
    }
    return routed;
}

} // namespace yaos::providers
