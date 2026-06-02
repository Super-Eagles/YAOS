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
