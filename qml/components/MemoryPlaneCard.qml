import QtQuick 2.14
import QtQuick.Layouts 1.14
import "../theme" as Design

NeoCard {
    id: root
    objectName: "runtimeCard_memoryPlane"
    property var app
    property var studioBridge
    property var memoryModes: []
    property var memoryBackends: []
    readonly property var summarySurface: Design.Theme.surface("summary")
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")
    readonly property var successStatusStyle: Design.Theme.status("success")

    width: parent ? parent.width : 0
    Layout.fillWidth: true
    Layout.minimumHeight: 508
    sectionKey: "memory"
    title: "记忆平面 Memory Plane"
    subtitle: "配置记忆模式,本地 / 集群后端与远端记忆服务"
    titleIconKey: "memory"
    titleIcon: "◍"
    guideText: "选择记忆模式与后端类型;如果接了远端服务,也在这里确认地址,状态和本地优先策略."

    function assign() {
        return app ? app.assign.apply(app, arguments) : undefined;
    }

    function read() {
        return app ? app.read.apply(app, arguments) : undefined;
    }

    function keyedOptionIndex() {
        return app ? app.keyedOptionIndex.apply(app, arguments) : 0;
    }

    function memoryServiceEndpointText() {
        return app ? app.memoryServiceEndpointText.apply(app, arguments) : "";
    }

    function memoryServiceStatusText() {
        return app ? app.memoryServiceStatusText.apply(app, arguments) : "";
    }

    function formColumns(containerWidth) {
        return containerWidth >= 860 ? 2 : 1;
    }

    function compactFieldWidth(containerWidth) {
        if (containerWidth >= 1180) {
            return 320;
        }
        return containerWidth >= 860 ? 280 : -1;
    }

    function compactEndpointWidth(containerWidth) {
        if (containerWidth >= 1180) {
            return 420;
        }
        return containerWidth >= 860 ? 360 : -1;
    }

    function compactMetricRowWidth(containerWidth) {
        if (containerWidth >= 1180) {
            return 248;
        }
        return containerWidth >= 860 ? 228 : -1;
    }

    function compactMetricLabelWidth(containerWidth) {
        return containerWidth >= 860 ? 84 : -1;
    }

    function compactMetricValueWidth(containerWidth) {
        return containerWidth >= 860 ? 76 : -1;
    }

    function maxLayoutWidth(preferredWidth) {
        return preferredWidth > 0 ? preferredWidth : 16777215;
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: memoryPlaneSummary.implicitHeight + 12
            radius: 8
            color: summaryBoxStyle.background
            border.width: 1
            border.color: summaryBoxStyle.border

            ColumnLayout {
                id: memoryPlaneSummary
                x: 10
                y: 10
                width: parent.width - 20
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    text: "当前记忆配置"
                    color: summaryBoxStyle.title
                    font.pixelSize: 11
                    font.weight: Font.Black
                        font.letterSpacing: 0.5
                }

                Text {
                    Layout.fillWidth: true
                    text: "模式  " + read("memory.mode", "legacy") +
                          "  ·  后端  " + read("memory.backend", "legacy") +
                          "  ·  服务  " + memoryServiceStatusText()
                    color: studioBridge && studioBridge.status && studioBridge.status.memoryServiceReachable
                        ? successStatusStyle.text
                        : summaryBoxStyle.text
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: formColumns(parent.width)
            columnSpacing: 12
            rowSpacing: 12

            NeoComboBox {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                Layout.preferredWidth: compactFieldWidth(root.width)
                Layout.maximumWidth: maxLayoutWidth(compactFieldWidth(root.width))
                model: memoryModes
                textRole: "title"
                currentIndex: keyedOptionIndex(memoryModes, read("memory.mode", "legacy"), "legacy")
                onActivated: {
                    if (currentIndex >= 0 && currentIndex < memoryModes.length) {
                        assign("memory.mode", memoryModes[currentIndex].key);
                    }
                }
            }

            NeoComboBox {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                Layout.preferredWidth: compactFieldWidth(root.width)
                Layout.maximumWidth: maxLayoutWidth(compactFieldWidth(root.width))
                model: memoryBackends
                textRole: "title"
                currentIndex: keyedOptionIndex(memoryBackends, read("memory.backend", "legacy"), "legacy")
                onActivated: {
                    if (currentIndex >= 0 && currentIndex < memoryBackends.length) {
                        assign("memory.backend", memoryBackends[currentIndex].key);
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: formColumns(parent.width)
            columnSpacing: 12
            rowSpacing: 12

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                Layout.preferredWidth: compactMetricRowWidth(root.width)
                Layout.maximumWidth: maxLayoutWidth(compactMetricRowWidth(root.width))
                spacing: 8

                Text {
                    Layout.preferredWidth: compactMetricLabelWidth(root.width)
                    Layout.maximumWidth: maxLayoutWidth(compactMetricLabelWidth(root.width))
                    text: "排除条数"
                    color: summaryBoxStyle.title
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                GlassField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: compactMetricValueWidth(root.width)
                    Layout.maximumWidth: maxLayoutWidth(compactMetricValueWidth(root.width))
                    horizontalAlignment: TextInput.AlignHCenter
                    inputMethodHints: Qt.ImhDigitsOnly
                    text: read("memory.recentWindow", 24).toString()
                    placeholderText: "24"
                    onEditingFinished: {
                        var nextWindow = parseInt(text || "24");
                        assign("memory.recentWindow", isNaN(nextWindow) ? 24 : nextWindow);
                    }
                }

                Text {
                    text: "条"
                    color: summaryBoxStyle.meta
                    font.pixelSize: 12
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                Layout.preferredWidth: compactMetricRowWidth(root.width)
                Layout.maximumWidth: maxLayoutWidth(compactMetricRowWidth(root.width))
                spacing: 8

                Text {
                    Layout.preferredWidth: compactMetricLabelWidth(root.width)
                    Layout.maximumWidth: maxLayoutWidth(compactMetricLabelWidth(root.width))
                    text: "召回上限"
                    color: summaryBoxStyle.title
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                GlassField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: compactMetricValueWidth(root.width)
                    Layout.maximumWidth: maxLayoutWidth(compactMetricValueWidth(root.width))
                    horizontalAlignment: TextInput.AlignHCenter
                    inputMethodHints: Qt.ImhDigitsOnly
                    text: read("memory.retrievalTopK", 8).toString()
                    placeholderText: "8"
                    onEditingFinished: {
                        var nextTopK = parseInt(text || "8");
                        assign("memory.retrievalTopK", isNaN(nextTopK) ? 8 : nextTopK);
                    }
                }

                Text {
                    text: "条"
                    color: summaryBoxStyle.meta
                    font.pixelSize: 12
                }
            }
        }

        NeoCheckBox {
            text: "启用每日摘要"
            checked: read("memory.enableDailySummaries", true)
            onToggled: assign("memory.enableDailySummaries", checked)
        }

        NeoCheckBox {
            text: "启用远端记忆服务"
            checked: read("memory.service.enabled", false)
            onToggled: assign("memory.service.enabled", checked)
        }

        GridLayout {
            Layout.fillWidth: true
            columns: formColumns(parent.width)
            columnSpacing: 12
            rowSpacing: 12

            GlassField {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                Layout.preferredWidth: compactEndpointWidth(root.width)
                Layout.maximumWidth: maxLayoutWidth(compactEndpointWidth(root.width))
                text: read("memory.service.endpoint", "http://127.0.0.1:18891")
                placeholderText: "记忆服务端点"
                onEditingFinished: assign("memory.service.endpoint", text)
            }

            GlassField {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                Layout.preferredWidth: compactFieldWidth(root.width)
                Layout.maximumWidth: maxLayoutWidth(compactFieldWidth(root.width))
                text: read("memory.service.timeoutMs", 12000).toString()
                placeholderText: "HTTP 超时毫秒数"
                onEditingFinished: {
                    var nextTimeout = parseInt(text || "12000");
                    assign("memory.service.timeoutMs", isNaN(nextTimeout) ? 12000 : nextTimeout);
                }
            }

            NeoCheckBox {
                Layout.fillWidth: true
                Layout.columnSpan: formColumns(parent.width)
                Layout.alignment: Qt.AlignVCenter
                text: "自动拉起本地记忆服务"
                checked: read("memory.service.autoSpawnLocalService", true)
                onToggled: assign("memory.service.autoSpawnLocalService", checked)
            }
        }

        Text {
            Layout.fillWidth: true
            text: "服务状态： " + memoryServiceStatusText() +
                  "  |  端点： " + memoryServiceEndpointText() +
                  (studioBridge && studioBridge.status && studioBridge.status.memoryServiceAutoSpawn ? "  |  自动拉起：开启" : "  |  自动拉起：关闭")
            color: studioBridge && studioBridge.status && studioBridge.status.memoryServiceReachable
                ? successStatusStyle.text
                : summarySurface.meta
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }
}
