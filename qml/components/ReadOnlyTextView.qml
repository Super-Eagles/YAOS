import QtQuick 2.14
import QtQuick.Controls 2.14
import "../theme" as Design

Item {
    id: root
    property alias text: viewer.text
    property int wrapMode: TextEdit.Wrap
    property int textFormat: TextEdit.PlainText
    readonly property var textViewTheme: Design.Theme.textView("default")
    property color textColor: root.textViewTheme.text
    property int fontPixelSize: 12
    property bool selectable: true
    property int contentPadding: 12

    implicitHeight: 94
    clip: true

    // Outer shell
    Rectangle {
        anchors.fill: parent
        radius: 8
        color: root.textViewTheme.outer
        border.width: 1
        border.color: root.textViewTheme.border
    }
    // Inner surface
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: 7
        color: root.textViewTheme.inner
    }
    // Top scan accent
    Rectangle {
        width: parent.width * 0.35; height: 1
        anchors.top: parent.top; anchors.left: parent.left; anchors.leftMargin: 12
        color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.28)
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: root.contentPadding
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, viewer.contentHeight)
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        interactive: contentHeight > height

        ScrollBar.vertical: ScrollBar {
            policy: flick.contentHeight > flick.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        TextEdit {
            id: viewer
            width: flick.width
            readOnly: true
            selectByMouse: root.selectable
            activeFocusOnPress: root.selectable
            wrapMode: root.wrapMode
            textFormat: root.textFormat
            color: root.textColor
            font.pixelSize: root.fontPixelSize
            font.family: Design.Theme.foundation.fontFamilyMono
            cursorVisible: false
            persistentSelection: true
            selectionColor: root.textViewTheme.selection
            selectedTextColor: root.textViewTheme.selectedText
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onWheel: {
                var maxY = Math.max(0, flick.contentHeight - flick.height);
                if (maxY <= 0) { wheel.accepted = false; return; }
                var delta = wheel.pixelDelta.y;
                if (delta === 0) delta = wheel.angleDelta.y / 3;
                if (delta === 0) { wheel.accepted = false; return; }
                var nextY = flick.contentY - delta;
                if (nextY < 0) nextY = 0;
                else if (nextY > maxY) nextY = maxY;
                if (nextY === flick.contentY) { wheel.accepted = false; return; }
                flick.contentY = nextY;
                wheel.accepted = true;
            }
        }
    }
}
