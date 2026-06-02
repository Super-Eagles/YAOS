import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../theme" as Design

NeoCard {
    id: root
    objectName: "runtimeCard_batchDraft"
    property var app
    property var studioBridge
    property var previewBridge
    property string draftSourceLabel: ""
    property string groupLabelText: ""
    property string targetNodeText: ""
    property string targetRoleText: ""
    property string targetTagsText: ""
    property string requiredToolText: ""
    property string requiredChannelText: ""
    property string requiredMemoryBackendText: ""
    property var taskDrafts: []
    property int activeConstraintCount: (targetNodeText.trim().length > 0 ? 1 : 0) +
                                        (targetRoleText.trim().length > 0 ? 1 : 0) +
                                        (targetTagsText.trim().length > 0 ? 1 : 0) +
                                        (requiredToolText.trim().length > 0 ? 1 : 0) +
                                        (requiredChannelText.trim().length > 0 ? 1 : 0) +
                                        (requiredMemoryBackendText.trim().length > 0 ? 1 : 0)
    property string taskExportText: batchTaskLinesFromDraftTasks(taskDrafts)
    property var draftPayload: batchSpawnTemplateObject(
        groupLabelText,
        targetNodeText,
        targetRoleText,
        targetTagsText,
        requiredToolText,
        requiredChannelText,
        requiredMemoryBackendText,
        taskDrafts)
    property string draftPayloadText: JSON.stringify(draftPayload, null, 2)
    readonly property real taskListHeight: Number(batchDraftTaskListFrame.height || batchDraftTaskListFrame.implicitHeight || 0)
    readonly property real exportViewHeight: Number(batchDraftExportView.height || batchDraftExportView.implicitHeight || 0)
    readonly property real payloadViewHeight: Number(batchDraftPayloadView.height || batchDraftPayloadView.implicitHeight || 0)
    readonly property var summaryBoxStyle: Design.Theme.summaryBox("default")
    readonly property var detailSummaryBoxStyle: Design.Theme.summaryBox("alt")
    readonly property var listItemStyle: Design.Theme.listItem("default")
    readonly property var textViewStyle: Design.Theme.textView("default")

    width: parent ? parent.width : 0
    height: width >= 1180 ? 980 : 1220
    stretchContent: true
    sectionKey: "draft"
    title: "批量委托草稿 Batch Draft"
    subtitle: "编辑 `tasks[] + groupLabel` 的批量 Spawn 负载;每个子任务都可以单独覆盖节点,角色,标签,工具和记忆后端"
    titleIconKey: "resources"
    titleIcon: "⌬"
    guideText: "先带入预演或手动新增子任务,再按组补齐默认节点,角色和工具;确认无误后可直接提交整批委托."

    function batchSpawnTemplateObject() {
        return app ? app.batchSpawnTemplateObject.apply(app, arguments) : ({});
    }
    function batchTaskLinesFromDraftTasks() {
        return app ? app.batchTaskLinesFromDraftTasks.apply(app, arguments) : "";
    }
    function batchTaskOverrideSummary() {
        return app ? app.batchTaskOverrideSummary.apply(app, arguments) : "";
    }
    function cloneBatchDraftTasks() {
        return app ? app.cloneBatchDraftTasks.apply(app, arguments) : [];
    }
    function delegationExecutionSessionKey() {
        return app ? app.delegationExecutionSessionKey.apply(app, arguments) : "";
    }
    function delegationTemplateRecord() {
        return app ? app.delegationTemplateRecord.apply(app, arguments) : ({});
    }
    function firstNonEmptyValue() {
        return app ? app.firstNonEmptyValue.apply(app, arguments) : "";
    }
    function normalizedTagList() {
        return app ? app.normalizedTagList.apply(app, arguments) : [];
    }
    function normalizeBatchDraftTasks() {
        return app ? app.normalizeBatchDraftTasks.apply(app, arguments) : [];
    }
    function routingRequestFromTaskGroup() {
        return app ? app.routingRequestFromTaskGroup.apply(app, arguments) : ({});
    }
    function routingSeedLabelFromTaskGroup() {
        return app ? app.routingSeedLabelFromTaskGroup.apply(app, arguments) : "";
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
    function taskBatchDraftObject() {
        return app ? app.taskBatchDraftObject.apply(app, arguments) : ({});
    }
    function denseFormColumns(containerWidth) {
        if (containerWidth >= 1220) {
            return 3;
        }
        return containerWidth >= 820 ? 2 : 1;
    }
    function ultraDenseFormColumns(containerWidth) {
        if (containerWidth >= 1360) {
            return 4;
        }
        return containerWidth >= 980 ? 2 : 1;
    }

    function resetDraft() {
        draftSourceLabel = "";
        groupLabelText = "";
        targetNodeText = "";
        targetRoleText = "";
        targetTagsText = "";
        requiredToolText = "";
        requiredChannelText = "";
        requiredMemoryBackendText = "";
        taskDrafts = [];
    }

    function setTaskDrafts(tasks) {
        taskDrafts = cloneBatchDraftTasks(tasks);
    }

    function appendTaskDraft(seed) {
        var next = cloneBatchDraftTasks(taskDrafts);
        var seeded = taskBatchDraftObject(seed || {});
        next.push({
            "label": String(seeded.label || ("task_" + String(next.length + 1))),
            "task": String(seeded.task || ""),
            "targetNode": String(seeded.targetNode || ""),
            "targetRole": String(seeded.targetRole || ""),
            "targetTags": normalizedTagList(seeded.targetTags || []),
            "requiredTool": String(seeded.requiredTool || ""),
            "requiredChannel": String(seeded.requiredChannel || ""),
            "requiredMemoryBackend": String(seeded.requiredMemoryBackend || "")
        });
        taskDrafts = next;
    }

    function updateTaskDraft(index, key, value) {
        if (index < 0 || index >= taskDrafts.length) {
            return;
        }
        var next = cloneBatchDraftTasks(taskDrafts);
        if (key === "targetTags") {
            next[index][key] = normalizedTagList(value || []);
        } else {
            next[index][key] = String(value || "");
        }
        taskDrafts = next;
    }

    function removeTaskDraft(index) {
        if (index < 0 || index >= taskDrafts.length) {
            return;
        }
        var next = cloneBatchDraftTasks(taskDrafts);
        next.splice(index, 1);
        taskDrafts = next;
    }

    function seedFromPreview() {
        if (!previewBridge) {
            return;
        }
        var payload = spawnTemplateObject(previewBridge.previewResult,
                                          previewBridge.previewRequest,
                                          previewBridge.selectedCandidate);
        groupLabelText = firstNonEmptyValue([
            (previewBridge.previewResult || {}).label,
            (previewBridge.previewRequest || {}).label
        ], "委托批次");
        targetNodeText = payload.targetNode || "";
        targetRoleText = payload.targetRole || "";
        targetTagsText = (payload.targetTags || []).join(",");
        requiredToolText = payload.requiredTool || "";
        requiredChannelText = payload.requiredChannel || previewBridge.requiredChannel || "";
        requiredMemoryBackendText = payload.requiredMemoryBackend || "";
        if (normalizeBatchDraftTasks(taskDrafts).length === 0) {
            setTaskDrafts([taskBatchDraftObject(payload)]);
        }
        draftSourceLabel = previewBridge.previewSourceLabel.length > 0
            ? previewBridge.previewSourceLabel
            : (((previewBridge.selectedCandidate || {}).node || {}).nodeId || "当前预演");
    }

    function seedFromTaskGroup(group) {
        var item = group || {};
        var request = routingRequestFromTaskGroup(item);
        groupLabelText = firstNonEmptyValue([
            item.title,
            item.rootId
        ], "委托批次");
        targetNodeText = firstNonEmptyValue(item.targetNodes || [], "");
        targetRoleText = request.targetRole || "";
        targetTagsText = (request.targetTags || []).join(",");
        requiredToolText = request.requiredTool || "";
        requiredChannelText = request.requiredChannel || "";
        requiredMemoryBackendText = request.requiredMemoryBackend || "";
        setTaskDrafts(item.tasks || []);
        draftSourceLabel = routingSeedLabelFromTaskGroup(item);
    }

    function previewRequestFromDraft() {
        var normalizedTasks = normalizeBatchDraftTasks(taskDrafts);
        var previewTask = normalizedTasks.length > 0
            ? (normalizedTasks[0].task || groupLabelText || "预演委托批次")
            : (groupLabelText || "预演委托批次");
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
            "label": groupLabelText || "批量路由预演",
            "task": previewTask,
            "parentTaskId": previewRequest.parentTaskId || "",
            "traceId": previewRequest.traceId || "preview-trace"
        };
    }

    function submitRequestFromDraft() {
        var request = previewRequestFromDraft();
        request.sessionKey = delegationExecutionSessionKey(request.sessionKey,
                                                          request.originChannel,
                                                          request.originChatId);
        request.groupLabel = groupLabelText;
        request.targetNode = targetNodeText;
        request.tasks = normalizeBatchDraftTasks(taskDrafts);
        return request;
    }

    function loadTemplate(record) {
        var request = ((record || {}).request || {});
        groupLabelText = request.groupLabel || request.label || "";
        targetNodeText = request.targetNode || "";
        targetRoleText = request.targetRole || "";
        targetTagsText = (request.targetTags || []).join(",");
        requiredToolText = request.requiredTool || "";
        requiredChannelText = request.requiredChannel || "";
        requiredMemoryBackendText = request.requiredMemoryBackend || "";
        setTaskDrafts(request.tasks || []);
        draftSourceLabel = ((record || {}).name || "模板") + "  |  已保存模板";
    }

    function savedTemplateRecord(templateId, templateName, templateNote) {
        return delegationTemplateRecord({
            "id": templateId,
            "name": templateName,
            "kind": "batch",
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
            implicitHeight: batchDraftSummaryColumn.implicitHeight + 22
            radius: 8
            color: summaryBoxStyle.background
            border.width: 1
            border.color: summaryBoxStyle.border

            Column {
                id: batchDraftSummaryColumn
                x: 11
                y: 11
                width: parent.width - 22
                spacing: 6

                Text {
                    width: parent.width
                    text: root.draftSourceLabel.length > 0
                        ? ("来源  " + root.draftSourceLabel)
                        : "来源  尚未带入"
                    color: root.draftSourceLabel.length > 0 ? Design.Theme.section("draft").accent : listItemStyle.text
                    font.pixelSize: Design.Foundation.textMd
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: "批次标签  " +
                          (root.groupLabelText.trim().length > 0 ? root.groupLabelText : "未填写") +
                          "  ·  子任务  " + String(root.taskDrafts.length) +
                          "  ·  默认约束  " + String(root.activeConstraintCount) + " 项"
                    color: summaryBoxStyle.text
                    font.pixelSize: Design.Foundation.textMd
                    wrapMode: Text.WordWrap
                }
            }
        }

        ResponsiveGridStrip {
            Layout.fillWidth: true
            itemCount: 7
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
                    showToast("已带入批量草稿",
                              "当前候选节点和共享路由条件已更新到批量委托草稿.",
                              "success");
                }
            }

            ActionButton {
                compact: true
                text: "回放预演"
                enabled: normalizeBatchDraftTasks(root.taskDrafts).length > 0 ||
                         root.targetRoleText.trim().length > 0 ||
                         root.targetTagsText.trim().length > 0 ||
                         root.requiredToolText.trim().length > 0
                onClicked: {
                    if (previewBridge) {
                        previewBridge.applyPreviewRequest(
                            root.previewRequestFromDraft(),
                            root.draftSourceLabel.length > 0
                                ? ("批量草稿  |  " + root.draftSourceLabel)
                                : "批量草稿");
                    }
                }
            }

            ActionButton {
                compact: true
                text: "新增子任务"
                onClicked: root.appendTaskDraft()
            }

            ActionButton {
                compact: true
                text: "提交批量任务"
                enabled: !!studioBridge &&
                         !studioBridge.busy &&
                         normalizeBatchDraftTasks(root.taskDrafts).length > 0
                onClicked: {
                    var result = studioBridge.submitDelegationRequest(
                        root.submitRequestFromDraft());
                    if (result.ok) {
                        showToast("批量委托已提交",
                                  result.message || "当前批量委托草稿已提交到运行时.",
                                  "success");
                    } else {
                        showToast("提交失败",
                                  result.message || result.error || "无法提交当前批量委托草稿.",
                                  "warning");
                    }
                }
            }

            ActionButton {
                compact: true
                text: "复制批量负载"
                enabled: !!studioBridge &&
                         normalizeBatchDraftTasks(root.taskDrafts).length > 0
                onClicked: {
                    studioBridge.copyToClipboard(root.draftPayloadText);
                    showToast("已复制批量 Spawn 负载",
                              "当前批量委托草稿已经复制到剪贴板,可直接用于 `tasks[] + groupLabel`.",
                              "success");
                }
            }

            ActionButton {
                compact: true
                text: "复制任务行"
                enabled: !!studioBridge &&
                         root.taskExportText.trim().length > 0
                onClicked: {
                    studioBridge.copyToClipboard(root.taskExportText);
                    showToast("已复制批量任务摘要",
                              "结构化子任务清单已导出为只读文本摘要,便于复查和共享.",
                              "success");
                }
            }

            ActionButton {
                compact: true
                text: "重置"
                onClicked: root.resetDraft()
            }
        }

        ResponsiveGridStrip {
            Layout.fillWidth: true
            forcedColumns: ultraDenseFormColumns(width)
            itemCount: 3
            minimumCellWidth: 220
            maximumColumns: 3
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: root.groupLabelText
                placeholderText: "组标签"
                onEditingFinished: root.groupLabelText = text
            }

            GlassField {
                Layout.fillWidth: true
                text: root.targetNodeText
                placeholderText: "默认目标节点"
                onEditingFinished: root.targetNodeText = text
            }

            GlassField {
                Layout.fillWidth: true
                text: root.targetRoleText
                placeholderText: "默认目标角色"
                onEditingFinished: root.targetRoleText = text
            }
        }

        ResponsiveGridStrip {
            Layout.fillWidth: true
            forcedColumns: ultraDenseFormColumns(width)
            itemCount: 4
            minimumCellWidth: 220
            maximumColumns: 4
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: root.targetTagsText
                placeholderText: "默认目标标签"
                onEditingFinished: root.targetTagsText = text
            }

            GlassField {
                Layout.fillWidth: true
                text: root.requiredToolText
                placeholderText: "默认所需工具"
                onEditingFinished: root.requiredToolText = text
            }

            GlassField {
                Layout.fillWidth: true
                text: root.requiredChannelText
                placeholderText: "默认所需频道"
                onEditingFinished: root.requiredChannelText = text
            }

            GlassField {
                Layout.fillWidth: true
                text: root.requiredMemoryBackendText
                placeholderText: "默认记忆后端"
                onEditingFinished: root.requiredMemoryBackendText = text
            }
        }

        Text {
            Layout.fillWidth: true
            text: "顶层 targetNode / targetRole / tags / tool / memory 会作为整组默认值;每个子任务都可以继续单独覆盖这些路由条件."
            color: listItemStyle.text
            font.pixelSize: Design.Foundation.textSm
            wrapMode: Text.WordWrap
        }

        Rectangle {
            id: batchDraftTaskListFrame
            Layout.fillWidth: true
            Layout.preferredHeight: 320
            radius: 8
            clip: true
            color: batchDraftTaskBlankArea.containsMouse ? detailSummaryBoxStyle.background : textViewStyle.outer
            border.width: 1
            border.color: batchDraftTaskBlankArea.containsMouse ? Design.Theme.palette.accentBlue : textViewStyle.border

            Flickable {
                id: batchDraftTaskListView
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                contentWidth: width
                contentHeight: batchDraftTaskListContent.height
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                interactive: contentHeight > height

                ScrollBar.vertical: ScrollBar {
                    id: batchDraftTaskListScrollBar
                    policy: batchDraftTaskListView.contentHeight > batchDraftTaskListView.height
                        ? ScrollBar.AsNeeded
                        : ScrollBar.AlwaysOff
                }

                Item {
                    id: batchDraftTaskListContent
                    width: batchDraftTaskListView.width -
                           (batchDraftTaskListScrollBar.visible
                                ? batchDraftTaskListScrollBar.width + 6
                                : 0)
                    height: Math.max(batchDraftTaskListView.height, batchDraftTaskColumn.implicitHeight)

                    MouseArea {
                        id: batchDraftTaskBlankArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                        onWheel: {
                            var maxY = Math.max(0, batchDraftTaskListView.contentHeight - batchDraftTaskListView.height);
                            if (maxY <= 0) {
                                wheel.accepted = false;
                                return;
                            }

                            var delta = wheel.pixelDelta.y;
                            if (delta === 0) {
                                delta = wheel.angleDelta.y / 3;
                            }
                            if (delta === 0) {
                                wheel.accepted = false;
                                return;
                            }

                            var nextY = batchDraftTaskListView.contentY - delta;
                            if (nextY < 0) {
                                nextY = 0;
                            } else if (nextY > maxY) {
                                nextY = maxY;
                            }

                            if (nextY === batchDraftTaskListView.contentY) {
                                wheel.accepted = false;
                                return;
                            }

                            batchDraftTaskListView.contentY = nextY;
                            wheel.accepted = true;
                        }
                    }

                    Column {
                        id: batchDraftTaskColumn
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: root.taskDrafts

                            delegate: Rectangle {
                                width: parent.width
                                radius: 8
                                color: listItemStyle.background
                                border.width: 1
                                border.color: listItemStyle.border
                                implicitHeight: taskDraftColumn.implicitHeight + 24

                                Column {
                                    id: taskDraftColumn
                                    width: parent.width - 24
                                    anchors.left: parent.left
                                    anchors.leftMargin: 12
                                    anchors.top: parent.top
                                    anchors.topMargin: 12
                                    spacing: 8

                                    GridLayout {
                                        width: parent.width
                                        columns: parent.width >= 900 ? 3 : 1
                                        columnSpacing: 8
                                        rowSpacing: 8

                                        GlassField {
                                            Layout.fillWidth: true
                                            text: modelData.label || ""
                                            placeholderText: "标签"
                                            onEditingFinished: root.updateTaskDraft(index, "label", text)
                                        }

                                        GlassField {
                                            Layout.fillWidth: true
                                            text: modelData.task || ""
                                            placeholderText: "任务内容"
                                            onEditingFinished: root.updateTaskDraft(index, "task", text)
                                        }

                                        ActionButton {
                                            Layout.fillWidth: parent.width < 900
                                            compact: true
                                            text: "删除"
                                            tone: "danger"
                                            onClicked: root.removeTaskDraft(index)
                                        }
                                    }

                                    GridLayout {
                                        width: parent.width
                                        columns: denseFormColumns(parent.width)
                                        columnSpacing: 8
                                        rowSpacing: 8

                                        GlassField {
                                            Layout.fillWidth: true
                                            text: modelData.targetNode || ""
                                            placeholderText: "子任务目标节点覆盖"
                                            onEditingFinished: root.updateTaskDraft(index, "targetNode", text)
                                        }

                                        GlassField {
                                            Layout.fillWidth: true
                                            text: modelData.targetRole || ""
                                            placeholderText: "子任务目标角色覆盖"
                                            onEditingFinished: root.updateTaskDraft(index, "targetRole", text)
                                        }

                                        GlassField {
                                            Layout.fillWidth: true
                                            text: (modelData.targetTags || []).join(",")
                                            placeholderText: "子任务目标标签覆盖"
                                            onEditingFinished: root.updateTaskDraft(index, "targetTags", splitCsv(text))
                                        }
                                    }

                                    GridLayout {
                                        width: parent.width
                                        columns: denseFormColumns(parent.width)
                                        columnSpacing: 8
                                        rowSpacing: 8

                                        GlassField {
                                            Layout.fillWidth: true
                                            text: modelData.requiredTool || ""
                                            placeholderText: "子任务所需工具覆盖"
                                            onEditingFinished: root.updateTaskDraft(index, "requiredTool", text)
                                        }

                                        GlassField {
                                            Layout.fillWidth: true
                                            text: modelData.requiredChannel || ""
                                            placeholderText: "子任务所需频道覆盖"
                                            onEditingFinished: root.updateTaskDraft(index, "requiredChannel", text)
                                        }

                                        GlassField {
                                            Layout.fillWidth: true
                                            text: modelData.requiredMemoryBackend || ""
                                            placeholderText: "子任务记忆后端覆盖"
                                            onEditingFinished: root.updateTaskDraft(index, "requiredMemoryBackend", text)
                                        }
                                    }

                                    Text {
                                        width: parent.width
                                        text: batchTaskOverrideSummary(modelData)
                                        color: listItemStyle.text
                                        font.pixelSize: Design.Foundation.textSm
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }

                        Text {
                            width: parent.width
                            visible: root.taskDrafts.length === 0
                            text: "还没有子任务.你可以从当前预演或任务树带入,也可以手动新增结构化子任务."
                            color: listItemStyle.meta
                            font.pixelSize: Design.Foundation.textMd
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: "只读导出摘要"
            color: listItemStyle.text
            font.pixelSize: Design.Foundation.textSm
        }

        ReadOnlyTextView {
            id: batchDraftExportView
            Layout.fillWidth: true
            Layout.preferredHeight: 74
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.PlainText
            textColor: textViewStyle.text
            text: root.taskExportText
        }

        ReadOnlyTextView {
            id: batchDraftPayloadView
            Layout.fillWidth: true
            Layout.fillHeight: true
            wrapMode: TextEdit.WrapAnywhere
            textFormat: TextEdit.PlainText
            textColor: textViewStyle.text
            text: root.draftPayloadText
        }
    }
}
