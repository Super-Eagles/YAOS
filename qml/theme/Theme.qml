pragma Singleton
import QtQuick 2.14
import "." as ThemeKit

QtObject {
    readonly property var palette: ({
        "surfaceBase": ThemeKit.Palette.surfaceBase,
        "surfacePanel": ThemeKit.Palette.surfacePanel,
        "surfacePanelInset": ThemeKit.Palette.surfacePanelInset,
        "surfaceGuide": ThemeKit.Palette.surfaceGuide,
        "textStrong": ThemeKit.Palette.textStrong,
        "textPrimary": ThemeKit.Palette.textPrimary,
        "textSecondary": ThemeKit.Palette.textSecondary,
        "textTertiary": ThemeKit.Palette.textTertiary,
        "textMuted": ThemeKit.Palette.textMuted,
        "textGuide": ThemeKit.Palette.textGuide,
        "textInverse": ThemeKit.Palette.textInverse,
        "accentCyan": ThemeKit.Palette.accentCyan,
        "accentCyanSoft": ThemeKit.Palette.accentCyanSoft,
        "accentBlue": ThemeKit.Palette.accentBlue,
        "accentIndigo": ThemeKit.Palette.accentIndigo,
        "accentGreen": ThemeKit.Palette.accentGreen,
        "accentPink": ThemeKit.Palette.accentPink,
        "accentAmber": ThemeKit.Palette.accentAmber,
        "accentSlate": ThemeKit.Palette.accentSlate
    })

    readonly property var foundation: ({
        "fontFamilySans": ThemeKit.Foundation.fontFamilySans,
        "fontFamilyMono": ThemeKit.Foundation.fontFamilyMono,
        "iconFontFamily": ThemeKit.Foundation.iconFontFamily,
        "space2": ThemeKit.Foundation.space2,
        "space3": ThemeKit.Foundation.space3,
        "space4": ThemeKit.Foundation.space4,
        "space5": ThemeKit.Foundation.space5,
        "space6": ThemeKit.Foundation.space6,
        "borderThin": ThemeKit.Foundation.borderThin,
        "radiusSm": ThemeKit.Foundation.radiusSm,
        "radiusMd": ThemeKit.Foundation.radiusMd,
        "radiusLg": ThemeKit.Foundation.radiusLg,
        "radiusXl": ThemeKit.Foundation.radiusXl,
        "iconSizeSm": ThemeKit.Foundation.iconSizeSm,
        "iconSizeMd": ThemeKit.Foundation.iconSizeMd,
        "iconSizeLg": ThemeKit.Foundation.iconSizeLg,
        "durationFast": ThemeKit.Foundation.durationFast,
        "durationBase": ThemeKit.Foundation.durationBase,
        "durationMedium": ThemeKit.Foundation.durationMedium,
        "durationSlow": ThemeKit.Foundation.durationSlow
    })

    function _stripPrefix(value, prefix) {
        return value.slice(prefix.length + 1);
    }

    function section(key) {
        return ThemeKit.SemanticTokens.section(key);
    }

    function provider(key) {
        return ThemeKit.SemanticTokens.provider(key);
    }

    function channel(key) {
        return ThemeKit.SemanticTokens.channel(key);
    }

    function card(variant) {
        return ThemeKit.ComponentTheme.card(variant);
    }

    function hero(variant) {
        return ThemeKit.ComponentTheme.hero(variant);
    }

    function button(tone) {
        return ThemeKit.ComponentTheme.button(tone);
    }

    function field(variant) {
        return ThemeKit.ComponentTheme.field(variant);
    }

    function comboBox(variant) {
        return ThemeKit.ComponentTheme.comboBox(variant);
    }

    function checkBox(variant) {
        return ThemeKit.ComponentTheme.checkBox(variant);
    }

    function textView(variant) {
        return ThemeKit.ComponentTheme.textView(variant);
    }

    function surface(variant) {
        return ThemeKit.ComponentTheme.surface(variant);
    }

    function summaryBox(variant) {
        return ThemeKit.ComponentTheme.summaryBox(variant);
    }

    function metricChip(accentColor) {
        return ThemeKit.ComponentTheme.metricChip(accentColor);
    }

    function listItem(variant) {
        return ThemeKit.ComponentTheme.listItem(variant);
    }

    function kindChip(kind) {
        return ThemeKit.ComponentTheme.kindChip(kind);
    }

    function navChip(accentColor) {
        return ThemeKit.ComponentTheme.navChip(accentColor);
    }

    function windowControl(danger, hovered, pressed) {
        return ThemeKit.ComponentTheme.windowControl(danger, hovered, pressed);
    }

    function toast(tone) {
        return ThemeKit.ComponentTheme.toast(tone);
    }

    function shellChrome() {
        return ThemeKit.ComponentTheme.shellChrome();
    }

    function startupOverlay() {
        return ThemeKit.ComponentTheme.startupOverlay();
    }

    function conversationBubble(role, hasError) {
        return ThemeKit.ComponentTheme.conversationBubble(role, hasError);
    }

    function traceItem() {
        return ThemeKit.ComponentTheme.traceItem();
    }

    function activityPill(accentColor) {
        return ThemeKit.ComponentTheme.activityPill(accentColor);
    }

    function status(kind) {
        return ThemeKit.ComponentTheme.status(kind);
    }

    function alpha(colorValue, amount) {
        return ThemeKit.Palette.alpha(colorValue, amount);
    }

    function resolveSectionIcon(key) {
        return ThemeKit.IconRegistry.section(key);
    }

    function resolveProviderIcon(key) {
        return ThemeKit.IconRegistry.provider(key);
    }

    function resolveChannelIcon(key) {
        return ThemeKit.IconRegistry.channel(key);
    }

    function resolveIcon(specOrKey, namespaceHint) {
        if (specOrKey === undefined || specOrKey === null) {
            return ThemeKit.IconRegistry.empty();
        }
        if (typeof specOrKey === "object") {
            return ThemeKit.IconRegistry.normalize(specOrKey);
        }

        var value = String(specOrKey || "");
        if (!value.length) {
            return ThemeKit.IconRegistry.empty();
        }
        if (value.indexOf("section.") === 0) {
            return ThemeKit.IconRegistry.section(_stripPrefix(value, "section"));
        }
        if (value.indexOf("provider.") === 0) {
            return ThemeKit.IconRegistry.provider(_stripPrefix(value, "provider"));
        }
        if (value.indexOf("channel.") === 0) {
            return ThemeKit.IconRegistry.channel(_stripPrefix(value, "channel"));
        }
        if (value.indexOf("action.") === 0) {
            return ThemeKit.IconRegistry.action(_stripPrefix(value, "action"));
        }
        if (namespaceHint === "section") {
            return ThemeKit.IconRegistry.section(value);
        }
        if (namespaceHint === "provider") {
            return ThemeKit.IconRegistry.provider(value);
        }
        if (namespaceHint === "channel") {
            return ThemeKit.IconRegistry.channel(value);
        }
        if (namespaceHint === "action") {
            return ThemeKit.IconRegistry.action(value);
        }
        if (value.indexOf("qrc:/") === 0 || value.indexOf("/") !== -1) {
            return ThemeKit.IconRegistry.image(value);
        }
        return ThemeKit.IconRegistry.glyph(value);
    }
}
