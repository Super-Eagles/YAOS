.pragma library

var PROVIDER_DEFINITIONS = [
    {"key": "anthropic", "title": "Anthropic", "hint": "Claude 原生接口"},
    {"key": "openai", "title": "OpenAI", "hint": "GPT / Responses 接口"},
    {"key": "openrouter", "title": "OpenRouter", "hint": "统一模型路由"},
    {"key": "deepseek", "title": "DeepSeek", "hint": "高速推理"},
    {"key": "groq", "title": "Groq", "hint": "超低延迟"},
    {"key": "gemini", "title": "Gemini", "hint": "Google 多模态"},
    {"key": "dashscope", "title": "DashScope", "hint": "通义 / 阿里云"},
    {"key": "zhipu", "title": "Zhipu", "hint": "GLM 系列"},
    {"key": "moonshot", "title": "Moonshot", "hint": "Kimi 长上下文"},
    {"key": "minimax", "title": "MiniMax", "hint": "通用模型云"},
    {"key": "volcengine", "title": "Volcengine", "hint": "豆包 / Ark"},
    {"key": "siliconflow", "title": "SiliconFlow", "hint": "中文聚合路由"},
    {"key": "aihubmix", "title": "AIHubMix", "hint": "聚合型接口"},
    {"key": "vllm", "title": "vLLM", "hint": "本地 / 自托管"},
    {"key": "custom", "title": "Custom", "hint": "OpenAI 兼容接口"},
    {"key": "azureOpenai", "title": "Azure OpenAI", "hint": "Azure 部署"},
    {"key": "openaiCodex", "title": "OpenAI Codex", "hint": "代码代理端点"},
    {"key": "githubCopilot", "title": "GitHub Copilot", "hint": "OAuth / 手动模型"},
    {"key": "codebuddy", "title": "CodeBuddy", "hint": "手动凭据 / API Base"}
];

var QUICK_CHANNELS = [
    {"key": "telegram", "title": "Telegram", "description": "Bot Token 与访问策略"},
    {"key": "slack", "title": "Slack", "description": "Socket App 与私信策略"},
    {"key": "whatsapp", "title": "WhatsApp", "description": "桥接地址与会话路由"},
    {"key": "discord", "title": "Discord", "description": "Bot Token 与允许名单"},
    {"key": "feishu", "title": "Feishu", "description": "应用密钥与校验"},
    {"key": "dingtalk", "title": "DingTalk", "description": "客户端凭据"}
];

var RUNTIME_CAPABILITIES = [
    {"key": "web", "title": "网页工具"},
    {"key": "filesystem", "title": "文件系统"},
    {"key": "exec", "title": "命令执行"},
    {"key": "messaging", "title": "消息发送"},
    {"key": "spawn", "title": "子代理"},
    {"key": "cron", "title": "定时任务"},
    {"key": "mcp", "title": "MCP 接入"}
];

var WEB_SEARCH_PROVIDERS = [
    {"key": "brave", "title": "Brave"},
    {"key": "duckduckgo", "title": "DuckDuckGo"},
    {"key": "tavily", "title": "Tavily"},
    {"key": "searxng", "title": "SearXNG"},
    {"key": "jina", "title": "Jina"}
];

var DEPLOYMENT_MODES = [
    {"key": "standalone", "title": "单机模式"},
    {"key": "cluster", "title": "集群模式"}
];

var RUNTIME_MODES = [
    {"key": "embedded", "title": "内嵌运行时"},
    {"key": "daemon", "title": "守护进程"},
    {"key": "remote", "title": "远端运行时"}
];

var MEMORY_MODES = [
    {"key": "legacy", "title": "传统摘要"},
    {"key": "layered", "title": "分层混合"}
];

var MEMORY_BACKENDS = [
    {"key": "legacy", "title": "传统 Markdown"},
    {"key": "hybrid_local", "title": "本地混合"},
    {"key": "hybrid_cluster", "title": "集群混合"}
];

var DELEGATION_TEMPLATE_KINDS = [
    {"key": "single", "title": "单任务草稿"},
    {"key": "batch", "title": "批量草稿"}
];

