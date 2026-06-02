import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../components"
import "../theme" as Design
import QtGraphicalEffects 1.14

Item {
    id: automationPage
    property var app
    property var studioBridge
    property real stackWidth: width
    property var appRoot: app
    property string currentPage: app ? app.currentPage : ""
    property var draftConfig: app ? app.draftConfig : ({})
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")
    readonly property var detailSummaryBoxStyle: Design.Theme.summaryBox("alt")
    readonly property var warningSummaryBoxStyle: Design.Theme.summaryBox("warning")
    readonly property var listItemStyle: Design.Theme.listItem("default")
    readonly property var selectedListItemStyle: Design.Theme.listItem("selected")

    QtObject {
        id: stack
        property real width: automationPage.stackWidth
    }

    function automationModelChoices() {
        return app ? app.automationModelChoices.apply(app, arguments) : undefined;
    }
    function automationProviderOptions() {
        return app ? app.automationProviderOptions.apply(app, arguments) : undefined;
    }
    function automationScheduleSummary() {
        return app ? app.automationScheduleSummary.apply(app, arguments) : undefined;
    }
    function automationStatusLabel() {
        return app ? app.automationStatusLabel.apply(app, arguments) : undefined;
    }
    function automationWorkbenchColumns() {
        return app ? app.automationWorkbenchColumns.apply(app, arguments) : undefined;
    }
    function canonicalProviderKey() {
        return app ? app.canonicalProviderKey.apply(app, arguments) : undefined;
    }
    function chooseProviderModel() {
        return app ? app.chooseProviderModel.apply(app, arguments) : undefined;
    }
    function firstLines() {
        return app ? app.firstLines.apply(app, arguments) : undefined;
    }
    function formatIsoDateTime() {
        return app ? app.formatIsoDateTime.apply(app, arguments) : undefined;
    }
    function joinCsv() {
        return app ? app.joinCsv.apply(app, arguments) : undefined;
    }
    function modelIndex() {
        return app ? app.modelIndex.apply(app, arguments) : undefined;
    }
    function pageListModel() {
        return app ? app.pageListModel.apply(app, arguments) : undefined;
    }
    function providerTitle() {
        return app ? app.providerTitle.apply(app, arguments) : undefined;
    }
    function providerValue() {
        return app ? app.providerValue.apply(app, arguments) : undefined;
    }
    function read() {
        return app ? app.read.apply(app, arguments) : undefined;
    }
    function showToast() {
        return app ? app.showToast.apply(app, arguments) : undefined;
    }
    function splitCsv() {
        return app ? app.splitCsv.apply(app, arguments) : undefined;
    }
    function syncProviderCatalog() {
        return app ? app.syncProviderCatalog.apply(app, arguments) : undefined;
    }

    visible: appRoot.currentPage === "automation"
    enabled: visible
    property string selectedAutomationId: ""
    property bool initialized: false
    property string draftName: ""
    property string draftProvider: "auto"
    property string draftModel: ""
    property string draftTags: ""
    property string draftPrompt: ""
    property bool draftEnabled: true
    property string draftScheduleKind: "manual"
    property string draftScheduleValue: ""
    property string draftTimeZone: ""
    property var providerChoices: []
    property var modelChoices: []
    property var runHistory: []

    function ensureInitialized() {
        if (initialized) {
            return;
        }
        initialized = true;
        resetDraft();
    }
    property var triggerChoices: [
        { "key": "manual", "title": "手动" },
        { "key": "once", "title": "单次" },
        { "key": "every", "title": "间隔" },
        { "key": "cron", "title": "Cron" }
    ]

    function currentAutomation() {
        if (currentPage !== "automation") {
            return {};
        }
        var list = studioBridge.automations || [];
        for (var i = 0; i < list.length; ++i) {
            if ((list[i].id || "") === selectedAutomationId) {
                return list[i];
            }
        }
        return {};
    }

    function currentProviderIndex() {
        for (var i = 0; i < providerChoices.length; ++i) {
            if ((providerChoices[i].key || "") === canonicalProviderKey(draftProvider || "auto")) {
                return i;
            }
        }
        return providerChoices.length > 0 ? 0 : -1;
    }

    function currentTriggerIndex() {
        for (var i = 0; i < triggerChoices.length; ++i) {
            if ((triggerChoices[i].key || "") === draftScheduleKind) {
                return i;
            }
        }
        return 0;
    }

    function rebuildProviderChoices() {
        if (!initialized || currentPage !== "automation") {
            providerChoices = [];
            modelChoices = [];
            return;
        }
        providerChoices = automationProviderOptions();
        var nextProvider = canonicalProviderKey(draftProvider || "auto");
        var found = false;
        for (var i = 0; i < providerChoices.length; ++i) {
            if (providerChoices[i].key === nextProvider) {
                found = true;
                break;
            }
        }
        if (!found) {
            nextProvider = "auto";
        }
        draftProvider = nextProvider;
        rebuildModelChoices();
    }

    function rebuildModelChoices() {
        if (!initialized || currentPage !== "automation") {
            modelChoices = [];
            return;
        }
        modelChoices = automationModelChoices(draftProvider);
        if (draftModel && modelIndex(modelChoices, draftModel) >= 0) {
            return;
        }
        if (modelChoices.length > 0) {
            if (canonicalProviderKey(draftProvider) === "auto") {
                draftModel = read("agents.defaults.model", "");
                if (modelIndex(modelChoices, draftModel) < 0) {
                    draftModel = modelChoices[0];
                }
            } else {
                draftModel = chooseProviderModel(draftProvider, providerValue(draftProvider, "model", ""));
            }
            return;
        }
        draftModel = "";
    }

    function rebuildRunHistory() {
        if (!initialized || currentPage !== "automation") {
            runHistory = [];
            return;
        }
        var allRuns = studioBridge.automationRuns || [];
        var nextRuns = [];
        for (var i = 0; i < allRuns.length; ++i) {
            if (selectedAutomationId.length === 0 || (allRuns[i].automationId || "") === selectedAutomationId) {
                nextRuns.push(allRuns[i]);
            }
        }
        runHistory = nextRuns;
    }

    function resetDraft() {
        selectedAutomationId = "";
        draftName = "";
        draftProvider = "auto";
        draftModel = read("agents.defaults.model", "");
        draftTags = "";
        draftPrompt = "";
        draftEnabled = true;
        draftScheduleKind = "manual";
        draftScheduleValue = "";
        draftTimeZone = "";
        rebuildProviderChoices();
        rebuildRunHistory();
    }

    function loadSelection() {
        if (!initialized || currentPage !== "automation") {
            return;
        }
        var current = currentAutomation();
        if (!current.id) {
            resetDraft();
            return;
        }
        draftName = current.name || "";
        draftProvider = canonicalProviderKey(current.provider || "auto");
        draftModel = current.model || "";
        draftTags = joinCsv(current.tags || []);
        draftPrompt = current.prompt || "";
        draftEnabled = current.enabled === undefined ? true : !!current.enabled;
        draftScheduleKind = current.scheduleKind || current.trigger || "manual";
        draftScheduleValue = current.scheduleValue || "";
        draftTimeZone = current.timeZone || "";
        rebuildProviderChoices();
        rebuildRunHistory();
    }

    function schedulePlaceholder() {
        if (draftScheduleKind === "once") {
            return "例如 2026-03-13T09:30:00";
        }
        if (draftScheduleKind === "every") {
            return "例如 30m / 2h / 1d";
        }
        if (draftScheduleKind === "cron") {
            return "例如 0 9 * * 1-5";
        }
        return "";
    }

    function scheduleHint() {
        if (draftScheduleKind === "once") {
            return "单次执行会在指定 ISO 时间运行一次,执行完自动停用该调度.";
        }
        if (draftScheduleKind === "every") {
            return "间隔调度支持 `15m`、`2h`、`1d`，纯数字默认按分钟处理。";
        }
        if (draftScheduleKind === "cron") {
            return "Cron 使用 5 段表达式，可选时区字段建议填写 IANA 时区，例如 Asia/Shanghai。";
        }
        return "手动模式不会注册定时任务,只有你点击“执行”时才会运行.";
    }

    function saveCurrent() {
        if (draftName.trim().length === 0 || draftPrompt.trim().length === 0) {
            showToast("信息不完整", "自动化名称和提示词正文不能为空。", "warning");
            return;
        }
        if (draftScheduleKind !== "manual" && draftScheduleValue.trim().length === 0) {
            showToast("缺少调度", "当前触发方式需要填写调度参数。", "warning");
            return;
        }

        var savedId = studioBridge.saveAutomation({
            "id": selectedAutomationId,
            "name": draftName,
            "trigger": draftScheduleKind,
            "scheduleKind": draftScheduleKind,
            "scheduleValue": draftScheduleValue,
            "timeZone": draftTimeZone,
            "provider": draftProvider,
            "model": draftModel,
            "prompt": draftPrompt,
            "tags": splitCsv(draftTags),
            "enabled": draftEnabled
        });
        if (savedId && savedId.length > 0) {
            selectedAutomationId = savedId;
            studioBridge.refreshAll();
        }
    }

    onSelectedAutomationIdChanged: if (initialized && appRoot.currentPage === "automation") loadSelection()
    Component.onCompleted: {
        if (appRoot.currentPage === "automation") {
            ensureInitialized();
        }
    }

    Connections {
        target: studioBridge ? studioBridge : null
        onAutomationsChanged: if (appRoot.currentPage === "automation") automationPage.loadSelection()
        onAutomationRunsChanged: if (appRoot.currentPage === "automation") automationPage.rebuildRunHistory()
        onConfigChanged: if (appRoot.currentPage === "automation") automationPage.rebuildProviderChoices()
    }

    Connections {
        target: appRoot ? appRoot : null
        onCurrentPageChanged: {
            if (appRoot.currentPage === "automation") {
                automationPage.ensureInitialized();
                automationPage.loadSelection();
            }
        }
    }

    ScrollView {
            anchors.fill: parent
            clip: true
            visible: appRoot.currentPage === "automation"
            enabled: visible

            Column {
                width: pageWidth(stack.width)
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 14

                PageHero {
                    width: parent.width
                    sectionKey: "automation"
                    overline: "自动化编排 Automation Fabric"
                    title: "自动化编排中枢"
                    description: "高频提示词可以在这里沉淀为可调度、可回看、可复制的运行单元，既能手动触发，也能接入定时器。"
                    metrics: [
                        {
                            "label": "已保存 Saved",
                            "value": (studioBridge.automations || []).length,
                            "accent": Design.Theme.section("catalog").accent
                        },
                        {
                            "label": "运行 Runs",
                            "value": (studioBridge.automationRuns || []).length,
                            "accent": Design.Theme.section("runtime").accent
                        },
                        {
                            "label": "调度器 Scheduler",
                            "value": studioBridge.status.gatewayRunning ? "在线 Online" : "离线 Offline",
                            "accent": studioBridge.status.gatewayRunning ? Design.Theme.status("success").accent : Design.Theme.status("warning").accent
                        }
                    ]
                }

                NeoCard {
                    width: parent.width
                    height: 152
                    sectionKey: "automation"
                    title: "自动化控制台 Automation"
                    subtitle: "把高频提示词升级成可调度、可追踪、可复用的运行单元"
                    titleIconKey: "automation"

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: 10
                        spacing: 10

                        ResponsiveGridStrip {
                            Layout.fillWidth: true
                            itemCount: 4
                            minimumCellWidth: 128
                            maximumColumns: 4
                            columnSpacing: 10
                            rowSpacing: 10

                        Repeater {
                            model: [
                                { "label": "已保存", "value": ((studioBridge.automations || []).length || 0), "accent": Design.Theme.section("automation").accent },
                                { "label": "最近运行", "value": ((studioBridge.automationRuns || []).length || 0), "accent": Design.Theme.section("runtime").accent },
                                { "label": "定时任务", "value": (studioBridge.status.cronJobCount || 0), "accent": Design.Theme.section("tasks").accent },
                                { "label": "调度器", "value": studioBridge.status.gatewayRunning ? "运行中" : "未运行", "accent": studioBridge.status.gatewayRunning ? Design.Theme.status("success").accent : Design.Theme.status("warning").accent }
                            ]

                            delegate: Rectangle {
                                Layout.fillWidth: true
                                property color accentColor: modelData.accent
                                property var chipStyle: Design.Theme.metricChip(accentColor)
                                radius: 14
                                implicitHeight: automationConsoleMetricRow.implicitHeight + 12
                                color: chipStyle.background
                                border.width: 1
                                border.color: chipStyle.border

                                Row {
                                    id: automationConsoleMetricRow
                                    x: 10
                                    y: 6
                                    spacing: 8

                                    Text {
                                        text: modelData.label
                                        color: chipStyle.label
                                        font.pixelSize: 11
                                    }

                                    Text {
                                        text: modelData.value
                                        color: chipStyle.value
                                        font.pixelSize: 12
                                        font.weight: Font.Black
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "内置占位符：`{{now}}`,`{{today}}`,`{{automation.name}}`,`{{automation.id}}`,`{{run.source}}`,`{{run.count}}`.定时自动化会写入 `cron/jobs.json`,真正触发依赖顶部网关服务处于运行中."
                        color: listItemStyle.meta
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }

                GridLayout {
                    id: automationWorkbenchLayout
                    width: parent.width
                    visible: appRoot.currentPage === "automation"
                    enabled: visible
                    columns: automationWorkbenchColumns(parent.width)
                    rowSpacing: 14
                    columnSpacing: 14

                    NeoCard {
                        Layout.fillWidth: automationWorkbenchLayout.columns === 1
                        Layout.preferredWidth: automationWorkbenchLayout.columns > 1 ? 320 : -1
                        Layout.minimumWidth: automationWorkbenchLayout.columns > 1 ? 300 : -1
                        Layout.preferredHeight: 840
                        Layout.minimumHeight: 840
                        stretchContent: true
                        sectionKey: "catalog"
                        title: "自动化索引 Automation Index"
                        subtitle: "选择、复制和审阅每条自动化"
                        titleIconKey: "catalog"

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            ActionButton {
                                compact: true
                                text: "新建"
                                onClicked: automationPage.resetDraft()
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: ((studioBridge.automations || []).length || 0) + " 条"
                                color: Design.Theme.section("automation").accent
                                font.pixelSize: 12
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: automationIndexSummary.implicitHeight + 22
                            radius: 16
                            color: summaryBoxStyle.background
                            border.width: 1
                            border.color: summaryBoxStyle.border

                            Column {
                                id: automationIndexSummary
                                anchors.fill: parent
                                anchors.margins: 11
                                spacing: 6

                                Text {
                                    width: parent.width
                                    text: automationPage.currentAutomation().id
                                        ? (automationPage.currentAutomation().name || automationPage.currentAutomation().id)
                                        : "未选中自动化"
                                    color: summaryBoxStyle.title
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    text: automationPage.currentAutomation().id
                                        ? (automationScheduleSummary(automationPage.currentAutomation()) +
                                           ((automationPage.currentAutomation().enabled === false) ? "  ·  已停用" : "  ·  已启用"))
                                        : "从左侧列表选择一条自动化，右侧会展开完整配置和运行历史"
                                    color: automationPage.currentAutomation().id ? Design.Theme.section("automation").accent : summaryBoxStyle.text
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            cacheBuffer: 6000
                            spacing: 10
                            model: pageListModel("automation", studioBridge.automations)

                            delegate: Rectangle {
                                property bool isSelected: automationPage.selectedAutomationId === (modelData.id || "")
                                property var itemStyle: isSelected ? selectedListItemStyle : listItemStyle
                                width: ListView.view.width
                                implicitHeight: automationIndexEntryColumn.implicitHeight + 28
                                radius: 8
                                color: itemStyle.background
                                border.width: 1
                                border.color: itemStyle.border

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: automationPage.selectedAutomationId = modelData.id || ""
                                }

                                Column {
                                    id: automationIndexEntryColumn
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 6

                                    Text {
                                        text: modelData.name || modelData.id || "自动化"
                                        color: itemStyle.title
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        width: parent.width
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: automationScheduleSummary(modelData) +
                                              ((modelData.enabled === false) ? "  ·  已停用" : "  ·  已启用")
                                        color: modelData.enabled === false ? Design.Theme.status("error").text : Design.Theme.section("automation").accent
                                        font.pixelSize: 12
                                        width: parent.width
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: providerTitle(modelData.provider || "auto") +
                                              "  ·  " +
                                              ((modelData.model || "").length > 0 ? modelData.model : "默认模型")
                                        color: itemStyle.text
                                        font.pixelSize: 12
                                        width: parent.width
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: firstLines(modelData.lastResultPreview || modelData.prompt || "", 120)
                                        color: itemStyle.body
                                        font.pixelSize: 12
                                        width: parent.width
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }
                }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 860
                        Layout.minimumHeight: 860
                        spacing: 14

                        NeoCard {
                            id: automationEditorCard
                            Layout.fillWidth: true
                            Layout.preferredHeight: 480
                            Layout.minimumHeight: 480
                            stretchContent: true
                            sectionKey: "draft"
                            title: "自动化编辑器 Automation Editor"
                            subtitle: "选择厂商与模型、定义调度方式，并编排真正执行的提示词"
                            titleIconKey: "draft"

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: automationEditorSummary.implicitHeight + 22
                            radius: 16
                            color: summaryBoxStyle.background
                            border.width: 1
                            border.color: summaryBoxStyle.border

                            ColumnLayout {
                                id: automationEditorSummary
                                x: 11
                                y: 11
                                width: parent.width - 22
                                spacing: 6

                                Text {
                                    Layout.fillWidth: true
                                    text: automationPage.selectedAutomationId.length > 0
                                        ? ("当前编辑  " + (automationPage.currentAutomation().name || automationPage.selectedAutomationId))
                                        : "当前正在创建一条新的自动化"
                                    color: summaryBoxStyle.title
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "触发方式  " + (automationPage.triggerChoices[automationPage.currentTriggerIndex()] || { "title": "手动" }).title +
                                          "  ·  厂商  " + providerTitle(automationPage.draftProvider || "auto") +
                                          "  ·  模型  " + ((automationPage.draftModel || "").length > 0 ? automationPage.draftModel : "默认模型")
                                    color: summaryBoxStyle.meta
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        GlassField {
                            Layout.fillWidth: true
                            text: automationPage.draftName
                            placeholderText: "自动化名称，例如 每日收件箱整理"
                            onTextChanged: automationPage.draftName = text
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            NeoComboBox {
                                model: automationPage.triggerChoices
                                textRole: "title"
                                currentIndex: automationPage.currentTriggerIndex()
                                onActivated: {
                                    if (currentIndex >= 0 && currentIndex < automationPage.triggerChoices.length) {
                                        automationPage.draftScheduleKind = automationPage.triggerChoices[currentIndex].key;
                                        if (automationPage.draftScheduleKind === "manual") {
                                            automationPage.draftScheduleValue = "";
                                            automationPage.draftTimeZone = "";
                                        }
                                    }
                                }
                            }

                            NeoComboBox {
                                model: automationPage.providerChoices
                                textRole: "title"
                                currentIndex: automationPage.currentProviderIndex()
                                onActivated: {
                                    if (currentIndex >= 0 && currentIndex < automationPage.providerChoices.length) {
                                        automationPage.draftProvider = automationPage.providerChoices[currentIndex].key;
                                        automationPage.rebuildModelChoices();
                                    }
                                }
                            }

                            NeoComboBox {
                                Layout.fillWidth: true
                                model: automationPage.modelChoices.length > 0 ? automationPage.modelChoices : ["未同步模型"]
                                enabled: automationPage.modelChoices.length > 0
                                currentIndex: automationPage.modelChoices.length > 0
                                    ? modelIndex(automationPage.modelChoices, automationPage.draftModel)
                                    : 0
                                onActivated: {
                                    if (automationPage.modelChoices.length > 0) {
                                        automationPage.draftModel = currentText;
                                    }
                                }
                            }

                            ActionButton {
                                compact: true
                                text: "同步模型"
                                enabled: automationPage.draftProvider !== "auto"
                                onClicked: {
                                    syncProviderCatalog(automationPage.draftProvider);
                                    automationPage.rebuildProviderChoices();
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            visible: automationPage.draftScheduleKind !== "manual"

                            GlassField {
                                Layout.fillWidth: true
                                text: automationPage.draftScheduleValue
                                placeholderText: automationPage.schedulePlaceholder()
                                onTextChanged: automationPage.draftScheduleValue = text
                            }

                            GlassField {
                                visible: automationPage.draftScheduleKind === "cron"
                                text: automationPage.draftTimeZone
                                placeholderText: "时区，例如 Asia/Shanghai"
                                onTextChanged: automationPage.draftTimeZone = text
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: automationPage.scheduleHint()
                            color: listItemStyle.meta
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }

                        GlassField {
                            Layout.fillWidth: true
                            text: automationPage.draftTags
                            placeholderText: "标签,逗号分隔"
                            onTextChanged: automationPage.draftTags = text
                        }

                        NeoCheckBox {
                            Layout.fillWidth: true
                            text: "启用这条自动化"
                            checked: automationPage.draftEnabled
                            onToggled: automationPage.draftEnabled = checked
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "执行提示词"
                            color: Design.Theme.section("draft").accent
                            font.pixelSize: 11
                            font.weight: Font.Black
                        }

                        GlassArea {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: automationPage.draftPrompt
                            placeholderText: "提示词正文，可以使用内置占位符，例如 {{now}}、{{automation.name}}、{{run.source}}。"
                            onTextChanged: automationPage.draftPrompt = text
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            RuntimeSectionLinkButton {
                                app: appRoot
                                sectionKey: "deployment"
                            }

                            Item { Layout.fillWidth: true }

                            ActionButton {
                                compact: true
                                text: "新建"
                                onClicked: automationPage.resetDraft()
                            }

                            ActionButton {
                                compact: true
                                text: "保存"
                                enabled: automationPage.draftName.trim().length > 0 &&
                                         automationPage.draftPrompt.trim().length > 0 &&
                                         (automationPage.draftScheduleKind === "manual" ||
                                          automationPage.draftScheduleValue.trim().length > 0)
                                onClicked: automationPage.saveCurrent()
                            }

                            ActionButton {
                                compact: true
                                text: "执行"
                                enabled: automationPage.selectedAutomationId.length > 0 &&
                                         automationPage.draftEnabled &&
                                         !studioBridge.busy
                                onClicked: {
                                    if (automationPage.selectedAutomationId.length > 0) {
                                        studioBridge.runAutomation(automationPage.selectedAutomationId);
                                    }
                                }
                            }

                            ActionButton {
                                compact: true
                                text: "删除"
                                tone: "danger"
                                enabled: automationPage.selectedAutomationId.length > 0
                                onClicked: {
                                    if (automationPage.selectedAutomationId.length > 0) {
                                        studioBridge.deleteAutomation(automationPage.selectedAutomationId);
                                        automationPage.resetDraft();
                                    }
                                }
                            }
                        }
                    }
                }

                    NeoCard {
                        id: automationRunHistoryCard
                        Layout.fillWidth: true
                        Layout.preferredHeight: 352
                        Layout.minimumHeight: 352
                        stretchContent: true
                        sectionKey: "runtime"
                        title: "运行状态 Run History"
                        subtitle: "查看下次运行,最近一次执行结果,以及完整历史"
                        titleIconKey: "runtime"

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: automationRunSummaryColumn.implicitHeight + 28
                            Layout.minimumHeight: 168
                            radius: 8
                            color: detailSummaryBoxStyle.background
                            border.width: 1
                            border.color: detailSummaryBoxStyle.border

                            Column {
                                id: automationRunSummaryColumn
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8

                                Text {
                                    text: automationPage.currentAutomation().id
                                        ? automationStatusLabel(automationPage.currentAutomation().lastStatus || "")
                                        : "未选择自动化"
                                    color: detailSummaryBoxStyle.title
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    width: parent.width
                                    text: "调度: " + automationScheduleSummary(automationPage.currentAutomation())
                                    color: Design.Theme.section("runtime").accent
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }

                                RowLayout {
                                    width: parent.width
                                    spacing: 16

                                    Text {
                                        Layout.fillWidth: true
                                        text: "下次运行: " + formatIsoDateTime((automationPage.currentAutomation().nextRunAt || ""))
                                        color: listItemStyle.text
                                        font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: "最近运行  " + formatIsoDateTime((automationPage.currentAutomation().lastRunAt || ""))
                                        color: listItemStyle.text
                                        font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                Text {
                                    text: "厂商 / 模型: " + providerTitle(automationPage.currentAutomation().provider || "auto") +
                                          "  ·  " +
                                          (((automationPage.currentAutomation().model || "").length > 0)
                                            ? automationPage.currentAutomation().model
                                            : "默认模型")
                                    color: listItemStyle.text
                                    font.pixelSize: 12
                                    width: parent.width
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: "累计执行: " + (automationPage.currentAutomation().runCount || 0) +
                                          ((automationPage.currentAutomation().cronJobId || "").length > 0
                                            ? ("  ·  定时任务 ID: " + automationPage.currentAutomation().cronJobId)
                                            : "")
                                    color: listItemStyle.meta
                                    font.pixelSize: 12
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    visible: !studioBridge.status.gatewayRunning &&
                                             automationPage.draftScheduleKind !== "manual"
                                    width: parent.width
                                    text: "当前网关未运行，定时自动化已保存但不会自动触发"
                                    color: warningSummaryBoxStyle.text
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }

                                RuntimeSectionQuickLinks {
                                    width: parent.width
                                    app: appRoot
                                    sections: ["deployment", "cluster"]
                                    summarySegments: [
                                        { "key": "deployment", "description": "负责部署,网关与控制平面." },
                                        { "key": "cluster", "description": "负责节点、候选路由与执行面健康。" }
                                    ]
                                    summaryColor: listItemStyle.meta
                                }
                            }
                        }

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true

                            Column {
                                width: parent.width
                                spacing: 10

                                Repeater {
                                    model: pageListModel("automation", automationPage.runHistory)

                                    delegate: Rectangle {
                                        property var historyStyle: modelData.status === "error" ? Design.Theme.listItem("danger") : listItemStyle
                                        width: parent.width
                                        radius: 16
                                        color: historyStyle.background
                                        border.width: 1
                                        border.color: historyStyle.border
                                        implicitHeight: historyColumn.implicitHeight + 24

                                        Column {
                                            id: historyColumn
                                            width: parent.width - 24
                                            anchors.left: parent.left
                                            anchors.leftMargin: 12
                                            anchors.top: parent.top
                                            anchors.topMargin: 12
                                            spacing: 6

                                            RowLayout {
                                                width: parent.width
                                                spacing: 10

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: (modelData.automationName || "自动化") + "  ·  " +
                                                          ((modelData.triggerSource || "manual") === "scheduled" ? "定时触发" : "手动触发")
                                                    color: historyStyle.title
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }

                                                ActionButton {
                                                    compact: true
                                                    text: "复制结果"
                                                    onClicked: studioBridge.copyToClipboard(modelData.result || "")
                                                }
                                            }

                                            Text {
                                                width: parent.width
                                                text: formatIsoDateTime(modelData.finishedAt || modelData.createdAt || "") +
                                                      "  ·  " +
                                                      ((modelData.status || "ok") === "error" ? "失败" : "成功") +
                                                      "  ·  " +
                                                      providerTitle(modelData.provider || "auto") +
                                                      " / " +
                                                      ((modelData.model || "").length > 0 ? modelData.model : "默认模型")
                                                color: modelData.status === "error" ? Design.Theme.status("error").text : Design.Theme.section("runtime").accent
                                                font.pixelSize: 12
                                                wrapMode: Text.WordWrap
                                            }

                                            Text {
                                                width: parent.width
                                                text: firstLines(modelData.resultPreview || modelData.error || "", 220)
                                                color: historyStyle.text
                                                font.pixelSize: 12
                                                wrapMode: Text.WordWrap
                                            }
                                        }
                                    }
                                }

                                Text {
                                    width: parent.width
                                    visible: automationPage.runHistory.length === 0
                                    text: automationPage.selectedAutomationId.length === 0
                                        ? "选中一条自动化后，这里会显示它的运行历史。"
                                        : "这条自动化还没有运行记录."
                                    color: listItemStyle.meta
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
                
                Item { width: 1; height: 32 }
            }
        }
    }
}
}
