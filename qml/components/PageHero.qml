import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import QtGraphicalEffects 1.14
import "../theme" as Design

Rectangle {
    id: root
    property string overline: ""
    property string title: ""
    property string description: ""
    property string sectionKey: ""
    property string overlineIconKey: ""
    property var overlineIconSpec: undefined
    property string heroVariant: "default"
    readonly property var sectionTheme: Design.Theme.section(root.sectionKey)
    readonly property var heroTheme: Design.Theme.hero(root.heroVariant)
    readonly property var resolvedOverlineIconSpec: {
        if (root.overlineIconSpec !== undefined) return Design.Theme.resolveIcon(root.overlineIconSpec);
        if (root.overlineIconKey.length > 0)     return Design.Theme.resolveIcon(root.overlineIconKey, "section");
        if (root.sectionKey.length > 0)          return root.sectionTheme.icon;
        return Design.Theme.resolveIcon("");
    }
    readonly property bool hasOverlineIcon: (resolvedOverlineIconSpec.type === "glyph" && String(resolvedOverlineIconSpec.value || "").length > 0)
        || (resolvedOverlineIconSpec.type === "image" && String(resolvedOverlineIconSpec.source || "").length > 0)
    property color accent: root.sectionKey.length > 0 ? root.sectionTheme.accent : Design.Theme.palette.accentCyan
    property bool compact: false
    property var metrics: []
    readonly property int horizontalPadding: compact ? 14 : 18
    readonly property int topPadding:        compact ? 12 : 16
    readonly property int bottomPadding:     compact ? 12 : 16
    readonly property int sectionSpacing:    compact ? 8  : 12
    readonly property int headingSpacing:    compact ? 4  : 6
    readonly property int metricSpacing:     compact ? 6  : 8
    readonly property int metricCount: (root.metrics || []).length
    readonly property int metricColumns: metricCount <= 1
        ? 1
        : (root.width >= 1080 ? Math.min(metricCount, 4)
            : (root.width >= 720 ? Math.min(metricCount, 2) : 1))
    readonly property real metricWidth: metricColumns > 0
        ? Math.max(0, (metricsGrid.width - ((metricColumns - 1) * metricsGrid.spacing)) / metricColumns)
        : metricsGrid.width

    radius: 8
    clip: true
    color: root.heroTheme.background

    Accessible.role: Accessible.Heading
    Accessible.name: root.title.length > 0 ? root.title : "Page header"
    implicitHeight: root.topPadding + heroColumn.height + root.bottomPadding

    // ── Entry animation ──────────────────────────────
    opacity: 0
    Component.onCompleted: {
        entryAnim.start()
    }
    NumberAnimation {
        id: entryAnim; target: root; property: "opacity"
        from: 0; to: 1; duration: 500; easing.type: Easing.OutExpo
    }

    // ── Background ───────────────────────────────────
    // Inner surface
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: parent.radius - 1
        color: root.heroTheme.innerBackground
    }

    // Ambient accent glow (left-anchored)
    Rectangle {
        width: 200; height: 80
        x: -40; y: (parent.height - height) / 2
        radius: 40
        color: Design.Theme.alpha(root.accent, 0.06)
    }

    // Neon border
    Rectangle {
        anchors.fill: parent; radius: parent.radius
        color: "transparent"
        border.width: 1
        border.color: Design.Theme.alpha(root.accent, compact ? 0.22 : 0.28)
    }

    // Top scan line accent
    Rectangle {
        width: parent.width * 0.4; height: 1
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.leftMargin: 18
        color: Design.Theme.alpha(root.accent, 0.55)

        layer.enabled: true
        layer.effect: Glow {
            radius: 4; samples: 9
            color: root.accent; spread: 0.3
        }
    }

    // Corner brackets
    Rectangle { width: 10; height: 1; color: Design.Theme.alpha(root.accent, 0.40); anchors.top: parent.top; anchors.right: parent.right; anchors.rightMargin: 10 }
    Rectangle { width: 1;  height: 10; color: Design.Theme.alpha(root.accent, 0.40); anchors.top: parent.top; anchors.right: parent.right; anchors.rightMargin: 10 }

    Column {
        id: heroColumn
        anchors.left:      parent.left
        anchors.right:     parent.right
        anchors.top:       parent.top
        anchors.leftMargin:  root.horizontalPadding
        anchors.rightMargin: root.horizontalPadding
        anchors.topMargin:   root.topPadding
        spacing: root.sectionSpacing

        Column {
            width: parent.width
            spacing: root.headingSpacing

            // Overline badge
            Rectangle {
                visible: root.overline.length > 0
                width:  overlineRow.width + 14
                height: overlineRow.height + 8
                radius: 4
                color:  Design.Theme.alpha(root.accent, 0.10)
                border.width: 1
                border.color: Design.Theme.alpha(root.accent, 0.30)

                Row {
                    id: overlineRow
                    x: 7; y: 4
                    spacing: 6

                    Item {
                        width:  root.hasOverlineIcon ? Design.Theme.foundation.iconSizeSm : 8
                        height: root.hasOverlineIcon ? Design.Theme.foundation.iconSizeSm : 8

                        NeoIcon {
                            anchors.centerIn: parent
                            visible: root.hasOverlineIcon
                            iconSpec: root.resolvedOverlineIconSpec
                            color: root.accent
                            size: Design.Theme.foundation.iconSizeSm
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            visible: !root.hasOverlineIcon
                            width: 6; height: 6; radius: 3
                            color: root.accent
                            opacity: 0.8
                        }
                    }

                    Text {
                        text: root.overline
                        color: root.heroTheme.overlineText
                        font.pixelSize: 10
                        font.weight: Font.Black
                        font.letterSpacing: 1.5
                    }
                }
            }

            Text {
                width: parent.width
                visible: root.title.length > 0
                text: root.title
                color: root.heroTheme.title
                font.pixelSize: compact ? 18 : 22
                font.weight: Font.Black
                font.letterSpacing: 0.5
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                visible: root.description.length > 0
                text: root.description
                color: root.heroTheme.description
                font.pixelSize: compact ? 11 : 12
                lineHeight: 1.30
                wrapMode: Text.WordWrap
            }
        }

        // Metrics grid
        Grid {
            id: metricsGrid
            width: parent.width
            visible: root.metricCount > 0
            columns: root.metricColumns
            spacing: root.metricSpacing

            Repeater {
                model: root.metrics || []

                delegate: Rectangle {
                    property color metricAccent: modelData.accent ? modelData.accent : root.accent
                    property var metricTheme: Design.Theme.metricChip(metricAccent)
                    width: root.metricWidth
                    radius: 6
                    height: metricColumn.height + 16
                    color: metricTheme.background
                    border.width: 1
                    border.color: metricTheme.border

                    Column {
                        id: metricColumn
                        x: 10; y: 8
                        width: parent.width - 20
                        spacing: 2

                        Text {
                            width: parent.width
                            text: modelData.label || ""
                            color: metricTheme.label
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.2
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            width: parent.width
                            text: modelData.value !== undefined ? String(modelData.value) : ""
                            color: metricTheme.value
                            font.pixelSize: root.compact ? 14 : 16
                            font.weight: Font.Thin
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
