#include "DelegationTemplateExchange.h"

#include <QDateTime>

namespace yaos::config {

namespace {

QString defaultTemplateNameFromRequest(const QString &kind, const QJsonObject &request) {
    if (kind == QStringLiteral("batch")) {
        const QString groupLabel = request.value(QStringLiteral("groupLabel")).toString().trimmed();
        if (!groupLabel.isEmpty()) {
            return groupLabel;
        }
        const QString label = request.value(QStringLiteral("label")).toString().trimmed();
        if (!label.isEmpty()) {
            return label;
        }
        return QStringLiteral("Delegation Batch Template");
    }

    const QString label = request.value(QStringLiteral("label")).toString().trimmed();
    if (!label.isEmpty()) {
        return label;
    }
    const QString task = request.value(QStringLiteral("task")).toString().trimmed();
    if (!task.isEmpty()) {
        return task;
    }
    return QStringLiteral("Delegation Template");
}

QString slugifiedTemplateName(QString value) {
    value = value.trimmed().toLower();
    QString slug;
    slug.reserve(value.size());
    bool pendingDash = false;
    for (const QChar ch : value) {
        if (ch.isLetterOrNumber()) {
            if (pendingDash && !slug.isEmpty()) {
                slug.append(QLatin1Char('-'));
            }
            slug.append(ch);
            pendingDash = false;
        } else if (!slug.isEmpty()) {
            pendingDash = true;
        }
    }
    while (slug.endsWith(QLatin1Char('-'))) {
        slug.chop(1);
    }
    return slug;
}

} // namespace

QString normalizedDelegationTemplateKind(QString kind) {
    kind = kind.trimmed().toLower();
    return kind == QStringLiteral("batch") ? QStringLiteral("batch") : QStringLiteral("single");
}

QJsonObject runtimeRequestFromDelegationTemplate(const DelegationTemplateConfig &record) {
    QJsonObject request = record.request;
    const QString kind = normalizedDelegationTemplateKind(record.kind);
    if (kind == QStringLiteral("batch")) {
        const QString groupLabel = request.value(QStringLiteral("groupLabel")).toString().trimmed().isEmpty()
            ? request.value(QStringLiteral("label")).toString()
            : request.value(QStringLiteral("groupLabel")).toString();
        if (!groupLabel.trimmed().isEmpty()) {
            request.insert(QStringLiteral("groupLabel"), groupLabel);
            if (request.value(QStringLiteral("label")).toString().trimmed().isEmpty()) {
                request.insert(QStringLiteral("label"), groupLabel);
            }
        }

        QString previewTask = request.value(QStringLiteral("task")).toString().trimmed();
        if (previewTask.isEmpty()) {
            const QJsonArray tasks = request.value(QStringLiteral("tasks")).toArray();
            if (!tasks.isEmpty()) {
                previewTask = tasks.first().toObject().value(QStringLiteral("task")).toString().trimmed();
            }
        }
        if (previewTask.isEmpty()) {
            previewTask = !groupLabel.trimmed().isEmpty()
                ? groupLabel
                : record.name.trimmed();
        }
        if (previewTask.isEmpty()) {
            previewTask = QStringLiteral("Preview delegated batch");
        }
        request.insert(QStringLiteral("task"), previewTask);
    } else {
        if (request.value(QStringLiteral("label")).toString().trimmed().isEmpty() &&
            !record.name.trimmed().isEmpty()) {
            request.insert(QStringLiteral("label"), record.name.trimmed());
        }
        if (request.value(QStringLiteral("task")).toString().trimmed().isEmpty()) {
            request.insert(QStringLiteral("task"),
                           record.name.trimmed().isEmpty()
                               ? QStringLiteral("Delegated task")
                               : record.name.trimmed());
        }
    }
    return request;
}

DelegationTemplateConfig normalizeDelegationTemplateRecord(const DelegationTemplateConfig &source,
                                                          int ordinal) {
    DelegationTemplateConfig record = source;
    record.kind = normalizedDelegationTemplateKind(record.kind);
    record.name = record.name.trimmed();
    record.note = record.note.trimmed();
    record.updatedAt = record.updatedAt.trimmed();
    if (record.name.isEmpty()) {
        record.name = defaultTemplateNameFromRequest(record.kind, record.request);
    }
    if (record.updatedAt.isEmpty()) {
        record.updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }
    record.id = record.id.trimmed();
    if (record.id.isEmpty()) {
        QString slug = slugifiedTemplateName(record.name);
        if (slug.isEmpty()) {
            slug = QStringLiteral("delegation-template");
        }
        record.id = QStringLiteral("%1-%2-%3")
                        .arg(record.kind,
                             slug,
                             QString::number(QDateTime::currentMSecsSinceEpoch() + ordinal));
    }
    return record;
}

QJsonObject delegationTemplateToExchangeJson(const DelegationTemplateConfig &record) {
    const DelegationTemplateConfig normalized = normalizeDelegationTemplateRecord(record);
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), normalized.id);
    obj.insert(QStringLiteral("name"), normalized.name);
    obj.insert(QStringLiteral("kind"), normalized.kind);
    obj.insert(QStringLiteral("note"), normalized.note);
    obj.insert(QStringLiteral("updatedAt"), normalized.updatedAt);
    obj.insert(QStringLiteral("request"), normalized.request);
    return obj;
}

QJsonArray delegationTemplateExchangeArray(const QList<DelegationTemplateConfig> &records) {
    QJsonArray array;
    for (const DelegationTemplateConfig &record : records) {
        array.append(delegationTemplateToExchangeJson(record));
    }
    return array;
}

