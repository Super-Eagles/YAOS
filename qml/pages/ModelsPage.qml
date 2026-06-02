import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../components"
import "../theme" as Design

Item {
    id: modelsPage

    property var app
    property var studioBridge
    property var chatPage
    property real stackWidth: width
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")
    readonly property var listItemStyle: Design.Theme.listItem("default")

    function headerColumns(containerWidth) {
        if (containerWidth >= 1180) {
            return 3;
        }
        return containerWidth >= 760 ? 2 : 1;
    }

    ScrollView {
        anchors.fill: parent
        clip: true

        Column {
            width: app ? app.pageWidth(modelsPage.stackWidth) : parent.width
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10

            PageHero {
                width: parent.width
                compact: true
                sectionKey: "models"
                overline: "模型路由 Model Routing"
                title: "模型与提供方矩阵"
                description: "统一维护默认路由,凭据,模型目录和 OAuth 状态,让聊天,自动化和运行时共享同一套底座."
                metrics: [
                    {
                        "label": "提供方 Providers",
                        "value": app && app.providerDefinitions ? app.providerDefinitions.length : 0,
                        "accent": Design.Theme.section("models").accent
                    },
                    {
                        "label": "默认 Default",
                        "value": app ? app.providerTitle(app.read('agents.defaults.provider', 'auto')) : "自动 Auto",
                        "accent": Design.Theme.section("routing").accent
                    },
                    {
                        "label": "模型 Model",
                        "value": app ? (app.read('agents.defaults.model', '') || "未设置 Unset") : "未设置 Unset",
                        "accent": Design.Theme.status("success").accent
                    }
                ]
            }

            NeoCard {
                width: parent.width
                sectionKey: "models"
                title: "模型设置 Model Settings"
                subtitle: "管理驱动凭据、模型目录和当前路由"
                titleIconKey: "models"
                titleIcon: "✶"
                guideText: "先选择默认厂商和模型，再保存配置；下方每个厂商卡片可继续填写凭据、同步模型并指定默认路由。"

                GridLayout {
                    Layout.fillWidth: true
                    width: parent.width
                    columns: headerColumns(width)
                    columnSpacing: 14
                    rowSpacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: "默认提供商"
                            color: Design.Theme.section("models").accent
                            font.pixelSize: Design.Foundation.textSm
                            font.weight: Font.DemiBold
                        }
                        NeoComboBox {
                            Layout.fillWidth: true
                            model: app ? app.defaultProviderOptions() : []
                            textRole: "title"
                            currentIndex: {
                                if (!app) {
                                    return -1;
                                }
                                var currentProvider = app.canonicalProviderKey(app.read("agents.defaults.provider", "auto"));
                                var options = app.defaultProviderOptions();
                                for (var i = 0; i < options.length; ++i) {
                                    if (options[i].key === currentProvider) {
                                        return i;
                                    }
                                }
                                return options.length > 0 ? 0 : -1;
                            }
                            onActivated: {
                                if (!app) {
                                    return;
                                }
                                var options = app.defaultProviderOptions();
                                if (currentIndex < 0 || currentIndex >= options.length) {
                                    return;
                                }
                                var providerKey = options[currentIndex].key;
                                app.assign("agents.defaults.provider",
                                    providerKey === "auto" ? "auto" : app.runtimeProviderKey(providerKey));
                                if (providerKey !== "auto") {
                                    var preferredModel = app.chooseProviderModel(providerKey, app.read("agents.defaults.model", ""));
                                    if (preferredModel.length > 0) {
                                        app.assign("agents.defaults.model", preferredModel);
                                    }
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: "默认模型"
                            color: Design.Theme.section("models").accent
                            font.pixelSize: Design.Foundation.textSm
                            font.weight: Font.DemiBold
                        }
                        NeoComboBox {
                            Layout.fillWidth: true
                            model: app ? app.defaultModelChoices() : []
                            enabled: model.length > 0
                            currentIndex: app ? app.modelIndex(model, app.read("agents.defaults.model", "")) : -1
                            onActivated: {
                                if (app && currentIndex >= 0 && currentIndex < model.length) {
                                    app.assign("agents.defaults.model", currentText);
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: "配置操作"
                            color: Design.Theme.section("models").accent
                            font.pixelSize: Design.Foundation.textSm
                            font.weight: Font.DemiBold
                        }
                        ActionButton {
                            Layout.fillWidth: true
                            text: (app && app.draftDirty) ? "保存模型配置 ⚠️ (未保存)" : "保存模型配置"
                            onClicked: if (app) app.saveDraft()
                        }
                    }
                }
            }

            ResponsiveCardGrid {
                id: providerGrid
                objectName: "providerGrid"
                width: parent.width
                forcedColumns: app ? app.providerGridColumnCount(width) : 2
                minimumCellWidth: 320
                spacing: 18
                visible: parent.width > 0

                Repeater {
                    id: providerRepeater
                    model: (app && parent.visible) ? app.providerDefinitions : []

                    delegate: NeoCard {
                        id: providerCard
                        objectName: "providerCard_" + canonicalProviderKeyValue
                        property bool showAdvanced: false
                        property var availableModels: app ? app.providerModelCatalog(modelData.key) : []
                        property bool usesOAuth: app ? app.providerUsesOAuth(modelData.key) : false
                        property string canonicalProviderKeyValue: app ? app.canonicalProviderKey(modelData.key) : ""
                        property bool supportsBrowserOAuth: canonicalProviderKeyValue === "openaiCodex"
                        property var authStateCache: ({})
                        property string authSummary: app ? app.providerAuthSummaryText(authStateCache) : ""
                        property string authDiagnostics: app ? app.providerAuthDiagnosticsText(authStateCache) : ""
                        property string authLink: String(authStateCache.authUrl || authStateCache.verificationUrl || "")
                        property bool canRefreshAuth: !!authStateCache.loggedIn || !!authStateCache.hasRefreshToken
                        property bool canLogoutAuth: !!authStateCache.loggedIn ||
                            !!authStateCache.pending ||
                            !!authStateCache.hasOAuthAccessToken ||
                            !!authStateCache.hasRefreshToken ||
                            !!authStateCache.hasIdToken
                        property bool authPanelVisible: usesOAuth
                        property bool browserOAuthActionVisible: supportsBrowserOAuth
                        property bool deviceOAuthActionVisible: usesOAuth
                        property bool refreshOAuthActionVisible: usesOAuth
                        property bool refreshOAuthActionEnabled: canRefreshAuth
                        property bool logoutOAuthActionVisible: usesOAuth
                        property bool logoutOAuthActionEnabled: canLogoutAuth
                        property bool defaultActionVisible: true
                        property bool modelSyncActionVisible: true
                        property bool modelSyncActionEnabled: app ? app.providerSupportsModelSync(modelData.key) : false
                        property string selectedProviderModel: app ? app.providerValue(modelData.key, "model", "") : ""
                        width: providerGrid.cellWidth
                        title: modelData.title
                        subtitle: modelData.hint
                        titleIconSpec: app ? app.providerIconSpec(modelData.key) : undefined
                        titleIcon: app ? app.providerIcon(modelData.key) : ""
                        guideText: usesOAuth
                            ? "支持桌面内直接完成凭据配置,OAuth 登录和模型同步;确认可用后可直接设为默认."
                            : "填写凭据后点击“同步模型”;确认可用后可直接设为默认,聊天页会优先采用这一组配置."
                        glowColor: app && app.providerIsConfigured(modelData.key)
                            ? Design.Theme.status("success").accent
                            : Design.Theme.section("models").accent

                        Component.onCompleted: {
                            if (app && usesOAuth) {
                                authStateCache = app.providerAuthState(modelData.key);
                            }
                        }

                        Connections {
                            target: studioBridge ? studioBridge : null
                            onConfigChanged: {
                                if (app && providerCard.usesOAuth) {
                                    providerCard.authStateCache = app.providerAuthState(modelData.key);
                                }
                            }
                        }

                        Column {
                            Layout.fillWidth: true
                            width: parent.width
                            spacing: 6

                            GlassField {
                                width: parent.width
                                text: app ? app.providerValue(modelData.key, "apiBase", "") : ""
                                placeholderText: "API Base / 接口地址"
                                onEditingFinished: if (app) app.setProviderValue(modelData.key, "apiBase", text)
                            }

                            GlassField {
                                width: parent.width
                                text: app ? app.providerValue(modelData.key, "apiKey", "") : ""
                                echoMode: TextInput.Password
                                placeholderText: app ? app.providerApiKeyPlaceholder(modelData.key) : ""
                                onEditingFinished: if (app) app.setProviderValue(modelData.key, "apiKey", text)
                            }

                            NeoMultiSelectComboBox {
                                width: parent.width
                                model: availableModels
                                enabled: availableModels.length > 0
                                checkedItems: app ? app.providerValue(modelData.key, "enabledModels", []) : []
                                placeholderText: availableModels.length > 0 ? "请选择启用的模型 (不选显示全部)" : "请先同步模型"
                                onSelectionChanged: {
                                    if (!app || availableModels.length === 0) return;
                                    var currentVal = items;
                                    app.setProviderValue(modelData.key, "enabledModels", currentVal);
                                    
                                    var currentModel = String(app.providerValue(modelData.key, "model", ""));
                                    if (currentVal.length > 0 &&
                                        (currentModel === "" || currentVal.indexOf(currentModel) < 0)) {
                                        app.setProviderValue(modelData.key, "model", currentVal[0]);
                                        if (app.canonicalProviderKey(app.read("agents.defaults.provider", "auto")) ===
                                            app.canonicalProviderKey(modelData.key)) {
                                            app.assign("agents.defaults.model", currentVal[0]);
                                        }
                                    }
                                }
                            }

                            Text {
                                width: parent.width
                                text: providerCard.showAdvanced ? "收起高级配置 ▲" : "展开高级配置 ▼"
                                color: Design.Theme.section("models").accent
                                font.pixelSize: Design.Foundation.textSm
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignRight
                                topPadding: 4
                                bottomPadding: 4
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: providerCard.showAdvanced = !providerCard.showAdvanced
                                }
                            }

                            Column {
                                width: parent.width
                                visible: providerCard.showAdvanced
                                spacing: 6

                                GlassField {
                                    width: parent.width
                                    text: app ? app.providerValue(modelData.key, "model", "") : ""
                                    placeholderText: app && app.providerSupportsModelSync(modelData.key)
                                        ? "Model name / manual override"
                                        : "Manual model or deployment name"
                                    onEditingFinished: {
                                        if (!app) {
                                            return;
                                        }
                                        var manualModel = text.trim();
                                        app.setProviderValue(modelData.key, "model", manualModel);
                                        if (app.canonicalProviderKey(app.read("agents.defaults.provider", "auto")) ===
                                            app.canonicalProviderKey(modelData.key) &&
                                            manualModel.length > 0) {
                                            app.assign("agents.defaults.model", manualModel);
                                        }
                                    }
                                }

                                GlassArea {
                                    id: headersInput
                                    width: parent.width
                                    height: 60
                                    property bool hasFormatError: false
                                    text: app ? app.mapToLines(app.providerValue(modelData.key, "extraHeaders", {})) : ""
                                    placeholderText: "Extra Headers, one key=value per line"
                                    onTextChanged: {
                                        if (!activeFocus) return;
                                        var lines = text.split(/\r?\n/);
                                        var ok = true;
                                        for (var i = 0; i < lines.length; i++) {
                                            var trimmed = lines[i].trim();
                                            if (trimmed.length > 0 && trimmed.indexOf('=') <= 0) {
                                                ok = false;
                                                break;
                                            }
                                        }
                                        hasFormatError = !ok;
                                    }
                                    onActiveFocusChanged: {
                                        if (app && !activeFocus && !hasFormatError) {
                                            app.setProviderValue(modelData.key, "extraHeaders", app.parseKeyValueLines(text));
                                        }
                                    }
                                }

                                Text {
                                    width: parent.width
                                    visible: headersInput.hasFormatError && headersInput.activeFocus
                                    text: "格式错误: 此处需为 key=value 键值对,每行一个"
                                    color: Design.Theme.status("warning").text
                                    font.pixelSize: Design.Foundation.textMd
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    width: parent.width
                                    visible: app && app.providerHeaderHint(modelData.key).length > 0
                                    text: app ? app.providerHeaderHint(modelData.key) : ""
                                    color: Design.Theme.section("models").accent
                                    font.pixelSize: Design.Foundation.textMd
                                    wrapMode: Text.WordWrap
                                }
                            }

                            Rectangle {
                                objectName: "providerAuthPanel_" + providerCard.canonicalProviderKeyValue
                                visible: providerCard.usesOAuth
                                width: parent.width
                                radius: 16
                                color: summaryBoxStyle.background
                                border.width: 1
                                border.color: summaryBoxStyle.border
                                implicitHeight: oauthPanelColumn.implicitHeight + 24

                                Column {
                                    id: oauthPanelColumn
                                    x: 12
                                    y: 12
                                    width: parent.width - 24
                                    spacing: 8

                                    Text {
                                        width: parent.width
                                        text: "OAuth / Auth 状态"
                                        color: summaryBoxStyle.title
                                        font.pixelSize: Design.Foundation.textLg
                                        font.weight: Font.DemiBold
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        width: parent.width
                                        text: providerCard.authSummary.length > 0 ? providerCard.authSummary : "尚未连接."
                                        color: summaryBoxStyle.text
                                        font.pixelSize: Design.Foundation.textMd
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        width: parent.width
                                        visible: providerCard.authDiagnostics.length > 0
                                        text: providerCard.authDiagnostics
                                        color: summaryBoxStyle.meta
                                        font.pixelSize: Design.Foundation.textMd
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        width: parent.width
                                        visible: String(providerCard.authStateCache.userCode || "").length > 0
                                        text: "验证码: " + String(providerCard.authStateCache.userCode || "")
                                        color: Design.Theme.status("warning").text
                                        font.pixelSize: Design.Foundation.textMd
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        width: parent.width
                                        visible: String(providerCard.authStateCache.verificationUrl || "").length > 0
                                        text: "验证地址: " + String(providerCard.authStateCache.verificationUrl || "")
                                        color: summaryBoxStyle.text
                                        font.pixelSize: Design.Foundation.textMd
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        width: parent.width
                                        visible: String(providerCard.authStateCache.redirectUri || "").length > 0
                                        text: "回调地址: " + String(providerCard.authStateCache.redirectUri || "")
                                        color: summaryBoxStyle.meta
                                        font.pixelSize: Design.Foundation.textMd
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        width: parent.width
                                        visible: String(providerCard.authStateCache.callbackUrl || "").length > 0
                                        text: "最近回调: " + String(providerCard.authStateCache.callbackUrl || "")
                                        color: summaryBoxStyle.meta
                                        font.pixelSize: Design.Foundation.textMd
                                        wrapMode: Text.WordWrap
                                    }

                                    ResponsiveGridStrip {
                                        width: parent.width
                                        minimumCellWidth: 150
                                        maximumColumns: 3
                                        columnSpacing: 8
                                        rowSpacing: 8

                                        ActionButton {
                                            objectName: "providerOAuthBrowserAction_" + providerCard.canonicalProviderKeyValue
                                            visible: providerCard.supportsBrowserOAuth
                                            Layout.fillWidth: true
                                            compact: true
                                            text: "浏览器登录"
                                            onClicked: if (app) providerCard.authStateCache = app.beginProviderOAuthFlow(modelData.key, "browser")
                                        }

                                        ActionButton {
                                            objectName: "providerOAuthDeviceAction_" + providerCard.canonicalProviderKeyValue
                                            visible: providerCard.usesOAuth
                                            Layout.fillWidth: true
                                            compact: true
                                            text: providerCard.authStateCache.pending &&
                                                String(providerCard.authStateCache.mode || "") !== "browser"
                                                ? "继续轮询"
                                                : "设备码登录"
                                            onClicked: {
                                                if (!app) {
                                                    return;
                                                }
                                                if (providerCard.authStateCache.pending &&
                                                    String(providerCard.authStateCache.mode || "") !== "browser") {
                                                    providerCard.authStateCache = app.pollProviderOAuthState(modelData.key);
                                                } else {
                                                    providerCard.authStateCache = app.beginProviderOAuthFlow(modelData.key, "device");
                                                }
                                            }
                                        }

                                        ActionButton {
                                            objectName: "providerOAuthRefreshAction_" + providerCard.canonicalProviderKeyValue
                                            visible: providerCard.usesOAuth
                                            Layout.fillWidth: true
                                            compact: true
                                            text: "刷新凭据"
                                            enabled: providerCard.canRefreshAuth
                                            onClicked: if (app) providerCard.authStateCache = app.refreshProviderOAuthState(modelData.key)
                                        }

                                        ActionButton {
                                            objectName: "providerOAuthLogoutAction_" + providerCard.canonicalProviderKeyValue
                                            visible: providerCard.usesOAuth
                                            Layout.fillWidth: true
                                            compact: true
                                            text: "退出登录"
                                            enabled: providerCard.canLogoutAuth
                                            onClicked: {
                                                if (app && app.logoutProviderOAuthState(modelData.key)) {
                                                    providerCard.authStateCache = app.providerAuthState(modelData.key);
                                                }
                                            }
                                        }

                                        ActionButton {
                                            visible: providerCard.authLink.length > 0
                                            Layout.fillWidth: true
                                            compact: true
                                            text: "复制链接"
                                            onClicked: if (studioBridge) studioBridge.copyToClipboard(providerCard.authLink)
                                        }

                                        ActionButton {
                                            visible: String(providerCard.authStateCache.userCode || "").length > 0
                                            Layout.fillWidth: true
                                            compact: true
                                            text: "复制验证码"
                                            onClicked: if (studioBridge) studioBridge.copyToClipboard(String(providerCard.authStateCache.userCode || ""))
                                        }
                                    }
                                }
                            }

                            ResponsiveGridStrip {
                                width: parent.width
                                minimumCellWidth: 160
                                maximumColumns: 2
                                columnSpacing: 10
                                rowSpacing: 8

                                ActionButton {
                                    objectName: "providerDefaultAction_" + providerCard.canonicalProviderKeyValue
                                    Layout.fillWidth: true
                                    text: "设为默认"
                                    compact: true
                                    onClicked: if (app) app.setProviderAsDefault(modelData.key)
                                }

                                ActionButton {
                                    objectName: "providerModelSyncAction_" + providerCard.canonicalProviderKeyValue
                                    Layout.fillWidth: true
                                    text: "同步模型"
                                    compact: true
                                    enabled: app ? app.providerSupportsModelSync(modelData.key) : false
                                    onClicked: {
                                        if (!app) {
                                            return;
                                        }
                                        app.syncProviderCatalog(modelData.key);
                                        if (chatPage && chatPage.rebuildProviderChoices) {
                                            chatPage.rebuildProviderChoices();
                                        }
                                    }
                                }
                            } 

                            Text {
                                width: parent.width
                                text: availableModels.length > 0
                                    ? ("可选模型 Available: " + availableModels.length + "  ·  当前 " +
                                        ((app ? app.providerValue(modelData.key, "model", "") : "") || availableModels[0]))
                                    : "填写 API Base / API Key 后点击“同步模型”,这里会显示可选模型列表."
                                color: listItemStyle.meta
                                font.pixelSize: Design.Foundation.textMd
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }
}
