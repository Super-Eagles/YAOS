import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../theme" as Design

Item {
    id: root

    implicitWidth: 200
    implicitHeight: 30

    property var model: []
    property var checkedItems: []
    property var selectedItems: []
    property string placeholderText: "请选择..."
    signal selectionChanged(var items)

    readonly property var comboTheme: Design.Theme.comboBox("default")
    readonly property var fieldTheme: Design.Theme.field("default")

    property string searchText: ""
    property var filteredModel: {
        if (!model) return [];
        if (searchText.trim() === "") return model;
        var s = searchText.toLowerCase();
        return model.filter(function(item) {
            return String(item).toLowerCase().indexOf(s) !== -1;
        });
    }

    function sameItems(left, right) {
        var a = left || []; var b = right || [];
        if (a.length !== b.length) return false;
        for (var i = 0; i < a.length; ++i) { if (a[i] !== b[i]) return false; }
        return true;
    }
    function copyItems(items) { return (items || []).slice(); }
    function setSelectedItems(items, notify) {
        var next = copyItems(items);
        if (sameItems(root.selectedItems, next)) return;
        root.selectedItems = next;
        if (notify) root.selectionChanged(copyItems(next));
    }

    Component.onCompleted: setSelectedItems(root.checkedItems, false)
    onCheckedItemsChanged: setSelectedItems(root.checkedItems, false)

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: 7
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: mouseArea.pressed ? root.fieldTheme.startPressed : (mouseArea.containsMouse ? root.fieldTheme.startHover : root.fieldTheme.start)
                Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
            }
            GradientStop {
                position: 1.0
                color: mouseArea.pressed ? root.fieldTheme.endPressed : (mouseArea.containsMouse ? root.fieldTheme.endHover : root.fieldTheme.end)
                Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }
            }
        }
        border.width: 1
        border.color: mouseArea.containsMouse ? root.fieldTheme.borderHover : root.fieldTheme.border
        Behavior on border.color { ColorAnimation { duration: Design.Theme.foundation.durationFast; easing.type: Easing.OutExpo } }

        Text {
            anchors.left: parent.left; anchors.leftMargin: 10
            anchors.right: indicator.left; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.selectedItems.length > 0
                ? ("已选 " + root.selectedItems.length + " 个模型")
                : root.placeholderText
            color: root.selectedItems.length > 0 ? root.comboTheme.text : root.fieldTheme.placeholder
            font.pixelSize: Design.Foundation.textMd
            font.letterSpacing: 0.2
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            id: indicator
            text: popup.visible ? "▴" : "▾"
            color: popup.visible ? root.comboTheme.indicatorOpen : (mouseArea.containsMouse ? root.comboTheme.indicatorHover : root.comboTheme.indicator)
            font.pixelSize: Design.Foundation.textSm
            anchors.right: parent.right; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast } }
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: { if (popup.visible) popup.close(); else popup.open(); }
        }
    }

    Popup {
        id: popup
        y: root.height + 5
        width: root.width
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        implicitHeight: Math.min(300, 58 + (listView.count * 38))

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140; easing.type: Easing.OutExpo }
                NumberAnimation { property: "scale";   from: 0.97; to: 1; duration: 160; easing.type: Easing.OutExpo }
            }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 }
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
                anchors.fill: parent; anchors.margins: 1
                radius: parent.radius - 1
                color: root.comboTheme.popupInner
            }
            Rectangle {
                width: parent.width * 0.5; height: 1
                anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
                color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.35)
            }
        }

        onOpened: { root.searchText = ""; searchInput.forceActiveFocus(); }

        ColumnLayout {
            anchors.fill: parent
            spacing: 5

            GlassField {
                id: searchInput
                Layout.fillWidth: true
                placeholderText: "搜索模型..."
                text: root.searchText
                onTextChanged: root.searchText = text
            }

            ListView {
                id: listView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 3
                boundsBehavior: Flickable.StopAtBounds
                model: popup.visible ? root.filteredModel : null

                ScrollBar.vertical: ScrollBar { active: true; policy: ScrollBar.AlwaysOn }

                delegate: ItemDelegate {
                    width: ListView.view.width - 12
                    implicitHeight: 36
                    padding: 0
                    hoverEnabled: true
                    property bool isChecked: root.selectedItems.indexOf(modelData) >= 0

                    contentItem: RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 12
                        spacing: 8
                        NeoCheckBox {
                            checked: isChecked
                            onCheckedChanged: {
                                var items = root.selectedItems.slice();
                                var idx = items.indexOf(modelData);
                                if (checked && idx < 0) items.push(modelData);
                                else if (!checked && idx >= 0) items.splice(idx, 1);
                                root.setSelectedItems(items, true);
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData
                            color: isChecked ? root.comboTheme.itemTextActive : root.comboTheme.itemText
                            font.pixelSize: Design.Foundation.textMd
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    background: Rectangle {
                        radius: 6
                        color: hovered ? root.comboTheme.itemBackgroundHover : "transparent"
                        Behavior on color { ColorAnimation { duration: Design.Theme.foundation.durationFast } }
                    }
                    onClicked: {
                        var items = root.selectedItems.slice();
                        var idx = items.indexOf(modelData);
                        if (idx < 0) items.push(modelData); else items.splice(idx, 1);
                        root.setSelectedItems(items, true);
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: listView.count === 0
                text: "未找到匹配模型"
                color: root.comboTheme.itemText
                font.pixelSize: Design.Foundation.textSm
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
