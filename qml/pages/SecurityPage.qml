import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../components"
import "../theme" as Design
import QtGraphicalEffects 1.14

Item {
    id: securityPage
    property var app
    property var studioBridge
    property real stackWidth: width
    property var appRoot: app
    property string currentPage: app ? app.currentPage : ""
    property var draftConfig: app ?app.draftConfig: ({})

    property var policyOptions: app ?app.policyOptions: undefined
    readonly property var listItemSurface: Design.Theme.surface("list-item")
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")

    QtObject {
        id: stack
        property real width: securityPage.stackWidth
    }

    function assign() {
        return app ? app.assign.apply(app, arguments) : undefined;
    }
    function pageListModel() {
        return app ? app.pageListModel.apply(app, arguments) : undefined;
    }
    function pageWidth() {
        return app ? app.pageWidth.apply(app, arguments) : undefined;
    }
    function policyIndex() {
        return app ? app.policyIndex.apply(app, arguments) : undefined;
    }
    function read() {
        return app ? app.read.apply(app, arguments) : undefined;
    }
    function twoColumnCount() {
        return app ? app.twoColumnCount.apply(app, arguments) : undefined;
    }

    function securityTopColumns(containerWidth) {
        return containerWidth >= 920 ? 2 : 1;
    }

    function policyColumns(containerWidth) {
        if (containerWidth >= 480) {
            return 3;
        }
        return containerWidth >= 320 ? 2 : 1;
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
                sectionKey: "security"
                overline: "安全护栏 Guardrails"
                title: "安全与审批中心"
                description: "把工具策略、审批队列和通知信号收进一个控制面，风险、提示和人工确认都在这里汇总。"
                metrics: [
                    {
                        "label": "待审批 Pending",
                        "value": studioBridge.status.pendingApprovalCount || 0,
                        "accent": Design.Theme.section("approvals").accent
                    },
                    {
                        "label": "未读 Unread",
                        "value": studioBridge.status.unreadNotificationCount || 0,
                        "accent": Design.Theme.status("warning").accent
                    },
                    {
                        "label": "审计 Audit",
                        "value": read("security.auditToolCalls", true) ? "开启 On" : "关闭 Off",
                        "accent": read("security.auditToolCalls", true) ? Design.Theme.status("success").accent : Design.Theme.palette.textMuted
                    }
                ]
            }

            GridLayout {
                width: parent.width
                columns: securityTopColumns(parent.width)
                columnSpacing: 14
                rowSpacing: 14

                NeoCard {
                    Layout.fillWidth: true
                    sectionKey: "security"
                    title: "工具策略 Tool Policies"
                    subtitle: "决定哪些操作自动执行,哪些必须审批"
                    titleIconKey: "security"
                    guideText: (app && app.draftDirty) ? "⚠️ 策略已变动,请点击侧边栏的「保存更改」按钮以存盘生效." : ""

                    GridLayout {
                        id: policyGrid
                        width: parent.width
                        columns: policyColumns(width)
                        columnSpacing: 12
                        rowSpacing: 8

                        Repeater {
                            model: [
                                { "label": "读取文件", "key": "read_file" },
                                { "label": "写入文件", "key": "write_file" },
                                { "label": "列出目录", "key": "list_dir" },
                                { "label": "命令执行", "key": "exec" },
                                { "label": "发送消息", "key": "message" },
                                { "label": "子代理", "key": "spawn" },
                                { "label": "定时任务", "key": "cron" },
                                { "label": "调用 MCP", "key": "mcp_call" },
                                { "label": "插件调用", "key": "plugin_call" }
                            ]
                            delegate: RowLayout {
                                Layout.preferredWidth: Math.max(0,
                                    (policyGrid.width
                                        - policyGrid.columnSpacing * (policyGrid.columns - 1))
                                    / policyGrid.columns)
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                spacing: 8

                                Text {
                                    Layout.preferredWidth: 72
                                    text: modelData.label
                                    color: Design.Theme.palette.textPrimary
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                NeoComboBox {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 104
                                    model: policyOptions
                                    textRole: "title"
                                    currentIndex: policyIndex(read("security.toolPolicies." + modelData.key, "confirm"))
                                    onActivated: assign("security.toolPolicies." + modelData.key, policyOptions[currentIndex].key)
                                }
                            }
                        }
                    }
                }

                NeoCard {
                    Layout.fillWidth: true
                    sectionKey: "notifications"
                    title: "安全遥测 Security Telemetry"
                    subtitle: "审计日志与人工通知"
                    titleIconKey: "notifications"

                    Column {
                        width: parent.width
                        spacing: 12

                        NeoCheckBox {
                            text: "记录工具调用审计"
                            checked: read("security.auditToolCalls", true)
                            onToggled: assign("security.auditToolCalls", checked)
                        }

                        NeoCheckBox {
                            text: "需要审批时通知"
                            checked: read("security.notifyOnApprovalRequired", true)
                            onToggled: assign("security.notifyOnApprovalRequired", checked)
                        }

                        NeoCheckBox {
                            text: "工具被拒绝时通知"
                            checked: read("security.notifyOnToolDenied", true)
                            onToggled: assign("security.notifyOnToolDenied", checked)
                        }

                        Text {
                            text: "待审批  " + (studioBridge.status.pendingApprovalCount || 0) + "\n未读通知  " + (studioBridge.status.unreadNotificationCount || 0)
                            color: summaryBoxStyle.meta
                            font.pixelSize: 13
                            width: parent.width
                            wrapMode: Text.WordWrap
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
                    Layout.preferredHeight: 392
                    stretchContent: true
                    sectionKey: "approvals"
                    title: "审批队列 Approvals"
                    subtitle: "敏感工具执行前需要人工决策"
                    titleIconKey: "approvals"
                    titleIcon: "✦"
                    guideText: "这里集中处理需要人工确认的工具调用;可单次批准,始终允许或直接拒绝."

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 10
                        model: pageListModel("security", studioBridge.approvals)
                        footer: Text {
                            width: ListView.view ? ListView.view.width : 0
                            visible: !!ListView.view && ListView.view.count === 0
                            text: "当前没有待审批项.需要人工确认的工具调用会显示在这里."
                            color: Design.Theme.palette.textMuted
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }

                        delegate: Rectangle {
                            width: ListView.view.width
                            implicitHeight: approvalColumn.implicitHeight + 24
                            radius: 8
                            color: securityPage.listItemSurface.background
                            border.width: 1
                            border.color: securityPage.listItemSurface.border

                            Column {
                                id: approvalColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                Text {
                                    text: (modelData.toolName || "工具") + "  ·  " + (modelData.state || "pending")
                                    color: securityPage.listItemSurface.text
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    width: parent.width
                                    text: modelData.summary || modelData.paramsPreview || ""
                                    color: securityPage.listItemSurface.body
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }

                                ResponsiveGridStrip {
                                    width: parent.width
                                    itemCount: 3
                                    minimumCellWidth: 132
                                    maximumColumns: 3
                                    columnSpacing: 10
                                    rowSpacing: 10
                                    ActionButton { Layout.fillWidth: true; text: "单次批准"; compact: true; onClicked: studioBridge.approve(modelData.id, "session") }
                                    ActionButton { Layout.fillWidth: true; text: "始终允许"; compact: true; onClicked: studioBridge.approve(modelData.id, "always") }
                                    ActionButton { Layout.fillWidth: true; text: "拒绝"; compact: true; tone: "danger"; onClicked: studioBridge.deny(modelData.id) }
                                }
                            }
                        }
                    }
                }

                NeoCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 392
                    stretchContent: true
                    sectionKey: "notifications"
                    title: "通知中心 Notifications"
                    subtitle: "需要人工关注的系统消息"
                    titleIconKey: "notifications"
                    titleIcon: "◈"
                    guideText: "这里聚合系统通知和提醒;处理完可统一标记已读,减少主界面的干扰."

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 10

                        ActionButton {
                            Layout.alignment: Qt.AlignRight
                            text: "全部已读"
                            compact: true
                            enabled: (studioBridge.status.unreadNotificationCount || 0) > 0
                            onClicked: studioBridge.markNotificationsRead()
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 10
                            model: pageListModel("security", studioBridge.notifications)
                            footer: Text {
                                width: ListView.view ? ListView.view.width : 0
                                visible: !!ListView.view && ListView.view.count === 0
                                text: "当前没有通知.运行时产生需要关注的系统消息后,这里会显示."
                                color: Design.Theme.palette.textMuted
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }

                            delegate: Rectangle {
                                width: ListView.view.width
                                implicitHeight: notificationColumn.implicitHeight + 24
                                radius: 16
                                color: securityPage.listItemSurface.background
                                border.width: 1
                                border.color: modelData.read ? securityPage.listItemSurface.border : Design.Theme.status("info").border

                                Column {
                                    id: notificationColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 6

                                    Text { text: modelData.title || "通知"; color: securityPage.listItemSurface.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                                    Text { text: modelData.body || ""; color: securityPage.listItemSurface.body; font.pixelSize: 12; width: parent.width; wrapMode: Text.WordWrap }
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
