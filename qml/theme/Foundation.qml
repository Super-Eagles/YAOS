pragma Singleton
import QtQuick 2.14

QtObject {
    id: foundation

    readonly property string fontFamilySans: "Microsoft YaHei UI"
    readonly property string fontFamilyMono: "Cascadia Mono"
    readonly property string iconFontFamily: foundation.fontFamilySans

    readonly property int iconSizeSm: 13
    readonly property int iconSizeMd: 15
    readonly property int iconSizeLg: 17

    // ── Font size scale (+2px across the board) ──────────
    readonly property int textXxs:  11   // was 9  — micro annotations
    readonly property int textXs:   12   // was 10 — tiny labels
    readonly property int textSm:   13   // was 11 — captions / helper text
    readonly property int textMd:   14   // was 12 — body (primary)
    readonly property int textLg:   15   // was 13 — sub-headings
    readonly property int textXl:   16   // was 14 — list titles
    readonly property int textXxl:  17   // was 15 — section headings
    readonly property int textHero: 18   // was 16 — hero / emphasis

    // Sharper, more angular radius for cyberpunk feel
    readonly property int radiusSm: 4
    readonly property int radiusMd: 6
    readonly property int radiusLg: 8
    readonly property int radiusXl: 10

    readonly property int space2: 6
    readonly property int space3: 8
    readonly property int space4: 12
    readonly property int space5: 16
    readonly property int space6: 20

    readonly property int borderThin: 1

    // Snappier durations with OutExpo feel
    readonly property int durationFast:   100
    readonly property int durationBase:   160
    readonly property int durationMedium: 220
    readonly property int durationSlow:   340
}
