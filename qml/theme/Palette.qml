pragma Singleton
import QtQuick 2.14

// ╔══════════════════════════════════════════════════╗
// ║  AI OS · CYBERPUNK PALETTE v2.0                  ║
// ║  Deep Abyss + Neon Glow System                   ║
// ╚══════════════════════════════════════════════════╝
QtObject {
    // ── Surface Depths ─────────────────────────────
    readonly property color surfaceBase:         "#080A10"
    readonly property color surfacePanel:        "#0A0D18"
    readonly property color surfacePanelInset:   "#060810"
    readonly property color surfaceCardStart:    "#0D1020"
    readonly property color surfaceCardEnd:      "#08090F"
    readonly property color surfaceGuide:        "#0A0E1C"
    readonly property color surfaceOverlay:      "#131728"

    // ── Text Hierarchy ──────────────────────────────
    readonly property color textStrong:          "#F0F8FF"
    readonly property color textPrimary:         "#C8E0F4"
    readonly property color textInverse:         "#E8F4FF"
    readonly property color textSecondary:       "#6A8BA8"
    readonly property color textMuted:           "#4A6275"
    readonly property color textTertiary:        "#354960"
    readonly property color textGuide:           "#7A9AB8"

    // ── Neon Accent System ─────────────────────────
    readonly property color accentCyan:          "#00E5FF"
    readonly property color accentCyanSoft:      "#5BBED8"
    readonly property color accentBlue:          "#4D9FFF"
    readonly property color accentGreen:         "#00FF94"
    readonly property color accentPink:          "#FF003C"
    readonly property color accentAmber:         "#FFB800"
    readonly property color accentIndigo:        "#1E3A88"
    readonly property color accentSlate:         "#1A2440"
    readonly property color accentPurple:        "#B026FF"
    readonly property color accentViolet:        "#6644DD"

    function alpha(colorValue, amount) {
        return Qt.rgba(colorValue.r, colorValue.g, colorValue.b, amount);
    }
}
