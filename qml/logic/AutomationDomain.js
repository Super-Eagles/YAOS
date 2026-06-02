.pragma library

function providerOptions(configuredProviderOptionsFn) {
    var options = [{ "key": "auto", "title": "自动路由" }];
    var configured = configuredProviderOptionsFn ? (configuredProviderOptionsFn() || []) : [];
    for (var i = 0; i < configured.length; ++i) {
        options.push(configured[i]);
    }
    return options;
}

function modelChoices(providerKey, canonicalProviderKeyFn, selectableProviderModelsFn, defaultModelChoicesFn) {
    var canonical = canonicalProviderKeyFn ? canonicalProviderKeyFn(providerKey || "auto") : String(providerKey || "auto");
    if (!canonical || canonical === "auto") {
        return defaultModelChoicesFn ? (defaultModelChoicesFn() || []) : [];
    }
    return selectableProviderModelsFn ? (selectableProviderModelsFn(canonical) || []) : [];
}

function triggerTitle(kind) {
    var normalized = String(kind || "manual").toLowerCase();
    if (normalized === "once") {
        return "单次";
    }
    if (normalized === "every") {
        return "间隔";
    }
    if (normalized === "cron") {
        return "Cron";
    }
    return "手动";
}

function scheduleSummary(record) {
    if (!record) {
        return "手动执行";
    }
    var current = record || ({ });
    var kind = String(current.scheduleKind || current.trigger || "manual").toLowerCase();
    if (kind === "once") {
        return "单次  ·  " + (current.scheduleValue || "未配置时间");
    }
    if (kind === "every") {
        return "间隔  ·  " + (current.scheduleValue || "未配置周期");
    }
    if (kind === "cron") {
        var tz = current.timeZone ? ("  ·  " + current.timeZone) : "";
        return "Cron 定时  ·  " + (current.scheduleValue || "未配置表达式") + tz;
    }
    return "手动执行";
}

function statusLabel(status) {
    var normalized = String(status || "").toLowerCase();
    if (normalized === "ok") {
        return "最近执行成功";
    }
    if (normalized === "error") {
        return "最近执行失败";
    }
    return "尚未执行";
}