var POLICY_OPTIONS = [
    {"key": "allow", "title": "允许"},
    {"key": "confirm", "title": "需确认"},
    {"key": "deny", "title": "拒绝"}
];

var APPROVAL_SCOPE_OPTIONS = ["session", "always"];

function cloneOptions(source) {
    var out = [];
    for (var i = 0; i < (source || []).length; ++i) {
        var item = source[i] || ({});
        var copy = {};
        for (var key in item) {
            copy[key] = item[key];
        }
        out.push(copy);
    }
    return out;
}

function providerDefinitions() {
    return cloneOptions(PROVIDER_DEFINITIONS);
}

function quickChannels() {
    return cloneOptions(QUICK_CHANNELS);
}

function runtimeCapabilities() {
    return cloneOptions(RUNTIME_CAPABILITIES);
}

function webSearchProviders() {
    return cloneOptions(WEB_SEARCH_PROVIDERS);
}

function deploymentModes() {
    return cloneOptions(DEPLOYMENT_MODES);
}

function runtimeModes() {
    return cloneOptions(RUNTIME_MODES);
}

function memoryModes() {
    return cloneOptions(MEMORY_MODES);
}

function memoryBackends() {
    return cloneOptions(MEMORY_BACKENDS);
}

function delegationTemplateKinds() {
    return cloneOptions(DELEGATION_TEMPLATE_KINDS);
}

function policyOptions() {
    return cloneOptions(POLICY_OPTIONS);
}

function approvalScopeOptions() {
    return APPROVAL_SCOPE_OPTIONS.slice(0);
}

function canonicalProviderKey(key, definitions) {
    var raw = String(key || "").trim();
    if (raw.length === 0) {
        return "";
    }
    var normalized = raw.toLowerCase().replace(/-/g, "_");
    if (normalized === "azureopenai" || normalized === "azure_openai") {
        return "azureOpenai";
    }
    if (normalized === "code_buddy" || normalized === "codebuddy") {
        return "codebuddy";
    }
    if (normalized === "openaicodex" || normalized === "openai_codex") {
        return "openaiCodex";
    }
    if (normalized === "githubcopilot" || normalized === "github_copilot") {
        return "githubCopilot";
    }

    var current = definitions || PROVIDER_DEFINITIONS;
    for (var i = 0; i < current.length; ++i) {
        var candidate = current[i].key;
        if (String(candidate || "").toLowerCase() === normalized) {
            return candidate;
        }
    }
    return raw;
}

function runtimeProviderKey(key, definitions) {
    var canonical = canonicalProviderKey(key, definitions);
    if (canonical === "azureOpenai") {
        return "azure_openai";
    }
    if (canonical === "openaiCodex") {
        return "openai_codex";
    }
    if (canonical === "githubCopilot") {
        return "github_copilot";
    }
    return canonical;
}

function providerDefinitionByKey(key, definitions) {
    var current = definitions || PROVIDER_DEFINITIONS;
    var canonical = canonicalProviderKey(key, current);
    for (var i = 0; i < current.length; ++i) {
        if (current[i].key === canonical) {
            return current[i];
        }
    }
    return null;
}

function selectedProviderDefinitions(selectedProviderPanelKey, definitions) {
    var current = definitions || PROVIDER_DEFINITIONS;
    var selected = canonicalProviderKey(selectedProviderPanelKey, current);
    if (selected.length === 0 && current.length > 0) {
        selected = canonicalProviderKey(current[0].key, current);
    }

    var out = [];
    for (var i = 0; i < current.length; ++i) {
        if (canonicalProviderKey(current[i].key, current) === selected) {
            out.push(current[i]);
            break;
        }
    }
    return out;
}

function delegationTemplateKindIndex(value, kinds) {
    var selected = String(value || "single").trim().toLowerCase();
    var current = kinds || DELEGATION_TEMPLATE_KINDS;
    for (var i = 0; i < current.length; ++i) {
        if ((current[i].key || "") === selected) {
            return i;
        }
    }
    return 0;
}

