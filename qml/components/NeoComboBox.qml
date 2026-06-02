import QtQuick 2.14
import QtQuick.Controls 2.14
import "../theme" as Design

ComboBox {
    id: root
    readonly property var comboTheme: Design.Theme.comboBox("default")
    readonly property var fieldTheme: Design.Theme.field("default")
    hoverEnabled: true

    Accessible.role: Accessible.ComboBox
    Accessible.name: root.displayText
    implicitHeight: 36
    implicitWidth: 160
    padding: 0

    indicator: Text {
        text: root.popup.visible ? "▴" : "▾"
        color: root.popup.visible
            ? root.comboTheme.indicatorOpen
            : (root.hovered ? root.comboTheme.indicatorHover : root.comboTheme.indicator)
        Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
        font.pixelSize: 11
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
    }

    contentItem: Text {
        leftPadding: 10
        rightPadding: 28
        text: root.displayText
        color: root.comboTheme.text
        font.pixelSize: 12
        font.letterSpacing: 0.2
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    delegate: ItemDelegate {
        id: comboDelegate
        width: ListView.view ? ListView.view.width : root.width
        implicitHeight: 36
        padding: 0
        hoverEnabled: true
        highlighted: root.highlightedIndex === index

        contentItem: Text {
            leftPadding: 12
            rightPadding: 12
            text: {
                if (typeof modelData === "string" || typeof modelData === "number") return String(modelData);
                if (root.textRole && modelData && modelData[root.textRole] !== undefined) return String(modelData[root.textRole]);
                return modelData !== undefined && modelData !== null ? String(modelData) : "";
            }
            color: comboDelegate.highlighted ? root.comboTheme.itemTextActive : root.comboTheme.itemText
            font.pixelSize: 12
            font.weight: comboDelegate.highlighted ? Font.DemiBold : Font.Normal
            font.letterSpacing: 0.2
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 6
            color: comboDelegate.highlighted
                ? root.comboTheme.itemBackgroundActive
                : (comboDelegate.hovered ? root.comboTheme.itemBackgroundHover : "transparent")
            border.width: comboDelegate.highlighted ? 1 : 0
            border.color: root.comboTheme.itemBorderActive
            Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
        }
    }

    popup: Popup {
        y: root.height + 5
        width: root.width
        padding: 6
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 260)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140; easing.type: Easing.OutExpo }
                NumberAnimation { property: "scale";   from: 0.97; to: 1.0; duration: 160; easing.type: Easing.OutExpo }
            }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100; easing.type: Easing.InExpo }
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            spacing: 3
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator { active: root.popup.visible }
        }

        background: Rectangle {
            radius: 8
            gradient: Gradient {
                GradientStop { position: 0.0; color: root.comboTheme.popupStart }
                GradientStop { position: 1.0; color: root.comboTheme.popupEnd }
            }
            border.width: 1
            border.color: root.comboTheme.popupBorder

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: parent.radius - 1
                color: root.comboTheme.popupInner
            }

            // Top neon accent line
            Rectangle {
                width: parent.width * 0.5; height: 1
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.35)
            }
        }
    }

    background: Rectangle {
        radius: 7
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: root.pressed ? root.fieldTheme.startPressed : (root.hovered ? root.fieldTheme.startHover : root.fieldTheme.start)
                Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
            }
            GradientStop {
                position: 1.0
                color: root.pressed ? root.fieldTheme.endPressed : (root.hovered ? root.fieldTheme.endHover : root.fieldTheme.end)
                Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
            }
        }
        border.width: 1
        border.color: root.visualFocus
            ? root.fieldTheme.borderFocus
            : (root.hovered ? root.fieldTheme.borderHover : root.fieldTheme.border)
        Behavior on border.color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
    }
}
