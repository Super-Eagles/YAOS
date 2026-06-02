import QtQuick 2.14
import QtQuick.Layouts 1.14
import "../theme" as Design

NeoCard {
    id: root
    objectName: "runtimeCard_runtimeTopology"
    property var app
    property var studioBridge
    property var deploymentModes: []
    property var runtimeModes: []
    readonly property var summarySurface: Design.Theme.surface("summary")
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")
    readonly property var successStatusStyle: Design.Theme.status("success")

    width: parent ? parent.width : 0
    Layout.fillWidth: true
    Layout.minimumHeight: 548
    sectionKey: "runtime"
    title: "部署与运行时 Runtime Topology"
    subtitle: "切换单机 / 集群,以及内嵌 / 守护进程 / 远端运行形态"
    titleIconKey: "runtime"
    titleIcon: "⛭"
    guideText: "先定部署模式,再填运行时,控制平面和注册表端点;这里决定整个桌面工作台连接哪一套后端."

    function assign() {
        return app ? app.assign.apply(app, arguments) : undefined;
    }

    function read() {
        return app ? app.read.apply(app, arguments) : undefined;
    }

    function keyedOptionIndex() {
        return app ? app.keyedOptionIndex.apply(app, arguments) : 0;
    }

    function joinCsv() {
        return app ? app.joinCsv.apply(app, arguments) : "";
    }

    function splitCsv() {
        return app ? app.splitCsv.apply(app, arguments) : [];
    }

    function controlPlaneEndpointText() {
        return app ? app.controlPlaneEndpointText.apply(app, arguments) : "";
    }

    function controlPlaneStatusText() {
        return app ? app.controlPlaneStatusText.apply(app, arguments) : "";
    }

    function controlTaskBusRecentEventsText() {
        return app ? app.controlTaskBusRecentEventsText.apply(app, arguments) : "";
    }

    function controlTaskBusSummaryText() {
        return app ? app.controlTaskBusSummaryText.apply(app, arguments) : "";
    }

    function hasControlTaskBusHealth() {
        return app ? app.hasControlTaskBusHealth.apply(app, arguments) : false;
    }

    function registryEndpointText() {
        return app ? app.registryEndpointText.apply(app, arguments) : "";
    }

    function registryStatusText() {
        return app ? app.registryStatusText.apply(app, arguments) : "";
    }

    function runtimeAdvertiseEndpointText() {
        return app ? app.runtimeAdvertiseEndpointText.apply(app, arguments) : "";
    }

    function runtimeEndpointText() {
        return app ? app.runtimeEndpointText.apply(app, arguments) : "";
    }

    function runtimeServiceStatusText() {
        return app ? app.runtimeServiceStatusText.apply(app, arguments) : "";
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: runtimeTopologySummary.implicitHeight + 12
            radius: 14
            color: summarySurface.background
            border.width: 1
            border.color: summarySurface.border

            ColumnLayout {
                id: runtimeTopologySummary
                x: 10
                y: 10
                width: parent.width - 20
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    text: "当前拓扑"
                    color: summarySurface.title
                    font.pixelSize: 11
                    font.weight: Font.Black
                }

                Text {
                    Layout.fillWidth: true
                    text: "模式  " + ((read("deployment.mode", "standalone") === "cluster") ? "集群" : "单机") +
                          "  ·  运行时  " + ((read("runtime.mode", "embedded") === "remote")
                                             ? "远端"
                                             : ((read("runtime.mode", "embedded") === "daemon") ? "守护进程" : "内嵌")) +
                          "  ·  节点  " + read("deployment.nodeId", "desktop-primary")
                    color: summarySurface.muted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            NeoComboBox {
                Layout.fillWidth: true
                model: deploymentModes
                textRole: "title"
                currentIndex: keyedOptionIndex(deploymentModes, read("deployment.mode", "standalone"), "standalone")
                onActivated: {
                    if (currentIndex >= 0 && currentIndex < deploymentModes.length) {
                        assign("deployment.mode", deploymentModes[currentIndex].key);
                    }
                }
            }

            NeoComboBox {
                Layout.fillWidth: true
                model: runtimeModes
                textRole: "title"
                currentIndex: keyedOptionIndex(runtimeModes, read("runtime.mode", "embedded"), "embedded")
                onActivated: {
                    if (currentIndex >= 0 && currentIndex < runtimeModes.length) {
                        assign("runtime.mode", runtimeModes[currentIndex].key);
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: read("deployment.clusterId", "local")
                placeholderText: "集群 ID"
                onEditingFinished: assign("deployment.clusterId", text)
            }

            GlassField {
                Layout.fillWidth: true
                text: read("deployment.nodeId", "desktop-primary")
                placeholderText: "节点 ID"
                onEditingFinished: assign("deployment.nodeId", text)
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: read("deployment.nodeRole", "desktop")
                placeholderText: "节点角色"
                onEditingFinished: assign("deployment.nodeRole", text)
            }

            GlassField {
                Layout.fillWidth: true
                text: joinCsv(read("deployment.nodeTags", []))
                placeholderText: "节点标签,逗号分隔"
                onEditingFinished: assign("deployment.nodeTags", splitCsv(text))
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: read("runtime.endpoint", "http://127.0.0.1:18890")
                placeholderText: "运行时端点"
                onEditingFinished: assign("runtime.endpoint", text)
            }

            GlassField {
                Layout.fillWidth: true
                text: read("runtime.advertiseEndpoint", "")
                placeholderText: "运行时对外广播地址（可选）"
                onEditingFinished: assign("runtime.advertiseEndpoint", text)
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: read("deployment.controlPlaneUrl", "")
                placeholderText: "控制平面端点"
                onEditingFinished: assign("deployment.controlPlaneUrl", text)
            }

            GlassField {
                Layout.fillWidth: true
                text: read("deployment.registryUrl", "")
                placeholderText: "注册表端点（可选）"
                onEditingFinished: assign("deployment.registryUrl", text)
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 10
            rowSpacing: 4

            NeoCheckBox {
                Layout.fillWidth: true
                text: "优先本地运行时"
                checked: read("runtime.preferLocal", true)
                onToggled: assign("runtime.preferLocal", checked)
            }

            NeoCheckBox {
                Layout.fillWidth: true
                text: "拉起本地 yaosd"
                checked: read("runtime.autoSpawnLocalDaemon", true)
                onToggled: assign("runtime.autoSpawnLocalDaemon", checked)
            }

            NeoCheckBox {
                Layout.fillWidth: true
                text: "拉起本地 HTTP服务"
                checked: read("runtime.autoSpawnLocalService", true)
                onToggled: assign("runtime.autoSpawnLocalService", checked)
            }
        }

        Text {
            Layout.fillWidth: true
            text: "运行时服务： " + runtimeServiceStatusText() +
                  "  |  端点： " + runtimeEndpointText() +
                  "  |  广播地址： " + runtimeAdvertiseEndpointText() +
                  (studioBridge && studioBridge.status && studioBridge.status.runtimeServiceAutoSpawn ? "  |  自动拉起：开启" : "  |  自动拉起：关闭")
            color: studioBridge && studioBridge.status && studioBridge.status.runtimeServiceReachable
                ? Design.Theme.status("success").text
                : summarySurface.meta
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: "控制平面： " + controlPlaneStatusText() +
                  "  |  端点： " + controlPlaneEndpointText()
            color: studioBridge && studioBridge.status && studioBridge.status.controlPlaneReachable
                ? Design.Theme.status("success").text
                : summarySurface.meta
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: hasControlTaskBusHealth()
            text: "任务总线： " + controlTaskBusSummaryText()
            color: summarySurface.meta
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            visible: hasControlTaskBusHealth()
            text: "最近租约活动： " + controlTaskBusRecentEventsText(4)
            color: summaryBoxStyle.meta
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: "注册表： " + registryStatusText() +
                  "  |  端点： " + registryEndpointText()
            color: studioBridge && studioBridge.status && studioBridge.status.registryReachable
                ? successStatusStyle.text
                : summarySurface.meta
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }
}