function delegationTemplateKindTitle(kind, kinds) {
    var current = kinds || DELEGATION_TEMPLATE_KINDS;
    var index = delegationTemplateKindIndex(kind, current);
    return (current[index] && current[index].title) || "单任务草稿";
}

function policyIndex(value, policies) {
    var current = policies || POLICY_OPTIONS;
    for (var i = 0; i < current.length; ++i) {
        if (current[i].key === value) {
            return i;
        }
    }
    return 1;
}

function webSearchProviderIndex(value, providers) {
    var selected = String(value || "brave").toLowerCase();
    var current = providers || WEB_SEARCH_PROVIDERS;
    for (var i = 0; i < current.length; ++i) {
        if ((current[i].key || "") === selected) {
            return i;
        }
    }
    return 0;
}

function canonicalKey(key, canonicalProviderKeyFn) {
    return canonicalProviderKeyFn ? canonicalProviderKeyFn(key) : String(key || "");
}

function stringValue(value) {
    return String(value || "").trim();
}

function usesOAuth(key, canonicalProviderKeyFn) {
    var canonical = canonicalKey(key, canonicalProviderKeyFn);
    return canonical === "openaiCodex" || canonical === "githubCopilot";
}

function supportsModelSync(key, canonicalProviderKeyFn) {
    return canonicalKey(key, canonicalProviderKeyFn) !== "azureOpenai";
}

function headerHint(key, canonicalProviderKeyFn) {
    var canonical = canonicalKey(key, canonicalProviderKeyFn);
    if (canonical === "azureOpenai") {
        return "Azure OpenAI 需要把部署名填在模型字段里,不能只靠自动同步.";
    }
    if (canonical === "codebuddy") {
        return "CodeBuddy 目前只支持手动凭据.先填写 API Base;API Key 会以 X-Api-Key 发送,也可以在额外请求头里填 Authorization=Bearer <token>.";
    }
    if (canonical === "openaiCodex") {
        return "支持浏览器登录和设备码登录.YAOS 会在本地保存刷新令牌.";
    }
    if (canonical === "githubCopilot") {
        return "支持 fine-grained PAT（需包含 Copilot Requests 权限）或设备码登录.这里不会启用浏览器 OAuth,classic PAT 也不能直接用.";
    }
    return "";
}

function authState(key, usesOAuthFn, providerAuthStatusFn) {
    return usesOAuthFn && usesOAuthFn(key) ? (providerAuthStatusFn ? (providerAuthStatusFn(key) || ({})) : ({})) : ({});
}

function beginOAuthFlow(key, mode, studioBridge, draftConfig, afterChangeFn) {
    var state = studioBridge ? (studioBridge.beginProviderOAuthWithConfig(key, mode, draftConfig) || ({})) : ({});
    if (afterChangeFn) {
        afterChangeFn();
    }
    return state;
}

function pollOAuthState(key, studioBridge, afterChangeFn) {
    var state = studioBridge ? (studioBridge.pollProviderOAuth(key) || ({})) : ({});
    if (afterChangeFn) {
        afterChangeFn();
    }
    return state;
}

function refreshOAuthState(key, studioBridge, afterChangeFn) {
    var state = studioBridge ? (studioBridge.refreshProviderOAuth(key) || ({})) : ({});
    if (afterChangeFn) {
        afterChangeFn();
    }
    return state;
}

function logoutOAuthState(key, studioBridge, afterChangeFn) {
    var ok = studioBridge ? !!studioBridge.logoutProviderOAuth(key) : false;
    if (afterChangeFn) {
        afterChangeFn();
    }
    return ok;
}