QJsonObject delegationTemplateExchangeEnvelope(const QList<DelegationTemplateConfig> &records,
                                               const QString &sourceConfigPath,
                                               const QString &sourceNodeId,
                                               const QString &clusterId) {
    QJsonObject envelope;
    envelope.insert(QStringLiteral("schema"), QStringLiteral("yaos.delegation-templates/v1"));
    envelope.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!sourceConfigPath.trimmed().isEmpty()) {
        envelope.insert(QStringLiteral("sourceConfig"), sourceConfigPath.trimmed());
    }
    if (!sourceNodeId.trimmed().isEmpty()) {
        envelope.insert(QStringLiteral("sourceNodeId"), sourceNodeId.trimmed());
    }
    if (!clusterId.trimmed().isEmpty()) {
        envelope.insert(QStringLiteral("clusterId"), clusterId.trimmed());
    }
    envelope.insert(QStringLiteral("templates"), delegationTemplateExchangeArray(records));
    return envelope;
}

bool parseDelegationTemplateExchangeDocument(const QJsonDocument &document,
                                             QList<DelegationTemplateConfig> *records,
                                             QString *error) {
    if (!records) {
        if (error) {
            *error = QStringLiteral("Delegation template import target is null.");
        }
        return false;
    }

    QList<DelegationTemplateConfig> parsed;
    auto appendObject = [&parsed](const QJsonObject &obj) {
        DelegationTemplateConfig record;
        record.id = obj.value(QStringLiteral("id")).toString().trimmed();
        record.name = obj.value(QStringLiteral("name")).toString().trimmed();
        record.kind = obj.value(QStringLiteral("kind")).toString().trimmed();
        record.note = obj.value(QStringLiteral("note")).toString().trimmed();
        record.updatedAt = obj.value(QStringLiteral("updatedAt")).toString().trimmed();
        if (record.updatedAt.isEmpty()) {
            record.updatedAt = obj.value(QStringLiteral("updated_at")).toString().trimmed();
        }

        if (obj.value(QStringLiteral("request")).isObject()) {
            record.request = obj.value(QStringLiteral("request")).toObject();
        } else {
            QJsonObject request;
            static const QStringList directFields = {
                QStringLiteral("task"),
                QStringLiteral("label"),
                QStringLiteral("groupLabel"),
                QStringLiteral("targetNode"),
                QStringLiteral("targetRole"),
                QStringLiteral("requiredTool"),
                QStringLiteral("requiredChannel"),
                QStringLiteral("requiredMemoryBackend"),
                QStringLiteral("originChannel"),
                QStringLiteral("originChatId"),
                QStringLiteral("sessionKey"),
                QStringLiteral("traceId"),
                QStringLiteral("parentTaskId")
            };
            for (const QString &field : directFields) {
                if (obj.contains(field)) {
                    request.insert(field, obj.value(field));
                }
            }
            if (obj.contains(QStringLiteral("targetTags"))) {
                request.insert(QStringLiteral("targetTags"), obj.value(QStringLiteral("targetTags")));
            }
            if (obj.contains(QStringLiteral("tasks"))) {
                request.insert(QStringLiteral("tasks"), obj.value(QStringLiteral("tasks")));
            }
            if (!request.isEmpty()) {
                record.request = request;
            }
        }

        if (record.kind.trimmed().isEmpty()) {
            record.kind = record.request.value(QStringLiteral("tasks")).isArray()
                ? QStringLiteral("batch")
                : QStringLiteral("single");
        }

        record = normalizeDelegationTemplateRecord(record, parsed.size());
        if (!record.name.trimmed().isEmpty()) {
            parsed.append(record);
        }
    };

    if (document.isArray()) {
        const QJsonArray array = document.array();
        for (const QJsonValue &value : array) {
            if (value.isObject()) {
                appendObject(value.toObject());
            }
        }
    } else if (document.isObject()) {
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("templates")).isArray()) {
            const QJsonArray array = object.value(QStringLiteral("templates")).toArray();
            for (const QJsonValue &value : array) {
                if (value.isObject()) {
                    appendObject(value.toObject());
                }
            }
        } else {
            appendObject(object);
        }
    }

    if (parsed.isEmpty()) {
        if (error) {
            *error = QStringLiteral("No valid delegation templates were found in the exchange payload.");
        }
        return false;
    }

    QList<DelegationTemplateConfig> unique;
    for (const DelegationTemplateConfig &record : parsed) {
        bool replaced = false;
        for (int i = 0; i < unique.size(); ++i) {
            if (unique.at(i).id.trimmed() == record.id.trimmed()) {
                unique[i] = record;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            unique.append(record);
        }
    }

    *records = unique;
    return true;
}

QList<DelegationTemplateConfig> mergeDelegationTemplateRecords(
    const QList<DelegationTemplateConfig> &existing,
    const QList<DelegationTemplateConfig> &incoming,
    bool replaceExisting) {
    if (replaceExisting) {
        return incoming;
    }

    QList<DelegationTemplateConfig> merged = incoming;
    for (const DelegationTemplateConfig &record : existing) {
        bool overridden = false;
        for (const DelegationTemplateConfig &candidate : incoming) {
            if (candidate.id.trimmed() == record.id.trimmed()) {
                overridden = true;
                break;
            }
        }
        if (!overridden) {
            merged.append(normalizeDelegationTemplateRecord(record, merged.size()));
        }
    }
    return merged;
}

} // namespace yaos::config
