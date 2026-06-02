import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../components"
import "../theme" as Design
import QtGraphicalEffects 1.14

Item {
    id: chatPage
    property var app
    property var studioBridge
    property real stackWidth: width
    property var appRoot: app
    property string currentPage: app ? app.currentPage : ""
    property var draftConfig: app ?app.draftConfig: ({})
    readonly property var listItemStyle: Design.Theme.listItem("default")

    QtObject {
        id: stack
        property real width: chatPage.stackWidth
    }

    function canonicalProviderKey() {
        return app ? app.canonicalProviderKey.apply(app, arguments) : undefined;
    }
    function chooseProviderModel() {
        return app ? app.chooseProviderModel.apply(app, arguments) : undefined;
    }
    function configuredProviderOptions() {
        return app ? app.configuredProviderOptions.apply(app, arguments) : undefined;
    }
    function modelIndex() {
        return app ? app.modelIndex.apply(app, arguments) : undefined;
    }
    function pageListModel() {
        return app ? app.pageListModel.apply(app, arguments) : undefined;
    }
    function pageWidth() {
        return app ? app.pageWidth.apply(app, arguments) : undefined;
    }
    function providerValue() {
        return app ? app.providerValue.apply(app, arguments) : undefined;
    }
    function read() {
        return app ? app.read.apply(app, arguments) : undefined;
    }
    function runtimeProviderKey() {
        return app ? app.runtimeProviderKey.apply(app, arguments) : undefined;
    }
    function selectableProviderModels() {
        return app ? app.selectableProviderModels.apply(app, arguments) : undefined;
    }
    function showToast() {
        return app ? app.showToast.apply(app, arguments) : undefined;
    }
    function syncProviderCatalog() {
        return app ? app.syncProviderCatalog.apply(app, arguments) : undefined;
    }

    property string chatSessionKey: "gui:primary"
    property string chatProviderKey: ""
    property string chatModelOverride: ""
    property string sendMode: "ctrl_enter"
    property bool initialized: false
    property var providerChoices: []
    property var modelChoices: []

    function toolbarColumns(containerWidth) {
        if (containerWidth >= 860) {
            return 5;
        }
        if (containerWidth >= 600) {
            return 3;
        }
        return 1;
    }

    function workspaceColumns(containerWidth) {
        return containerWidth >= 860 ? 2 : 1;
    }

    function ensureInitialized() {
        if (initialized) {
            return;
        }
        initialized = true;
        rebuildProviderChoices();
    }

    function rebuildProviderChoices() {
        if (!initialized || currentPage !== "chat") {
            providerChoices = [];
            modelChoices = [];
            return;
        }
        providerChoices = configuredProviderOptions();

        var nextProvider = canonicalProviderKey(chatProviderKey);
        if (!nextProvider || nextProvider.length === 0) {
            nextProvider = canonicalProviderKey(read("agents.defaults.provider", "auto"));
        }
        if (nextProvider === "auto") {
            nextProvider = "";
        }

        var found = false;
        for (var i = 0; i < providerChoices.length; ++i) {
            if (providerChoices[i].key === nextProvider) {
                found = true;
                break;
            }
        }
        if (!found) {
            nextProvider = providerChoices.length > 0 ? providerChoices[0].key : "";
        }
        chatProviderKey = nextProvider;
        rebuildModelChoices();
    }

    function rebuildModelChoices() {
        if (!initialized || currentPage !== "chat") {
            modelChoices = [];
            return;
        }
        modelChoices = selectableProviderModels(chatProviderKey);
        chatModelOverride = chooseProviderModel(chatProviderKey, chatModelOverride);
    }

    function syncCurrentProviderModels() {
        if (!initialized || currentPage !== "chat" || !chatProviderKey) {
            return;
        }
        var models = syncProviderCatalog(chatProviderKey);
        if (models.length > 0) {
            chatModelOverride = chooseProviderModel(chatProviderKey, providerValue(chatProviderKey, "model", ""));
            rebuildProviderChoices();
        }
    }

    function sendCurrentPrompt() {
        if (studioBridge.busy || promptEditor.text.trim().length === 0) {
            return;
        }
        var effectiveModel = chatModelOverride;
        if (modelChoices.length > 0 && modelIndex(modelChoices, effectiveModel) < 0) {
            var safeModelIndex = Math.max(0, Math.min(modelChoices.length - 1, modelSelector.currentIndex));
            effectiveModel = modelChoices[safeModelIndex];
            chatModelOverride = effectiveModel;
        }
        if (!chatProviderKey || !effectiveModel) {
            showToast("模型未就绪", "请先选择已配置厂商,并同步对应模型列表", "warning");
            return;
        }
        studioBridge.sendMessage(promptEditor.text,
            chatSessionKey,
            effectiveModel,
            runtimeProviderKey(chatProviderKey));
        promptEditor.clear();
    }

    Component.onCompleted: {
        if (appRoot.currentPage === "chat") {
            ensureInitialized();
        }
    }

    Connections {
        target: studioBridge ? studioBridge : null
        onConfigChanged: if (appRoot.currentPage === "chat") chatPage.rebuildProviderChoices()
    }

    Connections {
        target: appRoot ? appRoot : null
        onCurrentPageChanged: {
            if (appRoot.currentPage === "chat") {
                chatPage.ensureInitialized();
                chatPage.rebuildProviderChoices();
            }
        }
    }

    Item {
        id: chatShell
        width: app ? pageWidth(stack.width) : parent.width
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter

        ColumnLayout {
            anchors.fill: parent
            spacing: 14

        PageHero {
                Layout.fillWidth: true
                compact: true
                sectionKey: "conversation"
                overline: "实时会话 Live Session"
                title: "对话工作台"
                description: "这里处理实时对话,模型切换和消息编排,左侧看上下文,右侧直接下达任务."
                metrics: [
                    {
                        "label": "提供方 Providers",
                        "value": chatPage.providerChoices.length,
                        "accent": Design.Theme.section("conversation").accent
                    },
                    {
                        "label": "消息 Messages",
                        "value": (studioBridge.chatHistory || []).length,
                        "accent": Design.Theme.section("routing").accent
                    },
                    {
                        "label": "状态 Status",
                        "value": studioBridge.busy ? "忙碌 Busy" : "就绪 Ready",
                        "accent": studioBridge.busy ? Design.Theme.status("warning").accent : Design.Theme.status("success").accent
                    }
                ]
            }

        GridLayout {
                Layout.fillWidth: true
                width: parent.width
                columns: toolbarColumns(width)
                columnSpacing: 14
                rowSpacing: 14

            GlassField {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 180
                    Layout.preferredWidth: 240
                    text: chatPage.chatSessionKey
                    placeholderText: "会话标识,例如 gui:primary"
                    onEditingFinished: chatPage.chatSessionKey = text
                }

            NeoComboBox {
                    id: providerSelector
                    Layout.fillWidth: true
                    Layout.minimumWidth: 160
                    Layout.preferredWidth: 220
                    model: chatPage.providerChoices
                    textRole: "title"
                    enabled: chatPage.providerChoices.length > 0
                    currentIndex: {
                        for (var i = 0; i < chatPage.providerChoices.length; ++i) {
                            if (chatPage.providerChoices[i].key === chatPage.chatProviderKey) {
                                return i;
                            }
                        }
                        return chatPage.providerChoices.length > 0 ? 0 : -1;
                    }
                    onActivated: {
                        if (currentIndex >= 0 && currentIndex < chatPage.providerChoices.length) {
                            chatPage.chatProviderKey = chatPage.providerChoices[currentIndex].key;
                            chatPage.rebuildModelChoices();
                        }
                    }
                }

            NeoComboBox {
                    id: modelSelector
                    Layout.fillWidth: true
                    Layout.minimumWidth: 220
                    Layout.preferredWidth: 320
                    model: chatPage.modelChoices.length > 0 ? chatPage.modelChoices : ["请先同步模型"]
                    enabled: chatPage.modelChoices.length > 0
                    currentIndex: {
                        if (chatPage.modelChoices.length === 0) {
                            return 0;
                        }
                        var index = modelIndex(chatPage.modelChoices, chatPage.chatModelOverride);
                        return index >= 0 ? index : 0;
                    }
                    onActivated: {
                        if (chatPage.modelChoices.length > 0) {
                            chatPage.chatModelOverride = currentText;
                        }
                    }
                }

            ActionButton {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 120
                    text: "同步模型"
                    compact: true
                    enabled: chatPage.chatProviderKey.length > 0
                    onClicked: chatPage.syncCurrentProviderModels()
                }

            ActionButton {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 120
                    text: "刷新"
                    compact: true
                    onClicked: studioBridge.refreshAll()
                }
            }

        GridLayout {
                Layout.fillWidth: true
                width: parent.width
                Layout.fillHeight: true
                columns: workspaceColumns(chatShell.width)
                columnSpacing: 14
                rowSpacing: 14

            NeoCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 460
                    Layout.preferredWidth: workspaceColumns(chatShell.width) > 1 ? 640 : -1
                    Layout.preferredHeight: 420
                    Layout.minimumHeight: 200
                    stretchContent: true
                    sectionKey: "conversation"
                    title: "对话记录 Conversation"
                    subtitle: "Markdown 渲染,执行轨迹与一键复制"
                    titleIconKey: "conversation"

                Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                    ListView {
                            id: chatList
                            anchors.fill: parent
                            clip: true
                            cacheBuffer: 8000
                            spacing: 16
                            model: studioBridge ? studioBridge.chatHistory : []

                            ScrollBar.vertical: ScrollBar {
                                id: scrollBar
                                policy: ScrollBar.AsNeeded
                                active: true
                                width: 6
                                anchors.right: parent.right
                                anchors.rightMargin: 2
                                contentItem: Rectangle {
                                    implicitWidth: 6
                                    radius: 3
                                    color: scrollBar.hovered || scrollBar.active ? "#66ffffff" : "#26ffffff"
                                    Behavior on color { ColorAnimation { duration: 150 } }
                                }
                            }

                            onCountChanged: {
                                Qt.callLater(function() {
                                    chatList.positionViewAtEnd();
                                });
                            }

                            delegate: Item {
                                id: delegateRoot
                                readonly property var entry: {
                                    if(typeof modelData !== "undefined" && modelData !== null) {
                                        return modelData;
                                    }
                                    return {
                                        "role": typeof role !== "undefined" ? role : "",
                                        "speaker": typeof speaker !== "undefined" ? speaker : "",
                                        "content": typeof content !== "undefined" ? content : "",
                                        "thinking": typeof thinking !== "undefined" ? thinking : "",
                                        "meta": typeof meta !== "undefined" ? meta : "",
                                        "trace": typeof trace !== "undefined" ? trace : [],
                                        "error": typeof error !== "undefined" ? error : false,
                                        "pending": typeof pending !== "undefined" ? pending : false,
                                        "category": typeof category !== "undefined" ? category : "",
                                        "level": typeof level !== "undefined" ? level : "",
                                        "message": typeof message !== "undefined" ? message : ""
                                    };
                                }
                                readonly property bool isUser: entry.role === "user"

                                width: chatList.width
                                height: Math.max(avatar.height, messageColumn.height) + 16

                                // WeChat-Style Avatar
                                Rectangle {
                                    id: avatar
                                    width: 38
                                    height: 38
                                    radius: 8
                                    color: delegateRoot.isUser ? "#07c160" : "#00bcd4" // WeChat Green vs Cyber Blue
                                    anchors.top: parent.top
                                    anchors.topMargin: 4
                                    anchors.left: delegateRoot.isUser ? undefined : parent.left
                                    anchors.right: delegateRoot.isUser ? parent.right : undefined
                                    anchors.leftMargin: delegateRoot.isUser ? 0 : 8
                                    anchors.rightMargin: delegateRoot.isUser ? 8 : 0

                                    Text {
                                        anchors.centerIn: parent
                                        text: delegateRoot.isUser ? "我" : (entry.speaker ? entry.speaker.substring(0, 1) : "A")
                                        color: "#FFFFFF"
                                        font.pixelSize: Design.Foundation.textXl
                                        font.weight: Font.Bold
                                    }
                                }

                                // Message Bubble Column (Tag, Bubble, Copy button)
                                Column {
                                    id: messageColumn
                                    anchors.top: parent.top
                                    anchors.left: delegateRoot.isUser ? parent.left : avatar.right
                                    anchors.right: delegateRoot.isUser ? avatar.left : parent.right
                                    anchors.leftMargin: delegateRoot.isUser ? 48 : 12
                                    anchors.rightMargin: delegateRoot.isUser ? 12 : 48
                                    spacing: 6

                                    // Header Info Tag
                                    Row {
                                        spacing: 8
                                        anchors.right: delegateRoot.isUser ? parent.right : undefined
                                        anchors.left: delegateRoot.isUser ? undefined : parent.left

                                        Text {
                                            text: entry.speaker || (delegateRoot.isUser ? "你" : "Agent")
                                            color: delegateRoot.isUser ? "#95EC69" : "#C0DCF0"
                                            font.pixelSize: Design.Foundation.textMd
                                            font.weight: Font.DemiBold
                                        }

                                        Text {
                                            text: entry.meta || ""
                                            color: "#557088"
                                            font.pixelSize: Design.Foundation.textXs
                                        }

                                        Text {
                                            visible: !!entry.pending
                                            text: "思考中..."
                                            color: Design.Theme.status("warning").text
                                            font.pixelSize: Design.Foundation.textXs
                                            font.weight: Font.DemiBold
                                        }
                                    }

                                    // Message Bubble background
                                    Rectangle {
                                        id: bubbleBg
                                        anchors.right: delegateRoot.isUser ? parent.right : undefined
                                        anchors.left: delegateRoot.isUser ? undefined : parent.left
                                        width: {
                                            var maxW = parent.width;
                                            var textW = delegateRoot.isUser ? userText.implicitWidth : assistantText.implicitWidth;
                                            var desiredW = textW + 24;
                                            if (!delegateRoot.isUser && !entry.pending && !!entry.thinking) {
                                                desiredW = Math.max(desiredW, 200);
                                            }
                                            return Math.min(maxW, desiredW);
                                        }
                                        height: bubbleContentLayout.implicitHeight + 20
                                        radius: 12
                                        color: delegateRoot.isUser ? "#1B3B2B" : "#131625"
                                        border.width: 1
                                        border.color: delegateRoot.isUser ? "#2E5E43" : "#22273D"

                                        layer.enabled: true
                                        layer.effect: DropShadow {
                                            transparentBorder: true
                                            horizontalOffset: 0
                                            verticalOffset: 2
                                            radius: 6
                                            samples: 13
                                            color: "#40000000"
                                        }

                                        Column {
                                            id: bubbleContentLayout
                                            anchors {
                                                left: parent.left
                                                right: parent.right
                                                top: parent.top
                                                margins: 10
                                            }
                                            spacing: 8

                                            // User plain text bubble
                                            Text {
                                                id: userText
                                                visible: delegateRoot.isUser
                                                width: parent.width
                                                text: entry.content || ""
                                                color: "#E5F5E5"
                                                wrapMode: Text.WordWrap
                                                font.pixelSize: Design.Foundation.textXl
                                                lineHeight: 1.2
                                            }

                                            // Assistant rich text/markdown bubble
                                            TextEdit {
                                                id: assistantText
                                                visible: !delegateRoot.isUser
                                                width: parent.width
                                                height: Math.max(contentHeight, 24)
                                                readOnly: true
                                                selectByMouse: true
                                                wrapMode: TextEdit.Wrap
                                                textFormat: TextEdit.RichText
                                                color: "#C5D5E5"
                                                text: visible ? (studioBridge ? studioBridge.markdownToHtml(entry.content || "") : "") : ""
                                                cursorVisible: false
                                                persistentSelection: true
                                                selectByKeyboard: true
                                                font.pixelSize: Design.Foundation.textXl
                                            }

                                            // Collapsible model reasoning process
                                            Column {
                                                visible: !delegateRoot.isUser && !entry.pending && !!entry.thinking
                                                width: parent.width
                                                spacing: 4
                                                property bool expanded: false

                                                Rectangle {
                                                    width: parent.width
                                                    height: 28
                                                    radius: 6
                                                    color: "#0B0D18"
                                                    border.width: 1
                                                    border.color: "#1E2235"

                                                    Row {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: 8
                                                        anchors.rightMargin: 8
                                                        spacing: 6
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        Text {
                                                            text: parent.parent.expanded ? "▾" : "▸"
                                                            color: "#5B8DB8"
                                                            font.pixelSize: Design.Foundation.textSm
                                                            anchors.verticalCenter: parent.verticalCenter
                                                        }
                                                        Text {
                                                            text: "模型思考过程"
                                                            color: "#5B8DB8"
                                                            font.pixelSize: Design.Foundation.textSm
                                                            font.weight: Font.Medium
                                                            anchors.verticalCenter: parent.verticalCenter
                                                        }
                                                    }
                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: parent.parent.expanded = !parent.parent.expanded
                                                    }
                                                }

                                                Rectangle {
                                                    visible: parent.expanded
                                                    width: parent.width
                                                    height: thinkingEdit.implicitHeight + 16
                                                    color: "#070810"
                                                    border.width: 1
                                                    border.color: "#1E2235"
                                                    radius: 6

                                                    TextEdit {
                                                        id: thinkingEdit
                                                        anchors { fill: parent; margins: 8 }
                                                        readOnly: true
                                                        selectByMouse: true
                                                        wrapMode: TextEdit.Wrap
                                                        textFormat: TextEdit.PlainText
                                                        color: "#7A9BBF"
                                                        font.pixelSize: Design.Foundation.textSm
                                                        text: entry.thinking || ""
                                                        cursorVisible: false
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    // Action copy button
                                    ActionButton {
                                        visible: !delegateRoot.isUser && !entry.pending
                                        text: "复制本轮答复"
                                        compact: true
                                        onClicked: studioBridge.copyToClipboard(entry.content || "")
                                    }
                                }
                            }
                        }

                    Connections {
                        target: studioBridge ? studioBridge : null
                        onChatHistoryChanged: {
                            Qt.callLater(function() {
                                chatList.positionViewAtEnd();
                            });
                        }
                    }

                    Column {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 32, 400)
                        spacing: 12
                        visible: chatList.count === 0 && !studioBridge.busy

                        Text {
                            width: parent.width
                            text: "这里会展示每一轮对话和智能体的高质答复."
                            color: listItemStyle.title
                            font.pixelSize: Design.Foundation.textXxl
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Text {
                            width: parent.width
                            text: "输入一个任务,例如“检查当前工作区配置并给出改进建议”."
                            color: listItemStyle.meta
                            font.pixelSize: Design.Foundation.textMd
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    Rectangle {
                    id: busyPill
                        readonly property var busyTheme: Design.Theme.activityPill(Design.Theme.section("conversation").accent)
                    anchors.right: parent.right
                    anchors.top: parent.top
                    width: 178
                    height: 44
                    radius: 16
                    visible: studioBridge.busy
                    color: busyPill.busyTheme.background
                    border.width: 1
                    border.color: busyPill.busyTheme.border

                        Row {
                        anchors.centerIn: parent
                        spacing: 10

                            BusyIndicator {
                            running: studioBridge.busy
                            width: 20
                            height: 20
                        }

                            Text {
                            text: "Agent 正在处理"
                            color: busyPill.busyTheme.text
                            font.pixelSize: Design.Foundation.textMd
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }

            NeoCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 340
            Layout.preferredWidth: workspaceColumns(chatShell.width) > 1 ? 420 : -1
            Layout.preferredHeight: 360
            Layout.minimumHeight: 120
            stretchContent: true
            sectionKey: "composer"
            title: "消息编辑 Composer"
            subtitle: "给 Agent 下达任务,并按需指定模型"
            titleIconKey: "composer"

                ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 14

                    Text {
                    Layout.fillWidth: true
                    text: "当前后端： " + (studioBridge.status.actualBackend || "不可用")
                    color: Design.Theme.section("routing").accent
                    font.pixelSize: Design.Foundation.textLg
                    wrapMode: Text.WordWrap
                }

                    GlassArea {
                    id: promptEditor
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    placeholderText: "让 Agent 检查工作区,调用工具或总结结果..."

                    Keys.onPressed: {
                        var enterPressed = (event.key === Qt.Key_Return || event.key === Qt.Key_Enter);
                        var ctrlPressed = (event.modifiers & Qt.ControlModifier) !== 0;
                        if (!enterPressed) {
                            return;
                        }
                        if (chatPage.sendMode === "ctrl_enter" && ctrlPressed) {
                            event.accepted = true;
                            chatPage.sendCurrentPrompt();
                            return;
                        }
                        if (chatPage.sendMode === "enter" && !ctrlPressed) {
                            event.accepted = true;
                            chatPage.sendCurrentPrompt();
                        }
                    }
                }

                    GridLayout {
                    Layout.fillWidth: true
                    width: parent.width
                    columns: toolbarColumns(parent.width) >= 3 ? 3 : toolbarColumns(parent.width)
                    columnSpacing: 10
                    rowSpacing: 10

                        NeoComboBox {
                        Layout.fillWidth: true
                        model: ["Ctrl+Enter 发送", "Enter 发送"]
                        currentIndex: chatPage.sendMode === "enter" ? 1 : 0
                        onActivated: chatPage.sendMode = currentIndex === 1 ? "enter" : "ctrl_enter"
                    }

                        RuntimeSectionLinkButton {
                        Layout.fillWidth: true
                        app: appRoot
                        sectionKey: "cluster"
                    }

                        ActionButton {
                        Layout.fillWidth: true
                        text: studioBridge.busy ? "处理中..." : "发送任务"
                        enabled: !studioBridge.busy
                        onClicked: chatPage.sendCurrentPrompt()
                    }
                }
            }
        }
    }
}
}
}