function authSummaryText(state, formatIsoDateTimeFn) {
    if (!state || (state.providerId === undefined &&
        state.loggedIn === undefined &&
        state.pending === undefined &&
        state.error === undefined)) {
        return "";
    }
    if (state.pending) {
        if ((state.mode || "") === "browser") {
            if ((state.redirectUri || "").length > 0) {
                return "浏览器登录已发起,正在等待本机回调：" + state.redirectUri;
            }
            return "浏览器登录已发起,正在等待本机回调返回.";
        }
        return "设备码登录进行中.完成网页验证后,YAOS 会自动轮询结果.";
    }
    if (state.loggedIn) {
        var credentialMode = String(state.credentialMode || "");
        var summary = credentialMode === "fine_grained_pat"
            ? "PAT 已就绪"
            : (credentialMode === "copilot_runtime_token" ? "Copilot 令牌已就绪" : "OAuth 已就绪");
        if ((state.accountId || "").length > 0) {
            summary += "  ·  工作区 " + state.accountId;
        }
        if ((state.expiresAt || "").length > 0) {
            summary += "  ·  过期时间 " + (formatIsoDateTimeFn ? formatIsoDateTimeFn(state.expiresAt) : state.expiresAt);
        }
        return summary;
    }
    if ((state.error || "").length > 0) {
        return state.error;
    }
    if (state.requiresClientId) {
        return "开始设备码登录前,先填写 oauthClientId.";
    }
    return "尚未连接.";
}

function authDiagnosticsText(state, formatIsoDateTimeFn) {
    if (!state || (state.providerId === undefined &&
        state.hasApiKey === undefined &&
        state.hasOAuthAccessToken === undefined &&
        state.hasRefreshToken === undefined &&
        state.hasIdToken === undefined &&
        state.modelCount === undefined &&
        state.lastRefreshAt === undefined &&
        state.lastError === undefined)) {
        return "";
    }

    var providerId = stringValue(state.providerId);
    var credentialMode = stringValue(state.credentialMode);
    var tokens = [];
    tokens.push("API Key " + (state.hasApiKey ? "有" : "无"));
    tokens.push("OAuth Token " + (state.hasOAuthAccessToken ? "有" : "无"));
    tokens.push("Refresh Token " + (state.hasRefreshToken ? "有" : "无"));
    if (providerId === "openai_codex") {
        tokens.push("ID Token " + (state.hasIdToken ? "有" : "无"));
    }
    tokens.push("模型数 " + Number(state.modelCount || 0));

    var lines = ["诊断  ·  " + tokens.join("  ·  ")];
    var details = [];
    if (providerId === "github_copilot" && credentialMode.length > 0) {
        if (credentialMode === "fine_grained_pat") {
            details.push("凭据模式 fine-grained PAT");
        } else if (credentialMode === "copilot_runtime_token") {
            details.push("凭据模式 Copilot runtime token");
        } else if (credentialMode === "oauth_token") {
            details.push("凭据模式 GitHub OAuth token");
        } else if (credentialMode === "classic_pat") {
            details.push("凭据模式 classic PAT");
        }
    }
    if ((state.lastRefreshAt || "").length > 0) {
        details.push("最近刷新 " + (formatIsoDateTimeFn ? formatIsoDateTimeFn(state.lastRefreshAt) : state.lastRefreshAt));
    }
    if ((state.expiresAt || "").length > 0) {
        var expiresText = formatIsoDateTimeFn ? formatIsoDateTimeFn(state.expiresAt) : state.expiresAt;
        details.push((state.expiresSoon ? "即将过期 " : "过期时间 ") + expiresText);
    }
    if (state.requiresClientId) {
        details.push("缺少 client id");
    }
    if (details.length > 0) {
        lines.push(details.join("  ·  "));
    }
    if ((state.lastError || "").length > 0 && (state.lastError || "") !== (state.error || "")) {
        lines.push("最近失败: " + state.lastError);
    }
    return lines.join("\n");
}

function apiKeyPlaceholder(key, canonicalProviderKeyFn, headerHintFn) {
    var canonical = canonicalKey(key, canonicalProviderKeyFn);
    if (canonical === "codebuddy") {
        return "API Key（将以 X-Api-Key 发送）";
    }
    if (canonical === "githubCopilot") {
        return "Fine-grained PAT（需含 Copilot Requests）或 Copilot runtime token";
    }
    var hint = headerHintFn ? headerHintFn(key) : "";
    return hint.length > 0 ? "API Key（可选）" : "API Key";
}

