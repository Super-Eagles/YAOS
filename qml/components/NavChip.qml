import QtQuick 2.14
import QtQuick.Controls 2.14
import QtGraphicalEffects 1.14
import "../theme" as Design

Button {
    id: root
    property color accent: Design.Theme.palette.accentCyan
    readonly property var chipTheme: Design.Theme.navChip(root.accent)
    hoverEnabled: true

    Accessible.role: Accessible.Button
    Accessible.name: root.text

    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
    leftPadding: 18
    rightPadding: 18

    implicitHeight: 42
    implicitWidth: Math.max(196, chipLabel.implicitWidth + leftPadding + rightPadding + 6)

    // Entry animation
    opacity: 0
    Component.onCompleted: {
        entryAnim.start()
    }
    NumberAnimation {
        id: entryAnim
        target: root
        property: "opacity"
        from: 0; to: 1
        duration: 360
        easing.type: Easing.OutExpo
    }

    background: Item {
        // Glow effect when active
        Rectangle {
            id: glowRect
            anchors.fill: parent
            anchors.margins: -2
            radius: 14
            color: "transparent"
            border.width: root.checked ? 1 : 0
            border.color: Design.Theme.alpha(root.accent, root.checked ? 0.22 : 0.0)
            visible: root.checked || root.hovered
            Behavior on border.color { ColorAnimation { duration: Design.Theme.foundation.durationFast } }
        }

        Rectangle {
            id: chipBg
            anchors.fill: parent
            radius: 8
            color: root.checked ? "#0A1030" : (root.hovered ? "#0D1228" : "#09091A")
            Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationBase; easing.type: Easing.OutExpo } }

            // Neon left rail indicator
            Rectangle {
                id: rail
                width: 3
                radius: 1.5
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: 9
                anchors.bottomMargin: 9
                anchors.leftMargin: 7
                color: root.checked ? root.accent : Design.Theme.palette.accentViolet
                opacity: root.checked ? 1.0 : (root.hovered ? 0.50 : 0.0)
                Behavior on opacity { NumberAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
                Behavior on color   { ColorAnimation  { duration: Design.Theme.foundation.durationFast } }

                // Rail glow
                layer.enabled: root.checked
                layer.effect: Glow {
                    radius: 6
                    samples: 13
                    color: root.accent
                    spread: 0.2
                }
            }

            // Border
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.width: 1
                border.color: root.checked
                    ? Design.Theme.alpha(root.accent, 0.55)
                    : (root.hovered
                        ? Design.Theme.alpha(root.accent, 0.28)
                        : Design.Theme.alpha(root.accent, 0.08))
                Behavior on border.color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
            }

            // Top edge glint when checked
            Rectangle {
                visible: root.checked
                width: parent.width * 0.5
                height: 1
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                color: Design.Theme.alpha(root.accent, 0.50)
                Behavior on opacity { NumberAnimation { duration: Design.Theme.foundation.durationFast } }
            }
        }

        // Focus ring
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: 10
            color: "transparent"
            border.width: root.activeFocus ? 2 : 0
            border.color: root.accent
            visible: root.activeFocus
        }
    }

    contentItem: Text {
        id: chipLabel
        text: root.text
        width: root.width - 24
        color: root.checked
            ? root.accent
            : (root.hovered ? Design.Theme.palette.textPrimary : Design.Theme.palette.textSecondary)
        Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
        font.pixelSize: Design.Foundation.textMd
        font.weight: root.checked ? Font.DemiBold : Font.Normal
        font.letterSpacing: root.checked ? 0.4 : 0
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.NoWrap
        leftPadding: 18
        elide: Text.ElideRight
    }

    // Scale feedback on press
    scale: root.down ? 0.975 : 1.0
    Behavior on scale { NumberAnimation { duration: 100; easing.type: Easing.OutExpo } }
}
