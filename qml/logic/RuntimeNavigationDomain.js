.pragma library

var RUNTIME_SECTION_DEFINITIONS = [
    {
        "key": "overview",
        "title": "总览 Overview",
        "entryLabel": "Runtime · 总览",
        "description": "保留旧的整页控制台视图,适合一次巡检全部运行态,节点,路由和委托链路.",
        "accentKey": "runtime",
        "cardCount": 11
    },
    {
        "key": "core",
        "title": "Core 核心",
        "entryLabel": "Runtime · Core",
        "description": "聚焦 Agent 默认上下文,工具能力和网页工具默认链路.",
        "accentKey": "runtime",
        "cardCount": 3
    },
    {
        "key": "deployment",
        "title": "Deployment 部署",
        "entryLabel": "Runtime · Deployment",
        "description": "聚焦网关入口,运行时模式,控制平面和注册表端点.",
        "accentKey": "gateway",
        "cardCount": 2
    },
    {
        "key": "memory",
        "title": "Memory 记忆",
        "entryLabel": "Runtime · Memory",
        "description": "聚焦记忆模式,后端类型和远端记忆服务连接.",
        "accentKey": "memory",
        "cardCount": 1
    },
    {
        "key": "cluster",
        "title": "Cluster 集群",
        "entryLabel": "Runtime · Cluster",
        "description": "聚焦节点目录,能力清单和路由候选诊断.",
        "accentKey": "routing",
        "cardCount": 2
    },
    {
        "key": "delegation",
        "title": "Delegation 委托",
        "entryLabel": "Runtime · Delegation",
        "description": "聚焦模板库,单任务草稿和批量草稿提交流程.",
        "accentKey": "routing",
        "cardCount": 3
    }
];

function normalizedRuntimeSection(sectionKey) {
    var raw = String(sectionKey || "").trim().toLowerCase();
    if (raw === "core" ||
            raw === "deployment" ||
            raw === "memory" ||
            raw === "cluster" ||
            raw === "delegation") {
        return raw;
    }
    return "overview";
}

function runtimeSectionDefinitions() {
    return RUNTIME_SECTION_DEFINITIONS.slice(0);
}

function runtimeSectionDefinition(sectionKey) {
    var normalized = normalizedRuntimeSection(sectionKey);
    for (var i = 0; i < RUNTIME_SECTION_DEFINITIONS.length; ++i) {
        if ((RUNTIME_SECTION_DEFINITIONS[i].key || "") === normalized) {
            return RUNTIME_SECTION_DEFINITIONS[i];
        }
    }
    return RUNTIME_SECTION_DEFINITIONS.length > 0 ? RUNTIME_SECTION_DEFINITIONS[0] : ({
        "key": "overview",
        "title": "总览 Overview",
        "entryLabel": "Runtime · 总览",
        "description": "",
        "accentKey": "runtime",
        "cardCount": 0
    });
}

function runtimeSectionTitle(sectionKey) {
    return String(runtimeSectionDefinition(sectionKey).title || "");
}

function runtimeSectionLabel(sectionKey) {
    return String(runtimeSectionDefinition(sectionKey).entryLabel || "");
}

function runtimeSectionDescription(sectionKey) {
    return String(runtimeSectionDefinition(sectionKey).description || "");
}

function emptyPendingNavigation() {
    return {
        "section": "",
        "previewRequest": null,
        "previewLabel": "",
        "taskGroupSeed": null
    };
}

function clonePendingNavigation(state) {
    var source = state || ({ });
    return {
        "section": String(source.section || "").trim(),
        "previewRequest": source.previewRequest || null,
        "previewLabel": String(source.previewLabel || ""),
        "taskGroupSeed": source.taskGroupSeed || null
    };
}

function queuedSectionNavigation(sectionKey, currentState) {
    var next = clonePendingNavigation(currentState);
    next.section = normalizedRuntimeSection(sectionKey);
    return next;
}

function queuedRoutingPreviewNavigation(request, label, sectionKey, currentState) {
    var next = clonePendingNavigation(currentState);
    next.section = normalizedRuntimeSection(sectionKey || "cluster");
    next.previewRequest = request || ({ });
    next.previewLabel = String(label || "");
    next.taskGroupSeed = null;
    return next;
}

function queuedBatchTaskGroupSeedNavigation(taskGroup, sectionKey, currentState) {
    var next = clonePendingNavigation(currentState);
    next.section = normalizedRuntimeSection(sectionKey || "delegation");
    next.previewRequest = null;
    next.previewLabel = "";
    next.taskGroupSeed = taskGroup || null;
    return next;
}

function applyPendingNavigation(runtimePage, currentState) {
    var next = clonePendingNavigation(currentState);
    if (!runtimePage) {
        return next;
    }
    if (next.section.length > 0 && runtimePage.selectRuntimeSection) {
        runtimePage.selectRuntimeSection(next.section);
        next.section = "";
    }
    if (next.previewRequest && runtimePage.applyPreviewRequest) {
        runtimePage.applyPreviewRequest(next.previewRequest, next.previewLabel);
        next.previewRequest = null;
        next.previewLabel = "";
    }
    if (next.taskGroupSeed && runtimePage.seedBatchDraftFromTaskGroup) {
        runtimePage.seedBatchDraftFromTaskGroup(next.taskGroupSeed);
        next.taskGroupSeed = null;
    }
    return next;
}
