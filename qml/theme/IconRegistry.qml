pragma Singleton
import QtQuick 2.14
import "." as ThemeKit

QtObject {
    function _assign(target, source) {
        if (!source) {
            return target;
        }
        for (var key in source) {
            target[key] = source[key];
        }
        return target;
    }

    function _baseSpec() {
        return {
            "type": "glyph",
            "value": "",
            "fontFamily": ThemeKit.Foundation.iconFontFamily,
            "source": "",
            "tintable": true,
            "size": ThemeKit.Foundation.iconSizeMd
        };
    }

    function clone(spec) {
        return _assign(_baseSpec(), spec || {});
    }

    function glyph(value, options) {
        var spec = _baseSpec();
        spec.type = "glyph";
        spec.value = String(value || "");
        spec.source = "";
        return _assign(spec, options || {});
    }

    function image(source, options) {
        var spec = _baseSpec();
        spec.type = "image";
        spec.value = "";
        spec.source = String(source || "");
        spec.tintable = false;
        return _assign(spec, options || {});
    }

    function empty() {
        return glyph("");
    }

    function normalize(spec) {
        if (spec === undefined || spec === null) {
            return empty();
        }
        if (typeof spec === "string") {
            return glyph(spec);
        }
        var copy = clone(spec);
        if (copy.type === "image") {
            copy.value = "";
            copy.source = String(copy.source || "");
            if (copy.tintable === undefined) {
                copy.tintable = false;
            }
        } else {
            copy.type = "glyph";
            copy.source = "";
            copy.value = String(copy.value || "");
        }
        return copy;
    }

    function section(key) {
        switch (String(key || "").toLowerCase()) {
        case "tasks":
            return glyph("◎");
        case "events":
            return glyph("◈");
        case "approvals":
            return glyph("✦");
        case "overview":
            return glyph("◎");
        case "models":
            return glyph("✶");
        case "channels":
            return glyph("⇄");
        case "resources":
            return glyph("⌬");
        case "extensions":
            return glyph("⬢");
        case "security":
            return glyph("✦");
        case "plugins":
            return glyph("⛭");
        case "skills":
            return glyph("✶");
        case "mcp":
            return glyph("⌘");
        case "runtime":
            return glyph("⛭");
        case "gateway":
            return glyph("⇆");
        case "memory":
            return glyph("◍");
        case "routing":
            return glyph("⌕");
        case "template":
            return glyph("▣");
        case "draft":
            return glyph("✎");
        case "catalog":
            return glyph("⬢");
        case "notifications":
            return glyph("◈");
        case "search":
            return glyph("⌕");
        case "conversation":
            return glyph("◍");
        case "composer":
            return glyph("✎");
        case "automation":
            return glyph("≋");
        default:
            return glyph("◎");
        }
    }

    function provider(key) {
        switch (String(key || "").toLowerCase()) {
        case "anthropic":
            return glyph("◌");
        case "openai":
        case "openaicodex":
            return glyph("✶");
        case "openrouter":
            return glyph("⌁");
        case "deepseek":
            return glyph("◈");
        case "groq":
            return glyph("⚡");
        case "gemini":
            return glyph("✦");
        case "dashscope":
            return glyph("⎈");
        case "zhipu":
            return glyph("◎");
        case "moonshot":
            return glyph("☾");
        case "minimax":
            return glyph("⬢");
        case "volcengine":
            return glyph("⛭");
        case "siliconflow":
            return glyph("≋");
        case "aihubmix":
            return glyph("⌬");
        case "vllm":
            return glyph("▣");
        case "codebuddy":
            return glyph("✎");
        case "githubcopilot":
            return glyph("⌘");
        case "azureopenai":
            return glyph("▦");
        case "custom":
            return glyph("◎");
        default:
            return glyph("◎");
        }
    }

    function channel(key) {
        switch (String(key || "").toLowerCase()) {
        case "telegram":
            return glyph("✈");
        case "slack":
            return glyph("⌗");
        case "whatsapp":
            return glyph("◍");
        case "discord":
            return glyph("◆");
        case "feishu":
            return glyph("✦");
        case "dingtalk":
            return glyph("⬡");
        default:
            return glyph("◈");
        }
    }

    function action(key) {
        switch (String(key || "").toLowerCase()) {
        case "search":
            return glyph("⌕");
        case "edit":
            return glyph("✎");
        case "catalog":
            return glyph("⬢");
        case "template":
            return glyph("▣");
        case "sync":
            return glyph("⇆");
        case "runtime":
            return glyph("⛭");
        default:
            return glyph("◎");
        }
    }
}
