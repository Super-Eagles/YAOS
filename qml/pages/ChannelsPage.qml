import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../components"
import "../theme" as Design
import QtGraphicalEffects 1.14

Item {
    id: channelsPage

    property var app
    property var studioBridge
    property real stackWidth: width
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")
    readonly property var listItemStyle: Design.Theme.listItem("default")

    ScrollView {
        anchors.fill: parent
        clip: true

        Column {
            width: app ? app.pageWidth(channelsPage.stackWidth) : parent.width
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10

            PageHero {
                width: parent.width
                compact: true
                sectionKey: "channels"
                overline: "投递频道 Delivery Channels"
                title: "频道投递面板"
                description: "统一管理外部触达渠道的启用状态、主凭据和投递策略，让消息出口更干净也更可控。"
                metrics: [
                    {
                        "label": "已启用 Enabled",
                        "value": (studioBridge.status.enabledChannels || []).length,
                        "accent": Design.Theme.section("channels").accent
                    },
                    {
                        "label": "进度 Progress",
                        "value": (app && app.read("channels.sendProgress", true)) ? "开启 On" : "关闭 Off",
                        "accent": (app && app.read("channels.sendProgress", true)) ? Design.Theme.status("success").accent : Design.Theme.status("warning").accent
                    },
                    {
                        "label": "工具提示 Tool Hints",
                        "value": (app && app.read("channels.sendToolHints", false)) ? "开启 On" : "关闭 Off",
                        "accent": (app && app.read("channels.sendToolHints", false)) ? Design.Theme.status("info").accent : Design.Theme.palette.textMuted
                    }
                ]
            }

            ResponsiveCardGrid {
                id: channelsSummaryGrid
                width: parent.width
                minimumCellWidth: 268
                spacing: 10

                NeoCard {
                    width: channelsSummaryGrid.cellWidth
                    height: 184
                    sectionKey: "channels"
                    title: "频道信号 Channels"
                    subtitle: "统一收发的 Agent 触达面"
                    titleIconKey: "events"
                    titleIcon: "◈"
                    guideText: "这里先看全局频道状态;下方每个频道卡片负责填写凭据并控制是否启用."

                    Column {
                        width: parent.width
                        spacing: 10

                        Text {
                            width: parent.width
                            text: "启用频道  " + (((studioBridge.status.enabledChannels || []).join(", ")) || "无")
                            color: summaryBoxStyle.title
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            width: parent.width
                            text: "进度播报  " + ((app && app.read("channels.sendProgress", true)) ? "开启" : "关闭")
                            color: listItemStyle.meta
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            width: parent.width
                            text: "工具提示  " + ((app && app.read("channels.sendToolHints", false)) ? "开启" : "关闭")
                            color: listItemStyle.meta
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                NeoCard {
                    width: channelsSummaryGrid.cellWidth
                    height: 184
                    sectionKey: "channels"
                    title: "投递策略 Delivery"
                    subtitle: "全局消息控制"
                    titleIconKey: "channels"
                    titleIcon: "⇄"
                    guideText: "在这里决定是否向外部频道发送进度播报和工具提示,影响所有启用的消息通道."

                    Column {
                        spacing: 10

                        NeoCheckBox {
                            text: "发送进度播报"
                            checked: app ? app.read("channels.sendProgress", true) : true
                            onToggled: if (app) app.assign("channels.sendProgress", checked)
                        }

                        NeoCheckBox {
                            text: "发送工具提示"
                            checked: app ? app.read("channels.sendToolHints", false) : false
                            onToggled: if (app) app.assign("channels.sendToolHints", checked)
                        }
                    }
                }
            }

            ResponsiveCardGrid {
                id: quickChannelsGrid
                width: parent.width
                minimumCellWidth: 268
                spacing: 10
                visible: parent.width > 0

                Repeater {
                    model: (app && parent.visible) ? app.quickChannels : []

                    delegate: NeoCard {
                        width: quickChannelsGrid.cellWidth
                        title: modelData.title
                        subtitle: modelData.description
                        titleIconSpec: app ? app.channelIconSpec(modelData.key) : undefined
                        titleIcon: app ? app.channelIcon(modelData.key) : ""
                        guideText: "按需开启该频道并填写高频凭据;allowFrom 白名单填逗号分隔的 ID 或用户名,留空表示不限制."
                        glowColor: app && app.read("channels." + modelData.key + ".enabled", false)
                            ? Design.Theme.status("success").accent
                            : Design.Theme.section("channels").accent

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            NeoCheckBox {
                                text: "启用 " + modelData.title
                                checked: app ? app.read("channels." + modelData.key + ".enabled", false) : false
                                onToggled: if (app) app.assign("channels." + modelData.key + ".enabled", checked)
                            }

                            GlassField {
                                Layout.fillWidth: true
                                text: app ? app.read("channels." + modelData.key + ".token",
                                                     app.read("channels." + modelData.key + ".bridgeToken",
                                                              app.read("channels." + modelData.key + ".appSecret",
                                                                       app.read("channels." + modelData.key + ".clientSecret", ""))))
                                          : ""
                                echoMode: TextInput.Password
                                placeholderText: "主凭据 / Token / App Secret"
                                onEditingFinished: {
                                    if (!app) return;
                                    if (app.read("channels." + modelData.key + ".token", undefined) !== undefined) {
                                        app.assign("channels." + modelData.key + ".token", text);
                                    } else if (app.read("channels." + modelData.key + ".bridgeToken", undefined) !== undefined) {
                                        app.assign("channels." + modelData.key + ".bridgeToken", text);
                                    } else if (app.read("channels." + modelData.key + ".appSecret", undefined) !== undefined) {
                                        app.assign("channels." + modelData.key + ".appSecret", text);
                                    } else if (app.read("channels." + modelData.key + ".clientSecret", undefined) !== undefined) {
                                        app.assign("channels." + modelData.key + ".clientSecret", text);
                                    } else {
                                        app.assign("channels." + modelData.key + ".token", text);
                                    }
                                }
                            }

                            GlassField {
                                Layout.fillWidth: true
                                text: app ? app.read("channels." + modelData.key + ".bridgeUrl",
                                                     app.read("channels." + modelData.key + ".appId",
                                                              app.read("channels." + modelData.key + ".clientId", "")))
                                          : ""
                                placeholderText: "桥接地址 / App ID / Client ID（可选）"
                                onEditingFinished: {
                                    if (!app) return;
                                    if (app.read("channels." + modelData.key + ".bridgeUrl", undefined) !== undefined) {
                                        app.assign("channels." + modelData.key + ".bridgeUrl", text);
                                    } else if (app.read("channels." + modelData.key + ".appId", undefined) !== undefined) {
                                        app.assign("channels." + modelData.key + ".appId", text);
                                    } else if (app.read("channels." + modelData.key + ".clientId", undefined) !== undefined) {
                                        app.assign("channels." + modelData.key + ".clientId", text);
                                    }
                                }
                            }

                            GlassField {
                                Layout.fillWidth: true
                                text: app ? (app.read("channels." + modelData.key + ".allowFrom", []) || []).join(", ") : ""
                                placeholderText: "allowFrom 白名单（逗号分隔，留空不限制）"
                                onEditingFinished: {
                                    if (!app) return;
                                    var raw = text.trim();
                                    if (raw.length === 0) {
                                        app.assign("channels." + modelData.key + ".allowFrom", []);
                                    } else {
                                        var parts = raw.split(",");
                                        var cleaned = [];
                                        for (var i = 0; i < parts.length; ++i) {
                                            var s = parts[i].trim();
                                            if (s.length > 0) cleaned.push(s);
                                        }
                                        app.assign("channels." + modelData.key + ".allowFrom", cleaned);
                                    }
                                }
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: (app && app.draftDirty) ? "保存频道配置 ⚠️" : "保存频道配置"
                                onClicked: if (app) app.saveDraft()
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 32 }
        }
    }
}
