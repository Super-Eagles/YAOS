import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../theme" as Design

NeoCard {
    id: root
    objectName: "runtimeCard_routingDiagnostics"
    property var app
    property var studioBridge
    property var templateSummaryBridge
    property var draftSeedBridge
    property var nodeSelectionBridge
    property string currentPage: app ? app.currentPage : ""
    property string requestedRole: ""
    property string requestedTagsText: ""
    property string requiredTool: ""
    property string requiredChannel: ""
    property string requiredMemoryBackend: ""
    property bool includeOffline: false
    property string previewSourceLabel: ""
    property string selectedCandidateNodeId: ""
    property string templateMode: "spawn"
    property bool previewInitialized: false
    property bool previewDirty: false
    property var previewRequest: ({})
    property var previewResult: (studioBridge && studioBridge.delegationRoutePreview) ? studioBridge.delegationRoutePreview : ({})
    property var diagnosticResults: previewResult.nodes || []
    property var templateModes: [
        {"key": "spawn", "title": "Spawn 负载"},
        {"key": "delegation", "title": "运行时 JSON"},
        {"key": "cli", "title": "CLI 命令"}
    ]
    property var selectedCandidate: selectPreviewCandidate(diagnosticResults,
                                                           selectedCandidateNodeId,
                                                           previewResult.suggestedNodeId || "")
    property string delegationTemplateText: previewTemplateJson(previewResult,
                                                                 previewRequest,
                                                                 selectedCandidate)
    property string spawnTemplateText: spawnTemplateJson(previewResult,
                                                         previewRequest,
                                                         selectedCandidate)
    property string routePreviewCommandText: routePreviewCommand(previewResult,
                                                                 previewRequest)
    property string selectedTemplateText: templateMode === "delegation"
        ? delegationTemplateText
        : (templateMode === "cli" ? routePreviewCommandText : spawnTemplateText)
    property string selectedTemplateSummary: templateMode === "delegation"
        ? ("固定节点  " +
           (((selectedCandidate || {}).node || {}).nodeId || "无") + "  |  保留本次预演里的会话,追踪和回复上下文")
        : (templateMode === "cli"
            ? "会在 CLI 里回放同一组路由过滤条件;CLI 预演不会固定所选节点."
            : ("固定节点  " +
               (((selectedCandidate || {}).node || {}).nodeId || "无") +
               "  |  可直接复用为 spawn 工具参数"))
    readonly property real candidateListHeight: Number(routingCandidateListView.height || routingCandidateListView.implicitHeight || 0)
    readonly property real templateViewHeight: Number(routingTemplateView.height || routingTemplateView.implicitHeight || 0)
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")
    readonly property var detailSummaryBoxStyle: Design.Theme.summaryBox("alt")
    readonly property var warningSummaryBoxStyle: Design.Theme.summaryBox("warning")
    readonly property var listItemStyle: Design.Theme.listItem("default")
    readonly property var accentListItemStyle: Design.Theme.listItem("accent")
    readonly property var strongSelectedListItemStyle: Design.Theme.listItem("selected-strong")
    readonly property var dangerListItemStyle: Design.Theme.listItem("danger")

    stretchContent: true
    sectionKey: "routing"
    title: "路由诊断 Routing Diagnostics"
    subtitle: "按真实运行时委托规则预演候选节点,标签和上下文引用"
    titleIconKey: "routing"
    titleIcon: "⌕"
    guideText: "这里先做路由预演,确认节点排序,标签和上下文引用都合理后,再带入草稿或复制模板."

    function contextRefSummary() {
        return app ? app.contextRefSummary.apply(app, arguments) : "";
    }
    function delegationTemplateKindTitle() {
        return app ? app.delegationTemplateKindTitle.apply(app, arguments) : "单任务草稿";
    }
    function denseFormColumns(containerWidth) {
        if (containerWidth >= 1220) {
            return 3;
        }
        return containerWidth >= 820 ? 2 : 1;
    }
    function joinValues() {
        return app ? app.joinValues.apply(app, arguments) : "";
    }
    function nodeCapabilityText() {
        return app ? app.nodeCapabilityText.apply(app, arguments) : "";
    }
    function nodeIdentityText() {
        return app ? app.nodeIdentityText.apply(app, arguments) : "";
    }
    function pageListModel() {
        return app ? app.pageListModel.apply(app, arguments) : [];
    }
    function previewTemplateJson() {
        return app ? app.previewTemplateJson.apply(app, arguments) : "";
    }
    function routePreviewCommand() {
        return app ? app.routePreviewCommand.apply(app, arguments) : "";
    }
    function selectPreviewCandidate() {
        return app ? app.selectPreviewCandidate.apply(app, arguments) : null;
    }
    function showToast() {
        return app ? app.showToast.apply(app, arguments) : undefined;
    }
    function syncSelectedNode(nodeId) {
        if (nodeSelectionBridge && nodeSelectionBridge.selectNodeById) {
            nodeSelectionBridge.selectNodeById(nodeId || "");
        }
    }
    function spawnTemplateJson() {
        return app ? app.spawnTemplateJson.apply(app, arguments) : "";
    }
    function splitCsv() {
        return app ? app.splitCsv.apply(app, arguments) : [];
    }
    function templateModeIndex(value) {
        for (var i = 0; i < templateModes.length; ++i) {
            if (templateModes[i].key === value) {
                return i;
            }
        }
        return 0;
    }

    Timer {
        id: routingPreviewTimer
        interval: 220
        repeat: false
        onTriggered: {
            if (!root.visible || currentPage !== "runtime") {
                return;
            }
            if (studioBridge) {
                studioBridge.requestDelegationRoutePreview(root.previewRequest);
            }
        }
    }

    function runPreview() {
        if (!visible || currentPage !== "runtime") {
            return;
        }
        previewRequest = {
            "targetRole": requestedRole,
            "targetTags": splitCsv(requestedTagsText),
            "requiredTool": requiredTool,
            "requiredChannel": requiredChannel,
            "requiredMemoryBackend": requiredMemoryBackend,
            "includeOffline": includeOffline,
            "originChannel": "gui",
            "originChatId": "desktop",
            "sessionKey": "gui:preview",
            "label": "路由预演",
            "task": "预演委托任务"
        };
        previewInitialized = true;
        previewDirty = false;
        routingPreviewTimer.restart();
    }

    function refreshCurrentPreview() {
        if (!visible || currentPage !== "runtime") {
            return;
        }
        if (!previewInitialized) {
            ensurePreview();
            return;
        }
        previewDirty = false;
        routingPreviewTimer.restart();
    }

    function ensurePreview() {
        if (!visible || currentPage !== "runtime") {
            return;
        }
        runPreview();
    }

    function applyPreviewRequest(request, sourceLabel) {
        var next = request || ({});
        requestedRole = next.targetRole || "";
        requestedTagsText = (next.targetTags || []).join(",");
        requiredTool = next.requiredTool || "";
        requiredChannel = next.requiredChannel || "";
        requiredMemoryBackend = next.requiredMemoryBackend || "";
        includeOffline = !!next.includeOffline;
        previewSourceLabel = sourceLabel || "";
        if (app) {
            app.currentPage = "runtime";
        }
        previewRequest = next;
        previewInitialized = true;
        previewDirty = false;
        routingPreviewTimer.restart();
    }

    onPreviewResultChanged: {
        var selected = selectPreviewCandidate(diagnosticResults,
                                              selectedCandidateNodeId,
                                              previewResult.suggestedNodeId || "");
        if (selected) {
            selectedCandidateNodeId = selected.nodeId || "";
        } else {
            selectedCandidateNodeId = "";
        }
    }

    Component.onCompleted: {
        if (currentPage === "runtime") {
            ensurePreview();
        }
    }

    onVisibleChanged: {
        if (visible && !previewInitialized) {
            ensurePreview();
        }
    }

    Connections {
        target: studioBridge ? studioBridge : null
        onNodesChanged: {
            if (currentPage === "runtime" && root.previewInitialized) {
                root.previewDirty = true;
            }
        }
        onConfigChanged: {
            if (currentPage === "runtime" && root.previewInitialized) {
                root.previewDirty = true;
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: delegationTemplateSummaryColumn.implicitHeight + 22
            radius: 8
            color: summaryBoxStyle.background
            border.width: 1
            border.color: summaryBoxStyle.border

            ColumnLayout {
                id: delegationTemplateSummaryColumn
                x: 11
                y: 11
                width: parent.width - 22
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: "当前模板库  " + String(templateSummaryBridge ? (templateSummaryBridge.templateCount || 0) : 0) + " 条"
                    color: summaryBoxStyle.title
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: (templateSummaryBridge && templateSummaryBridge.hasSelectedTemplate)
                        ? ("已选中  " + (templateSummaryBridge.selectedTemplateName || templateSummaryBridge.selectedTemplateId || "模板") +
                           "  ·  类型  " + delegationTemplateKindTitle(templateSummaryBridge.selectedTemplateKind || "single"))
                        : "先从下方列表选中模板,或直接从单任务 / 批量草稿保存出第一条模板."
                    color: (templateSummaryBridge && templateSummaryBridge.hasSelectedTemplate)
                        ? Design.Theme.section("template").accent
                        : summaryBoxStyle.text
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }

        ResponsiveGridStrip {
            Layout.fillWidth: true
            forcedColumns: denseFormColumns(width)
            itemCount: 3
            minimumCellWidth: 220
            maximumColumns: 3
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: root.requestedRole
                placeholderText: "目标角色"
                onEditingFinished: {
                    root.requestedRole = text;
                    root.runPreview();
                }
            }

            GlassField {
                Layout.fillWidth: true
                text: root.requestedTagsText
                placeholderText: "目标标签"
                onEditingFinished: {
                    root.requestedTagsText = text;
                    root.runPreview();
                }
            }

            GlassField {
                Layout.fillWidth: true
                text: root.requiredTool
                placeholderText: "所需工具"
                onEditingFinished: {
                    root.requiredTool = text;
                    root.runPreview();
                }
            }
        }

        ResponsiveGridStrip {
            Layout.fillWidth: true
            forcedColumns: denseFormColumns(width)
            itemCount: 5
            minimumCellWidth: 220
            maximumColumns: 3
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: root.requiredChannel
                placeholderText: "所需频道"
                onEditingFinished: {
                    root.requiredChannel = text;
                    root.runPreview();
                }
            }

            GlassField {
                Layout.fillWidth: true
                text: root.requiredMemoryBackend
                placeholderText: "所需记忆后端"
                onEditingFinished: {
                    root.requiredMemoryBackend = text;
                    root.runPreview();
                }
            }

            NeoCheckBox {
                Layout.fillWidth: denseFormColumns(parent.width) === 1
                Layout.alignment: Qt.AlignVCenter
                text: "包含离线节点"
                checked: root.includeOffline
                onToggled: {
                    root.includeOffline = checked;
                    root.runPreview();
                }
            }

            ActionButton {
                Layout.fillWidth: denseFormColumns(parent.width) === 1
                compact: true
                text: root.previewResult.pending ? "预演中" : "刷新预演"
                enabled: !root.previewResult.pending
                onClicked: root.refreshCurrentPreview()
            }

            ActionButton {
                Layout.fillWidth: denseFormColumns(parent.width) === 1
                compact: true
                text: "清空来源"
                visible: root.previewSourceLabel.length > 0
                onClicked: root.previewSourceLabel = ""
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.previewSourceLabel.length > 0
            text: "来源  " + root.previewSourceLabel
            color: listItemStyle.meta
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        ResponsiveGridStrip {
            id: routingMetricStrip
            Layout.fillWidth: true
            minimumCellWidth: 180
            maximumColumns: 3
            columnSpacing: 10
            rowSpacing: 10

            Rectangle {
                id: routingMetricPrimary
                property var chipStyle: Design.Theme.metricChip(Design.Theme.section("routing").accent)
                Layout.fillWidth: true
                radius: 13
                implicitHeight: routingMetricOne.implicitHeight + 10
                color: chipStyle.background
                border.width: 1
                border.color: chipStyle.border

                Text {
                    id: routingMetricOne
                    anchors.centerIn: parent
                    text: "候选  " + String((root.diagnosticResults || []).length)
                    color: routingMetricPrimary.chipStyle.value
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                visible: !!root.selectedCandidate
                property var chipStyle: Design.Theme.metricChip(Design.Theme.status("success").accent)
                Layout.fillWidth: true
                radius: 13
                implicitHeight: routingMetricTwo.implicitHeight + 10
                color: chipStyle.background
                border.width: 1
                border.color: chipStyle.border

                Text {
                    id: routingMetricTwo
                    anchors.centerIn: parent
                    text: "建议节点  " + ((((root.selectedCandidate || {}).node || {}).displayName || (root.selectedCandidate || {}).nodeId || "未选择"))
                    color: Design.Theme.status("success").text
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                visible: root.previewDirty && !root.previewResult.pending
                property var chipStyle: Design.Theme.metricChip(Design.Theme.status("warning").accent)
                Layout.fillWidth: true
                radius: 13
                implicitHeight: routingMetricThree.implicitHeight + 10
                color: warningSummaryBoxStyle.background
                border.width: 1
                border.color: chipStyle.border

                Text {
                    id: routingMetricThree
                    anchors.centerIn: parent
                    text: "快照已变化"
                    color: Design.Theme.status("warning").text
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.previewResult.pending
                ? "正在计算委托预演..."
                : ((root.previewResult.message || "").length > 0
                    ? (root.previewResult.message +
                       ((root.previewResult.resolutionSource || "").length > 0
                            ? ("  |  来源 " + root.previewResult.resolutionSource)
                            : ""))
                    : (root.previewInitialized
                        ? "当前结果可用于生成委托草稿;如节点快照已更新,请点击“刷新预演”."
                        : "点击“刷新预演”开始计算委托预演."))
            color: root.previewResult.pending
                ? Design.Theme.status("info").text
                : (root.previewResult.resolved ? Design.Theme.status("success").text : Design.Theme.status("warning").text)
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: root.previewDirty && !root.previewResult.pending
            text: "运行时节点或配置快照已变化,当前预演结果可能过期.点击“刷新预演”获取最新结果."
            color: Design.Theme.status("warning").text
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: (root.previewResult.routeSummary || "").length > 0
            text: "路由摘要  " + (root.previewResult.routeSummary || "")
            color: Design.Theme.section("routing").accent
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: (root.previewResult.labels || []).length > 0
            text: "标签  " + joinValues(root.previewResult.labels || [])
            color: listItemStyle.meta
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: (root.previewResult.contextRefs || []).length > 0
            text: "上下文引用  " + contextRefSummary(root.previewResult.contextRefs || [])
            color: listItemStyle.meta
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            visible: !!root.selectedCandidate
            radius: 8
            color: detailSummaryBoxStyle.background
            border.width: 1
            border.color: detailSummaryBoxStyle.border
            implicitHeight: templateColumn.implicitHeight + 22
            clip: true

            ColumnLayout {
                id: templateColumn
                x: 11
                y: 11
                width: parent.width - 22
                spacing: 8

                GridLayout {
                    Layout.fillWidth: true
                    columns: denseFormColumns(parent.width)
                    columnSpacing: 10
                    rowSpacing: 10

                    Text {
                        Layout.fillWidth: true
                        text: root.templateMode === "cli"
                            ? "CLI 回放命令"
                            : ((root.templateMode === "delegation"
                                ? "委托模板  "
                                : "Spawn 负载  ") +
                               ((((root.selectedCandidate || {}).node || {}).displayName ||
                                 (root.selectedCandidate || {}).nodeId || "node")))
                        color: detailSummaryBoxStyle.title
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    NeoComboBox {
                        Layout.fillWidth: true
                        model: root.templateModes
                        textRole: "title"
                        currentIndex: root.templateModeIndex(root.templateMode)
                        onActivated: root.templateMode = root.templateModes[currentIndex].key
                    }

                    ActionButton {
                        Layout.fillWidth: denseFormColumns(parent.width) === 1
                        compact: true
                        text: "带入草稿"
                        onClicked: {
                            if (draftSeedBridge &&
                                    draftSeedBridge.seedSingleDraftFromPreview &&
                                    draftSeedBridge.seedSingleDraftFromPreview()) {
                                showToast("已带入委托草稿",
                                          "当前候选节点和任务路由条件已带入委托草稿.",
                                          "success");
                            }
                        }
                    }

                    ActionButton {
                        Layout.fillWidth: denseFormColumns(parent.width) === 1
                        compact: true
                        text: "带入批量草稿"
                        onClicked: {
                            if (draftSeedBridge &&
                                    draftSeedBridge.seedBatchDraftFromPreview &&
                                    draftSeedBridge.seedBatchDraftFromPreview()) {
                                showToast("已带入批量草稿",
                                          "当前候选节点和共享路由条件已带入批量草稿.",
                                          "success");
                            }
                        }
                    }

                    ActionButton {
                        Layout.fillWidth: denseFormColumns(parent.width) === 1
                        compact: true
                        text: "复制当前"
                        enabled: !!studioBridge
                        onClicked: studioBridge.copyToClipboard(root.selectedTemplateText)
                    }

                    ActionButton {
                        Layout.fillWidth: denseFormColumns(parent.width) === 1
                        compact: true
                        text: "复制 CLI"
                        enabled: !!studioBridge
                        onClicked: studioBridge.copyToClipboard(root.routePreviewCommandText)
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.selectedTemplateSummary
                    color: Design.Theme.section("routing").accent
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                ReadOnlyTextView {
                    id: routingTemplateView
                    Layout.fillWidth: true
                    Layout.preferredHeight: 132
                    wrapMode: TextEdit.WrapAnywhere
                    textFormat: TextEdit.PlainText
                    textColor: listItemStyle.text
                    text: root.selectedTemplateText
                    clip: true
                }
            }
        }

        ListView {
            id: routingCandidateListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: pageListModel("runtime", root.diagnosticResults)

            delegate: Rectangle {
                property bool isSelected: (root.selectedCandidateNodeId || "") === (modelData.nodeId || "")
                property bool isMatched: !!modelData.matched
                property var itemStyle: isSelected ? strongSelectedListItemStyle : (isMatched ? accentListItemStyle : listItemStyle)
                width: ListView.view.width
                implicitHeight: routingCandidateColumn.implicitHeight + 24
                radius: 8
                color: itemStyle.background
                border.width: 1
                border.color: itemStyle.border

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        root.selectedCandidateNodeId = modelData.nodeId || "";
                        root.syncSelectedNode(modelData.nodeId || "");
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 10
                        Layout.fillHeight: true
                        radius: 5
                        color: modelData.matched
                            ? (modelData.rank === 1 ? Design.Theme.section("routing").accent : Design.Theme.status("success").accent)
                            : listItemStyle.meta
                    }

                    Column {
                        id: routingCandidateColumn
                        Layout.fillWidth: true
                        spacing: 5

                        Text {
                            width: parent.width
                            text: "#" + String(modelData.rank || 0) + "  " +
                                  ((modelData.node || {}).displayName || (modelData.node || {}).nodeId || "节点") +
                                  (modelData.isLocal ? "  [本地]" : "") +
                                  (modelData.matched ? "" : "  [已过滤]")
                            color: itemStyle.title
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: modelData.matched
                                ? ("匹配  |  压力 " + Number(modelData.pressure || 0).toFixed(2) +
                                   "  |  排队 " + String(Number(modelData.queuedTaskCount || 0)) +
                                   "  |  权重 " + String(Number(modelData.weight || 0)))
                                : modelData.reasonText
                            color: modelData.matched ? Design.Theme.section("routing").accent : dangerListItemStyle.text
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            width: parent.width
                            text: nodeIdentityText(modelData.node || {})
                            color: itemStyle.body
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: nodeCapabilityText(modelData.node || {})
                            color: itemStyle.meta
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
