.pragma library

function normalizedKind(value) {
    return String(value || "single").trim().toLowerCase() === "batch" ? "batch" : "single";
}

function kindTitle(kind, options) {
    if (options && options.kindTitle) {
        return options.kindTitle(kind);
    }
    return normalizedKind(kind) === "batch" ? "批量草稿" : "单任务草稿";
}

function normalizedDelegationTemplateRequest(kind, requestSource, options) {
    var request = options.cloneMap(requestSource || ({}));
    var templateKind = normalizedKind(kind);
    if (templateKind === "batch") {
        return options.compactTemplateObject({
            "groupLabel": String(request.groupLabel || request.label || "").trim(),
            "targetNode": String(request.targetNode || "").trim(),
            "targetRole": String(request.targetRole || "").trim(),
            "targetTags": options.normalizedTagList(request.targetTags || []),
            "requiredTool": String(request.requiredTool || "").trim(),
            "requiredChannel": String(request.requiredChannel || "").trim(),
            "requiredMemoryBackend": String(request.requiredMemoryBackend || "").trim(),
            "tasks": options.normalizeBatchDraftTasks(request.tasks || [])
        });
    }
    return options.compactTemplateObject({
        "task": String(request.task || "").trim(),
        "label": String(request.label || "").trim(),
        "targetNode": String(request.targetNode || "").trim(),
        "targetRole": String(request.targetRole || "").trim(),
        "targetTags": options.normalizedTagList(request.targetTags || []),
        "requiredTool": String(request.requiredTool || "").trim(),
        "requiredChannel": String(request.requiredChannel || "").trim(),
        "requiredMemoryBackend": String(request.requiredMemoryBackend || "").trim()
    });
}

function delegationTemplateRecord(source, forSave, options) {
    var item = options.cloneMap(source || ({}));
    var inferredKind = item.kind || (((item.request || {}).tasks || []).length > 0 ? "batch" : "single");
    var templateKind = normalizedKind(inferredKind);
    var request = normalizedDelegationTemplateRequest(templateKind, item.request || ({}), options);
    var defaultName = templateKind === "batch"
        ? options.firstNonEmptyValue([request.groupLabel, "Delegation Batch Template"], "Delegation Batch Template")
        : options.firstNonEmptyValue([request.label, request.task, "Delegation Template"], "Delegation Template");
    var record = {
        "id": String(item.id || "").trim(),
        "name": options.firstNonEmptyValue([item.name, defaultName], defaultName),
        "kind": templateKind,
        "note": String(item.note || "").trim(),
        "updatedAt": String(item.updatedAt || item.updated_at || "").trim(),
        "request": request
    };
    if (forSave) {
        if (record.id.length === 0) {
            var slug = record.name.toLowerCase()
                .replace(/[^a-z0-9]+/g, "-")
                .replace(/^-+|-+$/g, "");
            if (slug.length === 0) {
                slug = "delegation-template";
            }
            record.id = templateKind + "-" + slug + "-" + String(Date.now());
        }
        record.updatedAt = new Date().toISOString();
    }
    return record;
}

function delegationTemplateList(rawRecords, options) {
    var raw = rawRecords || [];
    var out = [];
    for (var i = 0; i < raw.length; ++i) {
        var record = delegationTemplateRecord(raw[i], false, options);
        if (record.name.trim().length === 0) {
            continue;
        }
        out.push(record);
    }
    out.sort(function(left, right) {
        return String(right.updatedAt || "").localeCompare(String(left.updatedAt || ""));
    });
    return out;
}

function delegationTemplateById(records, templateId) {
    var selectedId = String(templateId || "").trim();
    if (selectedId.length === 0) {
        return null;
    }
    var list = records || [];
    for (var i = 0; i < list.length; ++i) {
        if ((list[i].id || "") === selectedId) {
            return list[i];
        }
    }
    return null;
}

function delegationTemplateSummary(record, options) {
    var item = record || ({});
    var request = item.request || ({});
    var parts = [kindTitle(item.kind || "single", options)];
    if (normalizedKind(item.kind) === "batch") {
        parts.push("tasks " + String(options.normalizeBatchDraftTasks(request.tasks || []).length));
        if (String(request.groupLabel || "").trim().length > 0) {
            parts.push(request.groupLabel);
        }
    } else {
        var taskPreview = String(request.task || "").trim();
        if (taskPreview.length > 72) {
            taskPreview = taskPreview.slice(0, 72) + "...";
        }
        if (taskPreview.length > 0) {
            parts.push(taskPreview);
        }
    }
    if (String(request.targetRole || "").trim().length > 0) {
        parts.push("role " + request.targetRole);
    }
    if ((request.targetTags || []).length > 0) {
        parts.push("tags " + (request.targetTags || []).join(","));
    }
    if (String(request.requiredTool || "").trim().length > 0) {
        parts.push("tool " + request.requiredTool);
    }
    if (String(request.requiredChannel || "").trim().length > 0) {
        parts.push("channel " + request.requiredChannel);
    }
    if (String(request.requiredMemoryBackend || "").trim().length > 0) {
        parts.push("memory " + request.requiredMemoryBackend);
    }
    return parts.join("  |  ");
}

