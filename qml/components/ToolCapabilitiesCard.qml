import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../theme" as Design

NeoCard {
    id: root
    objectName: "runtimeCard_toolCapabilities"
    property var app
    property var runtimeCapabilities: []
    readonly property real gridHeight: Number(toolCapabilitiesGrid.height || toolCapabilitiesGrid.implicitHeight || 0)
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")
    readonly property var listItemStyle: Design.Theme.listItem("default")
    readonly property var selectedListItemStyle: Design.Theme.listItem("selected")

    width: parent ? parent.width : 0
    Layout.minimumHeight: 330
    sectionKey: "resources"
    title: "工具能力 Tool Capabilities"
    subtitle: "决定 Agent 可以注册哪些工具能力"
    titleIconKey: "resources"
    titleIcon: "⌬"
    guideText: "按需勾选 Agent 可用的工具能力；关闭后，对话和自动化都不会再注册这一类工具。"

    function assign() {
        return app ? app.assign.apply(app, arguments) : undefined;
    }
    function read() {
        return app ? app.read.apply(app, arguments) : undefined;
    }
    function threeColumnCount() {
        return app ? app.threeColumnCount.apply(app, arguments) : 1;
    }
    function countEnabledCapabilities() {
        var list = runtimeCapabilities || [];
        var count = 0;
        for (var i = 0; i < list.length; ++i) {
            if (read("tools.capabilities." + list[i].key, false)) {
                count += 1;
            }
        }
        return count;
    }

    Column {
        width: parent.width
        spacing: 12

        Rectangle {
            width: parent.width
            implicitHeight: 32
            radius: 8
            color: summaryBoxStyle.background
            border.width: 1
            border.color: summaryBoxStyle.border

            RowLayout {
                id: toolCapabilitySummaryRow
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Text {
                    text: "已启用 " + String(countEnabledCapabilities()) + " / " + String((runtimeCapabilities || []).length)
                    color: summaryBoxStyle.title
                    font.pixelSize: Design.Foundation.textMd
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    Layout.fillHeight: true
                    Layout.topMargin: 8
                    Layout.bottomMargin: 8
                    color: summaryBoxStyle.border
                }

                Text {
                    Layout.fillWidth: true
                    text: "关闭某类能力后,对话,自动化和委托链里都不会再注册对应工具."
                    color: summaryBoxStyle.text
                    font.pixelSize: Design.Foundation.textSm
                    elide: Text.ElideRight
                }
            }
        }

        RowLayout {
            id: toolCapabilitiesGrid
            width: parent.width
            spacing: 12

            Repeater {
                model: runtimeCapabilities
                delegate: Rectangle {
                    property bool enabledCapability: read("tools.capabilities." + modelData.key, false)
                    property var capabilityStyle: enabledCapability ? selectedListItemStyle : listItemStyle
                    Layout.fillWidth: true
                    Layout.preferredWidth: 100
                    height: 60
                    radius: 8
                    color: capabilityStyle.background
                    border.width: 1
                    border.color: capabilityStyle.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: modelData.title
                            color: capabilityStyle.title
                            font.pixelSize: Design.Foundation.textMd
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        NeoCheckBox {
                            checked: read("tools.capabilities." + modelData.key, false)
                            onToggled: assign("tools.capabilities." + modelData.key, checked)
                        }
                    }
                }
            }
        }
    }
}
