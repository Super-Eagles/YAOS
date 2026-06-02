import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../theme" as Design

NeoCard {
    id: root
    objectName: "runtimeCard_nodeDirectory"
    property var app
    property var studioBridge
    property string currentPage: app ? app.currentPage : ""
    property string selectedNodeId: ""
    property var selectedNode: currentPage === "runtime"
        ? selectNodeRecord(studioBridge ? (studioBridge.nodes || []) : [], selectedNodeId)
        : ({})
    property string effectiveSelectedNodeId: selectedNode ? (selectedNode.nodeId || "") : ""
    property int splitColumns: runtimeSplitColumns(width)
    readonly property real listHeight: Number(nodeDirectoryListView.height || nodeDirectoryListView.implicitHeight || 0)
    readonly property real detailHeight: Number(nodeDetailPanel.height || nodeDetailPanel.implicitHeight || 0)
    readonly property real capabilityScrollHeight: Number(nodeCapabilityScroll.height || nodeCapabilityScroll.implicitHeight || 0)
    readonly property var detailSummaryBoxStyle: Design.Theme.summaryBox("alt")
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")
    readonly property var listItemStyle: Design.Theme.listItem("default")
    readonly property var selectedListItemStyle: Design.Theme.listItem("selected")
    readonly property var successStatusStyle: Design.Theme.status("success")

    stretchContent: true
    title: "节点目录 Node Directory"
    subtitle: "查看集群内节点,实时负载和可用并发"

    function nodeCapabilityFlags() {
        return app ? app.nodeCapabilityFlags.apply(app, arguments) : "";
    }
    function nodeCapabilitySummary() {
        return app ? app.nodeCapabilitySummary.apply(app, arguments) : "";
    }
    function nodeCapabilityText() {
        return app ? app.nodeCapabilityText.apply(app, arguments) : "";
    }
    function nodeEndpointHealthText() {
        return app ? app.nodeEndpointHealthText.apply(app, arguments) : "";
    }
    function nodeIdentityText() {
        return app ? app.nodeIdentityText.apply(app, arguments) : "";
    }
    function nodePressureText() {
        return app ? app.nodePressureText.apply(app, arguments) : "";
    }
    function nodeRoutingSummary() {
        return app ? app.nodeRoutingSummary.apply(app, arguments) : "";
    }
    function pageListModel() {
        return app ? app.pageListModel.apply(app, arguments) : [];
    }
    function runtimeSplitColumns(containerWidth) {
        return containerWidth >= 1120 ? 2 : 1;
    }
    function selectNodeRecord() {
        return app ? app.selectNodeRecord.apply(app, arguments) : ({});
    }

    GridLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        columns: root.splitColumns
        columnSpacing: 14
        rowSpacing: 14

        ListView {
            id: nodeDirectoryListView
            Layout.fillWidth: true
            Layout.preferredWidth: root.splitColumns === 2
                ? root.width * 0.42
                : -1
            Layout.preferredHeight: root.splitColumns === 1 ? 240 : -1
            Layout.fillHeight: true
            clip: true
            spacing: 12
            model: pageListModel("runtime", studioBridge ? (studioBridge.nodes || []) : [])

            delegate: Rectangle {
                property var nodeItemStyle: root.effectiveSelectedNodeId === (modelData.nodeId || "")
                    ? selectedListItemStyle
                    : listItemStyle
                width: ListView.view.width
                implicitHeight: nodeDirectoryEntryColumn.implicitHeight + 28
                radius: 8
                color: nodeItemStyle.background
                border.width: 1
                border.color: root.effectiveSelectedNodeId === (modelData.nodeId || "")
                    ? nodeItemStyle.border
                    : (modelData.online ? successStatusStyle.accent : nodeItemStyle.border)

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.selectedNodeId = modelData.nodeId || ""
                }

                Row {
                    x: 14
                    y: 14
                    width: parent.width - 28
                    height: parent.height - 28
                    spacing: 14

                    Rectangle {
                        width: 10
                        height: parent.height
                        radius: 5
                        color: modelData.online ? successStatusStyle.accent : nodeItemStyle.meta
                    }

                    Column {
                        id: nodeDirectoryEntryColumn
                        width: parent.width - 24
                        spacing: 5

                        Text {
                            width: parent.width
                            text: (modelData.displayName || modelData.nodeId || "节点") +
                                  ((modelData.online === false) ? "  [离线]" : "")
                            color: nodeItemStyle.title
                            font.pixelSize: Design.Foundation.textXxl
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: nodePressureText(modelData)
                            color: modelData.online ? successStatusStyle.text : nodeItemStyle.text
                            font.pixelSize: Design.Foundation.textMd
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: nodeIdentityText(modelData)
                            color: nodeItemStyle.accent
                            font.pixelSize: Design.Foundation.textSm
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: nodeCapabilityText(modelData)
                            color: nodeItemStyle.meta
                            font.pixelSize: Design.Foundation.textSm
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Rectangle {
            id: nodeDetailPanel
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: root.splitColumns === 1 ? 420 : -1
            radius: 10
            color: detailSummaryBoxStyle.background
            border.width: 1
            border.color: detailSummaryBoxStyle.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: root.selectedNode
                        ? (root.selectedNode.displayName ||
                           root.selectedNode.nodeId ||
                           "节点")
                        : "未选择节点"
                    color: detailSummaryBoxStyle.title
                    font.pixelSize: Design.Foundation.textHero
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Rectangle {
                    visible: !!root.selectedNode
                    Layout.fillWidth: true
                    implicitHeight: 32
                    radius: 8
                    color: summaryBoxStyle.background
                    border.width: 1
                    border.color: summaryBoxStyle.border

                    RowLayout {
                        id: nodeDetailSummaryRow
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        Text {
                            text: root.selectedNode && root.selectedNode.online === false
                                ? "离线"
                                : "在线"
                            color: root.selectedNode && root.selectedNode.online === false
                                ? listItemStyle.meta
                                : successStatusStyle.text
                            font.pixelSize: Design.Foundation.textSm
                            font.weight: Font.Black
                        font.letterSpacing: 0.5
                        }

                        Rectangle {
                            Layout.fillHeight: true
                            Layout.topMargin: 8
                            Layout.bottomMargin: 8
                            color: summaryBoxStyle.border
                        }

                        Text {
                            Layout.fillWidth: true
                            text: nodeIdentityText(root.selectedNode || {})
                            color: summaryBoxStyle.text
                            font.pixelSize: Design.Foundation.textSm
                            elide: Text.ElideRight
                        }
                    }
                }

                Text {
                    visible: !!root.selectedNode
                    Layout.fillWidth: true
                    text: nodeRoutingSummary(root.selectedNode)
                    color: root.selectedNode && root.selectedNode.online === false
                        ? listItemStyle.text
                        : successStatusStyle.text
                    font.pixelSize: Design.Foundation.textMd
                    wrapMode: Text.WordWrap
                }

                Text {
                    visible: !!root.selectedNode
                    Layout.fillWidth: true
                    text: nodePressureText(root.selectedNode)
                    color: Design.Theme.section("routing").accent
                    font.pixelSize: Design.Foundation.textMd
                    wrapMode: Text.WordWrap
                }

                Text {
                    visible: !!root.selectedNode
                    Layout.fillWidth: true
                    text: nodeIdentityText(root.selectedNode)
                    color: detailSummaryBoxStyle.text
                    font.pixelSize: Design.Foundation.textMd
                    wrapMode: Text.WordWrap
                }

                Text {
                    visible: !!root.selectedNode &&
                             (root.selectedNode.endpoint || "").length > 0
                    Layout.fillWidth: true
                    text: "端点  " + (((root.selectedNode || {}).endpoint) || "")
                    color: listItemStyle.text
                    font.pixelSize: Design.Foundation.textMd
                    wrapMode: Text.WordWrap
                }

                Text {
                    visible: !!root.selectedNode
                    Layout.fillWidth: true
                    text: "端点健康  " + nodeEndpointHealthText(root.selectedNode || ({})) +
                          (((((root.selectedNode || {}).endpointHealthError) || "").length > 0)
                            ? ("  |  " + ((root.selectedNode || {}).endpointHealthError || ""))
                            : "")
                    color: ((root.selectedNode || {}).endpointHealthChecked) &&
                           !((root.selectedNode || {}).endpointReachable)
                        ? Design.Theme.status("error").text
                        : listItemStyle.text
                    font.pixelSize: Design.Foundation.textMd
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: detailSummaryBoxStyle.border
                    visible: !!root.selectedNode
                }

                ScrollView {
                    id: nodeCapabilityScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    visible: !!root.selectedNode

                    Column {
                        width: parent.width
                        spacing: 10

                        Repeater {
                            model: root.selectedNode
                                ? (root.selectedNode.capabilities || [])
                                : []

                            delegate: Rectangle {
                                width: parent.width
                                radius: 8
                                color: listItemStyle.background
                                border.width: 1
                                border.color: listItemStyle.border
                                implicitHeight: capabilityDetailColumn.implicitHeight + 22

                                Column {
                                    id: capabilityDetailColumn
                                    width: parent.width - 24
                                    anchors.left: parent.left
                                    anchors.leftMargin: 12
                                    anchors.top: parent.top
                                    anchors.topMargin: 11
                                    spacing: 6

                                    Text {
                                        width: parent.width
                                        text: (modelData.name || "能力") +
                                              ((modelData.version || "").length > 0
                                                ? ("  ·  " + modelData.version)
                                                : "")
                                        color: listItemStyle.title
                                        font.pixelSize: Design.Foundation.textLg
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: nodeCapabilitySummary(modelData)
                                        color: listItemStyle.accent
                                        font.pixelSize: Design.Foundation.textMd
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        width: parent.width
                                        text: nodeCapabilityFlags(modelData)
                                        color: listItemStyle.text
                                        font.pixelSize: Design.Foundation.textSm
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }

                        Text {
                            width: parent.width
                            visible: root.selectedNode &&
                                     (root.selectedNode.capabilities || []).length === 0
                            text: "该节点还没有上报能力清单."
                            color: listItemStyle.meta
                            font.pixelSize: Design.Foundation.textMd
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Text {
                    visible: !root.selectedNode
                    width: parent.width
                    text: "当前没有可用节点快照.切到集群模式,或等待本地 / 远端节点注册表刷新后,这里会显示详细能力."
                    color: listItemStyle.meta
                    font.pixelSize: Design.Foundation.textMd
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
