import QtQuick 2.14
import QtQuick.Controls 2.14
import QtQuick.Layouts 1.14
import "components"
import "logic/AutomationDomain.js" as AutomationDomain
import "logic/DelegationTemplateDomain.js" as DelegationTemplateDomain
import "logic/ProviderDomain.js" as ProviderDomain
import "logic/RuntimeNavigationDomain.js" as RuntimeNavigationDomain
import "logic/RuntimeStatusDomain.js" as RuntimeStatusDomain
import "pages"
import "theme" as Design
import QtGraphicalEffects 1.14

Rectangle {
    id: root
    width: 1480
    height: 940
    readonly property int minWidth: 1024
    readonly property int minHeight: 680
    readonly property bool compactMode: root.width < 1280
    readonly property bool narrowMode: root.width < 1024
    color: shellTheme.root
    radius: 24
    clip: true
    readonly property var shellTheme: Design.Theme.shellChrome()
    readonly property var startupTheme: Design.Theme.startupOverlay()
    readonly property var toastTheme: Design.Theme.toast(notificationTone)
    readonly property var successStatusStyle: Design.Theme.status("success")
    readonly property var warningStatusStyle: Design.Theme.status("warning")
    readonly property var errorStatusStyle: Design.Theme.status("error")
    readonly property var infoStatusStyle: Design.Theme.status("info")

    property string currentPage: studioBridge.initialPage || "overview"
    property var bridge: studioBridge
    property var draftConfig: ({})
    property bool draftDirty: false
    property bool startupOverlayEnabled: true
    property string activeOAuthProviderPanel: ""
    property var providerDefinitions: ProviderDomain.providerDefinitions()
    property string selectedProviderPanelKey: providerDefinitions.length > 0 ? providerDefinitions[0].key : ""
    property var quickChannels: ProviderDomain.quickChannels()
    property var runtimeCapabilities: ProviderDomain.runtimeCapabilities()
    property var webSearchProviders: ProviderDomain.webSearchProviders()
    property var deploymentModes: ProviderDomain.deploymentModes()
    property var runtimeModes: ProviderDomain.runtimeModes()
    property var memoryModes: ProviderDomain.memoryModes()
    property var memoryBackends: ProviderDomain.memoryBackends()
    property var delegationTemplateKinds: ProviderDomain.delegationTemplateKinds()
    property var policyOptions: ProviderDomain.policyOptions()
    property var approvalScopeOptions: ProviderDomain.approvalScopeOptions()
    property var notificationTone: "neutral"
    property string notificationTitle: ""
    property string notificationBody: ""
    property int pageContentMaxWidth: 1760
    property real startupVisualProgress: 2
    property string startupVisualMessage: "正在准备工作台 / Preparing console"
    property bool startupOverlayDismissed: false
    property var chatPage: chatPageLoader.item
    property var runtimePage: runtimePageLoader.item
    property var pendingRuntimeNavigation: RuntimeNavigationDomain.emptyPendingNavigation()

    Timer {
        interval: 0
        running: true
        repeat: false
        onTriggered: {
            syncDraft();
            syncStartupFeedback();
        }
    }

    Component.onCompleted: {
        console.log("Main.qml component completed");
        syncDraft();
    }

    Connections {
        target: bridge
        onConfigChanged: {
            syncDraft();
        }
        function onSaveFinished(success) {
            if (success) {
                draftDirty = false;
                draftConfig = cloneMap(bridge.config);
            }
        }
        onStartupChanged: {
            syncStartupFeedback();
        }
    }

    Timer {
        id: startupHideTimer
        interval: 320
        repeat: false
        onTriggered: startupOverlayDismissed = true
    }

    Behavior on startupVisualProgress {
        NumberAnimation {
            duration: 240
            easing.type: Easing.OutCubic
        }
    }

    function cloneMap(value) {
        if (value === undefined || value === null) {
            return ({});
        }
        return JSON.parse(JSON.stringify(value));
    }

    function syncLiveOAuthStateIntoDraft() {
        if (!draftDirty || !bridge || !bridge.config) {
            return;
        }

        var liveProviders = bridge.config.providers || ({});
        var nextConfig = cloneMap(draftConfig);
        var nextProviders = cloneMap(nextConfig.providers || ({}));
        var providerKeys = ["openaiCodex", "githubCopilot"];
        var defaultFields = ["apiBase", "oauthIssuer", "oauthClientId", "oauthScope"];
        var runtimeFields = [
            "oauthAccessToken",
            "oauthRefreshToken",
            "oauthIdToken",
            "oauthTokenType",
            "oauthAccountId",
            "oauthExpiresAt",
            "oauthLastRefreshAt",
            "oauthDeviceCode",
            "oauthDeviceAuthId",
            "oauthUserCode",
            "oauthVerificationUrl",
            "oauthLastError",
            "oauthIntervalSec"
        ];
        var changed = false;

        for (var i = 0; i < providerKeys.length; ++i) {
            var providerKey = providerKeys[i];
            var liveProvider = liveProviders[providerKey] || ({});
            var draftProvider = cloneMap(nextProviders[providerKey] || ({}));
            var providerChanged = false;

            for (var j = 0; j < defaultFields.length; ++j) {
                var defaultField = defaultFields[j];
                var draftValue = draftProvider[defaultField];
                var liveValue = liveProvider[defaultField];
                var draftEmpty = draftValue === undefined ||
                                 draftValue === null ||
                                 String(draftValue).trim().length === 0;
                var livePresent = liveValue !== undefined &&
                                  liveValue !== null &&
                                  String(liveValue).trim().length > 0;
                if (draftEmpty && livePresent) {
                    draftProvider[defaultField] = liveValue;
                    providerChanged = true;
                }
            }

            for (var k = 0; k < runtimeFields.length; ++k) {
                var runtimeField = runtimeFields[k];
                var nextValue = liveProvider[runtimeField];
                if (JSON.stringify(draftProvider[runtimeField]) !== JSON.stringify(nextValue)) {
                    draftProvider[runtimeField] = nextValue;
                    providerChanged = true;
                }
            }

            if (providerChanged) {
                nextProviders[providerKey] = draftProvider;
                changed = true;
            }
        }

        if (changed) {
            nextConfig.providers = nextProviders;
            draftConfig = nextConfig;
        }
    }

    function syncDraft() {
        if (!draftDirty) {
            draftConfig = cloneMap(bridge.config);
            return;
        }
        // Keep OAuth runtime fields aligned with live config without clobbering other dirty edits.
        syncLiveOAuthStateIntoDraft();
    }

    function syncStartupFeedback() {
        var nextProgress = Math.max(1, Math.min(100, Number(bridge.startupProgress || 0)));
        startupVisualProgress = nextProgress;
        if ((bridge.startupMessage || "").length > 0) {
            startupVisualMessage = bridge.startupMessage;
        }
        if (!bridge.startupComplete) {
            startupOverlayDismissed = false;
            startupHideTimer.stop();
            return;
        }
        startupHideTimer.restart();
    }

    function pageListModel(pageKey, value) {
        // Explicitly reference currentPage so QML tracks it as a binding
        // dependency. Without this, the Repeater model won't update when
        // navigating away and back to the page.
        var page = currentPage;
        if (page !== pageKey) {
            return [];
        }
        return value || [];
    }

    function overviewStatCard(index) {
        var cards = [
            {"key": "tasks", "title": "任务", "value": bridge.status.taskCount || 0, "detail": "活跃执行台账", "accent": Design.Theme.section("tasks").accent, "icon": Design.Theme.resolveSectionIcon("tasks").value, "iconSpec": Design.Theme.resolveSectionIcon("tasks"), "guide": "查看当前活跃任务数;点进下方任务列表可继续追踪执行状态."},
            {"key": "events", "title": "事件", "value": bridge.status.eventCount || 0, "detail": "系统遥测流", "accent": Design.Theme.section("events").accent, "icon": Design.Theme.resolveSectionIcon("events").value, "iconSpec": Design.Theme.resolveSectionIcon("events"), "guide": "汇总系统事件量;配合事件流区块可快速定位最新告警或状态变化."},
            {"key": "approvals", "title": "审批", "value": bridge.status.pendingApprovalCount || 0, "detail": "等待人工决策", "accent": Design.Theme.section("approvals").accent, "icon": Design.Theme.resolveSectionIcon("approvals").value, "iconSpec": Design.Theme.resolveSectionIcon("approvals"), "guide": "显示待你确认的动作数量;进入安全页可批量批准,拒绝或设置长期策略."},
            {"key": "resources", "title": "资源", "value": bridge.resourceSummary.totalCount || 0, "detail": "已索引系统对象", "accent": Design.Theme.section("resources").accent, "icon": Design.Theme.resolveSectionIcon("resources").value, "iconSpec": Design.Theme.resolveSectionIcon("resources"), "guide": "反映资源索引规模;进入资源页可以查看最近对象,插件和扩展目录."}
        ];
        return cards[index] || ({});
    }

    function extensionSummaryCard(index) {
        var cards = [
            {"key": "plugins", "title": "插件", "value": bridge.status.pluginCount || 0, "detail": "已发现扩展清单", "accent": Design.Theme.section("plugins").accent, "icon": Design.Theme.resolveSectionIcon("plugins").value, "iconSpec": Design.Theme.resolveSectionIcon("plugins"), "guide": "插件会注册成可调用工具;安装后可在下方继续分配默认模型和启用状态."},
            {"key": "skills", "title": "技能", "value": bridge.status.skillCount || 0, "detail": "已发现技能目录", "accent": Design.Theme.section("skills").accent, "icon": Design.Theme.resolveSectionIcon("skills").value, "iconSpec": Design.Theme.resolveSectionIcon("skills"), "guide": "技能会在对话中按触发词注入上下文;可在本页继续配置启用状态和默认模型."},
            {"key": "mcp", "title": "MCP 服务", "value": bridge.status.mcpServerCount || 0, "detail": "已配置工具端点", "accent": Design.Theme.section("mcp").accent, "icon": Design.Theme.resolveSectionIcon("mcp").value, "iconSpec": Design.Theme.resolveSectionIcon("mcp"), "guide": "MCP 端点会扩展工具能力;通过目录安装或配置后,运行时会自动接入."}
        ];
        return cards[index] || ({});
    }

    function sectionIconSpec(sectionKey) {
        return Design.Theme.resolveSectionIcon(sectionKey);
    }

    function read(path, fallbackValue) {
        var cursor = draftConfig;
        var parts = path.split(".");
        for (var i = 0; i < parts.length; ++i) {
            if (cursor === undefined || cursor === null || cursor[parts[i]] === undefined) {
                return fallbackValue;
            }
            cursor = cursor[parts[i]];
        }
        return cursor;
    }

    function assign(path, value) {
        var parts = path.split(".");
        var next = cloneMap(draftConfig);
        var cursor = next;
        for (var i = 0; i < parts.length - 1; ++i) {
            if (cursor[parts[i]] === undefined || cursor[parts[i]] === null) {
                cursor[parts[i]] = ({});
            }
            cursor = cursor[parts[i]];
        }
        cursor[parts[parts.length - 1]] = value;
        draftConfig = next;
        draftDirty = true;
    }

    function taskTreePrefix(depth) {
        var level = Math.max(0, Number(depth || 0));
        var prefix = "";
        for (var i = 0; i < level; ++i) {
            prefix += "|  ";
        }
        return level > 0 ? (prefix + "-> ") : "";
    }

    function taskTreeSummary(task) {
        var state = taskStateLabel(task.state || "pending");
        var hasTreeContext = (task.parentTaskId || "").length > 0 ||
                             Number(task.childCount || 0) > 0 ||
                             Number(task.descendantCount || 0) > 0;
        if (hasTreeContext && (task.rootTaskId || "").length > 0) {
            return state + "  |  根任务 " + task.rootTaskId +
                   "  |  深度 " + String(Number(task.depth || 0));
        }
        return state + "  |  " +
               ((task.parentTaskId || "").length > 0 ? ("父任务 " + task.parentTaskId) : (task.sessionKey || "会话"));
    }

    function taskTraceSummary(task) {
        var parts = [];
        var metadata = task.metadata || ({});
        if ((task.traceId || "").length > 0) {
            parts.push("追踪 " + task.traceId);
        }
        if ((task.targetNode || "").length > 0) {
            parts.push("目标节点 " + task.targetNode);
        } else if ((task.originNode || "").length > 0) {
            parts.push("来源节点 " + task.originNode);
        }
        var targetRole = metadata.target_role || metadata.targetRole || "";
        if (targetRole.length > 0) {
            parts.push("角色 " + targetRole);
        }
        var requiredTool = metadata.required_tool || metadata.requiredTool || "";
        if (requiredTool.length > 0) {
            parts.push("工具 " + requiredTool);
        }
        var requiredMemory = metadata.required_memory_backend || metadata.requiredMemoryBackend || "";
        if (requiredMemory.length > 0) {
            parts.push("记忆后端 " + requiredMemory);
        }
        var targetTags = metadata.target_tags || metadata.targetTags || [];
        if (typeof targetTags === "string" && targetTags.length > 0) {
            targetTags = splitCsv(targetTags);
        }
        if (targetTags && targetTags.length > 0) {
            parts.push("标签 " + targetTags.join(","));
        }
        if (Number(task.childCount || 0) > 0) {
            parts.push("子任务 " + String(Number(task.childCount || 0)));
        }
        if (Number(task.descendantCount || 0) > 0) {
            parts.push("后代任务 " + String(Number(task.descendantCount || 0)));
        }
        return parts.join("  |  ");
    }

    function taskStateLabel(value) {
        var normalized = String(value || "pending").toLowerCase();
        if (normalized === "completed" || normalized === "succeeded") {
            return "已完成";
        }
        if (normalized === "failed") {
            return "失败";
        }
        if (normalized === "cancelled" || normalized === "canceled") {
            return "已取消";
        }
        if (normalized === "queued") {
            return "排队中";
        }
        if (normalized === "running") {
            return "运行中";
        }
        if (normalized === "created") {
            return "已创建";
        }
        return "待处理";
    }

    function isoTimeValue(value) {
        if (!value || String(value).length === 0) {
            return 0;
        }
        var parsed = Date.parse(String(value));
        return isNaN(parsed) ? 0 : parsed;
    }

    function taskRecordTimestamp(task) {
        return Math.max(isoTimeValue(task.finishedAt || ""),
                        isoTimeValue(task.startedAt || ""),
                        isoTimeValue(task.createdAt || ""));
    }

    function uniquePush(items, value) {
        if (!value || String(value).length === 0) {
            return;
        }
        if (items.indexOf(value) < 0) {
            items.push(value);
        }
    }

    function taskTreeStateColor(state) {
        var normalized = String(state || "pending").toLowerCase();
        if (normalized === "failed") {
            return errorStatusStyle.text;
        }
        if (normalized === "completed" || normalized === "succeeded") {
            return successStatusStyle.text;
        }
        if (normalized === "cancelled" || normalized === "canceled") {
            return warningStatusStyle.accent;
        }
        if (normalized === "queued" || normalized === "pending" || normalized === "created") {
            return warningStatusStyle.text;
        }
        return infoStatusStyle.text;
    }

    function taskGroupSummary(group) {
        var parts = [
            taskStateLabel(group.state || "pending"),
            "任务 " + String(Number(group.total || 0)),
            "成功 " + String(Number(group.succeeded || 0))
        ];
        if (Number(group.failed || 0) > 0) {
            parts.push("失败 " + String(Number(group.failed || 0)));
        }
        if (Number(group.active || 0) > 0) {
            parts.push("运行中 " + String(Number(group.active || 0)));
        }
        if (Number(group.queued || 0) > 0) {
            parts.push("排队中 " + String(Number(group.queued || 0)));
        }
        if (Number(group.cancelled || 0) > 0) {
            parts.push("已取消 " + String(Number(group.cancelled || 0)));
        }
        if (Number(group.maxDepth || 0) > 0) {
            parts.push("深度 " + String(Number(group.maxDepth || 0)));
        }
        return parts.join("  |  ");
    }

    function taskGroupRouteSummary(group) {
        var parts = [];
        if ((group.traceId || "").length > 0) {
            parts.push("追踪 " + group.traceId);
        }
        if ((group.originNode || "").length > 0) {
            parts.push("来源节点 " + group.originNode);
        }
        var targets = group.targetNodes || [];
        if (targets.length > 0) {
            parts.push("目标节点 " + targets.join(","));
        }
        var roles = group.roles || [];
        if (roles.length > 0) {
            parts.push("角色 " + roles.join(","));
        }
        var tools = group.tools || [];
        if (tools.length > 0) {
            parts.push("工具 " + tools.join(","));
        }
        var memoryBackends = group.memoryBackends || [];
        if (memoryBackends.length > 0) {
            parts.push("记忆后端 " + memoryBackends.join(","));
        }
        var tags = group.tags || [];
        if (tags.length > 0) {
            parts.push("标签 " + tags.join(","));
        }
        if ((group.sessionKey || "").length > 0) {
            parts.push("会话 " + group.sessionKey);
        }
        return parts.join("  |  ");
    }

    function taskTreeGroups(tasks) {
        var grouped = ({});
        var next = [];
        var list = tasks || [];
        for (var i = 0; i < list.length; ++i) {
            var task = list[i] || ({});
            var rootId = (task.rootTaskId || "").length > 0
                ? task.rootTaskId
                : (((task.parentTaskId || "").length > 0) ? task.parentTaskId : (task.id || ("task_" + String(i))));
            var group = grouped[rootId];
            if (!group) {
                group = {
                    "rootId": rootId,
                    "title": "",
                    "traceId": "",
                    "sessionKey": "",
                    "originNode": "",
                    "tasks": [],
                    "targetNodes": [],
                    "roles": [],
                    "tools": [],
                    "memoryBackends": [],
                    "tags": [],
                    "preview": "",
                    "state": "pending",
                    "total": 0,
                    "succeeded": 0,
                    "failed": 0,
                    "active": 0,
                    "queued": 0,
                    "cancelled": 0,
                    "maxDepth": 0,
                    "updatedAt": "",
                    "sortValue": 0,
                    "previewSortValue": 0,
                    "rootTaskDepth": 9999
                };
                grouped[rootId] = group;
                next.push(group);
            }

            group.total += 1;

            var depth = Number(task.depth || 0);
            if (depth > group.maxDepth) {
                group.maxDepth = depth;
            }

            var timestamp = taskRecordTimestamp(task);
            if (timestamp >= Number(group.sortValue || 0)) {
                group.sortValue = timestamp;
                group.updatedAt = task.finishedAt || task.startedAt || task.createdAt || "";
            }

            var state = String(task.state || "pending").toLowerCase();
            if (state === "completed" || state === "succeeded") {
                group.succeeded += 1;
            } else if (state === "failed") {
                group.failed += 1;
            } else if (state === "cancelled" || state === "canceled") {
                group.cancelled += 1;
            } else if (state === "queued" || state === "pending" || state === "created") {
                group.queued += 1;
            } else {
                group.active += 1;
            }

            if ((task.traceId || "").length > 0 && group.traceId.length === 0) {
                group.traceId = task.traceId;
            }
            if ((task.sessionKey || "").length > 0 && group.sessionKey.length === 0) {
                group.sessionKey = task.sessionKey;
            }
            if ((task.originNode || "").length > 0 && group.originNode.length === 0) {
                group.originNode = task.originNode;
            }
            group.tasks.push(taskBatchDraftObject(task));
            uniquePush(group.targetNodes, task.targetNode || "");

            var metadata = task.metadata || ({});
            uniquePush(group.roles, metadata.target_role || metadata.targetRole || "");
            uniquePush(group.tools, metadata.required_tool || metadata.requiredTool || "");
            uniquePush(group.memoryBackends, metadata.required_memory_backend || metadata.requiredMemoryBackend || "");

            var targetTags = metadata.target_tags || metadata.targetTags || [];
            if (typeof targetTags === "string" && targetTags.length > 0) {
                targetTags = splitCsv(targetTags);
            }
            for (var j = 0; j < targetTags.length; ++j) {
                uniquePush(group.tags, targetTags[j]);
            }

            var preview = task.resultPreview || task.summary || task.error || "";
            if (preview.length > 0 && timestamp >= Number(group.previewSortValue || 0)) {
                group.preview = preview;
                group.previewSortValue = timestamp;
            }

            var rootCandidate = (task.id || "") === rootId ||
                (depth === 0 && (task.parentTaskId || "").length === 0) ||
                depth < Number(group.rootTaskDepth || 9999);
            if (rootCandidate) {
                group.title = task.title || task.kind || task.id || rootId;
                group.rootTaskDepth = depth;
            }
        }

        for (var k = 0; k < next.length; ++k) {
            var candidate = next[k];
            if ((candidate.title || "").length === 0) {
                candidate.title = candidate.rootId;
            }
            if (candidate.failed > 0) {
                candidate.state = "failed";
            } else if (candidate.active > 0) {
                candidate.state = "running";
            } else if (candidate.queued > 0) {
                candidate.state = "queued";
            } else if (candidate.cancelled > 0 && candidate.cancelled === candidate.total) {
                candidate.state = "cancelled";
            } else if (candidate.succeeded > 0 && candidate.succeeded === candidate.total) {
                candidate.state = "succeeded";
            } else {
                candidate.state = "pending";
            }
            candidate.routeSummary = taskGroupRouteSummary(candidate);
        }

        next.sort(function(a, b) {
            return Number(b.sortValue || 0) - Number(a.sortValue || 0);
        });
        return next;
    }

    function taskBatchDraftObject(task) {
        var item = task || ({});
        var metadata = item.metadata || ({});
        return compactTemplateObject({
            "task": firstNonEmptyValue([
                item.task,
                item.title,
                item.summary,
                item.resultPreview,
                item.id
            ], ""),
            "label": firstNonEmptyValue([
                item.label,
                item.title,
                item.id
            ], ""),
            "targetNode": firstNonEmptyValue([
                item.targetNode,
                metadata.target_node,
                metadata.targetNode
            ], ""),
            "targetRole": firstNonEmptyValue([
                item.targetRole,
                metadata.target_role,
                metadata.targetRole
            ], ""),
            "targetTags": normalizedTagList(item.targetTags || metadata.target_tags || metadata.targetTags || []),
            "requiredTool": firstNonEmptyValue([
                item.requiredTool,
                metadata.required_tool,
                metadata.requiredTool
            ], ""),
            "requiredChannel": firstNonEmptyValue([
                item.requiredChannel,
                metadata.required_channel,
                metadata.requiredChannel
            ], ""),
            "requiredMemoryBackend": firstNonEmptyValue([
                item.requiredMemoryBackend,
                metadata.required_memory_backend,
                metadata.requiredMemoryBackend
            ], "")
        });
    }

    function normalizeBatchDraftTasks(tasks) {
        var list = tasks || [];
        var next = [];
        for (var i = 0; i < list.length; ++i) {
            var normalized = taskBatchDraftObject(list[i]);
            if (String(normalized.task || "").trim().length === 0) {
                continue;
            }
            next.push(normalized);
        }
        return next;
    }

    function cloneBatchDraftTasks(tasks) {
        var list = tasks || [];
        var next = [];
        for (var i = 0; i < list.length; ++i) {
            var item = list[i] || ({});
            next.push({
                "label": String(item.label || ""),
                "task": String(item.task || ""),
                "targetNode": String(item.targetNode || ""),
                "targetRole": String(item.targetRole || ""),
                "targetTags": normalizedTagList(item.targetTags || []),
                "requiredTool": String(item.requiredTool || ""),
                "requiredChannel": String(item.requiredChannel || ""),
                "requiredMemoryBackend": String(item.requiredMemoryBackend || "")
            });
        }
        return next;
    }

    function batchTaskLinesFromDraftTasks(tasks) {
        var list = tasks || [];
        var lines = [];
        for (var i = 0; i < list.length; ++i) {
            var item = list[i] || ({});
            var label = String(item.label || "").trim();
            var task = String(item.task || "").trim();
            if (task.length === 0) {
                continue;
            }
            var detail = [];
            var targetNode = String(item.targetNode || "").trim();
            var targetRole = String(item.targetRole || "").trim();
            var targetTags = normalizedTagList(item.targetTags || []);
            var requiredTool = String(item.requiredTool || "").trim();
            var requiredChannel = String(item.requiredChannel || "").trim();
            var requiredMemoryBackend = String(item.requiredMemoryBackend || "").trim();
            if (targetNode.length > 0) {
                detail.push("node=" + targetNode);
            }
            if (targetRole.length > 0) {
                detail.push("role=" + targetRole);
            }
            if (targetTags.length > 0) {
                detail.push("tags=" + targetTags.join(","));
            }
            if (requiredTool.length > 0) {
                detail.push("tool=" + requiredTool);
            }
            if (requiredChannel.length > 0) {
                detail.push("channel=" + requiredChannel);
            }
            if (requiredMemoryBackend.length > 0) {
                detail.push("memory=" + requiredMemoryBackend);
            }
            var line = (label.length > 0 ? label : ("task_" + String(i + 1))) + " :: " + task;
            if (detail.length > 0) {
                line += "  |  " + detail.join("  ");
            }
            lines.push(line);
        }
        return lines.join("\n");
    }

    function batchTaskOverrideSummary(task) {
        var item = task || ({});
        var detail = [];
        var targetNode = String(item.targetNode || "").trim();
        var targetRole = String(item.targetRole || "").trim();
        var targetTags = normalizedTagList(item.targetTags || []);
        var requiredTool = String(item.requiredTool || "").trim();
        var requiredChannel = String(item.requiredChannel || "").trim();
        var requiredMemoryBackend = String(item.requiredMemoryBackend || "").trim();
        if (targetNode.length > 0) {
            detail.push("节点 " + targetNode);
        }
        if (targetRole.length > 0) {
            detail.push("角色 " + targetRole);
        }
        if (targetTags.length > 0) {
            detail.push("标签 " + targetTags.join(","));
        }
        if (requiredTool.length > 0) {
            detail.push("工具 " + requiredTool);
        }
        if (requiredChannel.length > 0) {
            detail.push("频道 " + requiredChannel);
        }
        if (requiredMemoryBackend.length > 0) {
            detail.push("记忆后端 " + requiredMemoryBackend);
        }
        return detail.length > 0 ? detail.join("  |  ") : "继承整组默认条件";
    }

    function delegationExecutionSessionKey(sessionKey, originChannel, originChatId) {
        var normalizedSessionKey = String(sessionKey || "").trim();
        if (normalizedSessionKey.length === 0 || normalizedSessionKey === "gui:preview") {
            if (String(originChannel || "").trim() === "gui") {
                return "gui:primary";
            }
            return String(originChannel || "gui").trim() + ":" + String(originChatId || "desktop").trim();
        }
        return normalizedSessionKey;
    }

    function batchSpawnTemplateObject(groupLabel,
                                      targetNode,
                                      targetRole,
                                      targetTagsText,
                                      requiredTool,
                                      requiredChannel,
                                      requiredMemoryBackend,
                                      tasks) {
        return compactTemplateObject({
            "groupLabel": groupLabel,
            "targetNode": targetNode,
            "targetRole": targetRole,
            "targetTags": splitCsv(targetTagsText),
            "requiredTool": requiredTool,
            "requiredChannel": requiredChannel,
            "requiredMemoryBackend": requiredMemoryBackend,
            "tasks": normalizeBatchDraftTasks(tasks)
        });
    }

    function nodePressureText(node) {
        var item = node || ({});
        var concurrency = Number(item.declaredConcurrency || item.maxConcurrencyHint || 1);
        if (concurrency <= 0) {
            concurrency = 1;
        }
        var active = Number(item.activeTaskCount || 0);
        var queued = Number(item.queuedTaskCount || 0);
        var slots = Math.max(0, concurrency - active);
        return "运行中 " + active + "  |  排队中 " + queued + "  |  空闲槽位 " + slots + "/" + concurrency;
    }

    function nodeIdentityText(node) {
        var item = node || ({});
        var parts = [];
        if ((item.role || "").length > 0) {
            parts.push("角色 " + item.role);
        }
        if ((item.runtimeMode || "").length > 0) {
            parts.push("运行时 " + item.runtimeMode);
        }
        if ((item.clusterId || "").length > 0) {
            parts.push("集群 " + item.clusterId);
        }
        var tags = item.tags || [];
        if (tags.length > 0) {
            parts.push("标签 " + tags.join(","));
        }
        return parts.join("  |  ");
    }

    function nodeEndpointHealthText(node) {
        var item = node || ({});
        if (!item.endpointProbeSupported) {
            return "仅本地";
        }
        if (!item.endpointHealthChecked) {
            return "未检查";
        }
        return item.endpointReachable ? "可达" : "不可达";
    }

    function nodeCapabilityText(node) {
        var item = node || ({});
        var parts = [];
        var capabilities = item.capabilities || [];
        if (capabilities.length > 0) {
            var first = capabilities[0];
            if (first.tools && first.tools.length > 0) {
                parts.push("工具 " + first.tools.join(","));
            }
            if (first.channels && first.channels.length > 0) {
                parts.push("频道 " + first.channels.join(","));
            }
            if (first.memoryBackends && first.memoryBackends.length > 0) {
                parts.push("记忆后端 " + first.memoryBackends.join(","));
            }
        }
        if ((item.endpoint || "").length > 0) {
            parts.push(item.endpoint);
        }
        return parts.join("  |  ");
    }

    function selectNodeRecord(nodes, selectedNodeId) {
        var list = nodes || [];
        if (list.length === 0) {
            return null;
        }
        if (selectedNodeId && selectedNodeId.length > 0) {
            for (var i = 0; i < list.length; ++i) {
                if ((list[i].nodeId || "") === selectedNodeId) {
                    return list[i];
                }
            }
        }
        for (var j = 0; j < list.length; ++j) {
            if (list[j].online !== false) {
                return list[j];
            }
        }
        return list[0];
    }

    function nodeRoutingSummary(node) {
        if (!node) {
            return "";
        }
        var parts = [];
        parts.push(node.online === false ? "离线" : "在线");
        parts.push("端点 " + nodeEndpointHealthText(node));
        if ((node.nodeId || "").length > 0) {
            parts.push("节点 " + node.nodeId);
        }
        if ((node.clusterId || "").length > 0) {
            parts.push("集群 " + node.clusterId);
        }
        if (Number(node.weight || 0) > 0) {
            parts.push("权重 " + String(Number(node.weight || 0)));
        }
        return parts.join("  |  ");
    }

    function nodeCapabilitySummary(capability) {
        if (!capability) {
            return "";
        }
        var parts = [];
        var roles = capability.roles || [];
        var tools = capability.tools || [];
        var channels = capability.channels || [];
        var memoryBackends = capability.memoryBackends || [];
        if (roles.length > 0) {
            parts.push("角色 " + roles.join(","));
        }
        if (tools.length > 0) {
            parts.push("工具 " + tools.join(","));
        }
        if (channels.length > 0) {
            parts.push("频道 " + channels.join(","));
        }
        if (memoryBackends.length > 0) {
            parts.push("记忆后端 " + memoryBackends.join(","));
        }
        if (Number(capability.maxConcurrency || 0) > 0) {
            parts.push("并发 " + String(Number(capability.maxConcurrency || 0)));
        }
        return parts.join("  |  ");
    }

    function nodeCapabilityFlags(capability) {
        if (!capability) {
            return "";
        }
        var parts = [];
        parts.push(capability.supportsDelegation ? "支持委托" : "不支持委托");
        parts.push(capability.supportsStreaming ? "支持流式输出" : "不支持流式输出");
        return parts.join("  |  ");
    }

    function joinValues(values) {
        var list = values || [];
        return list.length > 0 ? list.join(", ") : "无";
    }

    function contextRefSummary(refs) {
        var list = refs || [];
        var parts = [];
        for (var i = 0; i < list.length; ++i) {
            var ref = list[i] || ({});
            var line = (ref.store || "?") + ":" + (ref.key || "?");
            if ((ref.kind || "").length > 0) {
                line += " [" + ref.kind + "]";
            }
            parts.push(line);
        }
        return parts.length > 0 ? parts.join("  |  ") : "无";
    }

    function selectPreviewCandidate(results, selectedNodeId, suggestedNodeId) {
        var list = results || [];
        var preferredId = String(selectedNodeId || "").trim();
        if (preferredId.length > 0) {
            for (var i = 0; i < list.length; ++i) {
                if (String(list[i].nodeId || "").trim() === preferredId) {
                    return list[i];
                }
            }
        }
        var suggestedId = String(suggestedNodeId || "").trim();
        if (suggestedId.length > 0) {
            for (var j = 0; j < list.length; ++j) {
                if (String(list[j].nodeId || "").trim() === suggestedId) {
                    return list[j];
                }
            }
        }
        for (var k = 0; k < list.length; ++k) {
            if (list[k].matched) {
                return list[k];
            }
        }
        return list.length > 0 ? list[0] : null;
    }

    function previewTemplateObject(previewResult, previewRequest, candidate) {
        var preview = previewResult || ({});
        var request = previewRequest || ({});
        var chosen = candidate || ({});
        var node = chosen.node || ({});
        var targetTags = preview.targetTags || request.targetTags || [];
        return {
            "task": request.task || preview.taskTitle || "Delegated task",
            "label": preview.label || request.label || "Delegated task",
            "targetNode": node.nodeId || chosen.nodeId || preview.suggestedNodeId || "",
            "targetRole": preview.targetRole || request.targetRole || node.role || "",
            "targetTags": targetTags,
            "requiredTool": preview.requiredTool || request.requiredTool || "",
            "requiredChannel": preview.requiredChannel || request.requiredChannel || "",
            "requiredMemoryBackend": preview.requiredMemoryBackend || request.requiredMemoryBackend || "",
            "originChannel": preview.originChannel || request.originChannel || "gui",
            "originChatId": preview.originChatId || request.originChatId || "desktop",
            "sessionKey": preview.sceneKey || request.sessionKey || "gui:preview",
            "parentTaskId": preview.parentTaskId || request.parentTaskId || "",
            "traceId": request.traceId || "preview-trace"
        };
    }

    function compactTemplateObject(source) {
        var input = source || ({});
        var result = {};
        for (var key in input) {
            if (!input.hasOwnProperty(key)) {
                continue;
            }
            var value = input[key];
            if (value === undefined || value === null) {
                continue;
            }
            if (typeof value === "string") {
                if (String(value).trim().length === 0) {
                    continue;
                }
                result[key] = value;
                continue;
            }
            if (Object.prototype.toString.call(value) === "[object Array]") {
                var cleaned = [];
                for (var i = 0; i < value.length; ++i) {
                    if (typeof value[i] === "string") {
                        var text = String(value[i]).trim();
                        if (text.length > 0) {
                            cleaned.push(text);
                        }
                    } else if (value[i] !== undefined && value[i] !== null) {
                        cleaned.push(value[i]);
                    }
                }
                if (cleaned.length > 0) {
                    result[key] = cleaned;
                }
                continue;
            }
            if (typeof value === "boolean") {
                if (value) {
                    result[key] = true;
                }
                continue;
            }
            result[key] = value;
        }
        return result;
    }

    function previewTemplateJson(previewResult, previewRequest, candidate) {
        var payload = compactTemplateObject(previewTemplateObject(previewResult, previewRequest, candidate));
        return JSON.stringify(payload, null, 2);
    }

    function spawnTemplateObject(previewResult, previewRequest, candidate) {
        var preview = previewResult || ({});
        var request = previewRequest || ({});
        var chosen = candidate || ({});
        var node = chosen.node || ({});
        var targetTags = preview.targetTags || request.targetTags || [];
        return compactTemplateObject({
            "task": request.task || preview.taskTitle || "Delegated task",
            "label": preview.label || request.label || request.task || preview.taskTitle || "Delegated task",
            "targetNode": node.nodeId || chosen.nodeId || preview.suggestedNodeId || "",
            "targetRole": preview.targetRole || request.targetRole || node.role || "",
            "targetTags": targetTags,
            "requiredTool": preview.requiredTool || request.requiredTool || "",
            "requiredChannel": preview.requiredChannel || request.requiredChannel || "",
            "requiredMemoryBackend": preview.requiredMemoryBackend || request.requiredMemoryBackend || ""
        });
    }

    function spawnTemplateJson(previewResult, previewRequest, candidate) {
        return JSON.stringify(spawnTemplateObject(previewResult, previewRequest, candidate), null, 2);
    }

    function cmdQuote(text) {
        var value = String(text || "");
        return "\"" + value.replace(/(["\\])/g, "\\$1") + "\"";
    }

    function appendCommandArg(parts, flag, value) {
        var text = String(value || "").trim();
        if (text.length === 0) {
            return;
        }
        parts.push(flag);
        parts.push(cmdQuote(text));
    }

    function routePreviewCommand(previewResult, previewRequest) {
        var preview = previewResult || ({});
        var request = previewRequest || ({});
        var parts = ["yaos", "route-preview", "--json"];
        appendCommandArg(parts, "--role", preview.targetRole || request.targetRole || "");
        appendCommandArg(parts, "--tags", (preview.targetTags || request.targetTags || []).join(","));
        appendCommandArg(parts, "--tool", preview.requiredTool || request.requiredTool || "");
        appendCommandArg(parts, "--channel", preview.requiredChannel || request.requiredChannel || "");
        appendCommandArg(parts, "--memory-backend",
                         preview.requiredMemoryBackend || request.requiredMemoryBackend || "");
        appendCommandArg(parts, "--origin-channel", preview.originChannel || request.originChannel || "gui");
        appendCommandArg(parts, "--origin-chat", preview.originChatId || request.originChatId || "desktop");
        appendCommandArg(parts, "--session", preview.sceneKey || request.sessionKey || "gui:preview");
        appendCommandArg(parts, "--task", request.task || preview.taskTitle || "Preview delegated task");
        appendCommandArg(parts, "--label", preview.label || request.label || "Routing preview");
        appendCommandArg(parts, "--parent-task-id", preview.parentTaskId || request.parentTaskId || "");
        appendCommandArg(parts, "--trace-id", request.traceId || "preview-trace");
        if (preview.includeOffline || request.includeOffline) {
            parts.push("--include-offline");
        }
        return parts.join(" ");
    }

    function firstNonEmptyValue(values, fallback) {
        var list = values || [];
        for (var i = 0; i < list.length; ++i) {
            var text = String(list[i] || "").trim();
            if (text.length > 0) {
                return text;
            }
        }
        return fallback || "";
    }

    function sessionChannelFromKey(sessionKey, fallback) {
        var text = String(sessionKey || "").trim();
        if (text.length === 0) {
            return fallback || "";
        }
        var parts = text.split(":");
        return parts.length > 0 ? String(parts[0] || "").trim() : (fallback || "");
    }

    function sessionChatIdFromKey(sessionKey, fallback) {
        var text = String(sessionKey || "").trim();
        if (text.length === 0) {
            return fallback || "";
        }
        var parts = text.split(":");
        if (parts.length <= 1) {
            return fallback || "";
        }
        parts.shift();
        return parts.join(":").trim();
    }

    function normalizedTagList(value) {
        if (typeof value === "string") {
            return splitCsv(value);
        }
        return value || [];
    }

    function routingRequestFromTask(task) {
        var item = task || ({});
        var metadata = item.metadata || ({});
        var sessionKey = String(item.sessionKey || "").trim();
        return {
            "targetRole": firstNonEmptyValue([
                metadata.target_role,
                metadata.targetRole
            ], ""),
            "targetTags": normalizedTagList(metadata.target_tags || metadata.targetTags || []),
            "requiredTool": firstNonEmptyValue([
                metadata.required_tool,
                metadata.requiredTool
            ], ""),
            "requiredChannel": firstNonEmptyValue([
                metadata.required_channel,
                metadata.requiredChannel,
                sessionChannelFromKey(sessionKey, "")
            ], ""),
            "requiredMemoryBackend": firstNonEmptyValue([
                metadata.required_memory_backend,
                metadata.requiredMemoryBackend
            ], ""),
            "originChannel": firstNonEmptyValue([
                metadata.origin_channel,
                metadata.originChannel,
                sessionChannelFromKey(sessionKey, "gui")
            ], "gui"),
            "originChatId": firstNonEmptyValue([
                metadata.origin_chat_id,
                metadata.originChatId,
                sessionChatIdFromKey(sessionKey, "desktop")
            ], "desktop"),
            "sessionKey": sessionKey.length > 0 ? sessionKey : "gui:preview",
            "label": firstNonEmptyValue([
                item.title,
                item.kind,
                item.id
            ], "Routing preview"),
            "task": firstNonEmptyValue([
                item.summary,
                item.title,
                item.kind,
                item.id
            ], "Preview delegated task"),
            "parentTaskId": String(item.parentTaskId || "").trim(),
            "traceId": firstNonEmptyValue([
                item.traceId,
                metadata.trace_id,
                metadata.traceId
            ], "preview-trace"),
            "includeOffline": false
        };
    }

    function routingSeedLabelFromTask(task) {
        var item = task || ({});
        var title = firstNonEmptyValue([
            item.title,
            item.kind,
            item.id
        ], "task");
        var extra = [];
        if ((item.traceId || "").length > 0) {
            extra.push("trace " + item.traceId);
        }
        if ((item.targetNode || "").length > 0) {
            extra.push("target " + item.targetNode);
        }
        return extra.length > 0 ? (title + "  |  " + extra.join("  |  ")) : title;
    }

    function routingRequestFromTaskGroup(group) {
        var item = group || ({});
        var sessionKey = String(item.sessionKey || "").trim();
        return {
            "targetRole": firstNonEmptyValue(item.roles || [], ""),
            "targetTags": item.tags || [],
            "requiredTool": firstNonEmptyValue(item.tools || [], ""),
            "requiredChannel": firstNonEmptyValue([
                sessionChannelFromKey(sessionKey, "")
            ], ""),
            "requiredMemoryBackend": firstNonEmptyValue(item.memoryBackends || [], ""),
            "originChannel": firstNonEmptyValue([
                sessionChannelFromKey(sessionKey, "gui")
            ], "gui"),
            "originChatId": firstNonEmptyValue([
                sessionChatIdFromKey(sessionKey, "desktop")
            ], "desktop"),
            "sessionKey": sessionKey.length > 0 ? sessionKey : "gui:preview",
            "label": firstNonEmptyValue([
                item.title,
                item.rootId
            ], "Routing preview"),
            "task": firstNonEmptyValue([
                item.preview,
                item.title,
                item.rootId
            ], "Preview delegated task"),
            "parentTaskId": String(item.rootId || "").trim(),
            "traceId": firstNonEmptyValue([
                item.traceId
            ], "preview-trace"),
            "includeOffline": false
        };
    }

    function routingSeedLabelFromTaskGroup(group) {
        var item = group || ({});
        var parts = [
            firstNonEmptyValue([item.title, item.rootId], "task-tree")
        ];
        if ((item.traceId || "").length > 0) {
            parts.push("trace " + item.traceId);
        }
        if ((item.rootId || "").length > 0) {
            parts.push("root " + item.rootId);
        }
        return parts.join("  |  ");
    }

    function equalsIgnoreCase(left, right) {
        return String(left || "").trim().toLowerCase() === String(right || "").trim().toLowerCase();
    }

    function listContainsIgnoreCase(values, target) {
        var normalizedTarget = String(target || "").trim().toLowerCase();
        if (normalizedTarget.length === 0) {
            return true;
        }
        var list = values || [];
        for (var i = 0; i < list.length; ++i) {
            if (String(list[i] || "").trim().toLowerCase() === normalizedTarget) {
                return true;
            }
        }
        return false;
    }

    function missingTags(nodeTags, requiredTags) {
        var out = [];
        var actual = nodeTags || [];
        var requested = requiredTags || [];
        for (var i = 0; i < requested.length; ++i) {
            if (!listContainsIgnoreCase(actual, requested[i])) {
                out.push(requested[i]);
            }
        }
        return out;
    }

    function capabilitySupportsRole(capability, role) {
        return !role || String(role).trim().length === 0 ||
            listContainsIgnoreCase(capability.roles || [], role);
    }

    function capabilitySupportsTool(capability, tool) {
        return !tool || String(tool).trim().length === 0 ||
            listContainsIgnoreCase(capability.tools || [], tool);
    }

    function capabilitySupportsChannel(capability, channel) {
        return !channel || String(channel).trim().length === 0 ||
            listContainsIgnoreCase(capability.channels || [], channel);
    }

    function capabilitySupportsMemoryBackend(capability, memoryBackend) {
        return !memoryBackend || String(memoryBackend).trim().length === 0 ||
            listContainsIgnoreCase(capability.memoryBackends || [], memoryBackend);
    }

    function nodeDeclaredConcurrency(node) {
        var concurrency = Number(node.declaredConcurrency || node.maxConcurrencyHint || 1);
        return concurrency > 0 ? concurrency : 1;
    }

    function nodeHasAvailableCapacity(node) {
        return Number(node.activeTaskCount || 0) < nodeDeclaredConcurrency(node);
    }

    function nodeSchedulingPressure(node) {
        var concurrency = nodeDeclaredConcurrency(node);
        var active = Number(node.activeTaskCount || 0);
        var queued = Number(node.queuedTaskCount || 0);
        return (active + queued * 0.5) / concurrency;
    }

    function nodeRoleMatches(node, requestedRole) {
        var role = String(requestedRole || "").trim();
        if (role.length === 0) {
            return true;
        }
        if (equalsIgnoreCase(node.role || "", role)) {
            return true;
        }
        var capabilities = node.capabilities || [];
        for (var i = 0; i < capabilities.length; ++i) {
            if (capabilitySupportsRole(capabilities[i], role)) {
                return true;
            }
        }
        return false;
    }

    function nodeCapabilityMatches(node, requestedRole, requiredTool, requiredChannel, requiredMemoryBackend) {
        var capabilities = node.capabilities || [];
        for (var i = 0; i < capabilities.length; ++i) {
            var capability = capabilities[i];
            if (capabilitySupportsRole(capability, requestedRole) &&
                capabilitySupportsTool(capability, requiredTool) &&
                capabilitySupportsChannel(capability, requiredChannel) &&
                capabilitySupportsMemoryBackend(capability, requiredMemoryBackend)) {
                return true;
            }
        }
        return false;
    }

    function nodeRoutingFailureReasons(node, requestedRole, requestedTags, requiredTool, requiredChannel, requiredMemoryBackend, includeOffline) {
        var reasons = [];
        if (!includeOffline && node.online === false) {
            reasons.push("offline");
        }
        if (!nodeRoleMatches(node, requestedRole)) {
            reasons.push("role mismatch");
        }
        var missing = missingTags(node.tags || [], requestedTags || []);
        if (missing.length > 0) {
            reasons.push("missing tags " + missing.join(","));
        }

        var capabilities = node.capabilities || [];
        if (capabilities.length === 0 &&
            ((requestedRole || "").trim().length > 0 ||
             (requiredTool || "").trim().length > 0 ||
             (requiredChannel || "").trim().length > 0 ||
             (requiredMemoryBackend || "").trim().length > 0)) {
            reasons.push("no capabilities");
            return reasons;
        }

        if (!nodeCapabilityMatches(node, requestedRole, requiredTool, requiredChannel, requiredMemoryBackend)) {
            if ((requiredTool || "").trim().length > 0) {
                var toolSupported = false;
                for (var i = 0; i < capabilities.length; ++i) {
                    if (capabilitySupportsTool(capabilities[i], requiredTool)) {
                        toolSupported = true;
                        break;
                    }
                }
                if (!toolSupported) {
                    reasons.push("tool mismatch");
                }
            }

            if ((requiredChannel || "").trim().length > 0) {
                var channelSupported = false;
                for (var j = 0; j < capabilities.length; ++j) {
                    if (capabilitySupportsChannel(capabilities[j], requiredChannel)) {
                        channelSupported = true;
                        break;
                    }
                }
                if (!channelSupported) {
                    reasons.push("channel mismatch");
                }
            }

            if ((requiredMemoryBackend || "").trim().length > 0) {
                var memorySupported = false;
                for (var k = 0; k < capabilities.length; ++k) {
                    if (capabilitySupportsMemoryBackend(capabilities[k], requiredMemoryBackend)) {
                        memorySupported = true;
                        break;
                    }
                }
                if (!memorySupported) {
                    reasons.push("memory mismatch");
                }
            }

            if ((requestedRole || "").trim().length > 0) {
                var capabilityRoleSupported = false;
                for (var m = 0; m < capabilities.length; ++m) {
                    if (capabilitySupportsRole(capabilities[m], requestedRole)) {
                        capabilityRoleSupported = true;
                        break;
                    }
                }
                if (!capabilityRoleSupported && reasons.indexOf("role mismatch") < 0) {
                    reasons.push("capability role mismatch");
                }
            }
        }

        return reasons;
    }

    function nodeRoutingDiagnostics(nodes,
                                    requestedRole,
                                    requestedTags,
                                    requiredTool,
                                    requiredChannel,
                                    requiredMemoryBackend,
                                    includeOffline,
                                    localNodeId) {
        var out = [];
        var list = nodes || [];
        var role = String(requestedRole || "").trim();
        var tool = String(requiredTool || "").trim();
        var channel = String(requiredChannel || "").trim();
        var memoryBackend = String(requiredMemoryBackend || "").trim();
        var tags = requestedTags || [];
        var normalizedLocalNodeId = String(localNodeId || "").trim().toLowerCase();

        for (var i = 0; i < list.length; ++i) {
            var node = list[i];
            var reasons = nodeRoutingFailureReasons(node,
                                                    role,
                                                    tags,
                                                    tool,
                                                    channel,
                                                    memoryBackend,
                                                    includeOffline);
            out.push({
                "nodeId": node.nodeId || "",
                "node": node,
                "matched": reasons.length === 0,
                "reasons": reasons,
                "reasonText": reasons.length === 0 ? "matches current route filters" : reasons.join("  |  "),
                "hasCapacity": nodeHasAvailableCapacity(node),
                "pressure": nodeSchedulingPressure(node),
                "queuedTaskCount": Number(node.queuedTaskCount || 0),
                "weight": Number(node.weight || 0),
                "isLocal": normalizedLocalNodeId.length > 0 &&
                    String(node.nodeId || "").trim().toLowerCase() === normalizedLocalNodeId
            });
        }

        out.sort(function(left, right) {
            if (left.matched !== right.matched) {
                return left.matched ? -1 : 1;
            }
            if (left.matched) {
                if (left.hasCapacity !== right.hasCapacity) {
                    return left.hasCapacity ? -1 : 1;
                }
                if (Math.abs(Number(left.pressure || 0) - Number(right.pressure || 0)) > 0.0001) {
                    return Number(left.pressure || 0) - Number(right.pressure || 0);
                }
                if (Number(left.queuedTaskCount || 0) !== Number(right.queuedTaskCount || 0)) {
                    return Number(left.queuedTaskCount || 0) - Number(right.queuedTaskCount || 0);
                }
                if (left.isLocal !== right.isLocal) {
                    return left.isLocal ? 1 : -1;
                }
                if (Number(left.weight || 0) !== Number(right.weight || 0)) {
                    return Number(right.weight || 0) - Number(left.weight || 0);
                }
                return String(left.nodeId || "").localeCompare(String(right.nodeId || ""));
            }
            if ((left.node.online === false) !== (right.node.online === false)) {
                return left.node.online === false ? 1 : -1;
            }
            return String(left.nodeId || "").localeCompare(String(right.nodeId || ""));
        });

        for (var j = 0; j < out.length; ++j) {
            out[j].rank = j + 1;
        }
        return out;
    }

    function canonicalProviderKey(key) {
        return ProviderDomain.canonicalProviderKey(key, providerDefinitions);
    }

    function runtimeProviderKey(key) {
        return ProviderDomain.runtimeProviderKey(key, providerDefinitions);
    }

    function providerDefinitionByKey(key) {
        return ProviderDomain.providerDefinitionByKey(key, providerDefinitions);
    }

    function providerTitle(key) {
        if (canonicalProviderKey(key) === "auto") {
            return "自动路由 Auto";
        }
        var definition = providerDefinitionByKey(key);
        return definition ? definition.title : canonicalProviderKey(key);
    }

    function providerValue(key, field, fallbackValue) {
        return read("providers." + canonicalProviderKey(key) + "." + field, fallbackValue);
    }

    function liveProviderValue(key, field, fallbackValue) {
        var canonical = canonicalProviderKey(key);
        var providers = (bridge.config && bridge.config.providers) || ({});
        var liveProvider = providers[canonical] || ({});
        if (liveProvider[field] === undefined || liveProvider[field] === null) {
            return fallbackValue;
        }
        return liveProvider[field];
    }

    function setProviderValue(key, field, value) {
        assign("providers." + canonicalProviderKey(key) + "." + field, value);
    }

    function providerUsesOAuth(key) {
        return ProviderDomain.usesOAuth(key, canonicalProviderKey);
    }

    function providerSupportsModelSync(key) {
        return ProviderDomain.supportsModelSync(key, canonicalProviderKey);
    }

    function providerHeaderHint(key) {
        return ProviderDomain.headerHint(key, canonicalProviderKey);
    }

    function providerAuthState(key) {
        return ProviderDomain.authState(
            key,
            providerUsesOAuth,
            function(providerKey) { return bridge.providerAuthStatus(providerKey); });
    }

    function rebuildChatProviderChoices() {
        if (chatPage && chatPage.rebuildProviderChoices) {
            chatPage.rebuildProviderChoices();
        }
    }

    function runtimeSectionDefinition(sectionKey) {
        return RuntimeNavigationDomain.runtimeSectionDefinition(sectionKey);
    }

    function runtimeSectionTitle(sectionKey) {
        return RuntimeNavigationDomain.runtimeSectionTitle(sectionKey);
    }

    function runtimeSectionLabel(sectionKey) {
        return RuntimeNavigationDomain.runtimeSectionLabel(sectionKey);
    }

    function runtimeSectionDescription(sectionKey) {
        return RuntimeNavigationDomain.runtimeSectionDescription(sectionKey);
    }

    function applyPendingRuntimeActions() {
        pendingRuntimeNavigation = RuntimeNavigationDomain.applyPendingNavigation(runtimePage, pendingRuntimeNavigation);
    }

    function openRuntimeSection(sectionKey) {
        pendingRuntimeNavigation = RuntimeNavigationDomain.queuedSectionNavigation(sectionKey, pendingRuntimeNavigation);
        currentPage = "runtime";
        Qt.callLater(applyPendingRuntimeActions);
    }

    function openRoutingPreview(request, label, sectionKey) {
        pendingRuntimeNavigation = RuntimeNavigationDomain.queuedRoutingPreviewNavigation(
            request,
            label,
            sectionKey,
            pendingRuntimeNavigation);
        openRuntimeSection(sectionKey || "cluster");
    }

    function openBatchTaskGroupSeed(taskGroup, sectionKey) {
        pendingRuntimeNavigation = RuntimeNavigationDomain.queuedBatchTaskGroupSeedNavigation(
            taskGroup,
            sectionKey,
            pendingRuntimeNavigation);
        openRuntimeSection(sectionKey || "delegation");
    }

    function beginProviderOAuthFlow(key, mode) {
        return ProviderDomain.beginOAuthFlow(key, mode, bridge, draftConfig, rebuildChatProviderChoices);
    }

    function pollProviderOAuthState(key) {
        return ProviderDomain.pollOAuthState(key, bridge, rebuildChatProviderChoices);
    }

    function refreshProviderOAuthState(key) {
        return ProviderDomain.refreshOAuthState(key, bridge, rebuildChatProviderChoices);
    }

    function logoutProviderOAuthState(key) {
        return ProviderDomain.logoutOAuthState(key, bridge, rebuildChatProviderChoices);
    }

    function providerAuthSummaryText(state) {
        return ProviderDomain.authSummaryText(state, formatIsoDateTime);
    }

    function providerAuthDiagnosticsText(state) {
        return ProviderDomain.authDiagnosticsText(state, formatIsoDateTime);
    }

    function providerApiKeyPlaceholder(key) {
        return ProviderDomain.apiKeyPlaceholder(key, canonicalProviderKey, providerHeaderHint);
    }

    function providerModelCatalog(key) {
        return ProviderDomain.modelCatalog(key, providerValue);
    }

    function setProviderModelCatalog(key, models) {
        ProviderDomain.setModelCatalog(key, models, setProviderValue);
    }

    function selectedProviderDefinitions() {
        return ProviderDomain.selectedProviderDefinitions(selectedProviderPanelKey, providerDefinitions);
    }

    function currentProviderDefinition() {
        var selected = selectedProviderDefinitions();
        return selected.length > 0 ? selected[0] : ({ "key": "", "title": "", "hint": "" });
    }

    function providerIsConfigured(key) {
        return ProviderDomain.isConfigured(
            key,
            canonicalProviderKey,
            providerValue,
            liveProviderValue,
            providerUsesOAuth);
    }

    function configuredProviderOptions() {
        return ProviderDomain.configuredProviderOptions(providerDefinitions, providerIsConfigured);
    }

    function defaultProviderOptions() {
        return ProviderDomain.defaultProviderOptions(providerDefinitions);
    }

    function modelIndex(options, value) {
        var list = options || [];
        var selected = (value || "").trim();
        for (var i = 0; i < list.length; ++i) {
            if (String(list[i]) === selected) {
                return i;
            }
        }
        return -1;
    }

    function keyedOptionIndex(options, value, fallbackKey) {
        var list = options || [];
        var selected = (value || fallbackKey || "").trim();
        for (var i = 0; i < list.length; ++i) {
            if (list[i].key === selected) {
                return i;
            }
        }
        for (var j = 0; j < list.length; ++j) {
            if (list[j].key === fallbackKey) {
                return j;
            }
        }
        return list.length > 0 ? 0 : -1;
    }

    function selectableProviderModels(key) {
        return ProviderDomain.selectableModels(
            key,
            providerModelCatalog,
            providerValue,
            canonicalProviderKey,
            read);
    }

    function defaultModelChoices() {
        return ProviderDomain.defaultModelChoices(
            read,
            providerDefinitions,
            canonicalProviderKey,
            selectableProviderModels);
    }

    function chooseProviderModel(key, currentValue) {
        return ProviderDomain.chooseProviderModel(
            key,
            currentValue,
            selectableProviderModels,
            providerValue,
            canonicalProviderKey,
            read,
            modelIndex);
    }

    function setProviderAsDefault(key) {
        return ProviderDomain.setProviderAsDefault(
            key,
            runtimeProviderKey,
            chooseProviderModel,
            providerValue,
            assign,
            read);
    }

    function syncProviderCatalog(key) {
        return ProviderDomain.syncProviderCatalog(
            key,
            bridge,
            draftConfig,
            setProviderModelCatalog,
            chooseProviderModel,
            providerValue,
            modelIndex,
            setProviderValue,
            canonicalProviderKey,
            read,
            assign);
    }

    function extensionValue(kind, id, field, fallbackValue) {
        var profiles = read("extensions." + kind, {});
        if (!profiles || profiles[id] === undefined || profiles[id] === null) {
            return fallbackValue;
        }
        var profile = profiles[id];
        return profile[field] === undefined ? fallbackValue : profile[field];
    }

    function setExtensionValue(kind, id, field, value) {
        var profiles = cloneMap(read("extensions." + kind, {}));
        var profile = cloneMap(profiles[id] || ({}));
        profile[field] = value;
        profiles[id] = profile;
        assign("extensions." + kind, profiles);
    }

    function splitCsv(text) {
        if (!text || text.trim().length === 0) {
            return [];
        }
        var parts = text.split(",");
        var out = [];
        for (var i = 0; i < parts.length; ++i) {
            var value = parts[i].trim();
            if (value.length > 0) {
                out.push(value);
            }
        }
        return out;
    }

    function joinCsv(values) {
        if (!values || values.length === 0) {
            return "";
        }
        return values.join(", ");
    }

    function sortedKeys(map) {
        var keys = [];
        if (!map) {
            return keys;
        }
        for (var key in map) {
            keys.push(key);
        }
        keys.sort();
        return keys;
    }

    function mapToLines(map) {
        var keys = sortedKeys(map);
        var out = [];
        for (var i = 0; i < keys.length; ++i) {
            out.push(keys[i] + "=" + map[keys[i]]);
        }
        return out.join("\n");
    }

    function parseKeyValueLines(text) {
        var result = ({});
        if (!text || text.trim().length === 0) {
            return result;
        }
        var lines = String(text).split(/\r?\n/);
        for (var i = 0; i < lines.length; ++i) {
            var line = lines[i].trim();
            if (line.length === 0) {
                continue;
            }
            var splitIndex = line.indexOf("=");
            if (splitIndex <= 0) {
                continue;
            }
            var key = line.substring(0, splitIndex).trim();
            var value = line.substring(splitIndex + 1).trim();
            if (key.length > 0) {
                result[key] = value;
            }
        }
        return result;
    }

    function mcpServerNames() {
        return sortedKeys(read("tools.mcpServers", {}));
    }

    function mcpServerValue(serverName, field, fallbackValue) {
        var servers = read("tools.mcpServers", {});
        if (!servers || servers[serverName] === undefined || servers[serverName] === null) {
            return fallbackValue;
        }
        var server = servers[serverName];
        return server[field] === undefined ? fallbackValue : server[field];
    }

    function setMcpServerValue(serverName, field, value) {
        var servers = cloneMap(read("tools.mcpServers", {}));
        var server = cloneMap(servers[serverName] || ({}));
        server[field] = value;
        servers[serverName] = server;
        assign("tools.mcpServers", servers);
    }

    function uniqueServerName(baseName) {
        var existing = mcpServerNames();
        var candidate = baseName;
        var suffix = 2;
        while (existing.indexOf(candidate) >= 0) {
            candidate = baseName + "_" + suffix;
            suffix += 1;
        }
        return candidate;
    }

    function addMcpServer(kind) {
        var servers = cloneMap(read("tools.mcpServers", {}));
        var baseName = kind === "streamableHttp"
            ? "http_server"
            : (kind === "sse" ? "sse_server" : "stdio_server");
        var name = uniqueServerName(baseName);
        servers[name] = {
            "type": kind,
            "command": "",
            "args": [],
            "env": ({ }),
            "url": "",
            "headers": ({ }),
            "toolTimeout": 30
        };
        assign("tools.mcpServers", servers);
    }

    function removeMcpServer(serverName) {
        var servers = cloneMap(read("tools.mcpServers", {}));
        delete servers[serverName];
        assign("tools.mcpServers", servers);
    }

    function firstLines(text, limit) {
        if (!text) {
            return "";
        }
        if (text.length <= limit) {
            return text;
        }
        return text.substring(0, limit) + "...";
    }

    function pad2(value) {
        return value < 10 ? "0" + value : String(value);
    }

    function formatIsoDateTime(value) {
        if (!value || String(value).trim().length === 0) {
            return "—";
        }
        var dt = new Date(value);
        if (isNaN(dt.getTime())) {
            return String(value).replace("T", " ");
        }
        return dt.getFullYear() + "-" + pad2(dt.getMonth() + 1) + "-" + pad2(dt.getDate()) +
            " " + pad2(dt.getHours()) + ":" + pad2(dt.getMinutes());
    }

    function memoryServiceStatusText() {
        return RuntimeStatusDomain.memoryServiceStatusText(bridge.status, read);
    }

    function memoryServiceEndpointText() {
        return RuntimeStatusDomain.memoryServiceEndpointText(bridge.status, read);
    }

    function runtimeServiceStatusText() {
        return RuntimeStatusDomain.runtimeServiceStatusText(bridge.status, read);
    }

    function runtimeEndpointText() {
        return RuntimeStatusDomain.runtimeEndpointText(bridge.status, read);
    }

    function runtimeAdvertiseEndpointText() {
        return RuntimeStatusDomain.runtimeAdvertiseEndpointText(bridge.status, read);
    }

    function controlPlaneStatusText() {
        return RuntimeStatusDomain.controlPlaneStatusText(bridge.status, read);
    }

    function controlPlaneEndpointText() {
        return RuntimeStatusDomain.controlPlaneEndpointText(bridge.status, read);
    }

    function hasControlTaskBusHealth() {
        return RuntimeStatusDomain.hasControlTaskBusHealth(bridge.status);
    }

    function controlTaskBusSummaryText() {
        return RuntimeStatusDomain.controlTaskBusSummaryText(bridge.status);
    }

    function controlTaskBusRecentEventsText(limit) {
        return RuntimeStatusDomain.controlTaskBusRecentEventsText(bridge.status, limit);
    }

    function registryStatusText() {
        return RuntimeStatusDomain.registryStatusText(bridge.status, read);
    }

    function registryEndpointText() {
        return RuntimeStatusDomain.registryEndpointText(bridge.status, read);
    }

    function automationProviderOptions() {
        return AutomationDomain.providerOptions(configuredProviderOptions);
    }

    function automationModelChoices(providerKey) {
        return AutomationDomain.modelChoices(
            providerKey,
            canonicalProviderKey,
            selectableProviderModels,
            defaultModelChoices);
    }

    function automationTriggerTitle(kind) {
        return AutomationDomain.triggerTitle(kind);
    }

    function automationScheduleSummary(record) {
        return AutomationDomain.scheduleSummary(record);
    }

    function automationStatusLabel(status) {
        return AutomationDomain.statusLabel(status);
    }

    function pageWidth(containerWidth) {
        return Math.max(0, Math.min(containerWidth - 28, pageContentMaxWidth));
    }

    function twoColumnCount(containerWidth) {
        return containerWidth >= 1040 ? 2 : 1;
    }

    function providerGridColumnCount(containerWidth) {
        return containerWidth >= 1040 ? 2 : 1;
    }

    function threeColumnCount(containerWidth) {
        if (containerWidth >= 1280) {
            return 3;
        }
        return containerWidth >= 960 ? 2 : 1;
    }

    function automationWorkbenchColumns(containerWidth) {
        return containerWidth >= 1040 ? 2 : 1;
    }

    function fourStatColumns(containerWidth) {
        return containerWidth >= 1280 ? 4 : 2;
    }

    function showToast(title, body, tone) {
        notificationTitle = title;
        notificationBody = body;
        notificationTone = tone || "neutral";
        toastTimer.restart();
        toastPopup.open();
    }

    function saveDraft() {
        return bridge.saveConfig(draftConfig);
    }

    function delegationTemplateKindIndex(value) {
        return ProviderDomain.delegationTemplateKindIndex(value, delegationTemplateKinds);
    }

    function delegationTemplateKindTitle(kind) {
        return ProviderDomain.delegationTemplateKindTitle(kind, delegationTemplateKinds);
    }

    function delegationTemplateDomainOptions() {
        return {
            "cloneMap": cloneMap,
            "compactTemplateObject": compactTemplateObject,
            "normalizedTagList": normalizedTagList,
            "normalizeBatchDraftTasks": normalizeBatchDraftTasks,
            "firstNonEmptyValue": firstNonEmptyValue,
            "kindTitle": delegationTemplateKindTitle,
            "includeOffline": runtimePage ? runtimePage.includeOfflinePreview : false,
            "delegationExecutionSessionKey": delegationExecutionSessionKey
        };
    }

    function normalizedDelegationTemplateRequest(kind, requestSource) {
        return DelegationTemplateDomain.normalizedDelegationTemplateRequest(
            kind,
            requestSource,
            delegationTemplateDomainOptions());
    }

    function delegationTemplateRecord(source, forSave) {
        return DelegationTemplateDomain.delegationTemplateRecord(
            source,
            forSave,
            delegationTemplateDomainOptions());
    }

    function delegationTemplateList() {
        return DelegationTemplateDomain.delegationTemplateList(
            read("memory.delegationTemplates", []),
            delegationTemplateDomainOptions());
    }

    function delegationTemplateById(templateId) {
        return DelegationTemplateDomain.delegationTemplateById(
            delegationTemplateList(),
            templateId);
    }

    function delegationTemplateSummary(record) {
        return DelegationTemplateDomain.delegationTemplateSummary(
            record,
            delegationTemplateDomainOptions());
    }

    function persistDelegationTemplateList(records) {
        assign("memory.delegationTemplates", records || []);
        return saveDraft();
    }

    function upsertDelegationTemplate(record) {
        var result = DelegationTemplateDomain.upsertDelegationTemplate(
            record,
            delegationTemplateList(),
            delegationTemplateDomainOptions());
        return persistDelegationTemplateList(result.records || []) ? result.record : null;
    }

    function deleteDelegationTemplate(templateId) {
        var result = DelegationTemplateDomain.deleteDelegationTemplate(
            templateId,
            delegationTemplateList());
        if (!result.ok) {
            return false;
        }
        return persistDelegationTemplateList(result.records || []);
    }

    function routingRequestFromDelegationTemplate(record) {
        return DelegationTemplateDomain.routingRequestFromDelegationTemplate(
            record,
            delegationTemplateDomainOptions());
    }

    function submissionRequestFromDelegationTemplate(record) {
        return DelegationTemplateDomain.submissionRequestFromDelegationTemplate(
            record,
            delegationTemplateDomainOptions());
    }

    function delegationTemplateExportEnvelope(records) {
        return DelegationTemplateDomain.delegationTemplateExportEnvelope(
            records || [],
            delegationTemplateDomainOptions());
    }

    function delegationTemplateExportText(records) {
        return DelegationTemplateDomain.delegationTemplateExportText(
            records || [],
            delegationTemplateDomainOptions());
    }

    function delegationTemplateImportRecords(value) {
        return DelegationTemplateDomain.delegationTemplateImportRecords(
            value,
            delegationTemplateDomainOptions());
    }

    function importDelegationTemplateText(text, replaceExisting) {
        var result = DelegationTemplateDomain.importDelegationTemplatePayload(
            text,
            replaceExisting,
            delegationTemplateList(),
            delegationTemplateDomainOptions());
        if (!result.ok) {
            return result;
        }
        if (!persistDelegationTemplateList(result.mergedRecords || [])) {
            return {
                "ok": false,
                "error": "导入后的委托模板保存失败."
            };
        }
        return {
            "ok": true,
            "replace": !!replaceExisting,
            "count": (result.importedRecords || []).length,
            "records": result.importedRecords || []
        };
    }

    function policyIndex(value) {
        return ProviderDomain.policyIndex(value, policyOptions);
    }

    function webSearchProviderIndex(value) {
        return ProviderDomain.webSearchProviderIndex(value, webSearchProviders);
    }

    function providerIcon(providerKey) {
        var key = canonicalProviderKey(providerKey || "");
        return Design.Theme.resolveProviderIcon(key).value || "";
    }

    function providerIconSpec(providerKey) {
        var key = canonicalProviderKey(providerKey || "");
        return Design.Theme.resolveProviderIcon(key);
    }

    function channelIcon(channelKey) {
        var key = String(channelKey || "").toLowerCase();
        return Design.Theme.resolveChannelIcon(key).value || "";
    }

    function channelIconSpec(channelKey) {
        var key = String(channelKey || "").toLowerCase();
        return Design.Theme.resolveChannelIcon(key);
    }

    onDraftConfigChanged: {
        rebuildChatProviderChoices();
    }

    Connections {
        target: bridge
        onConfigChanged: syncDraft()
        onToastRequested: showToast(title, body, tone)
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: shellTheme.backdropStart }
            GradientStop { position: 0.38; color: shellTheme.backdropMid }
            GradientStop { position: 1.0; color: shellTheme.backdropEnd }
        }
    }

    Rectangle {
        width: 420
        height: 420
        radius: 210
        x: -120
        y: -180
        color: shellTheme.ambientLeft
    }

    Rectangle {
        width: 360
        height: 360
        radius: 180
        anchors.right: parent.right
        anchors.rightMargin: -120
        anchors.top: parent.top
        anchors.topMargin: 120
        color: shellTheme.ambientRight
    }

    Rectangle {
        width: 520
        height: 520
        radius: 260
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: -260
        color: shellTheme.ambientBottom
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        Rectangle {
            Layout.preferredWidth: 292
            Layout.fillHeight: true
            radius: 24
            gradient: Gradient {
                GradientStop { position: 0.0; color: shellTheme.sidebarStart }
                GradientStop { position: 1.0; color: shellTheme.sidebarEnd }
            }
            border.width: 1
            border.color: shellTheme.sidebarBorder

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: 23
                color: shellTheme.sidebarInner
            }

            Rectangle {
                width: parent.width - 28
                height: 1
                anchors.top: parent.top
                anchors.topMargin: 18
                anchors.horizontalCenter: parent.horizontalCenter
                color: shellTheme.sidebarDivider
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Rectangle {
                        implicitWidth: consoleTagRow.implicitWidth + 18
                        implicitHeight: consoleTagRow.implicitHeight + 10
                        radius: implicitHeight / 2
                        color: shellTheme.tagBackground
                        border.width: 1
                        border.color: shellTheme.tagBorder

                        Row {
                            id: consoleTagRow
                            x: 9
                            y: 5
                            spacing: 8

                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: shellTheme.tagDot
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: "控制层 Control Layer"
                                color: shellTheme.tagText
                                font.pixelSize: 11
                                font.weight: Font.Black
                                font.letterSpacing: 1.2
                            }
                        }
                    }

                    Text {
                        text: "YAOS"
                        color: shellTheme.brand
                        font.pixelSize: 26
                        font.weight: Font.Black
                        font.letterSpacing: 6
                    }

                    Text {
                        text: "元智能操作系统工作台"
                        color: shellTheme.brandSubtitle
                        font.pixelSize: 11
                        width: parent.width
                        wrapMode: Text.WordWrap
                    }
                }

                Rectangle {
                    id: aiStatusCard
                    Layout.fillWidth: true
                    implicitHeight: aiStatusColumn.implicitHeight + 24
                    radius: 8
                    color: shellTheme.statusCard
                    border.width: 1
                    border.color: shellTheme.statusCardBorder

                    Rectangle { width: 8; height: 1; color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.45); anchors.top: parent.top; anchors.right: parent.right; anchors.rightMargin: 8 }
                    Rectangle { width: 1; height: 8; color: Design.Theme.alpha(Design.Theme.palette.accentCyan, 0.45); anchors.top: parent.top; anchors.right: parent.right; anchors.rightMargin: 8 }

                    Column {
                        id: aiStatusColumn
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 7

                        Row {
                            width: parent.width
                            spacing: 7
                            Rectangle {
                                id: aiDot
                                width: 8; height: 8; radius: 4
                                anchors.verticalCenter: parent.verticalCenter
                                color: Design.Theme.palette.accentCyan
                                opacity: bridge.busy ? 1.0 : 0.8
                                SequentialAnimation {
                                    running: bridge.busy
                                    loops: Animation.Infinite
                                    NumberAnimation { target: aiDot; property: "opacity"; to: 0.35; duration: 1200; easing.type: Easing.InOutSine }
                                    NumberAnimation { target: aiDot; property: "opacity"; to: 1.00; duration: 1200; easing.type: Easing.InOutSine }
                                }
                            }
                            Text {
                                text: bridge.status.actualBackend || "\u8fd0\u884c\u65f6\u79bb\u7ebf"
                                color: shellTheme.statusTitle
                                font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 0.2
                                width: parent.width - 15; wrapMode: Text.WordWrap
                            }
                        }

                        Text {
                            text: "\u5f53\u524d\u5382\u5546  " + providerTitle(bridge.status.routedProvider || "auto")
                            color: shellTheme.statusMeta
                            font.pixelSize: 10; font.letterSpacing: 0.5
                            width: parent.width; wrapMode: Text.WordWrap
                        }

                        Row {
                            width: parent.width; spacing: 6
                            Rectangle { width: 5; height: 5; radius: 2.5; anchors.verticalCenter: parent.verticalCenter; color: bridge.status.gatewayRunning ? successStatusStyle.accent : warningStatusStyle.accent }
                            Text {
                                text: bridge.status.gatewayRunning ? "GATEWAY  ONLINE" : "GATEWAY  IDLE"
                                color: bridge.status.gatewayRunning ? successStatusStyle.text : warningStatusStyle.text
                                font.pixelSize: 10; font.letterSpacing: 0.8
                            }
                        }

                        Row {
                            width: parent.width; spacing: 6
                            Rectangle { width: 5; height: 5; radius: 2.5; anchors.verticalCenter: parent.verticalCenter; color: bridge.status.workspaceReady ? successStatusStyle.accent : warningStatusStyle.accent }
                            Text {
                                text: bridge.status.workspaceReady ? "WORKSPACE  READY" : "WORKSPACE  PENDING"
                                color: bridge.status.workspaceReady ? successStatusStyle.text : warningStatusStyle.text
                                font.pixelSize: 10; font.letterSpacing: 0.8
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    NavChip { Layout.fillWidth: true; text: "总览 Overview"; checked: currentPage === "overview"; onClicked: currentPage = "overview" }
                    NavChip { Layout.fillWidth: true; text: "对话 Chat"; checked: currentPage === "chat"; onClicked: currentPage = "chat" }
                    NavChip { Layout.fillWidth: true; text: "模型 Models"; checked: currentPage === "providers"; onClicked: currentPage = "providers" }
                    NavChip { Layout.fillWidth: true; text: "频道 Channels"; checked: currentPage === "channels"; onClicked: currentPage = "channels" }
                    NavChip { Layout.fillWidth: true; text: "运行时 Runtime"; checked: currentPage === "runtime"; onClicked: currentPage = "runtime" }
                    NavChip { Layout.fillWidth: true; text: "安全 Security"; checked: currentPage === "security"; onClicked: currentPage = "security" }
                    NavChip { Layout.fillWidth: true; text: "资源 Resources"; checked: currentPage === "resources"; onClicked: currentPage = "resources" }
                    NavChip { Layout.fillWidth: true; text: "扩展 Extensions"; checked: currentPage === "extensions"; onClicked: currentPage = "extensions" }
                    NavChip { Layout.fillWidth: true; text: "自动化 Automation"; checked: currentPage === "automation"; onClicked: currentPage = "automation" }
                }

                Item { Layout.fillHeight: true }

                NeoCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 156
                    title: "系统信号 Signal"
                    subtitle: "最近遥测概览 / Recent Telemetry"

                    Column {
                        width: parent.width
                        spacing: 8

                        Row {
                            width: parent.width
                            spacing: 8

                            Text {
                                text: "任务  " + (bridge.status.taskCount || 0)
                                color: Design.Theme.section("tasks").accent
                                font.pixelSize: 13
                                width: (parent.width - parent.spacing) / 2
                            }
                            Text {
                                text: "事件  " + (bridge.status.eventCount || 0)
                                color: Design.Theme.section("events").accent
                                font.pixelSize: 13
                                width: (parent.width - parent.spacing) / 2
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: 8

                            Text {
                                text: "待审批  " + (bridge.status.pendingApprovalCount || 0)
                                color: Design.Theme.section("approvals").accent
                                font.pixelSize: 13
                                width: (parent.width - parent.spacing) / 2
                            }
                            Text {
                                text: "未读通知  " + (bridge.status.unreadNotificationCount || 0)
                                color: warningStatusStyle.text
                                font.pixelSize: 13
                                width: (parent.width - parent.spacing) / 2
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 24
            gradient: Gradient {
                GradientStop { position: 0.0; color: shellTheme.mainStart }
                GradientStop { position: 1.0; color: shellTheme.mainEnd }
            }
            border.width: 1
            border.color: shellTheme.mainBorder

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: 23
                color: shellTheme.mainInner
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 16

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 62
                    radius: 20
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: shellTheme.headerStart }
                        GradientStop { position: 1.0; color: shellTheme.headerEnd }
                    }
                    border.width: 1
                    border.color: shellTheme.headerBorder

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12

                        MouseArea {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton
                            onPressed: bridge.beginWindowDrag(mouse.screenX, mouse.screenY)
                            onPositionChanged: {
                                if (pressed) {
                                    bridge.dragWindow(mouse.screenX, mouse.screenY)
                                }
                            }
                            onReleased: bridge.endWindowDrag()
                            onCanceled: bridge.endWindowDrag()
                            onDoubleClicked: bridge.toggleMaximizeWindow()

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 4

                                Text {
                                    text: "未来控制中枢 Future Console"
                                    color: shellTheme.headerTitle
                                    font.pixelSize: 20
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    text: "桌面工作台 / Desktop Console"
                                    color: shellTheme.headerSubtitle
                                    font.pixelSize: 11
                                }
                            }
                        }

                        ActionButton {
                            compact: true
                            enabled: !bridge.saveInProgress
                            text: bridge.saveInProgress
                                ? "保存中..."
                                : (draftDirty ? "保存更改" : "同步状态")
                            onClicked: draftDirty ? saveDraft() : bridge.requestRefresh()
                        }

                        ActionButton {
                            compact: true
                            text: "初始化工作区"
                            onClicked: bridge.initializeWorkspace()
                        }

                        ActionButton {
                            compact: true
                            text: bridge.status.gatewayRunning ? "停止网关" : "启动网关"
                            onClicked: bridge.status.gatewayRunning ? bridge.stopGateway() : bridge.startGateway()
                        }

                        Row {
                            spacing: 8
                            WindowControlButton { symbol: "－"; onClicked: bridge.minimizeWindow() }
                            WindowControlButton { symbol: "□"; onClicked: bridge.toggleMaximizeWindow() }
                            WindowControlButton { symbol: "×"; danger: true; onClicked: bridge.closeWindow() }
                        }
                    }
                }

                StackLayout {
                    id: stack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: {
                        if (currentPage === "chat") return 1;
                        if (currentPage === "providers") return 2;
                        if (currentPage === "channels") return 3;
                        if (currentPage === "runtime") return 4;
                        if (currentPage === "security") return 5;
                        if (currentPage === "resources") return 6;
                        if (currentPage === "extensions") return 7;
                        if (currentPage === "automation") return 8;
                        return 0;
                    }

                    Component {
                        id: overviewPageComponent
                        OverviewPage {
                            app: root
                            studioBridge: root.bridge
                            stackWidth: stack.width
                        }
                    }

                    Component {
                        id: chatPageComponent
                        ChatPage {
                            app: root
                            studioBridge: root.bridge
                            stackWidth: stack.width
                        }
                    }

                    Component {
                        id: modelsPageComponent
                        ModelsPage {
                            app: root
                            studioBridge: root.bridge
                            chatPage: root.chatPage
                            stackWidth: stack.width
                        }
                    }

                    Component {
                        id: channelsPageComponent
                        ChannelsPage {
                            app: root
                            studioBridge: root.bridge
                            stackWidth: stack.width
                        }
                    }

                    Component {
                        id: runtimePageComponent
                        RuntimePage {
                            app: root
                            studioBridge: root.bridge
                            stackWidth: stack.width
                        }
                    }

                    Component {
                        id: securityPageComponent
                        SecurityPage {
                            app: root
                            studioBridge: root.bridge
                            stackWidth: stack.width
                        }
                    }

                    Component {
                        id: resourcesPageComponent
                        ResourcesPage {
                            app: root
                            studioBridge: root.bridge
                            stackWidth: stack.width
                        }
                    }

                    Component {
                        id: extensionsPageComponent
                        ExtensionsPage {
                            app: root
                            studioBridge: root.bridge
                            stackWidth: stack.width
                        }
                    }

                    Component {
                        id: automationPageComponent
                        AutomationPage {
                            app: root
                            studioBridge: root.bridge
                            stackWidth: stack.width
                        }
                    }

                    Loader {
                        id: overviewPageLoader
                        property bool keptLoaded: true
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: true
                        sourceComponent: overviewPageComponent
                    }

                    Loader {
                        id: chatPageLoader
                        property bool keptLoaded: false
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: keptLoaded || root.currentPage === "chat"
                        sourceComponent: chatPageComponent

                        Connections {
                            target: root
                            onCurrentPageChanged: if (root.currentPage === "chat") chatPageLoader.keptLoaded = true
                        }
                    }

                    Loader {
                        id: modelsPageLoader
                        property bool keptLoaded: false
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: keptLoaded || root.currentPage === "providers"
                        sourceComponent: modelsPageComponent

                        Connections {
                            target: root
                            onCurrentPageChanged: if (root.currentPage === "providers") modelsPageLoader.keptLoaded = true
                        }
                    }

                    Loader {
                        id: channelsPageLoader
                        property bool keptLoaded: false
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: keptLoaded || root.currentPage === "channels"
                        sourceComponent: channelsPageComponent

                        Connections {
                            target: root
                            onCurrentPageChanged: if (root.currentPage === "channels") channelsPageLoader.keptLoaded = true
                        }
                    }

                    Loader {
                        id: runtimePageLoader
                        property bool keptLoaded: false
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: keptLoaded || root.currentPage === "runtime"
                        sourceComponent: runtimePageComponent
                        onLoaded: root.applyPendingRuntimeActions()

                        Connections {
                            target: root
                            onCurrentPageChanged: if (root.currentPage === "runtime") runtimePageLoader.keptLoaded = true
                        }
                    }

                    Loader {
                        id: securityPageLoader
                        property bool keptLoaded: false
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: keptLoaded || root.currentPage === "security"
                        sourceComponent: securityPageComponent

                        Connections {
                            target: root
                            onCurrentPageChanged: if (root.currentPage === "security") securityPageLoader.keptLoaded = true
                        }
                    }

                    Loader {
                        id: resourcesPageLoader
                        property bool keptLoaded: false
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: keptLoaded || root.currentPage === "resources"
                        sourceComponent: resourcesPageComponent

                        Connections {
                            target: root
                            onCurrentPageChanged: if (root.currentPage === "resources") resourcesPageLoader.keptLoaded = true
                        }
                    }

                    Loader {
                        id: extensionsPageLoader
                        property bool keptLoaded: false
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: keptLoaded || root.currentPage === "extensions"
                        sourceComponent: extensionsPageComponent

                        Connections {
                            target: root
                            onCurrentPageChanged: if (root.currentPage === "extensions") extensionsPageLoader.keptLoaded = true
                        }
                    }

                    Loader {
                        id: automationPageLoader
                        property bool keptLoaded: false
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        active: keptLoaded || root.currentPage === "automation"
                        sourceComponent: automationPageComponent

                        Connections {
                            target: root
                            onCurrentPageChanged: if (root.currentPage === "automation") automationPageLoader.keptLoaded = true
                        }
                    }
                }
        }
    }
    }

    Popup {
        id: toastPopup
        x: root.width - width - 30
        y: root.height - height - 26
        width: 388
        height: contentColumn.implicitHeight + 24
        padding: 0
        modal: false
        focus: false
        closePolicy: Popup.NoAutoClose
        opacity: 1
        scale: 1
        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 }
                NumberAnimation { property: "scale"; from: 0.96; to: 1.0; duration: 180; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 140 }
                NumberAnimation { property: "scale"; from: 1.0; to: 0.98; duration: 140; easing.type: Easing.InCubic }
            }
        }
        background: Rectangle {
            radius: 18
            color: toastTheme.background
            border.width: 1
            border.color: toastTheme.border
        }

        Column {
            id: contentColumn
            anchors.fill: parent
            anchors.margins: 12
            spacing: 6

            Text {
                text: notificationTitle
                color: toastTheme.title
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }

            Text {
                width: parent.width
                text: notificationBody
                color: toastTheme.body
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
        }
    }

    Timer {
        id: toastTimer
        interval: 3000
        repeat: false
        onTriggered: toastPopup.close()
    }

    Rectangle {
        id: saveOverlay
        anchors.fill: parent
        z: 80
        visible: bridge.saveInProgress
        color: Qt.rgba(3 / 255, 11 / 255, 24 / 255, 0.68)

        MouseArea {
            anchors.fill: parent
            enabled: saveOverlay.visible
        }

        Rectangle {
            width: Math.min(parent.width - 80, 520)
            implicitHeight: saveOverlayColumn.implicitHeight + 34
            anchors.centerIn: parent
            radius: 24
            color: startupTheme.card
            border.width: 1
            border.color: startupTheme.cardBorder

            Column {
                id: saveOverlayColumn
                x: 20
                y: 18
                width: parent.width - 40
                spacing: 14

                Text {
                    text: "正在保存配置 / Saving configuration"
                    color: startupTheme.title
                    font.pixelSize: 24
                    font.weight: Font.Black
                }

                Text {
                    width: parent.width
                    text: bridge.saveMessage || "正在同步新的系统参数..."
                    color: startupTheme.body
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    width: parent.width
                    height: 16
                    radius: 8
                    color: startupTheme.progressTrack
                    border.width: 1
                    border.color: startupTheme.cardBorder

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(100, Number(bridge.saveProgress || 0))) / 100.0
                        height: parent.height
                        radius: parent.radius
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: startupTheme.progressStart }
                            GradientStop { position: 1.0; color: startupTheme.progressEnd }
                        }
                    }
                }

                Text {
                    text: Math.round(Number(bridge.saveProgress || 0)) + "%  ·  正在应用配置"
                    color: startupTheme.body
                    font.pixelSize: 12
                }
            }
        }
    }

    Rectangle {
        id: startupOverlay
        anchors.fill: parent
        z: 90
        color: "transparent"
        opacity: startupOverlayEnabled ? (startupOverlayDismissed ? 0 : 1) : 0
        visible: startupOverlayEnabled && opacity > 0.01
        property real scanPhase: -0.25

        Behavior on opacity {
            NumberAnimation {
                duration: 420
                easing.type: Easing.OutCubic
            }
        }

        NumberAnimation on scanPhase {
            from: -0.25
            to: 1.15
            duration: 5600
            loops: Animation.Infinite
            running: startupOverlay.visible
        }

        MouseArea {
            anchors.fill: parent
            enabled: startupOverlay.visible
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: startupTheme.scrimStart }
                GradientStop { position: 0.45; color: startupTheme.scrimMid }
                GradientStop { position: 1.0; color: startupTheme.scrimEnd }
            }
        }

        Repeater {
            model: 18
            Rectangle {
                width: startupOverlay.width
                height: 1
                y: index * 54
                color: index % 3 === 0 ? startupTheme.gridLineStrong : startupTheme.gridLineSoft
            }
        }

        Rectangle {
            width: startupOverlay.width * 0.38
            height: startupOverlay.height * 1.35
            x: startupOverlay.width * startupOverlay.scanPhase - width
            y: -startupOverlay.height * 0.15
            rotation: 15
            opacity: 0.22
            gradient: Gradient {
                GradientStop { position: 0.0; color: startupTheme.scanStart }
                GradientStop { position: 0.5; color: startupTheme.scanCenter }
                GradientStop { position: 1.0; color: startupTheme.scanEnd }
            }
        }

        Rectangle {
            width: 420
            height: 420
            radius: 210
            x: -110
            y: -150
            color: startupTheme.ambientLeft
        }

        Rectangle {
            width: 340
            height: 340
            radius: 170
            anchors.right: parent.right
            anchors.rightMargin: -90
            anchors.bottom: parent.bottom
            anchors.bottomMargin: -40
            color: startupTheme.ambientRight
        }

        Rectangle {
            width: 560
            implicitHeight: startupCardColumn.implicitHeight + 40
            anchors.centerIn: parent
            radius: 28
            color: startupTheme.card
            border.width: 1
            border.color: startupTheme.cardBorder

            Rectangle {
                width: parent.width - 28
                height: 1
                anchors.top: parent.top
                anchors.topMargin: 18
                anchors.horizontalCenter: parent.horizontalCenter
                color: startupTheme.cardRule
                opacity: 0.68
            }

            Column {
                id: startupCardColumn
                x: 22
                y: 22
                width: parent.width - 44
                spacing: 16

                Text {
                    text: "YAOS 未来控制面板 / Control Panel"
                    color: startupTheme.title
                    font.pixelSize: 32
                    font.weight: Font.Black
                    font.letterSpacing: 4
                }

                Text {
                    text: "主界面预加载中 / Main console is loading"
                    color: startupTheme.subtitle
                    font.pixelSize: 14
                }

                Text {
                    width: parent.width
                    text: startupVisualMessage
                    color: startupTheme.body
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    width: parent.width
                    height: 18
                    radius: 9
                    color: startupTheme.progressTrack
                    border.width: 1
                    border.color: startupTheme.cardBorder

                    Rectangle {
                        width: parent.width * startupVisualProgress / 100.0
                        height: parent.height
                        radius: parent.radius
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: startupTheme.progressStart }
                            GradientStop { position: 1.0; color: startupTheme.progressEnd }
                        }
                    }

                    Rectangle {
                        width: 82
                        height: parent.height
                        radius: parent.radius
                        x: Math.max(-width, Math.min(parent.width - width,
                                                     parent.width * startupOverlay.scanPhase - width / 2))
                        color: startupTheme.progressSweep
                    }
                }

                Text {
                    text: Math.round(startupVisualProgress) + "%  ·  正在接入系统"
                    color: startupTheme.body
                    font.pixelSize: 13
                }
            }
        }
    }
}
