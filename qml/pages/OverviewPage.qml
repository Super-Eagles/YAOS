import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../components"
import "../theme" as Design
import QtGraphicalEffects 1.14

Item {
    id: overviewPage
    property var app
    property var studioBridge
    property real stackWidth: width
    property var appRoot: app
    property string currentPage: app ? app.currentPage : ""
    property var draftConfig: app ? app.draftConfig : ({})
    readonly property var summarySurface: Design.Theme.surface("summary")
    readonly property var summaryAltSurface: Design.Theme.surface("summary-alt")
    readonly property var listItemSurface: Design.Theme.surface("list-item")
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")

    QtObject {
        id: stack
        property real width: overviewPage.stackWidth
    }

    function controlPlaneEndpointText() {
        return app ? app.controlPlaneEndpointText.apply(app, arguments) : undefined;
    }
    function controlPlaneStatusText() {
        return app ? app.controlPlaneStatusText.apply(app, arguments) : undefined;
    }
    function controlTaskBusRecentEventsText() {
        return app ? app.controlTaskBusRecentEventsText.apply(app, arguments) : undefined;
    }
    function controlTaskBusSummaryText() {
        return app ? app.controlTaskBusSummaryText.apply(app, arguments) : undefined;
    }
    function firstLines() {
        return app ? app.firstLines.apply(app, arguments) : undefined;
    }
    function fourStatColumns() {
        return app ? app.fourStatColumns.apply(app, arguments) : undefined;
    }
    function hasControlTaskBusHealth() {
        return app ? app.hasControlTaskBusHealth.apply(app, arguments) : undefined;
    }
    function memoryServiceEndpointText() {
        return app ? app.memoryServiceEndpointText.apply(app, arguments) : undefined;
    }
    function memoryServiceStatusText() {
        return app ? app.memoryServiceStatusText.apply(app, arguments) : undefined;
    }
    function overviewStatCard() {
        return app ? app.overviewStatCard.apply(app, arguments) : undefined;
    }
    function pageListModel() {
        return app ? app.pageListModel.apply(app, arguments) : undefined;
    }
    function pageWidth() {
        return app ? app.pageWidth.apply(app, arguments) : undefined;
    }
    function runtimeSectionTitle() {
        return app ? app.runtimeSectionTitle.apply(app, arguments) : undefined;
    }
    function providerTitle() {
        return app ? app.providerTitle.apply(app, arguments) : undefined;
    }
    function read() {
        return app ? app.read.apply(app, arguments) : undefined;
    }
    function routingRequestFromTask() {
        return app ? app.routingRequestFromTask.apply(app, arguments) : undefined;
    }
    function routingRequestFromTaskGroup() {
        return app ? app.routingRequestFromTaskGroup.apply(app, arguments) : undefined;
    }
    function routingSeedLabelFromTask() {
        return app ? app.routingSeedLabelFromTask.apply(app, arguments) : undefined;
    }
    function routingSeedLabelFromTaskGroup() {
        return app ? app.routingSeedLabelFromTaskGroup.apply(app, arguments) : undefined;
    }
    function runtimeAdvertiseEndpointText() {
        return app ? app.runtimeAdvertiseEndpointText.apply(app, arguments) : undefined;
    }
    function runtimeEndpointText() {
        return app ? app.runtimeEndpointText.apply(app, arguments) : undefined;
    }
    function runtimeServiceStatusText() {
        return app ? app.runtimeServiceStatusText.apply(app, arguments) : undefined;
    }
    function taskGroupSummary() {
        return app ? app.taskGroupSummary.apply(app, arguments) : undefined;
    }
    function taskTraceSummary() {
        return app ? app.taskTraceSummary.apply(app, arguments) : undefined;
    }
    function taskTreeGroups() {
        return app ? app.taskTreeGroups.apply(app, arguments) : undefined;
    }
    function taskTreePrefix() {
        return app ? app.taskTreePrefix.apply(app, arguments) : undefined;
    }
    function taskTreeStateColor() {
        return app ? app.taskTreeStateColor.apply(app, arguments) : undefined;
    }
    function taskTreeSummary() {
        return app ? app.taskTreeSummary.apply(app, arguments) : undefined;
    }
    function twoColumnCount() {
        return app ? app.twoColumnCount.apply(app, arguments) : undefined;
    }
    function overviewHeroMetrics() {
        return [
            {
                "label": "工作区 Workspace",
                "value": studioBridge.status.workspaceReady ? "就绪 Ready" : "待初始化 Pending",
                "accent": studioBridge.status.workspaceReady ? Design.Theme.status("success").accent : Design.Theme.status("warning").accent
            },
            {
                "label": "任务 Tasks",
                "value": studioBridge.status.taskCount || 0,
                "accent": Design.Theme.section("tasks").accent
            },
            {
                "label": "事件 Events",
                "value": studioBridge.status.eventCount || 0,
                "accent": Design.Theme.section("events").accent
            },
            {
                "label": "审批 Approvals",
                "value": studioBridge.status.pendingApprovalCount || 0,
                "accent": Design.Theme.section("approvals").accent
            }
        ];
    }
    function overviewSignalSummary(cardData) {
        switch (cardData.key || "") {
        case "tasks":
            return "活跃执行与委托链路的即时热度";
        case "events":
            return "审计与遥测流的噪声密度";
        case "approvals":
            return "需要人工接管的决策数量";
        case "resources":
            return "工作区内已建立索引的对象规模";
        default:
            return "当前工作台的关键面板摘要";
        }
    }
    function overviewSignalFootnote(cardData) {
        switch (cardData.key || "") {
        case "tasks":
            return studioBridge.status.workspaceReady ? "适合先看执行动态" : "等待任务开始累积";
        case "events":
            return (studioBridge.status.eventCount || 0) > 0 ? "优先关注最新告警" : "事件流仍然很干净";
        case "approvals":
            return (studioBridge.status.pendingApprovalCount || 0) > 0 ? "建议优先处理阻塞项" : "当前没有人工阻塞";
        case "resources":
            return (studioBridge.resourceSummary.totalCount || 0) > 0 ? "索引规模已建立" : "等待资源目录同步";
        default:
            return "";
        }
    }
    function overviewSignalTag(cardData) {
        switch (cardData.key || "") {
        case "tasks":
            return "执行热度";
        case "events":
            return "遥测密度";
        case "approvals":
            return "人工决策";
        case "resources":
            return "索引规模";
        default:
            return "概览信号";
        }
    }
    function overviewMetricValueText(value) {
        var numericValue = Number(value);
        if (isNaN(numericValue)) {
            return String(value || "");
        }
        return String(Math.trunc(numericValue)).replace(/\B(?=(\d{3})+(?!\d))/g, ",");
    }

    Flickable {
        id: providerPageFlick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: providerPageColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        Column {
            id: providerPageColumn
            width: pageWidth(stack.width)
            x: Math.max(0, (providerPageFlick.width - width) / 2)
            spacing: 10

            PageHero {
                width: parent.width
                compact: true
                sectionKey: "overview"
                overline: "控制总览 Control Overview"
                title: "总览中枢"
                description: "先看工作区是否健康，再决定进入任务、事件还是运行时排查。"
                metrics: overviewPage.overviewHeroMetrics()
            }

            GridLayout {
                width: parent.width
                columns: fourStatColumns(parent.width)
                rowSpacing: 14
                columnSpacing: 14

                Repeater {
                    model: 4
                    delegate: NeoCard {
                        property var cardData: overviewStatCard(index)
                        readonly property string signalSummaryText: overviewPage.overviewSignalSummary(cardData)
                        readonly property string signalFootnoteText: overviewPage.overviewSignalFootnote(cardData)
                        readonly property string signalTagText: overviewPage.overviewSignalTag(cardData)
                        Layout.fillWidth: true
                        Layout.preferredHeight: 215
                        Layout.minimumHeight: 215
                        stretchContent: true
                        sectionKey: cardData.key || ""
                        title: cardData.title || ""
                        subtitle: cardData.detail || ""
                        titleIconSpec: cardData.iconSpec
                        titleIcon: cardData.icon || ""
                        titleBadgeText: ""
                        guideText: ""
                        glowColor: cardData.accent || Design.Theme.section(cardData.key || "overview").accent

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Text {
                                    Layout.fillWidth: true
                                    text: overviewPage.overviewMetricValueText(cardData.value || 0)
                                    color: glowColor
                                    font.pixelSize: 34
                                    font.weight: Font.Thin
                                    font.letterSpacing: 2
                                }

                                Rectangle {
                                    visible: signalTagText.length > 0
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 2
                                    implicitWidth: signalTagLabel.implicitWidth + 18
                                    implicitHeight: signalTagLabel.implicitHeight + 10
                                    radius: implicitHeight / 2
                                    color: Design.Theme.alpha(glowColor, 0.12)
                                    border.width: 1
                                    border.color: Design.Theme.alpha(glowColor, 0.24)

                                    Text {
                                        id: signalTagLabel
                                        anchors.centerIn: parent
                                        text: signalTagText
                                        color: glowColor
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: signalSummaryText
                                color: summaryBoxStyle.text
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                lineHeight: 1.25
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: Design.Theme.alpha(glowColor, 0.14)
                            }

                            Item { Layout.fillHeight: true }

                             Rectangle {
                                 Layout.fillWidth: true
                                 Layout.bottomMargin: 10
                                 implicitHeight: calloutLayout.implicitHeight + 12
                                 radius: 8
                                 color: Design.Theme.alpha(glowColor, 0.03)
                                 border.width: 1
                                 border.color: Design.Theme.alpha(glowColor, 0.12)

                                 // Glowing vertical accent bar
                                 Rectangle {
                                     anchors.left: parent.left
                                     anchors.leftMargin: 8
                                     anchors.verticalCenter: parent.verticalCenter
                                     width: 3
                                     height: parent.height - 12
                                     radius: 1.5
                                     color: glowColor
                                     opacity: 0.8
                                 }

                                 RowLayout {
                                     id: calloutLayout
                                     anchors.left: parent.left
                                     anchors.leftMargin: 18
                                     anchors.right: parent.right
                                     anchors.rightMargin: 10
                                     anchors.verticalCenter: parent.verticalCenter
                                     spacing: 8

                                     Rectangle {
                                         implicitWidth: calloutTag.implicitWidth + 10
                                         implicitHeight: calloutTag.implicitHeight + 4
                                         radius: 4
                                         color: Design.Theme.alpha(glowColor, 0.12)
                                         border.width: 1
                                         border.color: Design.Theme.alpha(glowColor, 0.20)

                                         Text {
                                             id: calloutTag
                                             anchors.centerIn: parent
                                             text: "当前判断"
                                             color: glowColor
                                             font.pixelSize: 9
                                             font.weight: Font.DemiBold
                                         }
                                     }

                                     Text {
                                         Layout.fillWidth: true
                                         text: signalFootnoteText
                                         color: summaryBoxStyle.text
                                         font.pixelSize: 11
                                         wrapMode: Text.WordWrap
                                         opacity: 0.85
                                     }
                                 }
                             }
                        }
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
                    Layout.preferredHeight: 360
                    stretchContent: true
                    sectionKey: "tasks"
                    title: "最近任务 Recent Tasks"
                    subtitle: "执行状态,结果摘要与错误回看"
                    titleIconKey: "tasks"
                    titleIcon: ""
                    guideText: "按时间查看最近任务；结合结果摘要和“预演路径 -> " +
                               (runtimeSectionTitle("cluster") || "Cluster 集群") +
                               "”动作，可以快速追踪一次执行链。"

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        cacheBuffer: 6000
                        spacing: 12
                        model: pageListModel("overview", studioBridge.tasks)
                        footer: Text {
                            width: ListView.view ? ListView.view.width : 0
                            visible: !!ListView.view && ListView.view.count === 0
                            text: "当前还没有任务记录。开始一轮对话或提交委托后,这里会显示最近执行记录。"
                            color: Design.Theme.palette.textMuted
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }

                        delegate: Rectangle {
                            width: ListView.view.width
                            implicitHeight: recentTaskColumn.implicitHeight + 28
                            radius: 8
                            color: overviewPage.listItemSurface.background
                            border.width: 1
                            border.color: overviewPage.listItemSurface.borderSoft

                            Row {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 14

                                Rectangle {
                                    width: 10
                                    height: parent.height
                                    radius: 5
                                    color: modelData.state === "failed" ? Design.Theme.status("error").accent :
                                           ((modelData.state === "completed" || modelData.state === "succeeded") ? Design.Theme.status("success").accent :
                                           (modelData.state === "cancelled" ? Design.Theme.status("warning").accent : Design.Theme.status("info").accent))
                                }

                                Column {
                                    id: recentTaskColumn
                                    width: parent.width - 26
                                    spacing: 6

                                    Text {
                                        text: taskTreePrefix(modelData.depth) + (modelData.title || modelData.kind || modelData.id)
                                        color: overviewPage.listItemSurface.title
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }

                                    Text {
                                        text: taskTreeSummary(modelData)
                                        color: overviewPage.listItemSurface.accent
                                        font.pixelSize: 12
                                    }

                                    Text {
                                        width: parent.width
                                        text: taskTraceSummary(modelData)
                                        color: overviewPage.listItemSurface.meta
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: firstLines(modelData.resultPreview || modelData.summary || modelData.error || "", 160)
                                        color: overviewPage.listItemSurface.body
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 12
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "预演路由 -> " + (runtimeSectionTitle("cluster") || "Cluster 集群")
                                        onClicked: appRoot.openRoutingPreview(
                                                       routingRequestFromTask(modelData),
                                                       routingSeedLabelFromTask(modelData),
                                                       "cluster")
                                    }
                                }
                            }
                        }
                    }
                }

                NeoCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 360
                    stretchContent: true
                    sectionKey: "events"
                    title: "系统事件 System Events"
                    subtitle: "高价值审计与运行时遥测"
                    titleIconKey: "events"
                    titleIcon: ""
                    guideText: "这里汇总运行时事件和审计信息；优先关注最新 error / warn 项，便于快速定位故障。"

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        cacheBuffer: 6000
                        spacing: 10
                        model: pageListModel("overview", studioBridge.events)
                        footer: Text {
                            width: ListView.view ? ListView.view.width : 0
                            visible: !!ListView.view && ListView.view.count === 0
                            text: "当前还没有系统事件。执行对话,工具或服务操作后,这里会开始累积遥测。"
                            color: Design.Theme.palette.textMuted
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }

                        delegate: Rectangle {
                            width: ListView.view.width
                            implicitHeight: systemEventColumn.implicitHeight + 28
                            radius: 8
                            color: overviewPage.listItemSurface.background
                            border.width: 1
                            border.color: overviewPage.listItemSurface.borderSoft

                            Column {
                                id: systemEventColumn
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 6

                                Row {
                                    spacing: 10

                                    Text {
                                        text: (modelData.level || "info").toUpperCase()
                                        color: modelData.level === "error"
                                            ? Design.Theme.status("error").accent
                                            : Design.Theme.status("info").text
                                        font.pixelSize: 11
                                        font.weight: Font.Black
                                    }

                                    Text {
                                        text: modelData.category || "事件"
                                        color: overviewPage.listItemSurface.text
                                        font.pixelSize: 12
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: modelData.message || ""
                                    color: overviewPage.listItemSurface.text
                                    font.pixelSize: 13
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    text: modelData.timestamp || ""
                                    color: overviewPage.listItemSurface.meta
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }

            NeoCard {
                id: taskTreeCard
                width: parent.width
                height: 324
                stretchContent: true
                sectionKey: "tasks"
                title: "任务树聚合 Task Trees"
                subtitle: "按根任务（root task）聚合长链任务、扇出批次和跨节点委托结果"
                titleIconKey: "resources"
                titleIcon: ""
                guideText: "这里按根任务聚合整条执行树；适合排查多级委托、批量分发和跨节点任务的整体状态。"
                property var groupedTasks: appRoot.currentPage === "overview"
                    ? taskTreeGroups(studioBridge.tasks || [])
                    : []

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        anchors.fill: parent
                        clip: true
                        cacheBuffer: 6000
                        spacing: 10
                        model: taskTreeCard.groupedTasks

                        delegate: Rectangle {
                            width: ListView.view.width
                            radius: 8
                            color: overviewPage.listItemSurface.background
                            border.width: 1
                            border.color: overviewPage.listItemSurface.borderSoft
                            implicitHeight: taskTreeGroupColumn.implicitHeight + 24

                            Row {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 14

                                Rectangle {
                                    width: 10
                                    height: parent.height
                                    radius: 5
                                    color: taskTreeStateColor(modelData.state)
                                }

                                Column {
                                    id: taskTreeGroupColumn
                                    width: parent.width - 26
                                    spacing: 6

                                    Text {
                                        width: parent.width
                                        text: modelData.title || modelData.rootId || "任务"
                                        color: overviewPage.listItemSurface.title
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: taskGroupSummary(modelData)
                                        color: overviewPage.listItemSurface.accent
                                        font.pixelSize: 12
                                    }

                                    Text {
                                        visible: (modelData.routeSummary || "").length > 0
                                        width: parent.width
                                        text: modelData.routeSummary
                                        color: overviewPage.listItemSurface.meta
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        visible: (modelData.preview || "").length > 0
                                        width: parent.width
                                        text: firstLines(modelData.preview || "", 220)
                                        color: overviewPage.listItemSurface.body
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 12
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "预演任务组 -> " + (runtimeSectionTitle("cluster") || "Cluster 集群")
                                        onClicked: appRoot.openRoutingPreview(
                                                       routingRequestFromTaskGroup(modelData),
                                                       routingSeedLabelFromTaskGroup(modelData),
                                                       "cluster")
                                    }

                                    ActionButton {
                                        compact: true
                                        text: "带入批量草稿 -> " + (runtimeSectionTitle("delegation") || "Delegation 委托")
                                        onClicked: appRoot.openBatchTaskGroupSeed(modelData, "delegation")
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        visible: taskTreeCard.groupedTasks.length === 0
                        text: "还没有可聚合的任务树。后续的扇出任务和委托任务会在这里按根任务汇总。"
                        color: Design.Theme.palette.textMuted
                        font.pixelSize: 12
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
                    Layout.fillHeight: true
                    //Layout.preferredHeight: 318
                    sectionKey: "runtime"
                    title: "运行快照 Runtime Snapshot"
                    subtitle: "当前路由、模型与网关状态"
                    titleIconKey: "runtime"
                    titleIcon: ""
                    guideText: "这里给出当前桌面端正在采用的模型、路由和运行时状态；需要深入排查时，可继续切到 Runtime 各分页。"

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: runtimeSummaryTop.implicitHeight + 22
                            radius: 16
                            color: overviewPage.summarySurface.background
                            border.width: 1
                            border.color: overviewPage.summarySurface.border

                            ColumnLayout {
                                id: runtimeSummaryTop
                                x: 11

                                y: 11

                                width: parent.width - 22
                                spacing: 6

                                Text { text: "模型与路由"; color: overviewPage.summarySurface.title; font.pixelSize: 11; font.weight: Font.Black }
                                Text { text: "默认模型  " + (studioBridge.status.defaultModel || "未配置"); color: overviewPage.summarySurface.text; font.pixelSize: 14 }
                                Text {
                                    Layout.fillWidth: true
                                    text: "路由厂商  " + providerTitle(studioBridge.status.routedProvider || "auto") +
                                          "  ·  实际后端  " + (studioBridge.status.actualBackend || "未知")
                                    color: overviewPage.summarySurface.muted
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: runtimeSummaryMiddle.implicitHeight + 22
                            radius: 16
                            color: overviewPage.summaryAltSurface.background
                            border.width: 1
                            border.color: overviewPage.summaryAltSurface.border

                            ColumnLayout {
                                id: runtimeSummaryMiddle
                                x: 11

                                y: 11

                                width: parent.width - 22
                                spacing: 6

                                Text { text: "部署与服务"; color: overviewPage.summaryAltSurface.title; font.pixelSize: 11; font.weight: Font.Black }
                                Text {
                                    Layout.fillWidth: true
                                    text: "部署模式  " + ((read("deployment.mode", "standalone") === "cluster") ? "集群模式" : "单机模式") +
                                          "  ·  运行态  " + ((read("runtime.mode", "embedded") === "remote")
                                                             ? "远端运行"
                                                             : ((read("runtime.mode", "embedded") === "daemon") ? "守护进程" : "内嵌运行"))
                                    color: overviewPage.summaryAltSurface.text
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    text: "网关  " + (studioBridge.status.gatewayRunning ? "在线" : "空闲") +
                                          "  ·  记忆后端  " +
                                          ((read("memory.backend", "legacy") || "legacy") === "hybrid_cluster"
                                               ? "集群混合"
                                               : (((read("memory.backend", "legacy") || "legacy") === "hybrid_local")
                                                      ? "本地混合"
                                                      : "传统 Markdown"))
                                    color: overviewPage.summaryAltSurface.muted
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    text: "记忆服务  " + memoryServiceStatusText() + "  |  " + memoryServiceEndpointText()
                                    color: studioBridge.status.memoryServiceReachable ? Design.Theme.status("success").text : overviewPage.summaryAltSurface.meta
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    text: "运行时服务  " + runtimeServiceStatusText() + "  |  " +
                                          runtimeEndpointText() + "  |  广播地址 " + runtimeAdvertiseEndpointText()
                                    color: studioBridge.status.runtimeServiceReachable ? Design.Theme.status("success").text : overviewPage.summaryAltSurface.meta
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    text: "控制平面  " + controlPlaneStatusText() + "  |  " + controlPlaneEndpointText()
                                    color: studioBridge.status.controlPlaneReachable ? Design.Theme.status("success").text : overviewPage.summaryAltSurface.meta
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: runtimeSummaryBottom.implicitHeight + 22
                            radius: 16
                            color: overviewPage.summarySurface.background
                            border.width: 1
                            border.color: Design.Theme.alpha(overviewPage.summarySurface.border, 0.8)

                            ColumnLayout {
                                id: runtimeSummaryBottom
                                x: 11

                                y: 11

                                width: parent.width - 22
                                spacing: 6

                                Text { text: "租约与频道"; color: overviewPage.summarySurface.title; font.pixelSize: 11; font.weight: Font.Black }
                                Text {
                                    visible: hasControlTaskBusHealth()
                                    Layout.fillWidth: true
                                    text: "控制任务总线  " + controlTaskBusSummaryText()
                                    color: overviewPage.summarySurface.muted
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    visible: hasControlTaskBusHealth()
                                    Layout.fillWidth: true
                                    text: "最近租约活动  " + controlTaskBusRecentEventsText(3)
                                    color: Design.Theme.palette.textMuted
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "已启用频道  " + ((studioBridge.status.enabledChannels || []).join(", ") || "无")
                                    color: overviewPage.summarySurface.meta
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        RuntimeSectionQuickLinks {
                            Layout.fillWidth: true
                            app: appRoot
                            sections: ["deployment", "memory", "cluster", "delegation"]
                            summarySegments: [
                                { "key": "deployment", "description": "负责网关,部署与控制平面." },
                                { "key": "cluster", "description": "负责节点与路由。" },
                                { "key": "delegation", "description": "负责模板与草稿链路。" }
                            ]
                            summaryColor: overviewPage.summaryAltSurface.meta
                        }
                    }
                }

                NeoCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    sectionKey: "resources"
                    title: "资源索引 Resource Index"
                    subtitle: "工作区对象目录统计"
                    titleIconKey: "search"
                    titleIcon: ""
                    guideText: "这里展示资源索引的主要分类数量；如果数量异常，可继续到资源页检查索引是否缺失。"

                    GridLayout {
                        Layout.fillWidth: true
                    width: parent.width
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 12

                        Repeater {
                            model: [
                                { "label": "会话", "value": studioBridge.resourceSummary.sessionCount || 0, "accent": Design.Theme.section("conversation").accent },
                                { "label": "任务", "value": studioBridge.resourceSummary.taskCount || 0, "accent": Design.Theme.section("tasks").accent },
                                { "label": "自动化", "value": studioBridge.resourceSummary.automationCount || 0, "accent": Design.Theme.section("automation").accent },
                                { "label": "插件", "value": studioBridge.resourceSummary.pluginCount || 0, "accent": Design.Theme.section("plugins").accent },
                                { "label": "技能", "value": studioBridge.resourceSummary.skillCount || 0, "accent": Design.Theme.section("skills").accent }
                            ]

                            delegate: Rectangle {
                                property color accentColor: modelData.accent
                                property var chipStyle: Design.Theme.metricChip(accentColor)
                                Layout.fillWidth: true
                                Layout.preferredHeight: 78
                                radius: 18
                                color: chipStyle.background
                                border.width: 1
                                border.color: chipStyle.border

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 6

                                    Text {
                                        text: modelData.label
                                        color: chipStyle.label
                                        font.pixelSize: 12
                                    }

                                    Text {
                                        text: modelData.value
                                        color: chipStyle.value
                                        font.pixelSize: 22
                                        font.weight: Font.Black
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 32 }
        }
    }
}

