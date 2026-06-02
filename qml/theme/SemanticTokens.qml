pragma Singleton
import QtQuick 2.14
import "." as ThemeKit

QtObject {
    function _token(key, iconSpec, accentColor) {
        return {
            "key": key,
            "icon": ThemeKit.IconRegistry.normalize(iconSpec),
            "accent": accentColor,
            "surface": ThemeKit.Palette.alpha(accentColor, 0.10),
            "border": ThemeKit.Palette.alpha(accentColor, 0.24),
            "badge": ThemeKit.Palette.alpha(accentColor, 0.16),
            "text": ThemeKit.Palette.textPrimary,
            "mutedText": ThemeKit.Palette.textSecondary
        };
    }

    function section(key) {
        switch (String(key || "").toLowerCase()) {
        case "overview":
            return _token("overview", ThemeKit.IconRegistry.section("overview"), ThemeKit.Palette.accentCyan);
        case "models":
            return _token("models", ThemeKit.IconRegistry.section("models"), ThemeKit.Palette.accentBlue);
        case "channels":
            return _token("channels", ThemeKit.IconRegistry.section("channels"), ThemeKit.Palette.accentBlue);
        case "tasks":
            return _token("tasks", ThemeKit.IconRegistry.section("tasks"), ThemeKit.Palette.accentCyan);
        case "events":
            return _token("events", ThemeKit.IconRegistry.section("events"), ThemeKit.Palette.accentBlue);
        case "approvals":
            return _token("approvals", ThemeKit.IconRegistry.section("approvals"), ThemeKit.Palette.accentPink);
        case "resources":
            return _token("resources", ThemeKit.IconRegistry.section("resources"), ThemeKit.Palette.accentGreen);
        case "extensions":
            return _token("extensions", ThemeKit.IconRegistry.section("extensions"), ThemeKit.Palette.accentCyan);
        case "security":
            return _token("security", ThemeKit.IconRegistry.section("security"), ThemeKit.Palette.accentAmber);
        case "plugins":
            return _token("plugins", ThemeKit.IconRegistry.section("plugins"), ThemeKit.Palette.accentCyan);
        case "skills":
            return _token("skills", ThemeKit.IconRegistry.section("skills"), ThemeKit.Palette.accentGreen);
        case "mcp":
            return _token("mcp", ThemeKit.IconRegistry.section("mcp"), ThemeKit.Palette.accentAmber);
        case "runtime":
            return _token("runtime", ThemeKit.IconRegistry.section("runtime"), ThemeKit.Palette.accentCyan);
        case "gateway":
            return _token("gateway", ThemeKit.IconRegistry.section("gateway"), ThemeKit.Palette.accentBlue);
        case "memory":
            return _token("memory", ThemeKit.IconRegistry.section("memory"), ThemeKit.Palette.accentGreen);
        case "routing":
            return _token("routing", ThemeKit.IconRegistry.section("routing"), ThemeKit.Palette.accentCyanSoft);
        case "template":
            return _token("template", ThemeKit.IconRegistry.section("template"), ThemeKit.Palette.accentBlue);
        case "draft":
            return _token("draft", ThemeKit.IconRegistry.section("draft"), ThemeKit.Palette.accentAmber);
        case "catalog":
            return _token("catalog", ThemeKit.IconRegistry.section("catalog"), ThemeKit.Palette.accentAmber);
        case "notifications":
            return _token("notifications", ThemeKit.IconRegistry.section("notifications"), ThemeKit.Palette.accentBlue);
        case "automation":
            return _token("automation", ThemeKit.IconRegistry.section("automation"), ThemeKit.Palette.accentCyan);
        case "conversation":
            return _token("conversation", ThemeKit.IconRegistry.section("conversation"), ThemeKit.Palette.accentCyan);
        case "composer":
            return _token("composer", ThemeKit.IconRegistry.section("composer"), ThemeKit.Palette.accentAmber);
        default:
            return _token("default", ThemeKit.IconRegistry.section("default"), ThemeKit.Palette.accentCyan);
        }
    }

    function provider(key) {
        switch (String(key || "").toLowerCase()) {
        case "openai":
        case "openaicodex":
        case "githubcopilot":
            return _token(String(key || "").toLowerCase(), ThemeKit.IconRegistry.provider(key), ThemeKit.Palette.accentCyan);
        case "anthropic":
        case "gemini":
            return _token(String(key || "").toLowerCase(), ThemeKit.IconRegistry.provider(key), ThemeKit.Palette.accentPink);
        case "deepseek":
        case "openrouter":
        case "dashscope":
        case "volcengine":
            return _token(String(key || "").toLowerCase(), ThemeKit.IconRegistry.provider(key), ThemeKit.Palette.accentBlue);
        case "groq":
        case "minimax":
            return _token(String(key || "").toLowerCase(), ThemeKit.IconRegistry.provider(key), ThemeKit.Palette.accentAmber);
        case "moonshot":
        case "siliconflow":
        case "aihubmix":
        case "vllm":
        case "codebuddy":
        case "azureopenai":
        case "zhipu":
        case "custom":
            return _token(String(key || "").toLowerCase(), ThemeKit.IconRegistry.provider(key), ThemeKit.Palette.accentGreen);
        default:
            return _token("provider.default", ThemeKit.IconRegistry.provider(key), ThemeKit.Palette.accentCyan);
        }
    }

    function channel(key) {
        switch (String(key || "").toLowerCase()) {
        case "telegram":
        case "slack":
        case "discord":
            return _token(String(key || "").toLowerCase(), ThemeKit.IconRegistry.channel(key), ThemeKit.Palette.accentBlue);
        case "whatsapp":
            return _token("whatsapp", ThemeKit.IconRegistry.channel(key), ThemeKit.Palette.accentGreen);
        case "feishu":
        case "dingtalk":
            return _token(String(key || "").toLowerCase(), ThemeKit.IconRegistry.channel(key), ThemeKit.Palette.accentCyan);
        default:
            return _token("channel.default", ThemeKit.IconRegistry.channel(key), ThemeKit.Palette.accentBlue);
        }
    }
}