function mapHasEntries(map) {
    if (!map) {
        return false;
    }
    for (var entry in map) {
        return true;
    }
    return false;
}

function liveProviderHasOAuthState(key, liveProviderValueFn) {
    return stringValue(liveProviderValueFn ? liveProviderValueFn(key, "oauthAccessToken", "") : "").length > 0 ||
        stringValue(liveProviderValueFn ? liveProviderValueFn(key, "oauthIdToken", "") : "").length > 0 ||
        stringValue(liveProviderValueFn ? liveProviderValueFn(key, "oauthRefreshToken", "") : "").length > 0 ||
        stringValue(liveProviderValueFn ? liveProviderValueFn(key, "oauthUserCode", "") : "").length > 0;
}

function isConfigured(key, canonicalProviderKeyFn, providerValueFn, liveProviderValueFn, usesOAuthFn) {
    var canonical = canonicalKey(key, canonicalProviderKeyFn);
    var apiKey = stringValue(providerValueFn ? providerValueFn(key, "apiKey", "") : "");
    var apiBase = stringValue(providerValueFn ? providerValueFn(key, "apiBase", "") : "");
    var extraHeaders = providerValueFn ? providerValueFn(key, "extraHeaders", {}) : ({});
    var hasHeaders = mapHasEntries(extraHeaders);
    var hasOAuthState = usesOAuthFn && usesOAuthFn(key) && liveProviderHasOAuthState(key, liveProviderValueFn);
    if (canonical === "codebuddy") {
        return apiBase.length > 0 && (apiKey.length > 0 || hasHeaders);
    }
    if (canonical === "custom" || canonical === "vllm") {
        return apiBase.length > 0 || apiKey.length > 0 || hasHeaders;
    }
    return apiKey.length > 0 || hasHeaders || hasOAuthState;
}

function configuredProviderOptions(providerDefinitions, isConfiguredFn) {
    var out = [];
    for (var i = 0; i < (providerDefinitions || []).length; ++i) {
        if (isConfiguredFn && isConfiguredFn(providerDefinitions[i].key)) {
            out.push(providerDefinitions[i]);
        }
    }
    return out;
}

function modelCatalog(key, providerValueFn) {
    return providerValueFn ? (providerValueFn(key, "availableModels", []) || []) : [];
}

function setModelCatalog(key, models, setProviderValueFn) {
    if (setProviderValueFn) {
        setProviderValueFn(key, "availableModels", models || []);
    }
}

function defaultProviderOptions(providerDefinitions) {
    var out = [{
        "key": "auto",
        "title": "自动路由",
        "hint": "按模型自动匹配厂商"
    }];
    for (var i = 0; i < (providerDefinitions || []).length; ++i) {
        out.push(providerDefinitions[i]);
    }
    return out;
}

function selectableModels(key, providerModelCatalogFn, providerValueFn, canonicalProviderKeyFn, readFn) {
    var unique = {};
    var next = [];

    var synced = providerModelCatalogFn ? (providerModelCatalogFn(key) || []) : [];
    var enabled = providerValueFn ? providerValueFn(key, "enabledModels", []) : [];
    var restrictToEnabled = enabled && enabled.length > 0;
    if (restrictToEnabled) {
        synced = enabled;
    }

    function pushModel(value) {
        if (!value || value.trim().length === 0) {
            return;
        }
        var normalized = value.trim();
        if (unique[normalized]) {
            return;
        }
        unique[normalized] = true;
        next.push(normalized);
    }

    for (var i = 0; i < synced.length; ++i) {
        pushModel(String(synced[i]));
    }
    if (!restrictToEnabled) {
        pushModel(providerValueFn ? providerValueFn(key, "model", "") : "");
        if (canonicalProviderKeyFn && readFn &&
            canonicalProviderKeyFn(readFn("agents.defaults.provider", "auto")) === canonicalProviderKeyFn(key)) {
            pushModel(readFn("agents.defaults.model", ""));
        }
    }
    return next;
}

