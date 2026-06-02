import QtQuick 2.14

Item {
    id: root
    property var app
    property var studioBridge
    property string currentPage: ""
    property var runtimeCapabilities: []
    property var webSearchProviders: []
    property var nodeDirectoryCard
    property var routingDiagnosticsCard
    property var delegationTemplateCard
    property var delegationDraftCard
    property var batchDelegationDraftCard
    property var agentCoreCard
    property var gatewayCard
    property var runtimeTopologyCard
    property var memoryPlaneCard
    property var runtimePrimaryGrid
    property var runtimeSecondaryGrid
    property var toolCapabilitiesCard
    property var webSearchCard
    property var sectionBridge

    QtObject {
        id: nodeSelectionBridgeImpl
        function selectNodeById(nodeId) {
            if (root.nodeDirectoryCard) {
                root.nodeDirectoryCard.selectedNodeId = nodeId || "";
            }
        }
    }

    QtObject {
        id: previewBridgeImpl
        property var previewRequest: root.routingDiagnosticsCard ? (root.routingDiagnosticsCard.previewRequest || {}) : ({})
        property var previewResult: root.routingDiagnosticsCard ? (root.routingDiagnosticsCard.previewResult || {}) : ({})
        property var selectedCandidate: root.routingDiagnosticsCard ? root.routingDiagnosticsCard.selectedCandidate : null
        property string previewSourceLabel: root.routingDiagnosticsCard ? (root.routingDiagnosticsCard.previewSourceLabel || "") : ""
        property string requiredChannel: root.routingDiagnosticsCard ? (root.routingDiagnosticsCard.requiredChannel || "") : ""
        property bool includeOffline: root.routingDiagnosticsCard ? !!root.routingDiagnosticsCard.includeOffline : false

        function applyPreviewRequest(request, sourceLabel) {
            if (root.sectionBridge && root.sectionBridge.selectRuntimeSection) {
                root.sectionBridge.selectRuntimeSection("cluster");
            }
            if (root.routingDiagnosticsCard) {
                root.routingDiagnosticsCard.applyPreviewRequest(request, sourceLabel);
            }
        }
    }

    QtObject {
        id: templateSummaryBridgeImpl
        property int templateCount: root.delegationTemplateCard ? ((root.delegationTemplateCard.templates || []).length) : 0
        property string selectedTemplateName: root.delegationTemplateCard && root.delegationTemplateCard.selectedTemplate
            ? (root.delegationTemplateCard.selectedTemplate.name || "")
            : ""
        property string selectedTemplateId: root.delegationTemplateCard && root.delegationTemplateCard.selectedTemplate
            ? (root.delegationTemplateCard.selectedTemplate.id || "")
            : ""
        property string selectedTemplateKind: root.delegationTemplateCard && root.delegationTemplateCard.selectedTemplate
            ? (root.delegationTemplateCard.selectedTemplate.kind || "single")
            : "single"
        property bool hasSelectedTemplate: selectedTemplateName.length > 0 || selectedTemplateId.length > 0
    }

    QtObject {
        id: draftSeedBridgeImpl
        function seedSingleDraftFromPreview() {
            if (!root.delegationDraftCard) {
                return false;
            }
            if (root.sectionBridge && root.sectionBridge.selectRuntimeSection) {
                root.sectionBridge.selectRuntimeSection("delegation");
            }
            root.delegationDraftCard.seedFromPreview();
            return true;
        }

        function seedBatchDraftFromPreview() {
            if (!root.batchDelegationDraftCard) {
                return false;
            }
            if (root.sectionBridge && root.sectionBridge.selectRuntimeSection) {
                root.sectionBridge.selectRuntimeSection("delegation");
            }
            root.batchDelegationDraftCard.seedFromPreview();
            return true;
        }
    }

    property var nodeSelectionBridge: nodeSelectionBridgeImpl
    property var previewBridge: previewBridgeImpl
    property var templateSummaryBridge: templateSummaryBridgeImpl
    property var draftSeedBridge: draftSeedBridgeImpl
    readonly property bool includeOfflinePreview: previewBridgeImpl.includeOffline

    function normalizeBatchDraftTasks() {
        return app ? app.normalizeBatchDraftTasks.apply(app, arguments) : [];
    }

    function runtimeCardSnapshot(key, item) {
        return {
            "key": key,
            "visible": !!item && !!item.visible,
            "width": item ? Number(item.width || item.implicitWidth || 0) : 0,
            "height": item ? Number(item.height || item.implicitHeight || 0) : 0
        };
    }

    function prepareRuntimeRegressionSnapshot() {
        var changed = false;
        var nodes = studioBridge ? (studioBridge.nodes || []) : [];
        if (nodeDirectoryCard &&
                String(nodeDirectoryCard.selectedNodeId || "").trim().length === 0 &&
                nodes.length > 0) {
            var firstNodeId = String((nodes[0] || {}).nodeId || "");
            if (firstNodeId.length > 0) {
                nodeDirectoryCard.selectedNodeId = firstNodeId;
                changed = true;
            }
        }
        if (routingDiagnosticsCard &&
                !!routingDiagnosticsCard.selectedCandidate &&
                delegationDraftCard &&
                String(delegationDraftCard.taskText || "").trim().length === 0) {
            delegationDraftCard.seedFromPreview();
            changed = true;
        }
        if (routingDiagnosticsCard &&
                !!routingDiagnosticsCard.selectedCandidate &&
                batchDelegationDraftCard &&
                normalizeBatchDraftTasks(batchDelegationDraftCard.taskDrafts).length === 0) {
            batchDelegationDraftCard.seedFromPreview();
            changed = true;
        }
        var templates = delegationTemplateCard ? (delegationTemplateCard.templates || []) : [];
        if (delegationTemplateCard &&
                String(delegationTemplateCard.selectedTemplateId || "").length === 0 &&
                templates.length > 0) {
            delegationTemplateCard.selectTemplate(templates[0] || ({}));
            changed = true;
        }
        return !changed;
    }

    function runtimeRegressionSnapshot() {
        var nodes = studioBridge ? (studioBridge.nodes || []) : [];
        var selectedNode = nodeDirectoryCard ? (nodeDirectoryCard.selectedNode || {}) : ({});
        var selectedCapabilities = selectedNode ? (selectedNode.capabilities || []) : [];
        var templates = delegationTemplateCard ? (delegationTemplateCard.templates || []) : [];
        var normalizedTasks = batchDelegationDraftCard
            ? normalizeBatchDraftTasks(batchDelegationDraftCard.taskDrafts)
            : [];
        var cards = [
            runtimeCardSnapshot("agentCore", agentCoreCard),
            runtimeCardSnapshot("gateway", gatewayCard),
            runtimeCardSnapshot("runtimeTopology", runtimeTopologyCard),
            runtimeCardSnapshot("memoryPlane", memoryPlaneCard),
            runtimeCardSnapshot("nodeDirectory", nodeDirectoryCard),
            runtimeCardSnapshot("routingDiagnostics", routingDiagnosticsCard),
            runtimeCardSnapshot("delegationTemplates", delegationTemplateCard),
            runtimeCardSnapshot("delegationDraft", delegationDraftCard),
            runtimeCardSnapshot("batchDraft", batchDelegationDraftCard),
            runtimeCardSnapshot("toolCapabilities", toolCapabilitiesCard),
            runtimeCardSnapshot("webSearch", webSearchCard)
        ];
        return {
            "page": currentPage,
            "completed": currentPage === "runtime",
            "primaryColumns": runtimePrimaryGrid ? runtimePrimaryGrid.columns : 0,
            "secondaryColumns": runtimeSecondaryGrid ? runtimeSecondaryGrid.columns : 0,
            "cardCount": cards.length,
            "cards": cards,
            "nodeDirectory": {
                "visible": !!nodeDirectoryCard && !!nodeDirectoryCard.visible,
                "splitColumns": nodeDirectoryCard ? nodeDirectoryCard.splitColumns : 0,
                "cardHeight": Number(nodeDirectoryCard ? (nodeDirectoryCard.height || 0) : 0),
                "nodeCount": nodes.length,
                "selectedNodeId": nodeDirectoryCard ? nodeDirectoryCard.effectiveSelectedNodeId : "",
                "listHeight": Number(nodeDirectoryCard ? (nodeDirectoryCard.listHeight || 0) : 0),
                "detailHeight": Number(nodeDirectoryCard ? (nodeDirectoryCard.detailHeight || 0) : 0),
                "capabilityScrollHeight": Number(nodeDirectoryCard ? (nodeDirectoryCard.capabilityScrollHeight || 0) : 0),
                "capabilityCount": selectedCapabilities.length
            },
            "routing": {
                "visible": !!routingDiagnosticsCard && !!routingDiagnosticsCard.visible,
                "cardHeight": Number(routingDiagnosticsCard ? (routingDiagnosticsCard.height || 0) : 0),
                "candidateCount": routingDiagnosticsCard ? ((routingDiagnosticsCard.diagnosticResults || []).length) : 0,
                "selectedCandidateNodeId": routingDiagnosticsCard ? routingDiagnosticsCard.selectedCandidateNodeId : "",
                "candidateListHeight": Number(routingDiagnosticsCard ? (routingDiagnosticsCard.candidateListHeight || 0) : 0),
                "templateViewHeight": Number(routingDiagnosticsCard ? (routingDiagnosticsCard.templateViewHeight || 0) : 0),
                "templateTextLength": String(routingDiagnosticsCard ? (routingDiagnosticsCard.selectedTemplateText || "") : "").length
            },
            "delegationTemplates": {
                "cardHeight": Number(delegationTemplateCard ? (delegationTemplateCard.height || 0) : 0),
                "templateCount": templates.length,
                "selectedTemplateId": delegationTemplateCard ? delegationTemplateCard.selectedTemplateId : "",
                "transferEditorHeight": Number(delegationTemplateCard ? (delegationTemplateCard.transferEditorHeight || 0) : 0),
                "listScrollHeight": Number(delegationTemplateCard ? (delegationTemplateCard.listScrollHeight || 0) : 0)
            },
            "delegationDraft": {
                "cardHeight": Number(delegationDraftCard ? (delegationDraftCard.height || 0) : 0),
                "taskTextLength": delegationDraftCard ? String(delegationDraftCard.taskText || "").trim().length : 0,
                "payloadViewHeight": Number(delegationDraftCard ? (delegationDraftCard.payloadViewHeight || 0) : 0)
            },
            "batchDraft": {
                "cardHeight": Number(batchDelegationDraftCard ? (batchDelegationDraftCard.height || 0) : 0),
                "taskCount": normalizedTasks.length,
                "taskListHeight": Number(batchDelegationDraftCard ? (batchDelegationDraftCard.taskListHeight || 0) : 0),
                "exportViewHeight": Number(batchDelegationDraftCard ? (batchDelegationDraftCard.exportViewHeight || 0) : 0),
                "payloadViewHeight": Number(batchDelegationDraftCard ? (batchDelegationDraftCard.payloadViewHeight || 0) : 0)
            },
            "toolCapabilities": {
                "cardHeight": Number(toolCapabilitiesCard ? (toolCapabilitiesCard.height || toolCapabilitiesCard.implicitHeight || 0) : 0),
                "capabilityCount": (runtimeCapabilities || []).length,
                "gridHeight": Number(toolCapabilitiesCard ? (toolCapabilitiesCard.gridHeight || 0) : 0)
            },
            "webSearch": {
                "cardHeight": Number(webSearchCard ? (webSearchCard.height || webSearchCard.implicitHeight || 0) : 0),
                "formColumns": webSearchCard ? (webSearchCard.formColumns || 0) : 0,
                "providerCount": (webSearchProviders || []).length
            }
        };
    }

    function applyPreviewRequest(request, sourceLabel) {
        previewBridgeImpl.applyPreviewRequest(request, sourceLabel);
    }

    function seedBatchDraftFromTaskGroup(group) {
        if (batchDelegationDraftCard) {
            if (sectionBridge && sectionBridge.selectRuntimeSection) {
                sectionBridge.selectRuntimeSection("delegation");
            }
            batchDelegationDraftCard.seedFromTaskGroup(group);
        }
    }
}
