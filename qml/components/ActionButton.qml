import QtQuick 2.14
import QtQuick.Controls 2.14
import QtGraphicalEffects 1.14
import "../theme" as Design

Button {
    id: root
    property string tone: "neutral"
    property bool compact: false
    readonly property var buttonTheme: Design.Theme.button(root.tone)
    hoverEnabled: true
    opacity: root.enabled ? 1.0 : 0.46
    Behavior on opacity { NumberAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }

    Accessible.role: Accessible.Button
    Accessible.name: root.text

    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()

    scale: root.down ? 0.970 : 1.0
    Behavior on scale { NumberAnimation { duration: 100; easing.type: Easing.OutExpo } }

    leftPadding: compact ? 12 : 16
    rightPadding: compact ? 12 : 16

    implicitHeight: compact ? 32 : 36
    implicitWidth: Math.max(compact ? 76 : 100,
                            buttonLabel.implicitWidth + leftPadding + rightPadding)

    background: Item {
        // Outer glow when hovered (the "electric edge")
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: (compact ? 9 : 11) + 3
            color: "transparent"
            border.width: root.hovered && root.enabled ? 1 : 0
            border.color: Design.Theme.alpha(
                root.tone === "danger"   ? Design.Theme.palette.accentPink  :
                root.tone === "success"  ? Design.Theme.palette.accentGreen :
                root.tone === "warning"  ? Design.Theme.palette.accentAmber :
                Design.Theme.palette.accentCyan, 0.20)
            visible: root.hovered && root.enabled
            Behavior on border.color { ColorAnimation { duration: Design.Theme.foundation.durationFast } }
        }

        Rectangle {
            anchors.fill: parent
            radius: compact ? 9 : 11
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: {
                        if (!root.enabled) return root.buttonTheme.disabledStart;
                        return root.down ? root.buttonTheme.startDown
                            : (root.hovered ? root.buttonTheme.startHover : root.buttonTheme.start);
                    }
                    Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
                }
                GradientStop {
                    position: 1.0
                    color: {
                        if (!root.enabled) return root.buttonTheme.disabledEnd;
                        return root.down ? root.buttonTheme.endDown : root.buttonTheme.end;
                    }
                    Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
                }
            }

            // Neon border
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.width: 1
                border.color: {
                    if (!root.enabled) return Design.Theme.palette.accentSlate;
                    return root.hovered ? root.buttonTheme.borderHover : root.buttonTheme.border;
                }
                Behavior on border.color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
            }

            // Top highlight glint
            Rectangle {
                width: parent.width * 0.55
                height: 1
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                color: Qt.rgba(1, 1, 1, root.hovered ? 0.10 : 0.04)
                Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast } }
            }

            // Glass inner overlay
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: parent.radius - 1
                color: root.hovered ? root.buttonTheme.overlayHover : root.buttonTheme.overlay
                Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
            }
        }

        // Focus ring
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: (compact ? 9 : 11) + 2
            color: "transparent"
            border.width: root.activeFocus ? 2 : 0
            border.color: Design.Theme.palette.accentCyan
            visible: root.activeFocus
        }
    }

    contentItem: Text {
        id: buttonLabel
        text: root.text
        color: root.enabled ? root.buttonTheme.foreground : root.buttonTheme.foregroundDisabled
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: root.compact ? 11 : 12
        font.weight: Font.DemiBold
        font.letterSpacing: 0.5
        elide: Text.ElideRight
        wrapMode: Text.NoWrap
        maximumLineCount: 1
        renderType: Text.NativeRendering
        Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast } }
    }
}
