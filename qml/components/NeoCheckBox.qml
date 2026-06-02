import QtQuick 2.14
import QtQuick.Controls 2.14
import QtGraphicalEffects 1.14
import "../theme" as Design

CheckBox {
    id: root
    readonly property var checkTheme: Design.Theme.checkBox("default")
    hoverEnabled: true
    spacing: 6

    Accessible.role: Accessible.CheckBox
    Accessible.name: root.text
    Accessible.checked: root.checked

    Keys.onReturnPressed: root.toggle()
    Keys.onSpacePressed:  root.toggle()
    implicitHeight: Math.max(20, contentItem.implicitHeight)

    indicator: Rectangle {
        implicitWidth: 17
        implicitHeight: 17
        radius: 4
        color: root.checked
            ? root.checkTheme.indicatorChecked
            : (root.hovered ? root.checkTheme.indicatorHover : root.checkTheme.indicator)
        border.width: 1
        border.color: root.checked
            ? root.checkTheme.indicatorBorderChecked
            : (root.hovered ? root.checkTheme.indicatorBorderHover : root.checkTheme.indicatorBorder)

        Behavior on color        { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
        Behavior on border.color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }

        // Check mark — neon glow when active
        Rectangle {
            anchors.centerIn: parent
            width: 7; height: 7; radius: 2
            visible: root.checked
            color: root.checkTheme.mark

            layer.enabled: root.checked
            layer.effect: Glow {
                radius: 4; samples: 9
                color: root.checkTheme.mark; spread: 0.4
            }
        }
    }

    contentItem: Text {
        width: root.width
        text: root.text
        color: root.checked
            ? root.checkTheme.textChecked
            : (root.hovered ? root.checkTheme.textHover : root.checkTheme.text)
        Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
        font.pixelSize: Design.Foundation.textMd
        font.weight: root.checked ? Font.DemiBold : Font.Normal
        font.letterSpacing: 0.2
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator.width + root.spacing
        rightPadding: 4
        wrapMode: Text.WordWrap
    }
}
