import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../theme" as Design

NeoCard {
    id: root
    objectName: "runtimeCard_delegationDraft"
    property var app
    property var studioBridge
    property var previewBridge
    property string draftSourceLabel: ""
    property string taskText: ""
    property string labelText: ""
    property string targetNodeText: ""
    property string targetRoleText: ""
    property string targetTagsText: ""
    property string requiredToolText: ""
    property string requiredChannelText: ""
    property string requiredMemoryBackendText: ""
    property int activeConstraintCount: (targetNodeText.trim().length > 0 ? 1 : 0) +
                                        (targetRoleText.trim().length > 0 ? 1 : 0) +
                                        (targetTagsText.trim().length > 0 ? 1 : 0) +
                                        (requiredToolText.trim().length > 0 ? 1 : 0) +
                                        (requiredChannelText.trim().length > 0 ? 1 : 0) +
                                        (requiredMemoryBackendText.trim().length > 0 ? 1 : 0)
    property var draftPayload: compactTemplateObject({
        "task": taskText,
        "label": labelText,
        "targetNode": targetNodeText,
        "targetRole": targetRoleText,
        "targetTags": splitCsv(targetTagsText),
        "requiredTool": requiredToolText,
        "requiredChannel": requiredChannelText,
        "requiredMemoryBackend": requiredMemoryBackendText
    })
    property string draftPayloadText: JSON.stringify(draftPayload, null, 2)
    readonly property real payloadViewHeight: Number(delegationDraftPayloadView.height || delegationDraftPayloadView.implicitHeight || 0)
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")
    readonly property var listItemStyle: Design.Theme.listItem("default")
    readonly property var textViewStyle: Design.Theme.textView("default")

    width: parent ? parent.width : 0
    height: width >= 1180 ? 560 : 700
    stretchContent: true
    sectionKey: "draft"
    title: "委托草稿 Delegation Draft"
    subtitle: "把路由预演结果转成可编辑的 Spawn 负载,再继续调整任务,角色,标签和能力约束"
    titleIconKey: "draft"
    titleIcon: "✎"
    guideText: "先从预演带入,再微调任务,节点和能力约束;确认后可直接提交或保存到模板区."

    function compactTemplateObject() {
        return app ? app.compactTemplateObject.apply(app, arguments) : ({});
    }
    function delegationExecutionSessionKey() {
        return app ? app.delegationExecutionSessionKey.apply(app, arguments) : "";
    }
    function delegationTemplateRecord() {
        return app ? app.delegationTemplateRecord.apply(app, arguments) : ({});
    }
    function showToast() {
        return app ? app.showToast.apply(app, arguments) : undefined;
    }
    function spawnTemplateObject() {
        return app ? app.spawnTemplateObject.apply(app, arguments) : ({});
    }
    function splitCsv() {
        return app ? app.splitCsv.apply(app, arguments) : [];
    }
    function formColumns(containerWidth) {
        return containerWidth >= 860 ? 2 : 1;
    }
    function denseFormColumns(containerWidth) {
        if (containerWidth >= 1220) {
            return 3;
        }
        return containerWidth >= 820 ? 2 : 1;
    }

    function resetDraft() {
        draftSourceLabel = "";
        taskText = "";
        labelText = "";
        targetNodeText = "";
        targetRoleText = "";
        targetTagsText = "";
        requiredToolText = "";
        requiredChannelText = "";
        requiredMemoryBackendText = "";
    }

    function seedFromPreview() {
        if (!previewBridge) {
            return;
        }
        var payload = spawnTemplateObject(previewBridge.previewResult,
                                          previewBridge.previewRequest,
                                          previewBridge.selectedCandidate);
        taskText = payload.task || "";
        labelText = payload.label || "";
        targetNodeText = payload.targetNode || "";
        targetRoleText = payload.targetRole || "";
        targetTagsText = (payload.targetTags || []).join(",");
        requiredToolText = payload.requiredTool || "";
        requiredChannelText = payload.requiredChannel || previewBridge.requiredChannel || "";
        requiredMemoryBackendText = payload.requiredMemoryBackend || "";
        draftSourceLabel = previewBridge.previewSourceLabel.length > 0
            ? previewBridge.previewSourceLabel
            : (((previewBridge.selectedCandidate || {}).node || {}).nodeId || "当前预演");
    }

    function previewRequestFromDraft() {
        var previewRequest = previewBridge ? (previewBridge.previewRequest || {}) : ({});
        return {
            "targetRole": targetRoleText,
            "targetTags": splitCsv(targetTagsText),
            "requiredTool": requiredToolText,
            "requiredChannel": requiredChannelText,
            "requiredMemoryBackend": requiredMemoryBackendText,
            "includeOffline": previewBridge ? previewBridge.includeOffline : false,
            "originChannel": previewRequest.originChannel || "gui",
            "originChatId": previewRequest.originChatId || "desktop",
            "sessionKey": previewRequest.sessionKey || "gui:preview",
            "label": labelText || previewRequest.label || "路由预演",
            "task": taskText || previewRequest.task || "预演委托任务",
            "parentTaskId": previewRequest.parentTaskId || "",
            "traceId": previewRequest.traceId || "preview-trace"
        };
    }

    function submitRequestFromDraft() {
        var request = previewRequestFromDraft();
        request.sessionKey = delegationExecutionSessionKey(request.sessionKey,
                                                          request.originChannel,
                                                          request.originChatId);
        request.targetNode = targetNodeText;
        return request;
    }

    function loadTemplate(record) {
        var request = ((record || {}).request || {});
        taskText = request.task || "";
        labelText = request.label || "";
        targetNodeText = request.targetNode || "";
        targetRoleText = request.targetRole || "";
        targetTagsText = (request.targetTags || []).join(",");
        requiredToolText = request.requiredTool || "";
        requiredChannelText = request.requiredChannel || "";
        requiredMemoryBackendText = request.requiredMemoryBackend || "";
        draftSourceLabel = ((record || {}).name || "模板") + "  |  已保存模板";
    }

    function savedTemplateRecord(templateId, templateName, templateNote) {
        return delegationTemplateRecord({
            "id": templateId,
            "name": templateName,
            "kind": "single",
            "note": templateNote,
            "request": draftPayload
        }, true);
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: delegationDraftSummaryColumn.implicitHeight + 22
            radius: 8
            color: summaryBoxStyle.background
            border.width: 1
            border.color: summaryBoxStyle.border

            ColumnLayout {
                id: delegationDraftSummaryColumn
                x: 11
                y: 11
                width: parent.width - 22
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: root.draftSourceLabel.length > 0
                        ? ("来源  " + root.draftSourceLabel)
                        : "来源  尚未带入"
                    color: root.draftSourceLabel.length > 0 ? Design.Theme.section("draft").accent : listItemStyle.text
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: "任务  " + (root.taskText.trim().length > 0 ? "已填写" : "未填写") +
                          "  ·  路由约束  " + String(root.activeConstraintCount) + " 项"
                    color: summaryBoxStyle.text
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            ResponsiveGridStrip {
                Layout.fillWidth: true
                itemCount: 5
                minimumCellWidth: 176
                maximumColumns: 3
                columnSpacing: 10
                rowSpacing: 10

                ActionButton {
                    compact: true
                    text: "从预演带入"
                    enabled: !!previewBridge && !!previewBridge.selectedCandidate
                    onClicked: {
                        root.seedFromPreview();
                        showToast("已带入委托草稿",
                                  "当前候选节点和路由条件已更新到委托草稿.",
                                  "success");
                    }
                }

                ActionButton {
                    compact: true
                    text: "回放预演"
                    enabled: root.taskText.trim().length > 0 ||
                             root.targetRoleText.trim().length > 0 ||
                             root.targetTagsText.trim().length > 0 ||
                             root.requiredToolText.trim().length > 0
                    onClicked: {
                        if (previewBridge) {
                            previewBridge.applyPreviewRequest(
                                root.previewRequestFromDraft(),
                                root.draftSourceLabel.length > 0
                                    ? ("委托草稿  |  " + root.draftSourceLabel)
                                    : "委托草稿");
                        }
                    }
                }

                ActionButton {
                    compact: true
                    text: "直接提交"
                    enabled: !!studioBridge &&
                             !studioBridge.busy &&
                             root.taskText.trim().length > 0
                    onClicked: {
                        var result = studioBridge.submitDelegationRequest(
                            root.submitRequestFromDraft());
                        if (result.ok) {
                            showToast("委托已提交",
                                      result.message || "当前委托草稿已提交到运行时.",
                                      "success");
                        } else {
                            showToast("提交失败",
                                      result.message || result.error || "无法提交当前委托草稿.",
                                      "warning");
                        }
                    }
                }

                ActionButton {
                    compact: true
                    text: "复制 Spawn 负载"
                    enabled: !!studioBridge &&
                             root.taskText.trim().length > 0
                    onClicked: {
                        studioBridge.copyToClipboard(root.draftPayloadText);
                        showToast("已复制 spawn 负载",
                                  "当前委托草稿已经复制到剪贴板,可直接用于 spawn 参数.",
                                  "success");
                    }
                }

                ActionButton {
                    compact: true
                    text: "重置"
                    onClicked: root.resetDraft()
                }
            }
        }

        ResponsiveGridStrip {
            Layout.fillWidth: true
            forcedColumns: formColumns(width)
            itemCount: 2
            minimumCellWidth: 220
            maximumColumns: 2
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: root.taskText
                placeholderText: "任务内容"
                onEditingFinished: root.taskText = text
            }

            GlassField {
                Layout.fillWidth: true
                text: root.labelText
                placeholderText: "标签"
                onEditingFinished: root.labelText = text
            }
        }

        ResponsiveGridStrip {
            Layout.fillWidth: true
            forcedColumns: formColumns(width)
            itemCount: 2
            minimumCellWidth: 220
            maximumColumns: 2
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: root.targetNodeText
                placeholderText: "固定目标节点"
                onEditingFinished: root.targetNodeText = text
            }

            GlassField {
                Layout.fillWidth: true
                text: root.targetRoleText
                placeholderText: "目标角色"
                onEditingFinished: root.targetRoleText = text
            }
        }

        ResponsiveGridStrip {
            Layout.fillWidth: true
            forcedColumns: formColumns(width)
            itemCount: 2
            minimumCellWidth: 220
            maximumColumns: 2
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: root.targetTagsText
                placeholderText: "目标标签"
                onEditingFinished: root.targetTagsText = text
            }

            GlassField {
                Layout.fillWidth: true
                text: root.requiredToolText
                placeholderText: "所需工具"
                onEditingFinished: root.requiredToolText = text
            }
        }

        ResponsiveGridStrip {
            Layout.fillWidth: true
            forcedColumns: denseFormColumns(width)
            itemCount: 2
            minimumCellWidth: 220
            maximumColumns: 2
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: root.requiredChannelText
                placeholderText: "所需频道"
                onEditingFinished: root.requiredChannelText = text
            }

            GlassField {
                Layout.fillWidth: true
                text: root.requiredMemoryBackendText
                placeholderText: "所需记忆后端"
                onEditingFinished: root.requiredMemoryBackendText = text
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.targetNodeText.trim().length > 0
                ? ("固定目标  " + root.targetNodeText +
                   "  |  回放预演时仍会按角色,标签,工具等条件重新排序,不会强制固定节点.")
                : "这份草稿已经可以直接作为 spawn 工具负载.修改角色,标签,工具等条件后,可点击“回放预演”查看新的候选排序."
            color: listItemStyle.text
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }

        ReadOnlyTextView {
            id: delegationDraftPayloadView
            Layout.fillWidth: true
            Layout.fillHeight: true
            wrapMode: TextEdit.WrapAnywhere
            textFormat: TextEdit.PlainText
            textColor: textViewStyle.text
            text: root.draftPayloadText
        }
    }
}
