import QtQuick 2.14
import QtQuick.Layouts 1.14
import "../theme" as Design

NeoCard {
    id: root
    objectName: "runtimeCard_gateway"
    property var app
    property var studioBridge

    width: parent ? parent.width : 0
    Layout.fillWidth: true
    Layout.minimumHeight: 348
    sectionKey: "gateway"
    title: "网关入口 Gateway"
    subtitle: "网络入口与心跳配置"
    titleIconKey: "gateway"
    titleIcon: "⇆"
    guideText: "配置桌面端暴露出去的入口地址和心跳;改动后用于外部接入,轮询和状态保活."

    function assign() {
        return app ? app.assign.apply(app, arguments) : undefined;
    }

    function read() {
        return app ? app.read.apply(app, arguments) : undefined;
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12

        Rectangle {
            id: gatewaySummaryBox
            property var gatewaySummaryTheme: Design.Theme.summaryBox("default")
            Layout.fillWidth: true
            implicitHeight: gatewaySummaryRow.implicitHeight + 12
            radius: 8
            color: gatewaySummaryTheme.background
            border.width: 1
            border.color: gatewaySummaryTheme.border

            RowLayout {
                id: gatewaySummaryRow
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Text {
                    text: studioBridge && studioBridge.status && studioBridge.status.gatewayRunning ? "网关在线" : "网关空闲"
                    color: studioBridge && studioBridge.status && studioBridge.status.gatewayRunning
                        ? Design.Theme.status("success").text
                        : Design.Theme.status("warning").text
                    font.pixelSize: 11
                    font.weight: Font.Black
                        font.letterSpacing: 0.5
                }

                Rectangle {
                    Layout.fillHeight: true
                    Layout.topMargin: 8
                    Layout.bottomMargin: 8
                    color: gatewaySummaryBox.gatewaySummaryTheme.border
                }

                Text {
                    Layout.fillWidth: true
                    text: read("gateway.host", "0.0.0.0") + ":" + read("gateway.port", 18790).toString()
                    color: gatewaySummaryBox.gatewaySummaryTheme.meta
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: "监听设置"
            color: Design.Theme.section("gateway").accent
            font.pixelSize: 11
            font.weight: Font.Black
                        font.letterSpacing: 0.5
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: read("gateway.host", "0.0.0.0")
                placeholderText: "监听主机 / Host"
                onEditingFinished: assign("gateway.host", text)
            }

            GlassField {
                Layout.fillWidth: true
                text: read("gateway.port", 18790).toString()
                placeholderText: "监听端口 / Port"
                onEditingFinished: assign("gateway.port", parseInt(text || "18790"))
            }
        }

        Text {
            Layout.fillWidth: true
            text: "心跳与保活"
            color: Design.Theme.section("gateway").accent
            font.pixelSize: 11
            font.weight: Font.Black
                        font.letterSpacing: 0.5
        }

        NeoCheckBox {
            text: "启用心跳"
            checked: read("gateway.heartbeat.enabled", true)
            onToggled: assign("gateway.heartbeat.enabled", checked)
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 1
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: read("gateway.heartbeat.intervalS", 1800).toString()
                placeholderText: "心跳秒数"
                onEditingFinished: assign("gateway.heartbeat.intervalS", parseInt(text || "1800"))
            }
        }
    }
}