function upsertDelegationTemplate(record, records, options) {
    var normalized = delegationTemplateRecord(record, true, options);
    var next = (records || []).slice(0);
    var replaced = false;
    for (var i = 0; i < next.length; ++i) {
        if ((next[i].id || "") === normalized.id) {
            next[i] = normalized;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        next.unshift(normalized);
    }
    return {
        "record": normalized,
        "records": next
    };
}

function deleteDelegationTemplate(templateId, records) {
    var targetId = String(templateId || "").trim();
    if (targetId.length === 0) {
        return {
            "ok": false,
            "records": records || []
        };
    }
    var current = records || [];
    var next = [];
    for (var i = 0; i < current.length; ++i) {
        if ((current[i].id || "") !== targetId) {
            next.push(current[i]);
        }
    }
    return {
        "ok": next.length !== current.length,
        "records": next
    };
}

function routingRequestFromDelegationTemplate(record, options) {
    var item = delegationTemplateRecord(record || ({}), false, options);
    var request = item.request || ({});
    var firstTask = "";
    if (item.kind === "batch" && (request.tasks || []).length > 0) {
        firstTask = String(((request.tasks || [])[0] || {}).task || "").trim();
    }
    return {
        "targetRole": request.targetRole || "",
        "targetTags": options.normalizedTagList(request.targetTags || []),
        "requiredTool": request.requiredTool || "",
        "requiredChannel": request.requiredChannel || "",
        "requiredMemoryBackend": request.requiredMemoryBackend || "",
        "includeOffline": !!options.includeOffline,
        "originChannel": "gui",
        "originChatId": "desktop",
        "sessionKey": "gui:template-preview",
        "label": item.kind === "batch"
            ? options.firstNonEmptyValue([request.groupLabel, item.name], "Batch template preview")
            : options.firstNonEmptyValue([request.label, item.name], "Template preview"),
        "task": item.kind === "batch"
            ? options.firstNonEmptyValue([firstTask, request.groupLabel, item.name], "Preview delegated batch")
            : options.firstNonEmptyValue([request.task, item.name], "Preview delegated task"),
        "parentTaskId": "",
        "traceId": "template-preview-" + String(item.id || "draft")
    };
}

function submissionRequestFromDelegationTemplate(record, options) {
    var item = delegationTemplateRecord(record || ({}), false, options);
    var request = options.cloneMap(item.request || ({}));
    request.originChannel = "gui";
    request.originChatId = "desktop";
    request.sessionKey = options.delegationExecutionSessionKey(
        request.sessionKey || "",
        request.originChannel,
        request.originChatId);
    request.traceId = "template-submit-" + String(item.id || Date.now());
    if (item.kind === "batch") {
        request.groupLabel = options.firstNonEmptyValue([request.groupLabel, item.name], "Delegated batch");
        request.label = options.firstNonEmptyValue([request.label, request.groupLabel], request.groupLabel);
        request.tasks = options.normalizeBatchDraftTasks(request.tasks || []);
    } else {
        request.label = options.firstNonEmptyValue([request.label, item.name], "Delegated task");
        request.task = options.firstNonEmptyValue([request.task, item.name], "Delegated task");
    }
    return options.compactTemplateObject(request);
}

function delegationTemplateExportEnvelope(records, options) {
    var out = [];
    var source = records || [];
    for (var i = 0; i < source.length; ++i) {
        var record = delegationTemplateRecord(source[i], false, options);
        if (record.name.trim().length === 0) {
            continue;
        }
        out.push(record);
    }
    return {
        "schema": "yaos.delegation-templates/v1",
        "exportedAt": new Date().toISOString(),
        "templates": out
    };
}

function delegationTemplateExportText(records, options) {
    return JSON.stringify(delegationTemplateExportEnvelope(records || [], options), null, 2);
}

function delegationTemplateImportRecords(value, options) {
    var source = [];
    if (Array.isArray(value)) {
        source = value;
    } else if (value && typeof value === "object" && Array.isArray(value.templates)) {
        source = value.templates;
    } else if (value && typeof value === "object") {
        source = [value];
    }

    var out = [];
    for (var i = 0; i < source.length; ++i) {
        var record = delegationTemplateRecord(source[i], false, options);
        if (record.name.trim().length === 0) {
            continue;
        }
        out.push(record);
    }
    return out;
}

function importDelegationTemplatePayload(text, replaceExisting, existingRecords, options) {
    var payload = String(text || "").trim();
    if (payload.length === 0) {
        return {
            "ok": false,
            "error": "请先粘贴委托模板 JSON 封装."
        };
    }

    var parsed = null;
    try {
        parsed = JSON.parse(payload);
    } catch (error) {
        return {
            "ok": false,
            "error": "JSON 无效: " + error
        };
    }

    var imported = delegationTemplateImportRecords(parsed, options);
    if (imported.length === 0) {
        return {
            "ok": false,
            "error": "导入内容里没有找到有效的委托模板."
        };
    }

    var importedIds = {};
    for (var i = 0; i < imported.length; ++i) {
        importedIds[imported[i].id || ""] = true;
    }

    var merged = imported.slice(0);
    if (!replaceExisting) {
        var existing = existingRecords || [];
        for (var j = 0; j < existing.length; ++j) {
            var existingId = existing[j].id || "";
            if (!importedIds[existingId]) {
                merged.push(existing[j]);
            }
        }
    }

    return {
        "ok": true,
        "replace": !!replaceExisting,
        "importedRecords": imported,
        "mergedRecords": merged
    };
}
