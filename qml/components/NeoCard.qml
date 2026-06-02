import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import QtGraphicalEffects 1.14
import "../theme" as Design

Rectangle {
    id: root
    property string title: ""
    property string subtitle: ""
    property string titleIcon: ""
    property string titleIconKey: ""
    property var titleIconSpec: undefined
    property string titleBadgeText: ""
    property color titleBadgeColor: glowColor
    property string guideText: ""
    property string sectionKey: ""
    property string cardVariant: "default"
    readonly property var sectionTheme: Design.Theme.section(root.sectionKey)
    readonly property var cardTheme: Design.Theme.card(root.cardVariant)
    readonly property var resolvedTitleIconSpec: {
        if (root.titleIconSpec !== undefined)  return Design.Theme.resolveIcon(root.titleIconSpec);
        if (root.titleIconKey.length > 0)      return Design.Theme.resolveIcon(root.titleIconKey, "section");
        if (root.titleIcon.length > 0)         return Design.Theme.resolveIcon(root.titleIcon);
        if (root.sectionKey.length > 0)        return root.sectionTheme.icon;
        return Design.Theme.resolveIcon("");
    }
    readonly property bool hasTitleIcon: (resolvedTitleIconSpec.type === "glyph" && String(resolvedTitleIconSpec.value || "").length > 0)
        || (resolvedTitleIconSpec.type === "image" && String(resolvedTitleIconSpec.source || "").length > 0)
    property color glowColor: root.sectionKey.length > 0 ? root.sectionTheme.accent : Design.Theme.palette.accentCyan
    property bool stretchContent: false
    readonly property bool usesNaturalHeight: !root.stretchContent
    readonly property int frameMargin: 8
    readonly property int frameSpacing: 6
    readonly property real headerHeight: headerBlock.visible ? headerColumn.implicitHeight : 0
    readonly property real contentHeight: root.usesNaturalHeight ? contentColumn.implicitHeight : 0
    readonly property real naturalHeight: root.frameMargin * 2
                                         + root.headerHeight
                                         + root.contentHeight
                                         + ((root.headerHeight > 0 && root.contentHeight > 0)
                                                ? root.frameSpacing : 0)
    default property alias contentData: contentColumn.data

    radius: 8
    clip: true
    color: root.cardTheme.background
    implicitHeight: root.usesNaturalHeight ? root.naturalHeight : 0

    Accessible.role: Accessible.Pane
    Accessible.name: root.title.length > 0 ? root.title : "Card"

    property bool hovered: false

    // ── Entry animation ──────────────────────────────
    opacity: 0
    Component.onCompleted: {
        entryOpacity.start()
    }
    NumberAnimation { id: entryOpacity; target: root; property: "opacity"; from: 0; to: 1; duration: 420; easing.type: Easing.OutExpo }

    // ── Background layers ────────────────────────────
    // Deep gradient fill
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 7
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: hovered ? root.cardTheme.innerHoverStart : root.cardTheme.innerStart
                Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationBase; easing.type: Easing.OutExpo } }
            }
            GradientStop {
                position: 1.0
                color: hovered ? root.cardTheme.innerHoverEnd : root.cardTheme.innerEnd
                Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationBase; easing.type: Easing.OutExpo } }
            }
        }
    }

    // Neon border (the card "frame")
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: "transparent"
        border.width: 1
        border.color: hovered
            ? Design.Theme.alpha(root.glowColor, 0.42)
            : Design.Theme.alpha(root.glowColor, 0.16)
        Behavior on border.color { ColorAnimation { duration: Design.Theme.foundation.durationBase; easing.type: Easing.OutExpo } }
    }

    // Top neon accent bar
    Rectangle {
        id: topBar
        width: hovered ? 80 : 48
        height: 2
        radius: 1
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.leftMargin: 12
        color: Design.Theme.alpha(root.glowColor, hovered ? 1.0 : 0.65)
        Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast } }
        Behavior on width  { NumberAnimation { duration: Design.Theme.foundation.durationMedium; easing.type: Easing.OutExpo } }

        // Top bar glow
        layer.enabled: hovered
        layer.effect: Glow {
            radius: 5
            samples: 11
            color: root.glowColor
            spread: 0.3
        }
    }

    // Corner accent marks — cyberpunk "bracket" corners
    Rectangle { width: 8; height: 1; color: Design.Theme.alpha(root.glowColor, 0.35); anchors.top: parent.top; anchors.right: parent.right; anchors.rightMargin: 8 }
    Rectangle { width: 1; height: 8; color: Design.Theme.alpha(root.glowColor, 0.35); anchors.top: parent.top; anchors.right: parent.right; anchors.rightMargin: 8 }
    Rectangle { width: 8; height: 1; color: Design.Theme.alpha(root.glowColor, 0.20); anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.leftMargin: 8 }
    Rectangle { width: 1; height: 8; color: Design.Theme.alpha(root.glowColor, 0.20); anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.leftMargin: 8 }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onEntered: root.hovered = true
        onExited:  root.hovered = false
    }

    // ── Content ──────────────────────────────────────
    Item {
        id: frameLayout
        anchors.left:    parent.left
        anchors.right:   parent.right
        anchors.top:     parent.top
        anchors.bottom:  parent.bottom
        anchors.margins: root.frameMargin

        Item {
            id: headerBlock
            anchors.left:  parent.left
            anchors.right: parent.right
            anchors.top:   parent.top
            visible: root.title.length > 0 || root.subtitle.length > 0 ||
                     root.hasTitleIcon || root.titleBadgeText.length > 0 ||
                     root.guideText.length > 0
            implicitHeight: headerColumn.implicitHeight
            height: visible ? implicitHeight : 0

            Column {
                id: headerColumn
                width: parent.width
                spacing: 6

                Item {
                    id: titleRow
                    width: parent.width
                    visible: root.title.length > 0 || root.subtitle.length > 0 ||
                             root.hasTitleIcon || root.titleBadgeText.length > 0
                    height: Math.max(titleIconChip.visible ? titleIconChip.height : 0,
                                     titleTextColumn.height)

                    Rectangle {
                        id: titleIconChip
                        visible: root.hasTitleIcon
                        width: 26; height: 26; radius: 5
                        color: Design.Theme.alpha(root.glowColor, 0.10)
                        border.width: 1
                        border.color: Design.Theme.alpha(root.glowColor, 0.30)
                        anchors.left: parent.left
                        anchors.top:  parent.top

                        NeoIcon {
                            anchors.centerIn: parent
                            iconSpec: root.resolvedTitleIconSpec
                            color: root.glowColor
                            size: Design.Theme.foundation.iconSizeMd
                        }
                    }

                    Column {
                        id: titleTextColumn
                        anchors.left:       parent.left
                        anchors.leftMargin: titleIconChip.visible ? titleIconChip.width + 8 : 0
                        anchors.right:      parent.right
                        anchors.top:        parent.top
                        spacing: 3

                        Item {
                            id: titleLine
                            width: parent.width
                            visible: root.title.length > 0 || root.titleBadgeText.length > 0
                            height: Math.max(titleLabel.visible ? titleLabel.height : 0,
                                             titleBadgeChip.visible ? titleBadgeChip.height : 0)

                            Text {
                                id: titleLabel
                                visible: root.title.length > 0
                                width: titleBadgeChip.visible
                                    ? Math.max(0, parent.width - titleBadgeChip.width - 8)
                                    : parent.width
                                text: root.title
                                color: root.cardTheme.title
                                font.pixelSize: Design.Foundation.textLg
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.3
                                wrapMode: Text.WordWrap
                                anchors.left: parent.left
                                anchors.top:  parent.top
                            }

                            Rectangle {
                                id: titleBadgeChip
                                visible: root.titleBadgeText.length > 0
                                radius: 4
                                implicitWidth:  titleBadgeLabel.implicitWidth + 10
                                implicitHeight: titleBadgeLabel.implicitHeight + 5
                                color:  Design.Theme.alpha(root.titleBadgeColor, 0.12)
                                border.width: 1
                                border.color: Design.Theme.alpha(root.titleBadgeColor, 0.38)
                                anchors.right: parent.right
                                anchors.top:   parent.top

                                Text {
                                    id: titleBadgeLabel
                                    anchors.centerIn: parent
                                    text: root.titleBadgeText
                                    color: root.cardTheme.badgeText
                                    font.pixelSize: Design.Foundation.textXs
                                    font.weight: Font.Black
                                    font.letterSpacing: 0.8
                                }
                            }
                        }

                        Text {
                            visible: root.subtitle.length > 0
                            width: parent.width
                            text: root.subtitle
                            color: root.cardTheme.subtitle
                            wrapMode: Text.WordWrap
                            font.pixelSize: Design.Foundation.textXs
                            font.letterSpacing: 0.2
                        }
                    }
                }

                Rectangle {
                    visible: root.guideText.length > 0
                    width: parent.width
                    height: guideBody.height + 8
                    radius: 5
                    color: root.cardTheme.guideBackground
                    border.width: 1
                    border.color: Design.Theme.alpha(root.glowColor, 0.16)

                    Item {
                        id: guideBody
                        x: 8; y: 8
                        width: parent.width - 16
                        height: Math.max(guideLabel.implicitHeight, guideTextLabel.implicitHeight)

                        Text {
                            id: guideLabel
                            text: "指南"
                            color: root.cardTheme.guideLabel
                            font.pixelSize: Design.Foundation.textXs
                            font.weight: Font.Black
                            font.letterSpacing: 1.0
                            anchors.left: parent.left
                            anchors.top:  parent.top
                        }

                        Text {
                            id: guideTextLabel
                            anchors.left:       guideLabel.right
                            anchors.leftMargin: 6
                            anchors.right:      parent.right
                            anchors.top:        parent.top
                            text: root.guideText
                            color: root.cardTheme.guideText
                            font.pixelSize: Design.Foundation.textXs
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        ColumnLayout {
            id: contentColumn
            anchors.left:      parent.left
            anchors.right:     parent.right
            anchors.top:       headerBlock.visible ? headerBlock.bottom : parent.top
            anchors.topMargin: headerBlock.visible ? root.frameSpacing : 0
            anchors.bottom:    root.stretchContent ? parent.bottom : undefined
            spacing:           6
        }
    }
}
