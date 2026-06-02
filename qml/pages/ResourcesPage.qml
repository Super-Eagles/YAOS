import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../components"
import "../theme" as Design
import QtGraphicalEffects 1.14

Item {
    id: resourcesPage
    property var app
    property var studioBridge
    property real stackWidth: width
    property var appRoot: app
    property string currentPage: app ? app.currentPage : ""
    property var draftConfig: app ? app.draftConfig : ({})
    readonly property var listItemSurface: Design.Theme.surface("list-item")
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")

    QtObject {
        id: stack
        property real width: resourcesPage.stackWidth
    }

    function pageListModel() {
        return app ? app.pageListModel.apply(app, arguments) : undefined;
    }
    function pageWidth() {
        return app ? app.pageWidth.apply(app, arguments) : undefined;
    }
    function twoColumnCount() {
        return app ? app.twoColumnCount.apply(app, arguments) : undefined;
    }

    function resourceSummaryColumns(containerWidth) {
        return containerWidth >= 520 ? 2 : 1;
    }

    ScrollView {
        anchors.fill: parent
        clip: true

        Column {
            width: pageWidth(stack.width)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10

            PageHero {
                width: parent.width
                compact: true
                sectionKey: "resources"
                overline: "已索引资源 Indexed Assets"
                title: "资源索引大厅"
                description: "集中查看会话,文档,任务和扩展的索引密度,确认工作区是否在持续地产出和沉淀."
                metrics: [
                    {
                        "label": "会话 Sessions",
                        "value": studioBridge.resourceSummary.sessionCount || 0,
                        "accent": Design.Theme.section("channels").accent
                    },
                    {
                        "label": "文档 Docs",
                        "value": studioBridge.resourceSummary.documentCount || 0,
                        "accent": Design.Theme.section("resources").accent
                    },
                    {
                        "label": "任务 Tasks",
                        "value": studioBridge.resourceSummary.taskCount || 0,
                        "accent": Design.Theme.section("tasks").accent
                    },
                    {
                        "label": "插件 Plugins",
                        "value": studioBridge.resourceSummary.pluginCount || 0,
                        "accent": Design.Theme.section("plugins").accent
                    }
                ]
            }

            GridLayout {
                width: parent.width
                columns: twoColumnCount(parent.width)
                rowSpacing: 14
                columnSpacing: 14

                NeoCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: resourceSummaryMetrics.columns > 1 ? 220 : 288
                    sectionKey: "resources"
                    title: "资源总览 Resources"
                    subtitle: "会话、文档、任务与扩展的统一目录"
                    titleIconKey: "resources"
                    titleIcon: "⌬"
                    guideText: "这里先看资源池规模；下面的最近资源和插件目录区块用于继续追踪具体对象。"

                    GridLayout {
                        id: resourceSummaryMetrics
                        width: parent.width
                        columns: resourceSummaryColumns(width)
                        columnSpacing: 14
                        rowSpacing: 12

                        Repeater {
                            model: [
                                { "label": "会话 / Sessions", "value": (studioBridge.resourceSummary.sessionCount || 0), "accent": Design.Theme.section("conversation").accent },
                                { "label": "文档 / Documents", "value": (studioBridge.resourceSummary.documentCount || 0), "accent": Design.Theme.section("resources").accent },
                                { "label": "任务 / Tasks", "value": (studioBridge.resourceSummary.taskCount || 0), "accent": Design.Theme.section("tasks").accent },
                                { "label": "事件 / Events", "value": (studioBridge.resourceSummary.eventCount || 0), "accent": Design.Theme.section("events").accent }
                            ]
                            delegate: Rectangle {
                                property color accentColor: modelData.accent
                                property var chipStyle: Design.Theme.metricChip(accentColor)
                                Layout.fillWidth: true
                                height: 36
                                radius: 16
                                color: chipStyle.background
                                border.width: 1
                                border.color: chipStyle.border

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 12

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.label
                                        color: chipStyle.label
                                        font.pixelSize: Design.Foundation.textLg
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.alignment: Qt.AlignVCenter
                                        text: modelData.value
                                        color: chipStyle.border
                                        font.pixelSize: 26
                                        font.letterSpacing: 1
                                        font.weight: Font.Black
                                    }
                                }
                            }
                        }
                    }
                }

                NeoCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 220
                    sectionKey: "extensions"
                    title: "扩展矩阵 Extensions"
                    subtitle: "已发现的插件清单"
                    titleIconKey: "plugins"
                    titleIcon: "⛭"
                    guideText: "这里快速看扩展和自动化规模;若数量异常,可继续到扩展页或资源页定位明细."

                    Text {
                        width: parent.width
                        text: "插件在线  " + (studioBridge.status.pluginCount || 0) + "\n自动化  " + (studioBridge.status.automationCount || 0)
                        color: summaryBoxStyle.title
                        font.pixelSize: Design.Foundation.textXxl
                        wrapMode: Text.WordWrap

                    }
                }
            }

            GridLayout {
                width: parent.width
                columns: twoColumnCount(parent.width)
                rowSpacing: 14
                columnSpacing: 14

                NeoCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 384
                    stretchContent: true
                    sectionKey: "resources"
                    title: "最近资源 Recent Resources"
                    subtitle: "工作区最新索引对象"
                    titleIconKey: "conversation"
                    titleIcon: "◍"
                    guideText: "按时间查看最近被索引或刷新过的对象;用于快速确认工作区是否正在持续产出内容."

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 10
                        model: pageListModel("resources", studioBridge.resources)
                        footer: Text {
                            width: ListView.view ? ListView.view.width : 0
                            visible: !!ListView.view && ListView.view.count === 0
                            text: "当前还没有资源索引记录.初始化工作区并执行任务后,这里会显示最近对象."
                            color: Design.Theme.palette.textMuted
                            font.pixelSize: Design.Foundation.textMd
                            wrapMode: Text.WordWrap
                        }

                        delegate: Rectangle {
                            width: ListView.view.width
                            implicitHeight: recentResourceColumn.implicitHeight + 24
                            radius: 16
                            color: resourcesPage.listItemSurface.background
                            border.width: 1
                            border.color: resourcesPage.listItemSurface.border

                            Column {
                                id: recentResourceColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6
                                Text { text: (modelData.kind || "资源") + " | " + (modelData.title || modelData.id); color: resourcesPage.listItemSurface.text; font.pixelSize: Design.Foundation.textXl; font.weight: Font.DemiBold }
                                Text { text: modelData.summary || modelData.location || ""; color: resourcesPage.listItemSurface.body; font.pixelSize: Design.Foundation.textMd; width: parent.width; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }

                NeoCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 384
                    stretchContent: true
                    sectionKey: "plugins"
                    title: "插件目录 Plugin Catalog"
                    subtitle: "基于清单文件的扩展发现 / Manifest Discovery"
                    titleIconKey: "plugins"
                    titleIcon: "⌘"
                    guideText: "这里列出当前已发现的插件清单;当你放入新的 plugin.json 后,可以先在这里确认是否被识别."

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 10
                        model: pageListModel("resources", studioBridge.plugins)
                        footer: Text {
                            width: ListView.view ? ListView.view.width : 0
                            visible: !!ListView.view && ListView.view.count === 0
                            text: "当前工作区还没有发现插件;安装扩展或放入 plugin.json 后,这里会出现."
                            color: Design.Theme.palette.textMuted
                            font.pixelSize: Design.Foundation.textMd
                            wrapMode: Text.WordWrap
                        }

                        delegate: Rectangle {
                            width: ListView.view.width
                            implicitHeight: pluginCatalogColumn.implicitHeight + 24
                            radius: 16
                            color: resourcesPage.listItemSurface.background
                            border.width: 1
                            border.color: resourcesPage.listItemSurface.border

                            Column {
                                id: pluginCatalogColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6
                                Text { text: (modelData.name || modelData.id) + " | " + (modelData.version || "0.0.0"); color: resourcesPage.listItemSurface.text; font.pixelSize: Design.Foundation.textXl; font.weight: Font.DemiBold }
                                Text { text: modelData.description || modelData.rootPath || ""; color: resourcesPage.listItemSurface.body; font.pixelSize: Design.Foundation.textMd; width: parent.width; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 32 }
        }
    }
}
