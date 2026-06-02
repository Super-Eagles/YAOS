import QtQuick 2.14
import QtQuick.Controls 2.14
import "../theme" as Design

TextField {
    id: root
    readonly property var fieldTheme: Design.Theme.field("default")
    hoverEnabled: true

    Accessible.role: Accessible.EditableText
    Accessible.name: root.placeholderText || "Text field"
    color: root.fieldTheme.text
    font.pixelSize: 13
    placeholderTextColor: root.fieldTheme.placeholder
    selectByMouse: true
    selectedTextColor: root.fieldTheme.selectedText
    selectionColor: root.fieldTheme.selection
    implicitHeight: 30
    leftPadding: 12
    rightPadding: 12

    background: Rectangle {
        radius: 7
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: root.hovered ? root.fieldTheme.startHover : root.fieldTheme.start
                Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
            }
            GradientStop {
                position: 1.0
                color: root.hovered ? root.fieldTheme.endHover : root.fieldTheme.end
                Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
            }
        }
        border.width: 1
        border.color: root.activeFocus
            ? root.fieldTheme.borderFocus
            : (root.hovered ? root.fieldTheme.borderHover : root.fieldTheme.border)
        Behavior on border.color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
    }
}
