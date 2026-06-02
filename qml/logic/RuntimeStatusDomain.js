.pragma library

function readValue(readFn, path, fallbackValue) {
    if (readFn) {
        return readFn(path, fallbackValue);
    }
    return fallbackValue;
}

function stringValue(value) {
    return String(value || "").trim();
}

function statusMap(status) {
    return status || ({ });
}

function memoryServiceStatusText(status, readFn) {
    var current = statusMap(status);
    if (!current.memoryServiceEnabled) {
        return "未启用";
    }
    return current.memoryServiceReachable ? "可达" : "离线";
}

function memoryServiceEndpointText(status, readFn) {
    var current = statusMap(status);
    var endpoint = current.memoryServiceEndpoint || readValue(readFn, "memory.service.endpoint", "");
    return stringValue(endpoint).length > 0 ? endpoint : "未配置";
}

function runtimeServiceStatusText(status, readFn) {
    var current = statusMap(status);
    var mode = current.runtimeMode || readValue(readFn, "runtime.mode", "embedded");
    if (mode !== "remote") {
        return "未启用";
    }
    var endpoint = current.runtimeEndpoint || readValue(readFn, "runtime.endpoint", "");
    if (stringValue(endpoint).length === 0) {
        return "未配置";
    }
    return current.runtimeServiceReachable ? "可达" : "离线";
}

function runtimeEndpointText(status, readFn) {
    var current = statusMap(status);
    var endpoint = current.runtimeEndpoint || readValue(readFn, "runtime.endpoint", "");
    return stringValue(endpoint).length > 0 ? endpoint : "未配置";
}

function runtimeAdvertiseEndpointText(status, readFn) {
    var current = statusMap(status);
    var endpoint = current.runtimeAdvertiseEndpoint || readValue(readFn, "runtime.advertiseEndpoint", "");
    if (stringValue(endpoint).length === 0) {
        endpoint = current.runtimeEndpoint || readValue(readFn, "runtime.endpoint", "");
    }
    return stringValue(endpoint).length > 0 ? endpoint : "未配置";
}

function controlPlaneStatusText(status, readFn) {
    var current = statusMap(status);
    var endpoint = current.controlPlaneEndpoint || readValue(readFn, "deployment.controlPlaneUrl", "");
    if (stringValue(endpoint).length === 0) {
        return readValue(readFn, "deployment.mode", "standalone") === "cluster" ? "未配置" : "本地";
    }
    return current.controlPlaneReachable ? "可达" : "离线";
}

function controlPlaneEndpointText(status, readFn) {
    var current = statusMap(status);
    var endpoint = current.controlPlaneEndpoint || readValue(readFn, "deployment.controlPlaneUrl", "");
    return stringValue(endpoint).length > 0 ? endpoint : "本地传输";
}

function controlPlaneHealthMap(status) {
    var current = statusMap(status);
    return current.controlPlaneHealth || ({ });
}

function controlTaskBusHealthMap(status) {
    var health = controlPlaneHealthMap(status);
    return health.taskBus || ({ });
}

function hasControlTaskBusHealth(status) {
    var taskBus = controlTaskBusHealthMap(status);
    return taskBus &&
           (taskBus.taskCount !== undefined ||
            taskBus.queuedTaskCount !== undefined ||
            (taskBus.recentEvents || []).length > 0);
}

function taskBusEventLabel(type) {
    return String(type || "event").replace(/_/g, " ");
}

function controlTaskBusSummaryText(status) {
    var taskBus = controlTaskBusHealthMap(status);
    if (!hasControlTaskBusHealth(status)) {
        return "不可用";
    }
    return "排队 " + String(Number(taskBus.queuedTaskCount || 0)) +
           "  |  已租约 " + String(Number(taskBus.leasedTaskCount || 0)) +
           "  |  成功 " + String(Number(taskBus.succeededTaskCount || 0)) +
           "  |  失败 " + String(Number(taskBus.failedTaskCount || 0)) +
           "  |  已回收 " + String(Number(taskBus.expiredReclaimedCount || 0)) +
           "  |  过期结果抑制 " + String(Number(taskBus.staleSuppressedResultCount || 0));
}

function controlTaskBusRecentEventsText(status, limit) {
    if (!hasControlTaskBusHealth(status)) {
        return "不可用";
    }
    var events = controlTaskBusHealthMap(status).recentEvents || [];
    var maxItems = Math.max(1, Number(limit || 3));
    if (events.length === 0) {
        return "最近没有租约事件";
    }
    var parts = [];
    for (var i = 0; i < events.length && parts.length < maxItems; ++i) {
        var event = events[i] || ({ });
        var label = taskBusEventLabel(event.type || "");
        var taskId = stringValue(event.taskId);
        var nodeId = stringValue(event.nodeId);
        if (taskId.length > 0) {
            label += " " + taskId;
        }
        if (nodeId.length > 0) {
            label += " @" + nodeId;
        }
        parts.push(label);
    }
    return parts.join("  |  ");
}

function registryStatusText(status, readFn) {
    var current = statusMap(status);
    var endpoint = current.registryEndpoint || readValue(readFn, "deployment.registryUrl", "");
    if (stringValue(endpoint).length === 0) {
        return "本地";
    }
    return current.registryReachable ? "可达" : "离线";
}

function registryEndpointText(status, readFn) {
    var current = statusMap(status);
    var endpoint = current.registryEndpoint || readValue(readFn, "deployment.registryUrl", "");
    return stringValue(endpoint).length > 0 ? endpoint : "本地传输";
}
