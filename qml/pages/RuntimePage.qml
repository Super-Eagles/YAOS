import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "../components"
import "../logic/RuntimeNavigationDomain.js" as RuntimeNavigationDomain
import "../theme" as Design
import QtGraphicalEffects 1.14

Item {
    id: runtimePage
    objectName: "runtimePageRoot"
    property var app
    property var studioBridge
    property real stackWidth: width
    property var appRoot: app
    property string currentPage: app ? app.currentPage : ""
    property var deploymentModes: app ? app.deploymentModes : undefined
    property var memoryBackends: app ? app.memoryBackends : undefined
    property var memoryModes: app ? app.memoryModes : undefined
    property var runtimeCapabilities: app ? app.runtimeCapabilities : undefined
    property var runtimeModes: app ? app.runtimeModes : undefined
    property var webSearchProviders: app ? app.webSearchProviders : undefined
    property string activeRuntimeSection: "overview"
    readonly property var runtimeSectionDefinitions: RuntimeNavigationDomain.runtimeSectionDefinitions()
    readonly property var currentRuntimeSectionSpec: runtimeSectionDefinition(activeRuntimeSection)
    property var nodeDirectoryCardRef: nodeDirectoryCard
    property var routingDiagnosticsCardRef: routingDiagnosticsCard
    property var delegationTemplateCardRef: delegationTemplateCard
    property var delegationDraftCardRef: delegationDraftCard
    property var batchDelegationDraftCardRef: batchDelegationDraftCard
    property var agentCoreCardRef: agentCoreCard
    property var gatewayCardRef: gatewayCard
    property var runtimeTopologyCardRef: runtimeTopologyCard
    property var memoryPlaneCardRef: memoryPlaneCard
    property var runtimePrimaryGridRef: runtimePrimaryGrid
    property var runtimeSecondaryGridRef: runtimeSecondaryGrid
    property var toolCapabilitiesCardRef: toolCapabilitiesCard
    property var webSearchCardRef: webSearchCard

    QtObject {
        id: stack
        property real width: runtimePage.stackWidth
    }

    QtObject {
        id: runtimePrimaryGrid
        property int columns: 1
    }

    QtObject {
        id: runtimeSecondaryGrid
        property int columns: 1
    }

    RuntimePageCoordinator {
        id: runtimeCoordinator
        app: appRoot
        studioBridge: runtimePage.studioBridge
        currentPage: runtimePage.currentPage
        runtimeCapabilities: runtimePage.runtimeCapabilities
        webSearchProviders: runtimePage.webSearchProviders
        sectionBridge: runtimePage
        nodeDirectoryCard: runtimePage.nodeDirectoryCardRef
        routingDiagnosticsCard: runtimePage.routingDiagnosticsCardRef
        delegationTemplateCard: runtimePage.delegationTemplateCardRef
        delegationDraftCard: runtimePage.delegationDraftCardRef
        batchDelegationDraftCard: runtimePage.batchDelegationDraftCardRef
        agentCoreCard: runtimePage.agentCoreCardRef
        gatewayCard: runtimePage.gatewayCardRef
        runtimeTopologyCard: runtimePage.runtimeTopologyCardRef
        memoryPlaneCard: runtimePage.memoryPlaneCardRef
        runtimePrimaryGrid: runtimePage.runtimePrimaryGridRef
        runtimeSecondaryGrid: runtimePage.runtimeSecondaryGridRef
        toolCapabilitiesCard: runtimePage.toolCapabilitiesCardRef
        webSearchCard: runtimePage.webSearchCardRef
    }

    function pageWidth() {
        return app ? app.pageWidth.apply(app, arguments) : undefined;
    }

    function twoColumnCount() {
        return app ? app.twoColumnCount.apply(app, arguments) : undefined;
    }

    function runtimeSectionDefinition(sectionKey) {
        return RuntimeNavigationDomain.runtimeSectionDefinition(sectionKey);
    }

    function runtimeSectionAccent(sectionKey) {
        return Design.Theme.section(sectionKey || "runtime").accent;
    }

    function runtimeSectionVisible(sectionKey) {
        return activeRuntimeSection === "overview" || activeRuntimeSection === sectionKey;
    }

    function boundedRuntimeContentY(targetY) {
        var maxY = Math.max(0, runtimeScrollView.contentHeight - runtimeScrollView.height);
        return Math.max(0, Math.min(maxY, Number(targetY || 0)));
    }

    function scrollRuntimeContentTo(targetY) {
        if (!runtimeScrollView) {
            return;
        }
        runtimeScrollView.contentY = boundedRuntimeContentY(targetY);
    }

    function runtimeSectionItem(sectionKey) {
        var normalized = RuntimeNavigationDomain.normalizedRuntimeSection(sectionKey);
        if (normalized === "core") return coreSection;
        if (normalized === "deployment") return deploymentSection;
        if (normalized === "memory") return memorySection;
        if (normalized === "cluster") return clusterSection;
        if (normalized === "delegation") return delegationSection;
        return null;
    }

    function scrollToRuntimeSection(sectionKey) {
        var normalized = RuntimeNavigationDomain.normalizedRuntimeSection(sectionKey);
        var target = runtimeSectionItem(normalized);
        if (!target) {
            scrollRuntimeContentTo(0);
            return;
        }
        scrollRuntimeContentTo(target.y - 8);
    }

    function selectRuntimeSection(sectionKey, shouldScroll) {
        var nextSection = RuntimeNavigationDomain.runtimeSectionDefinition(sectionKey);
        activeRuntimeSection = nextSection.key || "overview";
        if (shouldScroll !== false) {
            scrollRuntimeContentTo(0);
            Qt.callLater(function() {
                scrollToRuntimeSection(runtimePage.activeRuntimeSection);
            });
        }
    }

    function prepareRuntimeRegressionSnapshot() {
        selectRuntimeSection("overview");
        return runtimeCoordinator ? runtimeCoordinator.prepareRuntimeRegressionSnapshot() : true;
    }

    function runtimeRegressionSnapshot() {
        var snapshot = runtimeCoordinator ? runtimeCoordinator.runtimeRegressionSnapshot() : ({});
        snapshot["activeSection"] = activeRuntimeSection;
        snapshot["activeSectionTitle"] = currentRuntimeSectionSpec.title || "";
        return snapshot;
    }

    property bool includeOfflinePreview: runtimeCoordinator ? runtimeCoordinator.includeOfflinePreview : false

    function applyPreviewRequest(request, sourceLabel) {
        if (runtimeCoordinator) {
            runtimeCoordinator.applyPreviewRequest(request, sourceLabel);
        }
    }

    function seedBatchDraftFromTaskGroup(group) {
        if (runtimeCoordinator) {
            runtimeCoordinator.seedBatchDraftFromTaskGroup(group);
        }
    }

    Flickable {
        id: runtimeScrollView
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: runtimeContentColumn.implicitHeight
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        Column {
            id: runtimeContentColumn
            width: pageWidth(stack.width)
            x: Math.max(0, (runtimeScrollView.width - width) / 2)
            spacing: 10

            PageHero {
                width: parent.width
                compact: true
                sectionKey: "runtime"
                overline: "运行时拓扑 Runtime Topology"
                title: "运行时控制台"
                description: "这里汇总 Agent 默认参数,网关入口,节点拓扑和委托链路,是最接近运行现场的一层控制面."
                metrics: [
                    {
                        "label": "网关 Gateway",
                        "value": studioBridge.status.gatewayRunning ? "在线 Online" : "空闲 Idle",
                        "accent": studioBridge.status.gatewayRunning ? Design.Theme.status("success").accent : Design.Theme.status("warning").accent
                    },
                    {
                        "label": "节点 Nodes",
                        "value": (studioBridge.nodes || []).length,
                        "accent": Design.Theme.section("routing").accent
                    },
                    {
                        "label": "运行时 Runtime",
                        "value": studioBridge.status.runtimeServiceReachable ? "可达 Reachable" : "离线 Detached",
                        "accent": studioBridge.status.runtimeServiceReachable ? Design.Theme.section("runtime").accent : Design.Theme.palette.textMuted
                    },
                    {
                        "label": "记忆 Memory",
                        "value": studioBridge.status.memoryServiceReachable ? "已连接 Connected" : "回退 Fallback",
                        "accent": studioBridge.status.memoryServiceReachable ? Design.Theme.section("memory").accent : Design.Theme.status("warning").accent
                    }
                ]
            }

            Rectangle {
                width: parent.width
                implicitHeight: runtimeSectionColumn.implicitHeight + 28
                radius: 22
                color: Design.Theme.surface("summary").background
                border.width: 1
                border.color: Design.Theme.surface("summary").border

                Column {
                    id: runtimeSectionColumn
                    x: 14

                    y: 14

                    width: parent.width - 28
                    spacing: 8

                    Text {
                        width: parent.width
                        text: "运行区段 Runtime Sections"
                        color: Design.Theme.surface("summary").title
                        font.pixelSize: Design.Foundation.textMd
                        font.weight: Font.Black
                    }

                    Text {
                        width: parent.width
                        text: (currentRuntimeSectionSpec.title || "") + "  ·  " +
                              String(currentRuntimeSectionSpec.cardCount || 0) + " 个面板"
                        color: runtimeSectionAccent(currentRuntimeSectionSpec.accentKey || "runtime")
                        font.pixelSize: Design.Foundation.textLg
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        text: currentRuntimeSectionSpec.description || ""
                        color: Design.Theme.surface("summary").muted
                        font.pixelSize: Design.Foundation.textMd
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        id: runtimeSectionRow
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: runtimeSectionDefinitions

                            delegate: NavChip {
                                width: Math.max(
                                    0,
                                    (runtimeSectionRow.width -
                                        ((runtimeSectionDefinitions.length - 1) * runtimeSectionRow.spacing)) /
                                        Math.max(1, runtimeSectionDefinitions.length))
                                text: modelData.title || ""
                                accent: runtimeSectionAccent(modelData.accentKey || "runtime")
                                checkable: true
                                checked: activeRuntimeSection === (modelData.key || "")
                                onClicked: runtimePage.selectRuntimeSection(modelData.key || "overview")
                            }
                        }
                    }
                }
            }

            Column {
                id: coreSection
                visible: runtimeSectionVisible("core")
                width: parent.width
                spacing: 14

                AgentCoreCard {
                    id: agentCoreCard
                    app: appRoot
                }

                ToolCapabilitiesCard {
                    id: toolCapabilitiesCard
                    app: appRoot
                    runtimeCapabilities: runtimePage.runtimeCapabilities
                }

                WebSearchCard {
                    id: webSearchCard
                    app: appRoot
                    webSearchProviders: runtimePage.webSearchProviders
                }
            }

            Column {
                id: deploymentSection
                visible: runtimeSectionVisible("deployment")
                width: parent.width
                spacing: 14

                GatewayCard {
                    id: gatewayCard
                    app: appRoot
                    studioBridge: runtimePage.studioBridge
                }

                RuntimeTopologyCard {
                    id: runtimeTopologyCard
                    app: appRoot
                    studioBridge: runtimePage.studioBridge
                    deploymentModes: runtimePage.deploymentModes
                    runtimeModes: runtimePage.runtimeModes
                }
            }

            Column {
                id: memorySection
                visible: runtimeSectionVisible("memory")
                width: parent.width
                spacing: 14

                MemoryPlaneCard {
                    id: memoryPlaneCard
                    app: appRoot
                    studioBridge: runtimePage.studioBridge
                    memoryModes: runtimePage.memoryModes
                    memoryBackends: runtimePage.memoryBackends
                }
            }

            Column {
                id: clusterSection
                visible: runtimeSectionVisible("cluster")
                width: parent.width
                spacing: 14

                ResponsiveCardGrid {
                    id: runtimeClusterGrid
                    width: parent.width
                    forcedColumns: parent.width >= 1120 ? 2 : 1
                    maximumColumns: 2
                    minimumCellWidth: 480
                    spacing: 14
                    onColumnsChanged: runtimeSecondaryGrid.columns = columns

                    Component.onCompleted: runtimeSecondaryGrid.columns = columns

                    NodeDirectoryCard {
                        id: nodeDirectoryCard
                        width: runtimeClusterGrid.cellWidth
                        height: runtimeClusterGrid.columns > 1 ? 560 : 760
                        app: appRoot
                        studioBridge: runtimePage.studioBridge
                    }

                    RoutingDiagnosticsCard {
                        id: routingDiagnosticsCard
                        width: runtimeClusterGrid.cellWidth
                        height: runtimeClusterGrid.columns > 1 ? 640 : 820
                        app: appRoot
                        studioBridge: runtimePage.studioBridge
                        templateSummaryBridge: runtimeCoordinator.templateSummaryBridge
                        draftSeedBridge: runtimeCoordinator.draftSeedBridge
                        nodeSelectionBridge: runtimeCoordinator.nodeSelectionBridge
                    }
                }
            }

            Column {
                id: delegationSection
                visible: runtimeSectionVisible("delegation")
                width: parent.width
                spacing: 14

                DelegationTemplateCard {
                    id: delegationTemplateCard
                    app: appRoot
                    studioBridge: runtimePage.studioBridge
                    delegationDraftCard: delegationDraftCard
                    batchDelegationDraftCard: batchDelegationDraftCard
                    previewBridge: runtimeCoordinator.previewBridge
                }

                DelegationDraftCard {
                    id: delegationDraftCard
                    app: appRoot
                    studioBridge: runtimePage.studioBridge
                    previewBridge: runtimeCoordinator.previewBridge
                }

                BatchDelegationDraftCard {
                    id: batchDelegationDraftCard
                    app: appRoot
                    studioBridge: runtimePage.studioBridge
                    previewBridge: runtimeCoordinator.previewBridge
                }
            }

            Item { height: 32; width: 1 }
        }
    }
}
