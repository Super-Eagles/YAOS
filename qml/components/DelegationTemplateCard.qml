import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../theme" as Design

NeoCard {
    id: root
    objectName: "runtimeCard_delegationTemplates"
    property var app
    property var studioBridge
    property var delegationDraftCard
    property var batchDelegationDraftCard
    property var previewBridge
    property var delegationTemplateKinds: app ? app.delegationTemplateKinds : []
    property string selectedTemplateId: ""
    property string templateNameText: ""
    property string templateNoteText: ""
    property string templateKind: "single"
    property string transferText: ""
    property var templates: delegationTemplateList() || []
    property var selectedTemplate: delegationTemplateById(selectedTemplateId)
    readonly property real transferEditorHeight: Number(templateTransferEditor.height || templateTransferEditor.implicitHeight || 0)
    readonly property real listScrollHeight: Number(delegationTemplateListScroll.height || delegationTemplateListScroll.implicitHeight || 0)
    readonly property var listItemStyle: Design.Theme.listItem("default")
    readonly property var strongSelectedListItemStyle: Design.Theme.listItem("selected-strong")
    readonly property var textViewStyle: Design.Theme.textView("default")

    width: parent ? parent.width : 0
    height: width >= 1180 ? 780 : 980
    stretchContent: true
    sectionKey: "template"
    title: "委托模板 Delegation Templates"
    subtitle: "把常用单任务和批量委托固化进配置;保存模板时会同步写回当前配置"
    titleIconKey: "template"
    titleIcon: "▣"
    guideText: "适合沉淀常用委托套路;选中模板后可直接预演,执行,导出或同步到控制平面"

    function delegationTemplateById() {
        return app ? app.delegationTemplateById.apply(app, arguments) : null;
    }
    function delegationTemplateExportText() {
        return app ? app.delegationTemplateExportText.apply(app, arguments) : "";
    }
    function delegationTemplateKindIndex() {
        return app ? app.delegationTemplateKindIndex.apply(app, arguments) : 0;
    }
    function delegationTemplateKindTitle() {
        return app ? app.delegationTemplateKindTitle.apply(app, arguments) : "单任务草稿";
    }
    function delegationTemplateList() {
        return app ? app.delegationTemplateList.apply(app, arguments) : [];
    }
    function delegationTemplateSummary() {
        return app ? app.delegationTemplateSummary.apply(app, arguments) : "";
    }
    function deleteDelegationTemplate() {
        return app ? app.deleteDelegationTemplate.apply(app, arguments) : false;
    }
    function formatIsoDateTime() {
        return app ? app.formatIsoDateTime.apply(app, arguments) : "";
    }
    function importDelegationTemplateText() {
        return app ? app.importDelegationTemplateText.apply(app, arguments) : {"ok": false, "error": "app unavailable"};
    }
    function normalizeBatchDraftTasks() {
        return app ? app.normalizeBatchDraftTasks.apply(app, arguments) : [];
    }
    function pageListModel() {
        return app ? app.pageListModel.apply(app, arguments) : [];
    }
    function routingRequestFromDelegationTemplate() {
        return app ? app.routingRequestFromDelegationTemplate.apply(app, arguments) : ({});
    }
    function showToast() {
        return app ? app.showToast.apply(app, arguments) : undefined;
    }
    function submissionRequestFromDelegationTemplate() {
        return app ? app.submissionRequestFromDelegationTemplate.apply(app, arguments) : ({});
    }
    function upsertDelegationTemplate() {
        return app ? app.upsertDelegationTemplate.apply(app, arguments) : null;
    }
    function formColumns(containerWidth) {
        return containerWidth >= 860 ? 2 : 1;
    }

    function resetEditor() {
        selectedTemplateId = "";
        templateNameText = "";
        templateNoteText = "";
        templateKind = "single";
    }

    function selectTemplate(record) {
        var item = record || null;
        if (!item) {
            resetEditor();
            return;
        }
        selectedTemplateId = item.id || "";
        templateNameText = item.name || "";
        templateNoteText = item.note || "";
        templateKind = item.kind || "single";
    }

    function saveFromSingleDraft() {
        if (!delegationDraftCard || delegationDraftCard.taskText.trim().length === 0) {
            showToast("无法保存模板",
                      "先填写一条可执行的委托草稿,再保存为模板.",
                      "warning");
            return;
        }
        templateKind = "single";
        var templateId = (!!selectedTemplate && selectedTemplate.kind === "single")
            ? selectedTemplateId
            : "";
        var saved = upsertDelegationTemplate(
            delegationDraftCard.savedTemplateRecord(
                templateId,
                templateNameText,
                templateNoteText));
        if (saved) {
            selectTemplate(saved);
            showToast("模板已保存",
                      saved.name + " 已写入 memory.delegationTemplates.",
                      "success");
        }
    }

    function saveFromBatchDraft() {
        if (!batchDelegationDraftCard ||
                normalizeBatchDraftTasks(batchDelegationDraftCard.taskDrafts).length === 0) {
            showToast("无法保存模板",
                      "先准备一组批量草稿子任务,再保存为模板.",
                      "warning");
            return;
        }
        templateKind = "batch";
        var templateId = (!!selectedTemplate && selectedTemplate.kind === "batch")
            ? selectedTemplateId
            : "";
        var saved = upsertDelegationTemplate(
            batchDelegationDraftCard.savedTemplateRecord(
                templateId,
                templateNameText,
                templateNoteText));
        if (saved) {
            selectTemplate(saved);
            showToast("批量模板已保存",
                      saved.name + " 已写入 memory.delegationTemplates.",
                      "success");
        }
    }

    function applyToSingle() {
        if (!selectedTemplate || selectedTemplate.kind !== "single" || !delegationDraftCard) {
            return;
        }
        delegationDraftCard.loadTemplate(selectedTemplate);
        showToast("模板已带入草稿",
                  "当前单任务模板已回填到委托草稿.",
                  "success");
    }

    function applyToBatch() {
        if (!selectedTemplate || selectedTemplate.kind !== "batch" || !batchDelegationDraftCard) {
            return;
        }
        batchDelegationDraftCard.loadTemplate(selectedTemplate);
        showToast("模板已带入草稿",
                  "当前批量模板已回填到批量委托草稿.",
                  "success");
    }

    function deleteCurrent() {
        if (selectedTemplateId.length === 0) {
            return;
        }
        var removedId = selectedTemplateId;
        if (deleteDelegationTemplate(removedId)) {
            resetEditor();
            showToast("模板已删除",
                      removedId + " 已从 memory.delegationTemplates 移除.",
                      "neutral");
        } else {
            showToast("删除失败",
                      "当前模板没有成功从配置中移除.",
                      "warning");
        }
    }

    function previewCurrent() {
        if (!selectedTemplate || !previewBridge) {
            return;
        }
        previewBridge.applyPreviewRequest(
            routingRequestFromDelegationTemplate(selectedTemplate),
            "模板  |  " + (selectedTemplate.name || selectedTemplate.id || "模板"));
    }

    function submitCurrent() {
        if (!selectedTemplate || !studioBridge || studioBridge.busy) {
            return;
        }
        var result = studioBridge.submitDelegationRequest(
            submissionRequestFromDelegationTemplate(selectedTemplate));
        if (result.ok) {
            showToast("模板已提交",
                      result.message || "当前委托模板已直接提交到运行时.",
                      "success");
        } else {
            showToast("提交失败",
                      result.message || result.error || "无法提交当前委托模板.",
                      "warning");
        }
    }

    function exportSelectedToTransfer() {
        if (!selectedTemplate) {
            showToast("无可导出模板",
                      "先在列表里选中一个委托模板,再导出到传输区.",
                      "warning");
            return;
        }
        transferText = delegationTemplateExportText([selectedTemplate]);
    }

    function exportAllToTransfer() {
        if ((templates || []).length === 0) {
            showToast("无可导出模板",
                      "当前配置中还没有已保存的委托模板.",
                      "warning");
            return;
        }
        transferText = delegationTemplateExportText(templates || []);
    }

    function copyTransferText() {
        if (transferText.trim().length === 0) {
            showToast("无可复制内容",
                      "先导出模板或在 transfer 区粘贴 JSON 内容.",
                      "warning");
            return;
        }
        if (!studioBridge) {
            showToast("复制失败",
                      "当前剪贴板接口不可用.",
                      "warning");
            return;
        }
        studioBridge.copyToClipboard(transferText);
        showToast("已复制传输 JSON",
                  "当前委托模板封装已复制到剪贴板.",
                  "success");
    }

    function importTransferText(replaceExisting) {
        var result = importDelegationTemplateText(transferText, replaceExisting);
        if (!result.ok) {
            showToast("导入失败",
                      result.error || "无法从传输区导入委托模板.",
                      "warning");
            return;
        }
        if ((result.records || []).length > 0) {
            selectTemplate((result.records || [])[0]);
            transferText = delegationTemplateExportText(result.records || []);
        }
        showToast(replaceExisting ? "模板已替换" : "模板已合并",
                  "已导入 " + String(result.count || 0) + " 个委托模板.",
                  "success");
    }

    function syncSelectionFromCurrentList(preferredId) {
        var records = delegationTemplateList() || [];
        var targetId = String(preferredId || selectedTemplateId || "").trim();
        if (targetId.length > 0) {
            var refreshed = delegationTemplateById(targetId);
            if (refreshed) {
                selectTemplate(refreshed);
                return;
            }
        }
        if (records.length > 0) {
            selectTemplate(records[0] || null);
        } else {
            resetEditor();
        }
    }

    function ensurePullSafe() {
        if (!app || !app.draftDirty) {
            return true;
        }
        showToast("请先保存更改",
                  "当前有未保存的配置草稿;先点顶部“保存更改”,再从 control plane 拉取模板.",
                  "warning");
        return false;
    }

    function pushSelectedToControl() {
        if (!selectedTemplate || !studioBridge) {
            showToast("无可推送模板",
                      "先在列表里选中一个委托模板,再推送到 control plane.",
                      "warning");
            return {"ok": false, "error": "no selected template"};
        }
        return studioBridge.pushDelegationTemplatesToControl([selectedTemplate], false);
    }

    function pushAllToControl() {
        var records = templates || [];
        if (records.length === 0 || !studioBridge) {
            showToast("无可推送模板",
                      "当前配置中还没有已保存的委托模板.",
                      "warning");
            return {"ok": false, "error": "no templates"};
        }
        return studioBridge.pushDelegationTemplatesToControl(records, false);
    }

    function pullFromControl(replaceExisting) {
        if (!studioBridge) {
            return {"ok": false, "error": "studio bridge unavailable"};
        }
        if (!ensurePullSafe()) {
            return {"ok": false, "error": "draft has unsaved changes"};
        }
        var result = studioBridge.pullDelegationTemplatesFromControl(!!replaceExisting);
        if (result.ok) {
            syncSelectionFromCurrentList();
        }
        return result;
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 10

        GridLayout {
            Layout.fillWidth: true
            columns: formColumns(parent.width) + 1
            columnSpacing: 10
            rowSpacing: 10

            GlassField {
                Layout.fillWidth: true
                text: root.templateNameText
                placeholderText: "模板名称"
                onEditingFinished: root.templateNameText = text
            }

            GlassField {
                Layout.fillWidth: true
                text: root.templateNoteText
                placeholderText: "备注 / 使用场景"
                onEditingFinished: root.templateNoteText = text
            }

            NeoComboBox {
                Layout.fillWidth: true
                model: delegationTemplateKinds
                textRole: "title"
                currentIndex: delegationTemplateKindIndex(root.templateKind)
                onActivated: root.templateKind = delegationTemplateKinds[currentIndex].key
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: root.selectedTemplate
                    ? ("当前选中  " + delegationTemplateSummary(root.selectedTemplate))
                    : "当前选中  无"
                color: root.selectedTemplate ? Design.Theme.section("template").accent : listItemStyle.text
                font.pixelSize: Design.Foundation.textMd
                wrapMode: Text.WordWrap
            }

            ResponsiveGridStrip {
                Layout.fillWidth: true
                itemCount: 8
                minimumCellWidth: 156
                maximumColumns: 4
                columnSpacing: 10
                rowSpacing: 10

                ActionButton {
                    compact: true
                    text: "新建"
                    onClicked: root.resetEditor()
                }

                ActionButton {
                    compact: true
                    text: "存单任务"
                    enabled: !!delegationDraftCard &&
                             !!studioBridge &&
                             !studioBridge.busy &&
                             delegationDraftCard.taskText.trim().length > 0
                    onClicked: root.saveFromSingleDraft()
                }

                ActionButton {
                    compact: true
                    text: "存批量"
                    enabled: !!batchDelegationDraftCard &&
                             !!studioBridge &&
                             !studioBridge.busy &&
                             normalizeBatchDraftTasks(batchDelegationDraftCard.taskDrafts).length > 0
                    onClicked: root.saveFromBatchDraft()
                }

                ActionButton {
                    compact: true
                    text: "带入单任务"
                    enabled: !!root.selectedTemplate &&
                             root.selectedTemplate.kind === "single"
                    onClicked: root.applyToSingle()
                }

                ActionButton {
                    compact: true
                    text: "带入批量"
                    enabled: !!root.selectedTemplate &&
                             root.selectedTemplate.kind === "batch"
                    onClicked: root.applyToBatch()
                }

                ActionButton {
                    compact: true
                    text: "预演"
                    enabled: !!root.selectedTemplate
                    onClicked: root.previewCurrent()
                }

                ActionButton {
                    compact: true
                    text: "执行"
                    enabled: !!root.selectedTemplate &&
                             !!studioBridge &&
                             !studioBridge.busy
                    onClicked: root.submitCurrent()
                }

                ActionButton {
                    compact: true
                    tone: "danger"
                    text: "删除"
                    enabled: root.selectedTemplateId.length > 0
                    onClicked: root.deleteCurrent()
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: "模板保存会走统一配置写回,所以会和当前配置草稿一起落盘.单任务保存当前委托草稿,批量保存当前批量委托草稿."
            color: listItemStyle.text
            font.pixelSize: Design.Foundation.textSm
            wrapMode: Text.WordWrap
        }

        GridLayout {
            Layout.fillWidth: true
            columns: formColumns(parent.width)
            columnSpacing: 10
            rowSpacing: 10

            ActionButton {
                compact: true
                text: "导出选中"
                enabled: !!root.selectedTemplate
                onClicked: root.exportSelectedToTransfer()
            }

            ActionButton {
                compact: true
                text: "导出全部"
                enabled: (root.templates || []).length > 0
                onClicked: root.exportAllToTransfer()
            }

            ActionButton {
                compact: true
                text: "复制 JSON"
                enabled: root.transferText.trim().length > 0
                onClicked: root.copyTransferText()
            }

            ActionButton {
                compact: true
                text: "合并导入"
                onClicked: root.importTransferText(false)
            }

            ActionButton {
                compact: true
                tone: "warning"
                text: "替换导入"
                onClicked: root.importTransferText(true)
            }

            ActionButton {
                compact: true
                text: "推送选中项"
                enabled: !!root.selectedTemplate &&
                         !!studioBridge &&
                         !studioBridge.busy
                onClicked: root.pushSelectedToControl()
            }

            ActionButton {
                compact: true
                text: "推送全部"
                enabled: (root.templates || []).length > 0 &&
                         !!studioBridge &&
                         !studioBridge.busy
                onClicked: root.pushAllToControl()
            }

            ActionButton {
                compact: true
                text: "拉取并合并"
                enabled: !!studioBridge &&
                         !studioBridge.busy
                onClicked: root.pullFromControl(false)
            }

            ActionButton {
                compact: true
                tone: "warning"
                text: "拉取并替换"
                enabled: !!studioBridge &&
                         !studioBridge.busy
                onClicked: root.pullFromControl(true)
            }

            ActionButton {
                compact: true
                text: "清空文本"
                enabled: root.transferText.trim().length > 0
                onClicked: root.transferText = ""
            }
        }

        Text {
            Layout.fillWidth: true
            text: "传输 JSON 可以在运行时页面和 CLI 之间共享;`template-export` / `template-import` 也使用同一个封装格式."
            color: listItemStyle.text
            font.pixelSize: Design.Foundation.textSm
            wrapMode: Text.WordWrap
        }

        TextEdit {
            id: templateTransferEditor
            Layout.fillWidth: true
            Layout.preferredHeight: 116
            selectByMouse: true
            wrapMode: TextEdit.WrapAnywhere
            textFormat: TextEdit.PlainText
            color: textViewStyle.text
            selectionColor: textViewStyle.selection
            selectedTextColor: textViewStyle.selectedText
            text: root.transferText
            onTextChanged: {
                if (root.transferText !== text) {
                    root.transferText = text;
                }
            }
        }

        ScrollView {
            id: delegationTemplateListScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Column {
                width: parent.width
                spacing: 8

                Repeater {
                    model: pageListModel("runtime", root.templates)

                    delegate: Rectangle {
                        property var itemStyle: (root.selectedTemplateId || "") === (modelData.id || "")
                            ? strongSelectedListItemStyle
                            : listItemStyle
                        width: parent.width
                        radius: 8
                        color: itemStyle.background
                        border.width: 1
                        border.color: itemStyle.border
                        implicitHeight: templateInfoColumn.implicitHeight + 24

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.selectTemplate(modelData)
                        }

                        Column {
                            id: templateInfoColumn
                            width: parent.width - 24
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.top: parent.top
                            anchors.topMargin: 12
                            spacing: 6

                            Text {
                                width: parent.width
                                text: (modelData.name || "模板") +
                                      "  |  " + delegationTemplateKindTitle(modelData.kind || "single")
                                color: itemStyle.title
                                font.pixelSize: Design.Foundation.textLg
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: delegationTemplateSummary(modelData)
                                color: itemStyle.accent
                                font.pixelSize: Design.Foundation.textSm
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                width: parent.width
                                visible: String(modelData.note || "").trim().length > 0
                                text: modelData.note || ""
                                color: itemStyle.body
                                font.pixelSize: Design.Foundation.textSm
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                width: parent.width
                                text: "更新于  " + formatIsoDateTime(modelData.updatedAt || "")
                                color: itemStyle.meta
                                font.pixelSize: Design.Foundation.textXs
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    visible: root.templates.length === 0
                    text: "还没有保存的委托模板.先准备一条草稿,再点 `存单任务` 或 `存批量`."
                    color: listItemStyle.text
                    font.pixelSize: Design.Foundation.textMd
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
