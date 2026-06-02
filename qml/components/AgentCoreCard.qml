import QtQuick 2.14
import QtQuick.Layouts 1.14
import "../theme" as Design

NeoCard {
    id: root
    objectName: "runtimeCard_agentCore"
    property var app

    width: parent ? parent.width : 0
    Layout.fillWidth: true
    Layout.minimumHeight: 348
    sectionKey: "runtime"
    title: "代理核心 Agent Core"
    subtitle: "工作区与模型默认参数"
    titleIconKey: "mcp"
    titleIcon: "⌘"
    guideText: "这里维护默认工作区和推理参数;改完后会影响新会话,新任务和工具执行时的默认上下文."

    function assign() {
        return app ? app.assign.apply(app, arguments) : undefined;
    }

    function read() {
        return app ? app.read.apply(app, arguments) : undefined;
    }

    Column {
        width: parent.width
        spacing: 12

        Text {
            width: parent.width
            text: "默认上下文"
            color: Design.Theme.section("runtime").accent
            font.pixelSize: Design.Foundation.textSm
            font.weight: Font.Black
                        font.letterSpacing: 0.5
        }

        GlassField {
            width: parent.width
            text: read("agents.defaults.workspace", "")
            placeholderText: "工作区路径"
            onEditingFinished: assign("agents.defaults.workspace", text)
        }

        Text {
            width: parent.width
            text: "推理参数"
            color: Design.Theme.section("runtime").accent
            font.pixelSize: Design.Foundation.textSm
            font.weight: Font.Black
                        font.letterSpacing: 0.5
        }

        GridLayout {
            width: parent.width
            columns: 3
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: read("agents.defaults.maxTokens", 8192).toString()
                placeholderText: "最大令牌数 / Max Tokens"
                onEditingFinished: assign("agents.defaults.maxTokens", parseInt(text || "8192"))
            }

            GlassField {
                Layout.fillWidth: true
                text: read("agents.defaults.temperature", 0.1).toString()
                placeholderText: "温度 Temperature"
                onEditingFinished: assign("agents.defaults.temperature", Number(text || "0.1"))
            }

            GlassField {
                Layout.fillWidth: true
                text: read("agents.defaults.reasoningEffort", "")
                placeholderText: "推理强度"
                onEditingFinished: assign("agents.defaults.reasoningEffort", text)
            }
        }
    }
}