function defaultModelChoices(readFn, providerDefinitions, canonicalProviderKeyFn, selectableProviderModelsFn) {
    var providerKey = canonicalProviderKeyFn ? canonicalProviderKeyFn(readFn ? readFn("agents.defaults.provider", "auto") : "auto") : "auto";
    if (!providerKey || providerKey === "auto") {
        var unique = {};
        var next = [];

        function pushModel(value) {
            if (!value || value.trim().length === 0) {
                return;
            }
            var normalized = value.trim();
            if (unique[normalized]) {
                return;
            }
            unique[normalized] = true;
            next.push(normalized);
        }

        for (var i = 0; i < (providerDefinitions || []).length; ++i) {
            var models = selectableProviderModelsFn ? (selectableProviderModelsFn(providerDefinitions[i].key) || []) : [];
            for (var j = 0; j < models.length; ++j) {
                pushModel(models[j]);
            }
        }
        pushModel(readFn ? readFn("agents.defaults.model", "") : "");
        return next;
    }
    return selectableProviderModelsFn ? (selectableProviderModelsFn(providerKey) || []) : [];
}

function chooseProviderModel(key, currentValue, selectableProviderModelsFn, providerValueFn, canonicalProviderKeyFn, readFn, modelIndexFn) {
    var models = selectableProviderModelsFn ? (selectableProviderModelsFn(key) || []) : [];
    if (currentValue && modelIndexFn && modelIndexFn(models, currentValue) >= 0) {
        return currentValue;
    }
    var selected = providerValueFn ? providerValueFn(key, "model", "") : "";
    if (selected && modelIndexFn && modelIndexFn(models, selected) >= 0) {
        return selected;
    }
    var defaultProvider = canonicalProviderKeyFn ? canonicalProviderKeyFn(readFn ? readFn("agents.defaults.provider", "auto") : "auto") : "auto";
    var defaultModel = readFn ? readFn("agents.defaults.model", "") : "";
    if (canonicalProviderKeyFn &&
        defaultProvider === canonicalProviderKeyFn(key) &&
        defaultModel &&
        modelIndexFn &&
        modelIndexFn(models, defaultModel) >= 0) {
        return defaultModel;
    }
    return models.length > 0 ? models[0] : "";
}

function setProviderAsDefault(key, runtimeProviderKeyFn, chooseProviderModelFn, providerValueFn, assignFn, readFn) {
    if (assignFn) {
        assignFn("agents.defaults.provider", runtimeProviderKeyFn ? runtimeProviderKeyFn(key) : key);
        assignFn("agents.defaults.model", chooseProviderModelFn
            ? chooseProviderModelFn(key, providerValueFn ? providerValueFn(key, "model", "") : "")
            : "");
    }
    return {
        "provider": readFn ? readFn("agents.defaults.provider", "auto") : "auto",
        "model": readFn ? readFn("agents.defaults.model", "") : ""
    };
}

function syncProviderCatalog(key, studioBridge, draftConfig, setProviderModelCatalogFn, chooseProviderModelFn, providerValueFn,
    modelIndexFn, setProviderValueFn, canonicalProviderKeyFn, readFn, assignFn) {
    var models = studioBridge ? (studioBridge.fetchProviderModels(key, draftConfig) || []) : [];
    if (models.length === 0) {
        return [];
    }
    if (setProviderModelCatalogFn) {
        setProviderModelCatalogFn(key, models);
    }
    var selected = chooseProviderModelFn
        ? chooseProviderModelFn(key, providerValueFn ? providerValueFn(key, "model", "") : "")
        : "";
    if (modelIndexFn && modelIndexFn(models, selected) < 0) {
        selected = models[0];
    }
    if (setProviderValueFn) {
        setProviderValueFn(key, "model", selected);
    }
    if (canonicalProviderKeyFn &&
        readFn &&
        assignFn &&
        canonicalProviderKeyFn(readFn("agents.defaults.provider", "auto")) === canonicalProviderKeyFn(key)) {
        assignFn("agents.defaults.model", selected);
    }
    return models;
}
