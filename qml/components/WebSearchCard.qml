import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../theme" as Design

NeoCard {
    id: root
    objectName: "runtimeCard_webSearch"
    property var app
    property var webSearchProviders: []
    readonly property int formColumns: webSearchHeaderGrid.columns
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")
    readonly property var listItemStyle: Design.Theme.listItem("default")

    width: parent ? parent.width : 0
    Layout.minimumHeight: 380
    sectionKey: "routing"
    title: "网页工具 Web Search"
    subtitle: "同步原码的多搜索源与 Jina Reader 抓取链路，配置 tools.web.search 与代理"
    titleIconKey: "search"
    titleIcon: "⌕"
    guideText: "先选默认搜索提供方，再按需填写 API Key、Base URL 和代理；这里会影响所有网页搜索和抓取。"

    function assign() {
        return app ? app.assign.apply(app, arguments) : undefined;
    }
    function read() {
        return app ? app.read.apply(app, arguments) : undefined;
    }
    function providerIndex() {
        return app ? app.webSearchProviderIndex.apply(app, arguments) : 0;
    }
    function formColumnCount(containerWidth) {
        return containerWidth >= 860 ? 2 : 1;
    }

    Column {
        width: parent.width
        spacing: 12

        Rectangle {
            width: parent.width
            implicitHeight: webSearchSummaryColumn.implicitHeight + 22
            radius: 8
            color: summaryBoxStyle.background
            border.width: 1
            border.color: summaryBoxStyle.border

            Column {
                id: webSearchSummaryColumn
                x: 11
                y: 11
                width: parent.width - 22
                spacing: 6

                Text {
                    width: parent.width
                    text: "当前提供方  " + read("tools.web.search.provider", "brave") +
                          "  ·  默认结果数  " + read("tools.web.search.maxResults", 5).toString()
                    color: summaryBoxStyle.title
                    font.pixelSize: Design.Foundation.textMd
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: ((read("tools.web.proxy", "") || "").length > 0)
                        ? ("代理已配置  " + read("tools.web.proxy", ""))
                        : "当前未配置代理,默认直接访问搜索 and 抓取服务."
                    color: summaryBoxStyle.text
                    font.pixelSize: Design.Foundation.textMd
                    wrapMode: Text.WordWrap
                }
            }
        }

        ResponsiveGridStrip {
            id: webSearchHeaderGrid
            width: parent.width
            forcedColumns: formColumnCount(width)
            itemCount: 2
            minimumCellWidth: 220
            maximumColumns: 2
            columnSpacing: 12
            rowSpacing: 12

            NeoComboBox {
                Layout.fillWidth: true
                model: webSearchProviders
                textRole: "title"
                currentIndex: providerIndex(read("tools.web.search.provider", "brave"))
                onActivated: assign("tools.web.search.provider", webSearchProviders[currentIndex].key)
            }

            GlassField {
                Layout.fillWidth: true
                text: read("tools.web.search.maxResults", 5).toString()
                placeholderText: "默认搜索结果数,例如 5"
                onEditingFinished: {
                    var nextCount = parseInt(text || "5");
                    assign("tools.web.search.maxResults", isNaN(nextCount) ? 5 : nextCount);
                }
            }
        }

        GlassField {
            width: parent.width
            text: read("tools.web.search.apiKey", "")
            placeholderText: "搜索 API Key;未配置时 Brave / Tavily / Jina 会回退到 DuckDuckGo"
            echoMode: TextInput.Password
            onEditingFinished: assign("tools.web.search.apiKey", text)
        }

        GlassField {
            width: parent.width
            text: read("tools.web.search.baseUrl", "")
            placeholderText: "SearXNG 地址 / Base URL,例如 https://searx.example.com"
            onEditingFinished: assign("tools.web.search.baseUrl", text)
        }

        GlassField {
            width: parent.width
            text: read("tools.web.proxy", "")
            placeholderText: "可选 HTTP 代理,例如 http://127.0.0.1:7890"
            onEditingFinished: assign("tools.web.proxy", text)
        }

        Text {
            width: parent.width
            text: "web_search 支持 Brave,DuckDuckGo,Tavily,SearXNG,Jina.web_fetch 会先尝试 Jina Reader,再回退 HTML 提取."
            color: listItemStyle.meta
            font.pixelSize: Design.Foundation.textMd
            wrapMode: Text.WordWrap
        }
    }
}
