pragma Singleton
import QtQuick 2.14
import "." as ThemeKit

QtObject {
    function card(variant) {
        return {
            "variant": String(variant || "default").toLowerCase(),
            "background":       ThemeKit.Palette.surfacePanel,
            "innerStart":       ThemeKit.Palette.surfaceCardStart,
            "innerEnd":         ThemeKit.Palette.surfaceCardEnd,
            "innerHoverStart":  "#111628",
            "innerHoverEnd":    "#0A0C16",
            "title":            ThemeKit.Palette.textStrong,
            "subtitle":         ThemeKit.Palette.textMuted,
            "badgeText":        ThemeKit.Palette.textInverse,
            "guideBackground":  ThemeKit.Palette.surfaceGuide,
            "guideLabel":       ThemeKit.Palette.accentCyan,
            "guideText":        ThemeKit.Palette.textGuide
        };
    }

    function hero(variant) {
        return {
            "variant":          String(variant || "default").toLowerCase(),
            "background":       ThemeKit.Palette.surfacePanel,
            "innerBackground":  ThemeKit.Palette.surfacePanelInset,
            "overlineText":     ThemeKit.Palette.textPrimary,
            "title":            ThemeKit.Palette.textStrong,
            "description":      ThemeKit.Palette.textSecondary,
            "metricLabel":      ThemeKit.Palette.textSecondary
        };
    }

    function field(variant) {
        return {
            "variant":        String(variant || "default").toLowerCase(),
            "text":           ThemeKit.Palette.textInverse,
            "placeholder":    "#3A566E",
            "selection":      ThemeKit.Palette.accentCyan,
            "selectedText":   "#020810",
            "start":          "#090C1A",
            "startHover":     "#0D1122",
            "startPressed":   "#12172E",
            "end":            "#060810",
            "endHover":       "#090C18",
            "endPressed":     "#0C1020",
            "border":         "#1A2840",
            "borderHover":    ThemeKit.Palette.accentBlue,
            "borderFocus":    ThemeKit.Palette.accentCyan
        };
    }

    function comboBox(variant) {
        return {
            "variant":              String(variant || "default").toLowerCase(),
            "text":                 ThemeKit.Palette.textInverse,
            "textMuted":            ThemeKit.Palette.textPrimary,
            "indicator":            ThemeKit.Palette.accentCyan,
            "indicatorHover":       "#A0E8FF",
            "indicatorOpen":        ThemeKit.Palette.textPrimary,
            "popupStart":           "#080B18",
            "popupEnd":             "#050710",
            "popupInner":           "#0A0D1C",
            "popupBorder":          "#1A2840",
            "itemText":             "#C0DCF0",
            "itemTextActive":       ThemeKit.Palette.textInverse,
            "itemBackgroundHover":  "#0E1528",
            "itemBackgroundActive": "#12203A",
            "itemBorderActive":     ThemeKit.Palette.accentCyan
        };
    }

    function checkBox(variant) {
        return {
            "variant":               String(variant || "default").toLowerCase(),
            "indicator":             "#080C1A",
            "indicatorHover":        "#0D1220",
            "indicatorChecked":      "#0E1A30",
            "indicatorBorder":       "#1A2840",
            "indicatorBorderHover":  ThemeKit.Palette.accentBlue,
            "indicatorBorderChecked":ThemeKit.Palette.accentCyan,
            "mark":                  ThemeKit.Palette.accentCyan,
            "text":                  "#7A9AB8",
            "textHover":             "#C0DCF0",
            "textChecked":           ThemeKit.Palette.textPrimary
        };
    }

    function textView(variant) {
        return {
            "variant":      String(variant || "default").toLowerCase(),
            "text":         "#A0C0D8",
            "selection":    ThemeKit.Palette.accentCyan,
            "selectedText": "#020810",
            "outer":        "#070A18",
            "inner":        "#090C1C",
            "border":       "#1A2840"
        };
    }

    function surface(variant) {
        switch (String(variant || "default").toLowerCase()) {
        case "summary":
            return {
                "background": "#07091A",
                "border":     Qt.rgba(0.0, 0.898, 1.0, 0.16),
                "title":      ThemeKit.Palette.accentCyanSoft,
                "text":       ThemeKit.Palette.textPrimary,
                "muted":      ThemeKit.Palette.textGuide,
                "meta":       ThemeKit.Palette.textSecondary
            };
        case "summary-alt":
            return {
                "background": "#0A0D1C",
                "border":     "#1A2840",
                "title":      ThemeKit.Palette.accentCyanSoft,
                "text":       "#C0DCF0",
                "muted":      ThemeKit.Palette.textGuide,
                "meta":       ThemeKit.Palette.textMuted
            };
        case "list-item":
            return {
                "background": "#0B0F20",
                "border":     "#1A2840",
                "borderSoft": Qt.rgba(0.0, 0.898, 1.0, 0.08),
                "title":      "#E0F4FF",
                "text":       ThemeKit.Palette.textPrimary,
                "body":       ThemeKit.Palette.textSecondary,
                "meta":       "#42607A",
                "accent":     ThemeKit.Palette.accentCyanSoft
            };
        default:
            return {
                "background": ThemeKit.Palette.surfacePanelInset,
                "border":     ThemeKit.Palette.accentSlate,
                "title":      ThemeKit.Palette.textPrimary,
                "text":       ThemeKit.Palette.textPrimary,
                "body":       ThemeKit.Palette.textSecondary,
                "meta":       ThemeKit.Palette.textMuted,
                "accent":     ThemeKit.Palette.accentCyanSoft
            };
        }
    }

    function summaryBox(variant) {
        switch (String(variant || "default").toLowerCase()) {
        case "alt":
            return {
                "background": "#0A0D1C",
                "border":     "#1A2840",
                "text":       "#C0DCF0",
                "accent":     ThemeKit.Palette.accentCyanSoft,
                "title":      ThemeKit.Palette.accentCyanSoft,
                "meta":       ThemeKit.Palette.textMuted
            };
        case "warning":
            return {
                "background": "#160A08",
                "border":     ThemeKit.Palette.alpha(ThemeKit.Palette.accentAmber, 0.40),
                "text":       ThemeKit.Palette.textPrimary,
                "accent":     ThemeKit.Palette.accentAmber,
                "title":      ThemeKit.Palette.accentAmber,
                "meta":       ThemeKit.Palette.textMuted
            };
        default:
            return {
                "background": "#07091A",
                "border":     Qt.rgba(0.0, 0.898, 1.0, 0.14),
                "text":       ThemeKit.Palette.textPrimary,
                "accent":     ThemeKit.Palette.accentCyan,
                "title":      ThemeKit.Palette.accentCyanSoft,
                "meta":       ThemeKit.Palette.textMuted
            };
        }
    }

    function metricChip(accentColor) {
        var accent = accentColor || ThemeKit.Palette.accentCyan;
        return {
            "background": ThemeKit.Palette.alpha(accent, 0.07),
            "border":     ThemeKit.Palette.alpha(accent, 0.22),
            "label":      ThemeKit.Palette.textMuted,
            "value":      accent
        };
    }

    function listItem(variant) {
        switch (String(variant || "default").toLowerCase()) {
        case "selected":
            return {
                "background": "#0E1830",
                "border":     ThemeKit.Palette.accentCyan,
                "title":      ThemeKit.Palette.textInverse,
                "text":       ThemeKit.Palette.textInverse,
                "body":       ThemeKit.Palette.textPrimary,
                "meta":       ThemeKit.Palette.accentCyanSoft,
                "accent":     ThemeKit.Palette.accentCyan
            };
        case "selected-strong":
            return {
                "background": "#122040",
                "border":     ThemeKit.Palette.accentCyan,
                "title":      ThemeKit.Palette.textInverse,
                "text":       ThemeKit.Palette.textInverse,
                "body":       ThemeKit.Palette.textPrimary,
                "meta":       ThemeKit.Palette.accentCyan,
                "accent":     ThemeKit.Palette.accentCyan
            };
        case "accent":
            return {
                "background": "#0A1428",
                "border":     ThemeKit.Palette.alpha(ThemeKit.Palette.accentBlue, 0.40),
                "title":      ThemeKit.Palette.textInverse,
                "text":       ThemeKit.Palette.textPrimary,
                "body":       ThemeKit.Palette.textSecondary,
                "meta":       ThemeKit.Palette.textMuted,
                "accent":     ThemeKit.Palette.accentBlue
            };
        case "danger":
            return {
                "background": "#180810",
                "border":     ThemeKit.Palette.alpha(ThemeKit.Palette.accentPink, 0.40),
                "title":      ThemeKit.Palette.textInverse,
                "text":       ThemeKit.Palette.textPrimary,
                "body":       ThemeKit.Palette.textSecondary,
                "meta":       ThemeKit.Palette.textMuted,
                "accent":     ThemeKit.Palette.accentPink
            };
        default:
            return {
                "background": "#0B0F20",
                "border":     "#1A2840",
                "title":      ThemeKit.Palette.textInverse,
                "text":       ThemeKit.Palette.textPrimary,
                "body":       ThemeKit.Palette.textSecondary,
                "meta":       ThemeKit.Palette.textMuted,
                "accent":     ThemeKit.Palette.accentCyanSoft
            };
        }
    }

    function kindChip(kind) {
        return {
            "background": ThemeKit.Palette.alpha(ThemeKit.Palette.accentIndigo, 0.18),
            "border":     ThemeKit.Palette.alpha(ThemeKit.Palette.accentBlue, 0.30),
            "text":       ThemeKit.Palette.accentCyanSoft
        };
    }

    function navChip(accentColor) {
        var accent = accentColor || ThemeKit.Palette.accentCyan;
        return {
            "start":         "#09091A",
            "end":           "#06060E",
            "hoverStart":    "#0D122A",
            "hoverEnd":      "#080C18",
            "checkedStart":  "#0A1030",
            "checkedEnd":    "#060A1C",
            "downStart":     "#10183C",
            "downEnd":       "#0A1028",
            "border":        ThemeKit.Palette.alpha(accent, 0.10),
            "borderHover":   ThemeKit.Palette.alpha(accent, 0.45),
            "borderChecked": accent,
            "rail":          ThemeKit.Palette.accentViolet,
            "railChecked":   accent,
            "text":          ThemeKit.Palette.textSecondary,
            "textHover":     ThemeKit.Palette.textPrimary,
            "textChecked":   accent
        };
    }

    function windowControl(danger, hovered, pressed) {
        var isDanger = !!danger;
        var isHovered = !!hovered;
        var isPressed = !!pressed;
        if (isDanger) {
            return {
                "background": isPressed ? "#4A1020" : (isHovered ? "#380D18" : "#220910"),
                "border":     isHovered ? ThemeKit.Palette.accentPink : "#880020",
                "overlay":    Qt.rgba(0.02, 0.05, 0.10, isHovered ? 0.22 : 0.08),
                "text":       ThemeKit.Palette.textInverse
            };
        }
        return {
            "background": isPressed ? "#0C1830" : (isHovered ? "#0A1426" : "#080E1C"),
            "border":     isHovered ? ThemeKit.Palette.accentCyan : ThemeKit.Palette.accentIndigo,
            "overlay":    Qt.rgba(0.02, 0.05, 0.10, isHovered ? 0.22 : 0.08),
            "text":       ThemeKit.Palette.textInverse
        };
    }

    function toast(tone) {
        switch (String(tone || "neutral").toLowerCase()) {
        case "warning":
            return {
                "background": "#160A08",
                "border":     ThemeKit.Palette.accentPink,
                "title":      ThemeKit.Palette.textInverse,
                "body":       ThemeKit.Palette.textPrimary
            };
        case "success":
            return {
                "background": "#061814",
                "border":     ThemeKit.Palette.accentGreen,
                "title":      ThemeKit.Palette.textInverse,
                "body":       ThemeKit.Palette.textPrimary
            };
        default:
            return {
                "background": "#080C20",
                "border":     ThemeKit.Palette.accentBlue,
                "title":      ThemeKit.Palette.textInverse,
                "body":       ThemeKit.Palette.textPrimary
            };
        }
    }

    function shellChrome() {
        return {
            "root":             ThemeKit.Palette.surfaceBase,
            "backdropStart":    "#04050E",
            "backdropMid":      "#070916",
            "backdropEnd":      "#020308",
            "ambientLeft":      ThemeKit.Palette.alpha(ThemeKit.Palette.accentPurple, 0.06),
            "ambientRight":     ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.05),
            "ambientBottom":    ThemeKit.Palette.alpha(ThemeKit.Palette.accentViolet, 0.08),
            "sidebarStart":     "#090C1C",
            "sidebarEnd":       "#060810",
            "sidebarInner":     "#060810",
            "sidebarBorder":    ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.18),
            "sidebarDivider":   Qt.rgba(0.0, 0.898, 1.0, 0.06),
            "tagBackground":    ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.10),
            "tagBorder":        ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.28),
            "tagDot":           ThemeKit.Palette.accentCyan,
            "tagText":          "#C0F0FF",
            "brand":            "#EAFAFF",
            "brandSubtitle":    ThemeKit.Palette.accentCyanSoft,
            "statusCard":       "#080B1A",
            "statusCardBorder": ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.18),
            "statusTitle":      ThemeKit.Palette.textInverse,
            "statusMeta":       ThemeKit.Palette.textMuted,
            "mainStart":        "#090C1C",
            "mainEnd":          "#060810",
            "mainBorder":       ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.20),
            "mainInner":        "#060810",
            "headerStart":      "#0B0F22",
            "headerEnd":        "#07091A",
            "headerBorder":     ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.14),
            "headerTitle":      ThemeKit.Palette.textInverse,
            "headerSubtitle":   ThemeKit.Palette.textMuted
        };
    }

    function startupOverlay() {
        return {
            "root":            "#06080E",
            "paneStart":       Qt.rgba(0.0, 0.02, 0.06, 0.10),
            "paneMid":         Qt.rgba(0.0, 0.02, 0.06, 0.16),
            "paneEnd":         Qt.rgba(0.0, 0.02, 0.06, 0.30),
            "scrimStart":      Qt.rgba(0.01, 0.01, 0.04, 0.80),
            "scrimMid":        Qt.rgba(0.01, 0.02, 0.06, 0.72),
            "scrimEnd":        Qt.rgba(0.01, 0.01, 0.04, 0.84),
            "gridLineStrong":  ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.10),
            "gridLineSoft":    ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.04),
            "scanStart":       ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.0),
            "scanCenter":      ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.28),
            "scanEnd":         ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.0),
            "ambientLeft":     ThemeKit.Palette.alpha(ThemeKit.Palette.accentPurple, 0.18),
            "ambientRight":    ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.15),
            "card":            Qt.rgba(0.02, 0.04, 0.12, 0.82),
            "cardBorder":      ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.30),
            "cardRule":        ThemeKit.Palette.accentCyan,
            "panel":           Qt.rgba(0.02, 0.04, 0.12, 0.46),
            "panelBorder":     Qt.rgba(0.0, 0.898, 1.0, 0.22),
            "track":           Qt.rgba(0.0, 0.898, 1.0, 0.12),
            "trackBorder":     Qt.rgba(0.0, 0.898, 1.0, 0.20),
            "progressStart":   ThemeKit.Palette.accentPurple,
            "progressEnd":     ThemeKit.Palette.accentCyan,
            "progressSweep":   Qt.rgba(0.0, 0.898, 1.0, 0.14),
            "title":           "#E8F8FF",
            "subtitle":        ThemeKit.Palette.accentCyanSoft,
            "body":            ThemeKit.Palette.textPrimary,
            "value":           ThemeKit.Palette.accentCyan,
            "meta":            "#5A9AB8",
            "timeline":        "#5A7A90",
            "timelineActive":  "#C0E8FF",
            "progressTrack":   "#05060E"
        };
    }

    function conversationBubble(role, hasError) {
        if (hasError) {
            return {
                "background": "#12060A",
                "border":     ThemeKit.Palette.accentPink,
                "title":      ThemeKit.Palette.textInverse,
                "meta":       ThemeKit.Palette.textMuted,
                "body":       ThemeKit.Palette.textInverse,
                "accent":     ThemeKit.Palette.accentPink
            };
        }
        if (String(role || "").toLowerCase() === "user") {
            return {
                "background": "#0A0E22",
                "border":     ThemeKit.Palette.accentBlue,
                "title":      "#E0F4FF",
                "meta":       "#345060",
                "body":       ThemeKit.Palette.textInverse,
                "accent":     ThemeKit.Palette.accentBlue
            };
        }
        return {
            "background": "#080B1C",
            "border":     ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.30),
            "title":      "#E0F4FF",
            "meta":       "#345060",
            "body":       ThemeKit.Palette.textInverse,
            "accent":     ThemeKit.Palette.accentCyanSoft
        };
    }

    function traceItem() {
        return {
            "background": "#090C1C",
            "border":     ThemeKit.Palette.alpha(ThemeKit.Palette.accentBlue, 0.12),
            "label":      ThemeKit.Palette.accentCyanSoft,
            "text":       ThemeKit.Palette.textPrimary
        };
    }

    function activityPill(accentColor) {
        var accent = accentColor || ThemeKit.Palette.accentCyan;
        return {
            "background": "#08101E",
            "border":     accent,
            "text":       "#C0EEFF"
        };
    }

    function status(kind) {
        switch (String(kind || "info").toLowerCase()) {
        case "success":
        case "completed":
        case "succeeded":
            return {
                "accent": ThemeKit.Palette.accentGreen,
                "text":   "#60FFB8",
                "border": ThemeKit.Palette.accentGreen
            };
        case "warning":
        case "cancelled":
            return {
                "accent": ThemeKit.Palette.accentAmber,
                "text":   "#FFB800",
                "border": ThemeKit.Palette.accentAmber
            };
        case "error":
        case "failed":
            return {
                "accent": ThemeKit.Palette.accentPink,
                "text":   ThemeKit.Palette.accentPink,
                "border": ThemeKit.Palette.accentPink
            };
        default:
            return {
                "accent": ThemeKit.Palette.accentBlue,
                "text":   ThemeKit.Palette.accentCyanSoft,
                "border": ThemeKit.Palette.accentBlue
            };
        }
    }

    function button(tone) {
        switch (String(tone || "neutral").toLowerCase()) {
        case "danger":
            return {
                "border":           "#880020",
                "borderHover":      ThemeKit.Palette.accentPink,
                "foreground":       ThemeKit.Palette.textPrimary,
                "foregroundDisabled": ThemeKit.Palette.textTertiary,
                "start":            "#200810",
                "startHover":       "#360C18",
                "startDown":        "#481020",
                "end":              "#140608",
                "endDown":          "#260A12",
                "disabledStart":    "#08091A",
                "disabledEnd":      ThemeKit.Palette.surfacePanel,
                "overlay":          Qt.rgba(0.02, 0.07, 0.14, 0.08),
                "overlayHover":     Qt.rgba(0.02, 0.07, 0.14, 0.18)
            };
        case "warning":
            return {
                "border":           "#886600",
                "borderHover":      ThemeKit.Palette.accentAmber,
                "foreground":       ThemeKit.Palette.textPrimary,
                "foregroundDisabled": ThemeKit.Palette.textTertiary,
                "start":            "#1E1200",
                "startHover":       "#2E1A00",
                "startDown":        "#3E2200",
                "end":              "#120C00",
                "endDown":          "#1C1200",
                "disabledStart":    "#08091A",
                "disabledEnd":      ThemeKit.Palette.surfacePanel,
                "overlay":          Qt.rgba(0.02, 0.07, 0.14, 0.08),
                "overlayHover":     Qt.rgba(0.02, 0.07, 0.14, 0.18)
            };
        case "success":
            return {
                "border":           "#006640",
                "borderHover":      ThemeKit.Palette.accentGreen,
                "foreground":       ThemeKit.Palette.textPrimary,
                "foregroundDisabled": ThemeKit.Palette.textTertiary,
                "start":            "#061A10",
                "startHover":       "#0A2818",
                "startDown":        "#0E3020",
                "end":              "#041008",
                "endDown":          "#081A10",
                "disabledStart":    "#08091A",
                "disabledEnd":      ThemeKit.Palette.surfacePanel,
                "overlay":          Qt.rgba(0.02, 0.07, 0.14, 0.08),
                "overlayHover":     Qt.rgba(0.02, 0.07, 0.14, 0.18)
            };
        default:
            return {
                "border":           ThemeKit.Palette.alpha(ThemeKit.Palette.accentCyan, 0.28),
                "borderHover":      ThemeKit.Palette.accentCyan,
                "foreground":       ThemeKit.Palette.textPrimary,
                "foregroundDisabled": ThemeKit.Palette.textTertiary,
                "start":            "#0C1030",
                "startHover":       "#101640",
                "startDown":        "#161E54",
                "end":              "#080C20",
                "endDown":          "#0C1234",
                "disabledStart":    "#08091A",
                "disabledEnd":      ThemeKit.Palette.surfacePanel,
                "overlay":          Qt.rgba(0.0, 0.898, 1.0, 0.04),
                "overlayHover":     Qt.rgba(0.0, 0.898, 1.0, 0.08)
            };
        }
    }
}
