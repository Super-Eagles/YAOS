import QtQuick 2.14
import QtQuick.Controls 2.14
import "../theme" as Design

Button {
    id: root
    property string symbol: ""
    property bool danger: false
    hoverEnabled: true

    property bool _hovered: false
    property bool _pressed: false

    readonly property var controlTheme: Design.Theme.windowControl(root.danger, root._hovered, root._pressed)

    implicitWidth: 28
    implicitHeight: 28

    background: Rectangle {
        radius: 6
        color: root.controlTheme.background
        border.width: 1
        border.color: root.controlTheme.border
        Behavior on color  { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
        Behavior on border.color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
    }

    contentItem: Text {
        text: root.symbol
        color: root.controlTheme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: 12
        font.weight: Font.Light
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered:  { root._hovered = true }
        onExited:   { root._hovered = false; root._pressed = false }
        onPressed:  { root._pressed = true }
        onReleased: { root._pressed = false }
        onClicked:  root.clicked()
    }

    scale: _pressed ? 0.92 : 1.0
    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutExpo } }
}
