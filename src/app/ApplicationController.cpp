#if defined(_MSC_VER) && (_MSC_VER >= 1600)
#pragma execution_character_set("utf-8")
#endif

#include "ApplicationController.h"

#include <FastNet/Config.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTextCodec>
#include <QTextStream>
#include <QThread>
#include <QUrl>

#include <string>

#include "../config/ConfigLoader.h"
#include "../config/DelegationTemplateExchange.h"
#include "../control/ControlHttpServer.h"
#include "../control/ControlServiceCore.h"
#include "../daemon/LocalDaemonProtocol.h"
#include "../distributed/RemoteControlClient.h"
#include "../memory/MemoryHttpServer.h"
#include "../memory/MemoryServiceCore.h"
#include "../providers/AnthropicProvider.h"
#include "../providers/OpenAICompatibleProvider.h"
#include "../providers/ProviderOAuth.h"
#include "../providers/ProviderRegistry.h"
#include "../runtime/LocalRuntimeClient.h"
#include "../runtime/RuntimeHttpServer.h"
#include "../runtime/RuntimeServiceSupport.h"
#include "../runtime/StructuredLog.h"
#include "../runtime/Templates.h"

namespace yaos::app {

namespace {

QString optionValue(const QStringList &args, const QString &name) {
    const int index = args.indexOf(name);
    if (index >= 0 && index + 1 < args.size()) {
        return args.at(index + 1).trimmed();
    }
    return QString();
}

QStringList optionValues(const QStringList &args, const QString &name) {
    QStringList values;
    const int index = args.indexOf(name);
    if (index < 0 || index + 1 >= args.size()) {
        return values;
    }

    const QString raw = args.at(index + 1).trimmed();
    if (raw.isEmpty()) {
        return values;
    }

    const QStringList parts = raw.split(',', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString normalized = part.trimmed();
        if (!normalized.isEmpty()) {
            values.append(normalized);
        }
    }
    return values;
}

QString firstOptionValue(const QStringList &args, const QStringList &names) {
    for (const QString &name : names) {
        const QString value = optionValue(args, name);
        if (!value.trimmed().isEmpty()) {
            return value.trimmed();
        }
    }
    return QString();
}

bool hasOption(const QStringList &args, const QString &name) {
    return args.contains(name);
}

bool parsePositiveIntOption(const QStringList &args,
                            const QString &name,
                            int fallback,
                            int *value,
                            QString *error = nullptr) {
    if (!value) {
        if (error) {
            *error = QStringLiteral("%1 target is null.").arg(name);
        }
        return false;
    }

    *value = fallback;
    if (!hasOption(args, name)) {
        return true;
    }

    const QString raw = optionValue(args, name).trimmed();
    bool ok = false;
    const int parsed = raw.toInt(&ok);
    if (!ok || parsed <= 0) {
        if (error) {
            *error = QStringLiteral("%1 expects a positive integer value in seconds.").arg(name);
        }
        return false;
    }

    *value = parsed;
    return true;
}

void assignStringOption(QJsonObject *request,
                        const QStringList &args,
                        const QStringList &names,
                        const QString &field,
                        const QString &fallback = QString()) {
    if (!request) {
        return;
    }

    bool provided = false;
    for (const QString &name : names) {
        if (hasOption(args, name)) {
            provided = true;
            break;
        }
    }

    if (provided) {
        request->insert(field, firstOptionValue(args, names));
        return;
    }

    const QString current = request->value(field).toString().trimmed();
    if (!current.isEmpty()) {
        request->insert(field, current);
        return;
    }
    if (!fallback.isEmpty()) {
        request->insert(field, fallback);
    }
}

QString normalizedDelegationTemplateKind(QString kind) {
    kind = kind.trimmed().toLower();
    return kind == QStringLiteral("batch") ? QStringLiteral("batch") : QStringLiteral("single");
}

QJsonObject runtimeRequestFromTemplate(const config::DelegationTemplateConfig &record) {
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

bool loadDelegationTemplateRequest(const config::Config &cfg,
                                   const QString &templateId,
                                   QJsonObject *request,
                                   QString *templateName,
                                   QString *templateKind,
                                   QString *error) {
    if (!request) {
        if (error) {
            *error = QStringLiteral("Delegation template target is null.");
        }
        return false;
    }

    const QString normalizedId = templateId.trimmed();
    if (normalizedId.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Delegation template id is empty.");
        }
        return false;
    }

    for (const config::DelegationTemplateConfig &record : cfg.memory.delegationTemplates) {
        if (record.id.trimmed() != normalizedId) {
            continue;
        }
        *request = runtimeRequestFromTemplate(record);
        if (templateName) {
            *templateName = record.name.trimmed();
        }
        if (templateKind) {
            *templateKind = normalizedDelegationTemplateKind(record.kind);
        }
        return true;
    }

    if (error) {
        *error = QStringLiteral("Delegation template '%1' was not found in memory.delegationTemplates.")
                     .arg(normalizedId);
    }
    return false;
}

void applyDelegationRequestArgs(QJsonObject *request,
                                const QStringList &args,
                                const QString &defaultOriginChatId,
                                const QString &defaultSessionKey,
                                const QString &defaultTask,
                                const QString &defaultLabel,
                                const QString &defaultTraceId) {
    if (!request) {
        return;
    }

    assignStringOption(request,
                       args,
                       {QStringLiteral("--node"), QStringLiteral("--target-node")},
                       QStringLiteral("targetNode"));
    assignStringOption(request,
                       args,
                       {QStringLiteral("--role")},
                       QStringLiteral("targetRole"));
    if (hasOption(args, QStringLiteral("--tags"))) {
        request->insert(QStringLiteral("targetTags"),
                        QJsonArray::fromStringList(optionValues(args, QStringLiteral("--tags"))));
    } else if (!request->contains(QStringLiteral("targetTags"))) {
        request->insert(QStringLiteral("targetTags"), QJsonArray());
    }
    assignStringOption(request,
                       args,
                       {QStringLiteral("--tool")},
                       QStringLiteral("requiredTool"));
    assignStringOption(request,
                       args,
                       {QStringLiteral("--channel")},
                       QStringLiteral("requiredChannel"));
    assignStringOption(request,
                       args,
                       {QStringLiteral("--memory-backend")},
                       QStringLiteral("requiredMemoryBackend"));
    assignStringOption(request,
                       args,
                       {QStringLiteral("--origin-channel")},
                       QStringLiteral("originChannel"),
                       QStringLiteral("cli"));
    assignStringOption(request,
                       args,
                       {QStringLiteral("--origin-chat")},
                       QStringLiteral("originChatId"),
                       defaultOriginChatId);
    assignStringOption(request,
                       args,
                       {QStringLiteral("--session")},
                       QStringLiteral("sessionKey"),
                       defaultSessionKey);
    assignStringOption(request,
                       args,
                       {QStringLiteral("--task")},
                       QStringLiteral("task"),
                       defaultTask);
    assignStringOption(request,
                       args,
                       {QStringLiteral("--label")},
                       QStringLiteral("label"),
                       defaultLabel);
    assignStringOption(request,
                       args,
                       {QStringLiteral("--group-label")},
                       QStringLiteral("groupLabel"));
    assignStringOption(request,
                       args,
                       {QStringLiteral("--parent-task-id")},
                       QStringLiteral("parentTaskId"));
    assignStringOption(request,
                       args,
                       {QStringLiteral("--trace-id")},
                       QStringLiteral("traceId"),
                       defaultTraceId);

    if (hasOption(args, QStringLiteral("--include-offline"))) {
        request->insert(QStringLiteral("includeOffline"), true);
    } else if (!request->contains(QStringLiteral("includeOffline"))) {
        request->insert(QStringLiteral("includeOffline"), false);
    }
}

bool loadJsonObjectFile(const QString &path, QJsonObject *object, QString *error) {
    if (!object) {
        if (error) {
            *error = QStringLiteral("JSON target is null.");
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString().isEmpty()
                ? QStringLiteral("Failed to open request file.")
                : file.errorString();
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = parseError.error == QJsonParseError::NoError
                ? QStringLiteral("Delegation request file must contain a JSON object.")
                : parseError.errorString();
        }
        return false;
    }

    *object = document.object();
    return true;
}

bool loadJsonDocumentFile(const QString &path, QJsonDocument *document, QString *error) {
    if (!document) {
        if (error) {
            *error = QStringLiteral("JSON document target is null.");
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString().isEmpty()
                ? QStringLiteral("Failed to open template file.")
                : file.errorString();
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        (!parsed.isObject() && !parsed.isArray())) {
        if (error) {
            *error = parseError.error == QJsonParseError::NoError
                ? QStringLiteral("Template file must contain a JSON object or array.")
                : parseError.errorString();
        }
        return false;
    }

    *document = parsed;
    return true;
}

bool saveJsonDocumentFile(const QString &path, const QJsonDocument &document, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = file.errorString().isEmpty()
                ? QStringLiteral("Failed to open output file.")
                : file.errorString();
        }
        return false;
    }

    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        if (error) {
            *error = file.errorString().isEmpty()
                ? QStringLiteral("Failed to write JSON output file.")
                : file.errorString();
        }
        return false;
    }

    if (!file.commit()) {
        if (error) {
            *error = file.errorString().isEmpty()
                ? QStringLiteral("Failed to finalize JSON output file.")
                : file.errorString();
        }
        return false;
    }
    return true;
}

QString controlPlaneEndpointFromConfig(const config::Config &cfg) {
    const QString control = cfg.deployment.controlPlaneUrl.trimmed();
    if (!control.isEmpty()) {
        return control;
    }
    return cfg.deployment.registryUrl.trimmed();
}

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

config::DelegationTemplateConfig normalizedTemplateRecord(const config::DelegationTemplateConfig &source,
                                                          int ordinal = 0) {
    config::DelegationTemplateConfig record = source;
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

QJsonObject delegationTemplateToJsonObject(const config::DelegationTemplateConfig &record) {
    const config::DelegationTemplateConfig normalized = normalizedTemplateRecord(record);
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), normalized.id);
    obj.insert(QStringLiteral("name"), normalized.name);
    obj.insert(QStringLiteral("kind"), normalized.kind);
    obj.insert(QStringLiteral("note"), normalized.note);
    obj.insert(QStringLiteral("updatedAt"), normalized.updatedAt);
    obj.insert(QStringLiteral("request"), normalized.request);
    return obj;
}

QJsonArray delegationTemplatesToJsonArray(const QList<config::DelegationTemplateConfig> &records) {
    QJsonArray array;
    for (const config::DelegationTemplateConfig &record : records) {
        array.append(delegationTemplateToJsonObject(record));
    }
    return array;
}

config::DelegationTemplateConfig delegationTemplateFromJsonObject(const QJsonObject &obj, int ordinal = 0) {
    config::DelegationTemplateConfig record;
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
    return normalizedTemplateRecord(record, ordinal);
}

QJsonObject delegationTemplateExportEnvelope(const QList<config::DelegationTemplateConfig> &records,
                                             const QString &sourcePath) {
    QJsonObject envelope;
    envelope.insert(QStringLiteral("schema"), QStringLiteral("yaos.delegation-templates/v1"));
    envelope.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!sourcePath.trimmed().isEmpty()) {
        envelope.insert(QStringLiteral("sourceConfig"), sourcePath.trimmed());
    }
    envelope.insert(QStringLiteral("templates"), delegationTemplatesToJsonArray(records));
    return envelope;
}

bool parseDelegationTemplateImportDocument(const QJsonDocument &document,
                                          QList<config::DelegationTemplateConfig> *records,
                                          QString *error) {
    if (!records) {
        if (error) {
            *error = QStringLiteral("Delegation template import target is null.");
        }
        return false;
    }

    QList<config::DelegationTemplateConfig> parsed;
    auto appendObject = [&parsed](const QJsonObject &obj) {
        const config::DelegationTemplateConfig record =
            delegationTemplateFromJsonObject(obj, parsed.size());
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
            *error = QStringLiteral("No valid delegation templates were found in the import payload.");
        }
        return false;
    }

    QList<config::DelegationTemplateConfig> unique;
    for (const config::DelegationTemplateConfig &record : parsed) {
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

QList<config::DelegationTemplateConfig> mergeDelegationTemplates(
    const QList<config::DelegationTemplateConfig> &existing,
    const QList<config::DelegationTemplateConfig> &imported,
    bool replaceExisting) {
    if (replaceExisting) {
        return imported;
    }

    QList<config::DelegationTemplateConfig> merged = imported;
    for (const config::DelegationTemplateConfig &record : existing) {
        bool overridden = false;
        for (const config::DelegationTemplateConfig &incoming : imported) {
            if (incoming.id.trimmed() == record.id.trimmed()) {
                overridden = true;
                break;
            }
        }
        if (!overridden) {
            merged.append(normalizedTemplateRecord(record, merged.size()));
        }
    }
    return merged;
}

QUrl serviceUrlFrom(QString endpoint, const QString &fallbackEndpoint) {
    endpoint = endpoint.trimmed();
    if (endpoint.isEmpty()) {
        endpoint = fallbackEndpoint.trimmed().isEmpty()
            ? QStringLiteral("http://127.0.0.1:18891")
            : fallbackEndpoint.trimmed();
    }
    if (!endpoint.contains("://")) {
        endpoint.prepend(QStringLiteral("http://"));
    }
    return QUrl(endpoint);
}

bool resolveListenHost(const QString &host, QString *listenHost) {
    if (!listenHost) {
        return false;
    }

    const QString trimmed = host.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("127.0.0.1") || trimmed == QStringLiteral("localhost")) {
        *listenHost = QStringLiteral("127.0.0.1");
        return true;
    }
    if (trimmed == QStringLiteral("*") || trimmed == QStringLiteral("0.0.0.0")) {
        *listenHost = QStringLiteral("0.0.0.0");
        return true;
    }
    if (trimmed == QStringLiteral("::")) {
        *listenHost = QStringLiteral("::");
        return true;
    }
    if (trimmed == QStringLiteral("::1")) {
        *listenHost = QStringLiteral("::1");
        return true;
    }

    const QByteArray utf8 = trimmed.toUtf8();
    const std::string value(utf8.constData(), static_cast<size_t>(utf8.size()));
    if (FastNet::Address::isValidIPv4(value) || FastNet::Address::isValidIPv6(value)) {
        *listenHost = trimmed;
        return true;
    }
    return false;
}

QString normalizedProviderId(const QString &providerId) {
    QString normalized = providerId.trimmed().toLower();
    normalized.replace('-', '_');
    if (normalized == QStringLiteral("azureopenai")) normalized = QStringLiteral("azure_openai");
    if (normalized == QStringLiteral("code_buddy")) normalized = QStringLiteral("codebuddy");
    if (normalized == QStringLiteral("openaicodex")) normalized = QStringLiteral("openai_codex");
    if (normalized == QStringLiteral("githubcopilot")) normalized = QStringLiteral("github_copilot");
    return normalized;
}

const config::ProviderConfig *providerConfigById(const config::Config &config, const QString &providerId) {
    const QString normalized = normalizedProviderId(providerId);
    if (normalized == QStringLiteral("custom")) return &config.providers.custom;
    if (normalized == QStringLiteral("azure_openai")) return &config.providers.azureOpenAI;
    if (normalized == QStringLiteral("anthropic")) return &config.providers.anthropic;
    if (normalized == QStringLiteral("openai")) return &config.providers.openai;
    if (normalized == QStringLiteral("codebuddy")) return &config.providers.codebuddy;
    if (normalized == QStringLiteral("openrouter")) return &config.providers.openrouter;
    if (normalized == QStringLiteral("deepseek")) return &config.providers.deepseek;
    if (normalized == QStringLiteral("groq")) return &config.providers.groq;
    if (normalized == QStringLiteral("zhipu")) return &config.providers.zhipu;
    if (normalized == QStringLiteral("dashscope")) return &config.providers.dashscope;
    if (normalized == QStringLiteral("vllm")) return &config.providers.vllm;
    if (normalized == QStringLiteral("gemini")) return &config.providers.gemini;
    if (normalized == QStringLiteral("moonshot")) return &config.providers.moonshot;
    if (normalized == QStringLiteral("minimax")) return &config.providers.minimax;
    if (normalized == QStringLiteral("aihubmix")) return &config.providers.aihubmix;
    if (normalized == QStringLiteral("siliconflow")) return &config.providers.siliconflow;
    if (normalized == QStringLiteral("volcengine")) return &config.providers.volcengine;
    if (normalized == QStringLiteral("openai_codex")) return &config.providers.openaiCodex;
    if (normalized == QStringLiteral("github_copilot")) return &config.providers.githubCopilot;
    return nullptr;
}

config::ProviderConfig *providerConfigById(config::Config &config, const QString &providerId) {
    const QString normalized = normalizedProviderId(providerId);
    if (normalized == QStringLiteral("custom")) return &config.providers.custom;
    if (normalized == QStringLiteral("azure_openai")) return &config.providers.azureOpenAI;
    if (normalized == QStringLiteral("anthropic")) return &config.providers.anthropic;
    if (normalized == QStringLiteral("openai")) return &config.providers.openai;
    if (normalized == QStringLiteral("codebuddy")) return &config.providers.codebuddy;
    if (normalized == QStringLiteral("openrouter")) return &config.providers.openrouter;
    if (normalized == QStringLiteral("deepseek")) return &config.providers.deepseek;
    if (normalized == QStringLiteral("groq")) return &config.providers.groq;
    if (normalized == QStringLiteral("zhipu")) return &config.providers.zhipu;
    if (normalized == QStringLiteral("dashscope")) return &config.providers.dashscope;
    if (normalized == QStringLiteral("vllm")) return &config.providers.vllm;
    if (normalized == QStringLiteral("gemini")) return &config.providers.gemini;
    if (normalized == QStringLiteral("moonshot")) return &config.providers.moonshot;
    if (normalized == QStringLiteral("minimax")) return &config.providers.minimax;
    if (normalized == QStringLiteral("aihubmix")) return &config.providers.aihubmix;
    if (normalized == QStringLiteral("siliconflow")) return &config.providers.siliconflow;
    if (normalized == QStringLiteral("volcengine")) return &config.providers.volcengine;
    if (normalized == QStringLiteral("openai_codex")) return &config.providers.openaiCodex;
    if (normalized == QStringLiteral("github_copilot")) return &config.providers.githubCopilot;
    return nullptr;
}

QString localModelForProvider(const QString &providerId, const QString &model) {
    QString local = model.trimmed();
    if (local.isEmpty()) {
        return local;
    }

    const providers::ProviderSpec spec = providers::findProviderSpec(normalizedProviderId(providerId));
    if (!spec.litellmPrefix.isEmpty()) {
        const QString prefix = spec.litellmPrefix + QStringLiteral("/");
        if (local.startsWith(prefix)) {
            local = local.mid(prefix.size());
        }
    }
    for (const QString &skip : spec.skipPrefixes) {
        if (!skip.isEmpty() && local.startsWith(skip)) {
            local = local.mid(skip.size());
            break;
        }
    }
    if (spec.stripModelPrefix && local.contains('/')) {
        local = local.section('/', 1);
    }
    return local.trimmed();
}

QString routedModelForProvider(const QString &providerId, const QString &model) {
    const providers::ProviderSpec spec = providers::findProviderSpec(normalizedProviderId(providerId));
    if (spec.name.isEmpty()) {
        return model.trimmed();
    }
    return providers::routeModelForProvider(spec, model.trimmed());
}

QString defaultApiBaseForProvider(const QString &providerId) {
    const QString normalized = normalizedProviderId(providerId);
    const QString oauthApiBase = providers::defaultApiBaseForProvider(normalized);
    if (!oauthApiBase.trimmed().isEmpty()) {
        return oauthApiBase.trimmed();
    }
    const providers::ProviderSpec spec = providers::findProviderSpec(normalized);
    if (!spec.defaultApiBase.trimmed().isEmpty()) {
        return spec.defaultApiBase.trimmed();
    }
    if (normalized == QStringLiteral("vllm")) {
        return QStringLiteral("http://127.0.0.1:8000/v1");
    }
    return QString();
}

QString resolvedApiBaseForProvider(const QString &providerId, const QString &apiBase) {
    const QString trimmed = apiBase.trimmed();
    return trimmed.isEmpty() ? defaultApiBaseForProvider(providerId) : trimmed;
}

QString preferredModelForProvider(const config::Config &config, const QString &providerId) {
    const config::ProviderConfig *provider = providerConfigById(config, providerId);
    if (!provider) {
        return QString();
    }
    if (!provider->model.trimmed().isEmpty()) {
        return provider->model.trimmed();
    }

    QString activeProvider = normalizedProviderId(config.agentDefaults.provider);
    if (activeProvider.isEmpty() || activeProvider == QStringLiteral("auto")) {
        activeProvider = normalizedProviderId(config.matchedProviderName(config.agentDefaults.model));
    }
    if (activeProvider == normalizedProviderId(providerId)) {
        return localModelForProvider(providerId, config.agentDefaults.model);
    }
    return QString();
}

QStringList fallbackModelCatalogForProvider(const QString &providerId) {
    const QString normalized = normalizedProviderId(providerId);
    if (normalized == QStringLiteral("github_copilot")) {
        return QStringList{
            QStringLiteral("claude-haiku-4.5"),
            QStringLiteral("claude-sonnet-4"),
            QStringLiteral("claude-sonnet-4.5"),
            QStringLiteral("gpt-4.1"),
            QStringLiteral("gpt-4o"),
            QStringLiteral("gpt-5"),
            QStringLiteral("gpt-5-mini")
        };
    }
    return {};
}

QJsonArray stringListToJson(const QStringList &values) {
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QJsonObject providerStatusJson(const providers::ProviderOAuthResult &result,
                               const config::ProviderConfig &providerConfig) {
    QJsonObject object = QJsonObject::fromVariantMap(result.toVariantMap());
    object.insert(QStringLiteral("providerId"), normalizedProviderId(result.providerId));
    object.insert(QStringLiteral("configuredModel"), providerConfig.model.trimmed());
    object.insert(QStringLiteral("availableModels"), stringListToJson(providerConfig.availableModels));
    object.insert(QStringLiteral("availableModelCount"), providerConfig.availableModels.size());
    object.insert(QStringLiteral("enabledModels"), stringListToJson(providerConfig.enabledModels));
    object.insert(QStringLiteral("enabledModelCount"), providerConfig.enabledModels.size());
    object.insert(QStringLiteral("extraHeaderCount"), providerConfig.extraHeaders.size());
    return object;
}

QJsonObject providerModelsJson(const QString &providerId,
                               const providers::ProviderOAuthResult &auth,
                               const QString &apiBase,
                               const QString &selectedModel,
                               const QStringList &models,
                               bool persistedCatalog,
                               bool persistedAuthState) {
    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("providerId"), normalizedProviderId(providerId)},
        {QStringLiteral("apiBase"), apiBase},
        {QStringLiteral("selectedModel"), selectedModel},
        {QStringLiteral("modelCount"), models.size()},
        {QStringLiteral("models"), stringListToJson(models)},
        {QStringLiteral("persistedCatalog"), persistedCatalog},
        {QStringLiteral("persistedAuthState"), persistedAuthState},
        {QStringLiteral("auth"), providerStatusJson(auth, auth.config)}
    };
}

struct ProviderModelSyncResult {
    bool ok = false;
    providers::ProviderOAuthResult auth;
    config::ProviderConfig providerConfig;
    QString apiBase;
    QString selectedModel;
    QStringList models;
    bool persistedAuthState = false;
    bool persistedCatalog = false;
    QString warning;
    QString error;
};

QJsonObject providerModelSyncJson(const ProviderModelSyncResult &result) {
    QJsonObject object{
        {QStringLiteral("ok"), result.ok},
        {QStringLiteral("apiBase"), result.apiBase},
        {QStringLiteral("selectedModel"), result.selectedModel},
        {QStringLiteral("modelCount"), result.models.size()},
        {QStringLiteral("models"), stringListToJson(result.models)},
        {QStringLiteral("persistedCatalog"), result.persistedCatalog},
        {QStringLiteral("persistedAuthState"), result.persistedAuthState}
    };
    if (!result.warning.trimmed().isEmpty()) {
        object.insert(QStringLiteral("warning"), result.warning.trimmed());
    }
    if (!result.error.trimmed().isEmpty()) {
        object.insert(QStringLiteral("error"), result.error.trimmed());
    }
    return object;
}

bool persistProviderOAuthState(config::Config *liveConfig,
                               const QString &providerId,
                               providers::ProviderOAuthResult *result,
                               runtime::RuntimeCore *runtimeCore,
                               bool *persistedAuthState,
                               const QString &saveErrorMessage) {
    if (!liveConfig || !result || !result->changed) {
        return true;
    }

    config::ProviderConfig *provider = providerConfigById(*liveConfig, providerId);
    if (!provider) {
        result->ok = false;
        result->pending = false;
        result->error = QStringLiteral("Unknown provider: %1").arg(providerId);
        return false;
    }

    *provider = result->config;
    if (!config::ConfigLoader::save(*liveConfig)) {
        result->ok = false;
        result->pending = false;
        result->error = saveErrorMessage;
        return false;
    }

    if (persistedAuthState) {
        *persistedAuthState = true;
    }
    if (runtimeCore) {
        runtimeCore->reloadFromDisk();
    }
    return true;
}

providers::ProviderOAuthResult waitForProviderDeviceFlow(providers::ProviderOAuthResult current,
                                                         config::Config *liveConfig,
                                                         const QString &providerId,
                                                         runtime::RuntimeCore *runtimeCore,
                                                         int timeoutSeconds,
                                                         bool *persistedAuthState,
                                                         int *pollAttempts,
                                                         int *waitedSeconds,
                                                         bool *timedOut,
                                                         QTextStream *progressOut) {
    if (pollAttempts) {
        *pollAttempts = 0;
    }
    if (waitedSeconds) {
        *waitedSeconds = 0;
    }
    if (timedOut) {
        *timedOut = false;
    }

    if (!liveConfig || !current.pending || timeoutSeconds <= 0) {
        return current;
    }

    const qint64 timeoutMs = static_cast<qint64>(timeoutSeconds) * 1000;
    int attempts = 0;
    QElapsedTimer timer;
    timer.start();

    while (current.pending && timer.elapsed() < timeoutMs) {
        config::ProviderConfig *provider = providerConfigById(*liveConfig, providerId);
        if (!provider) {
            current.ok = false;
            current.pending = false;
            current.error = QStringLiteral("Unknown provider: %1").arg(providerId);
            break;
        }

        const qint64 remainingMs = timeoutMs - timer.elapsed();
        if (remainingMs <= 0) {
            break;
        }

        const int nextPollDelay = qMax(1,
                                       qMin(provider->oauthIntervalSec,
                                            static_cast<int>((remainingMs + 999) / 1000)));
        if (progressOut) {
            *progressOut << "Waiting " << nextPollDelay << "s before poll " << (attempts + 1) << "...\n";
            progressOut->flush();
        }
        QThread::sleep(static_cast<unsigned long>(nextPollDelay));

        provider = providerConfigById(*liveConfig, providerId);
        if (!provider) {
            current.ok = false;
            current.pending = false;
            current.error = QStringLiteral("Unknown provider: %1").arg(providerId);
            break;
        }

        current = providers::pollDeviceFlow(providerId, *provider);
        ++attempts;
        if (!persistProviderOAuthState(liveConfig,
                                       providerId,
                                       &current,
                                       runtimeCore,
                                       persistedAuthState,
                                       QStringLiteral("failed to persist OAuth state"))) {
            break;
        }

        if (progressOut && current.pending) {
            const config::ProviderConfig *snapshot = providerConfigById(*liveConfig, providerId);
            const int nextInterval = snapshot ? snapshot->oauthIntervalSec : current.config.oauthIntervalSec;
            *progressOut << "Poll " << attempts << ": login still pending";
            if (nextInterval > 0) {
                *progressOut << " (next interval " << nextInterval << "s)";
            }
            *progressOut << "\n";
            progressOut->flush();
        }
    }

    if (pollAttempts) {
        *pollAttempts = attempts;
    }
    if (waitedSeconds) {
        *waitedSeconds = static_cast<int>((timer.elapsed() + 999) / 1000);
    }
    if (timedOut) {
        *timedOut = current.pending;
    }
    return current;
}

ProviderModelSyncResult syncProviderModelCatalog(config::Config *liveConfig,
                                                 const QString &providerId,
                                                 runtime::RuntimeCore *runtimeCore,
                                                 bool allowRefresh) {
    ProviderModelSyncResult out;
    if (!liveConfig) {
        out.error = QStringLiteral("Config target is null.");
        return out;
    }

    const QString normalized = normalizedProviderId(providerId);
    config::ProviderConfig *provider = providerConfigById(*liveConfig, normalized);
    if (!provider) {
        out.error = QStringLiteral("Unknown provider: %1").arg(normalized);
        return out;
    }
    out.providerConfig = *provider;
    out.auth = providers::providerOAuthStatus(normalized, *provider);

    const bool supportsCatalogSync = normalized != QStringLiteral("azure_openai");
    if (!supportsCatalogSync) {
        out.error = QStringLiteral("Model sync is not supported for this provider.");
        return out;
    }

    out.auth = providers::resolveProviderAccess(normalized, *provider, allowRefresh);
    if (out.auth.changed) {
        *provider = out.auth.config;
        if (!config::ConfigLoader::save(*liveConfig)) {
            out.warning = QStringLiteral("Refreshed provider credentials were used, but YAOS could not persist them.");
        } else {
            out.persistedAuthState = true;
            if (runtimeCore) {
                runtimeCore->reloadFromDisk();
            }
        }
    }

    const config::ProviderConfig providerSnapshot = *provider;
    out.providerConfig = providerSnapshot;
    out.apiBase = out.auth.apiBase.trimmed().isEmpty()
        ? resolvedApiBaseForProvider(normalized, providerSnapshot.apiBase)
        : out.auth.apiBase.trimmed();
    const bool trustStoredCredentialFallback =
        !(normalized == QStringLiteral("github_copilot") && !out.auth.ok);
    const QString apiKey = out.auth.apiKey.trimmed().isEmpty()
        ? (trustStoredCredentialFallback ? providerSnapshot.apiKey : QString())
        : out.auth.apiKey;
    const QHash<QString, QString> extraHeaders =
        out.auth.headers.isEmpty()
            ? (trustStoredCredentialFallback ? providerSnapshot.extraHeaders : QHash<QString, QString>())
            : out.auth.headers;
    const bool hasApiKey = !apiKey.trimmed().isEmpty();
    const bool hasExtraHeaders = !extraHeaders.isEmpty();
    const bool requiresApiKey = normalized == QStringLiteral("anthropic");
    const bool requiresCredential =
        normalized != QStringLiteral("custom") &&
        normalized != QStringLiteral("vllm") &&
        normalized != QStringLiteral("anthropic");
    if (!out.auth.ok && !hasApiKey && !hasExtraHeaders) {
        out.error = out.auth.error.trimmed().isEmpty()
            ? QStringLiteral("Complete the provider login before loading models.")
            : out.auth.error.trimmed();
        return out;
    }
    if ((requiresApiKey && !hasApiKey) || (requiresCredential && !hasApiKey && !hasExtraHeaders)) {
        out.error = requiresCredential
            ? QStringLiteral("Fill API key or extra headers before loading models.")
            : QStringLiteral("Fill the API key before loading models.");
        return out;
    }
    if (normalized != QStringLiteral("anthropic") && out.apiBase.isEmpty()) {
        out.error = QStringLiteral("Missing API base.");
        return out;
    }

    out.selectedModel = preferredModelForProvider(*liveConfig, normalized);
    QStringList models;
    if (normalized == QStringLiteral("anthropic")) {
        providers::AnthropicProvider providerClient(apiKey,
                                                    out.apiBase,
                                                    routedModelForProvider(normalized, out.selectedModel));
        models = providerClient.listModels();
    } else {
        providers::OpenAICompatibleProvider providerClient(apiKey,
                                                           out.apiBase,
                                                           routedModelForProvider(normalized, out.selectedModel),
                                                           normalized,
                                                           liveConfig->agentDefaults.reasoningEffort,
                                                           extraHeaders);
        models = providerClient.listModels();
    }

    QStringList normalizedModels;
    QSet<QString> seen;
    for (const QString &model : models) {
        const QString local = localModelForProvider(normalized, model);
        if (local.isEmpty()) {
            continue;
        }
        const QString dedupeKey = local.toLower();
        if (seen.contains(dedupeKey)) {
            continue;
        }
        seen.insert(dedupeKey);
        normalizedModels.append(local);
    }
    normalizedModels.sort(Qt::CaseInsensitive);

    if (normalizedModels.isEmpty()) {
        const QStringList fallbackModels = fallbackModelCatalogForProvider(normalized);
        if (fallbackModels.isEmpty()) {
            out.error = QStringLiteral("The provider returned an empty model list.");
            return out;
        }
        normalizedModels = fallbackModels;
        out.warning = QStringLiteral(
            "The provider did not return a live model catalog. YAOS used the built-in candidate list instead.");
    }

    provider = providerConfigById(*liveConfig, normalized);
    if (provider && provider->availableModels != normalizedModels) {
        provider->availableModels = normalizedModels;
        if (!config::ConfigLoader::save(*liveConfig)) {
            if (out.warning.isEmpty()) {
                out.warning = QStringLiteral("The model list was fetched, but YAOS could not persist it.");
            }
        } else {
            out.persistedCatalog = true;
            if (runtimeCore) {
                runtimeCore->reloadFromDisk();
            }
        }
    }

    provider = providerConfigById(*liveConfig, normalized);
    out.providerConfig = provider ? *provider : providerSnapshot;
    out.auth.config = out.providerConfig;
    out.models = normalizedModels;
    out.ok = true;
    return out;
}

} // namespace

ApplicationController::ApplicationController()
    : _runtime(std::make_unique<runtime::RuntimeCore>()) {}

ApplicationController::~ApplicationController() = default;

RunResult ApplicationController::run(const QStringList &args) {
    if (args.size() <= 1) {
        return help();
    }

    const QString command = args.at(1).trimmed().toLower();
    const bool wantsHelp = args.contains("--help") || args.contains("-h");
    if (wantsHelp && command != "agent" && command != "daemon" &&
        command != "runtime-service" && command != "memory-service" && command != "control-service" &&
        command != "provider-login" && command != "provider-poll" &&
        command != "provider-refresh" && command != "provider-logout" &&
        command != "provider-status" && command != "provider-models" &&
        command != "route-preview" && command != "submit-delegation" &&
        command != "template-export" && command != "template-import" &&
        command != "template-push" && command != "template-pull") {
        return help();
    }

    if (command == "init") return init();
    if (command == "config") return config();
    if (command == "status") return status();
    if (command == "provider-login") return providerLogin(args);
    if (command == "provider-poll") return providerPoll(args);
    if (command == "provider-refresh") return providerRefresh(args);
    if (command == "provider-logout") return providerLogout(args);
    if (command == "provider-status") return providerStatus(args);
    if (command == "provider-models") return providerModels(args);
    if (command == "route-preview") return routePreview(args);
    if (command == "submit-delegation") return submitDelegation(args);
    if (command == "template-export") return templateExport(args);
    if (command == "template-import") return templateImport(args);
    if (command == "template-push") return templatePush(args);
    if (command == "template-pull") return templatePull(args);
    if (command == "agent") return agent(args);
    if (command == "gateway") return gateway();
    if (command == "daemon") return daemon(args);
    if (command == "runtime-service") return runtimeService(args);
    if (command == "memory-service") return memoryService(args);
    if (command == "control-service") return controlService(args);
    return help();
}

StatusSnapshot ApplicationController::statusSnapshot() {
    return _runtime->statusSnapshot();
}

bool ApplicationController::initializeWorkspace(QString *message) {
    return _runtime->initializeWorkspace(message);
}

bool ApplicationController::reloadFromDisk(const QString &modelOverride, const QString &providerOverride) {
    return _runtime->reloadFromDisk(modelOverride, providerOverride);
}

bool ApplicationController::startGatewayServices() {
    return _runtime->startGatewayServices();
}

void ApplicationController::stopGatewayServices() {
    _runtime->stopGatewayServices();
}

bool ApplicationController::gatewayRunning() const {
    return _runtime->gatewayRunning();
}

QVector<runtime::ApprovalRecord> ApplicationController::recentApprovals(int limit, const QString &state) {
    return _runtime->recentApprovals(limit, state);
}

bool ApplicationController::resolveApproval(const QString &approvalId,
                                            const QString &decision,
                                            const QString &scope,
                                            const QString &note) {
    return _runtime->resolveApproval(approvalId, decision, scope, note);
}

QVector<runtime::NotificationRecord> ApplicationController::recentNotifications(int limit, bool unreadOnly) {
    return _runtime->recentNotifications(limit, unreadOnly);
}

void ApplicationController::markAllNotificationsRead() {
    _runtime->markAllNotificationsRead();
}

QVector<runtime::TaskRecord> ApplicationController::recentTasks(int limit) {
    return _runtime->recentTasks(limit);
}

QVector<runtime::EventRecord> ApplicationController::recentEvents(int limit) {
    return _runtime->recentEvents(limit);
}

runtime::ResourceSummary ApplicationController::resourceSummary() {
    return _runtime->resourceSummary();
}

QVector<runtime::ResourceRecord> ApplicationController::recentResources(int limit, const QString &kind) {
    return _runtime->recentResources(limit, kind);
}

QVector<runtime::AutomationRecord> ApplicationController::automations(int limit) {
    return _runtime->automations(limit);
}

QVector<runtime::AutomationRunRecord> ApplicationController::automationRuns(int limit,
                                                                            const QString &automationId) {
    return _runtime->automationRuns(limit, automationId);
}

runtime::AutomationRecord ApplicationController::automation(const QString &id) {
    return _runtime->automation(id);
}

QString ApplicationController::saveAutomation(const runtime::AutomationRecord &record, QString *error) {
    return _runtime->saveAutomation(record, error);
}

bool ApplicationController::removeAutomation(const QString &id) {
    return _runtime->removeAutomation(id);
}

QString ApplicationController::runAutomation(const QString &id,
                                             QString *error,
                                             const QString &sessionKey) {
    return _runtime->runAutomation(id, error, sessionKey);
}

QVector<runtime::PluginRecord> ApplicationController::plugins() {
    return _runtime->plugins();
}

QVector<runtime::SkillRecord> ApplicationController::skills() {
    return _runtime->skills();
}

ChatTurnResult ApplicationController::processMessageDetailed(const QString &content,
                                                             const QString &sessionKey,
                                                             const QString &channel,
                                                             const QString &chatId,
                                                             const QString &modelOverride,
                                                             const QString &providerOverride) {
    return _runtime->processMessageDetailed(content,
                                            sessionKey,
                                            channel,
                                            chatId,
                                            modelOverride,
                                            providerOverride);
}

QString ApplicationController::processMessage(const QString &content,
                                              const QString &sessionKey,
                                              const QString &channel,
                                              const QString &chatId,
                                              const QString &modelOverride,
                                              const QString &providerOverride) {
    return _runtime->processMessage(content,
                                    sessionKey,
                                    channel,
                                    chatId,
                                    modelOverride,
                                    providerOverride);
}

RunResult ApplicationController::init() {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    QString message;
    if (!initializeWorkspace(&message)) {
        out << message << "\n";
        return RunResult::Error;
    }
    out << message << "\n";
    return RunResult::Ok;
}

RunResult ApplicationController::config() {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    out << "Configuration is managed by the QML studio.\n";
    out << "Config file: " << config::ConfigLoader::defaultConfigPath() << "\n";
    return RunResult::Ok;
}

RunResult ApplicationController::status() {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    const StatusSnapshot snapshot = statusSnapshot();
    const bool configExists = QFileInfo::exists(snapshot.configPath);
    out << "--- YAOS Runtime Status ---\n";
    out << "Version: source tree\n";
    out << "Config: " << snapshot.configPath
        << (snapshot.configReady ? " [ready]" : (configExists ? " [invalid]" : " [missing]")) << "\n";
    out << "Workspace: " << snapshot.workspacePath
        << (snapshot.workspaceReady ? " [ready]" : " [missing]") << "\n";
    out << "Default model: " << snapshot.defaultModel << "\n";
    out << "Routed provider: " << snapshot.routedProvider << "\n";
    out << "Actual backend: " << snapshot.actualBackend
        << (snapshot.backendFallback ? " [fallback]" : "") << "\n";
    out << "Runtime mode: " << (snapshot.runtimeMode.isEmpty() ? "embedded" : snapshot.runtimeMode) << "\n";
    out << "Runtime service: "
        << (snapshot.runtimeServiceEnabled ? "enabled" : "inactive")
        << " | reachable=" << (snapshot.runtimeServiceEnabled ? (snapshot.runtimeServiceReachable ? "yes" : "no") : "n/a")
        << " | auto-spawn=" << (snapshot.runtimeServiceAutoSpawn ? "yes" : "no") << "\n";
    out << "Runtime endpoint: "
        << (snapshot.runtimeEndpoint.isEmpty() ? "none" : snapshot.runtimeEndpoint) << "\n";
    out << "Runtime advertise endpoint: "
        << (snapshot.runtimeAdvertiseEndpoint.isEmpty() ? "none" : snapshot.runtimeAdvertiseEndpoint) << "\n";
    out << "Control plane: "
        << (snapshot.controlPlaneEndpoint.isEmpty() ? "local fallback" : snapshot.controlPlaneEndpoint)
        << " | reachable="
        << (snapshot.controlPlaneEndpoint.isEmpty() ? "n/a" : (snapshot.controlPlaneReachable ? "yes" : "no"))
        << "\n";
    const QJsonObject controlTaskBus = snapshot.controlPlaneHealth.value(QStringLiteral("taskBus")).toObject();
    if (!controlTaskBus.isEmpty()) {
        out << "Control task bus: queued=" << controlTaskBus.value(QStringLiteral("queuedTaskCount")).toInt()
            << " | leased=" << controlTaskBus.value(QStringLiteral("leasedTaskCount")).toInt()
            << " | results=" << controlTaskBus.value(QStringLiteral("recentResultCount")).toInt()
            << " | reclaimed=" << controlTaskBus.value(QStringLiteral("expiredReclaimedCount")).toInt()
            << " | stale-suppressed=" << controlTaskBus.value(QStringLiteral("staleSuppressedResultCount")).toInt()
            << "\n";
        const QJsonArray recentEvents = controlTaskBus.value(QStringLiteral("recentEvents")).toArray();
        if (!recentEvents.isEmpty()) {
            QStringList summaries;
            summaries.reserve(qMin(3, recentEvents.size()));
            for (int i = 0; i < recentEvents.size() && i < 3; ++i) {
                const QJsonObject event = recentEvents.at(i).toObject();
                QString summary = event.value(QStringLiteral("type")).toString();
                const QString taskId = event.value(QStringLiteral("taskId")).toString();
                const QString nodeId = event.value(QStringLiteral("nodeId")).toString();
                if (!taskId.isEmpty()) {
                    summary += QStringLiteral(":%1").arg(taskId);
                }
                if (!nodeId.isEmpty()) {
                    summary += QStringLiteral("@%1").arg(nodeId);
                }
                summaries.append(summary);
            }
            out << "Control recent events: " << summaries.join(QStringLiteral(" || ")) << "\n";
        }
    }
    out << "Registry: "
        << (snapshot.registryEndpoint.isEmpty() ? "local fallback" : snapshot.registryEndpoint)
        << " | reachable="
        << (snapshot.registryEndpoint.isEmpty() ? "n/a" : (snapshot.registryReachable ? "yes" : "no"))
        << "\n";
    out << "Memory service: "
        << (snapshot.memoryServiceEnabled ? "enabled" : "disabled")
        << " | reachable=" << (snapshot.memoryServiceReachable ? "yes" : "no")
        << " | auto-spawn=" << (snapshot.memoryServiceAutoSpawn ? "yes" : "no") << "\n";
    out << "Memory service endpoint: "
        << (snapshot.memoryServiceEndpoint.isEmpty() ? "none" : snapshot.memoryServiceEndpoint) << "\n";
    out << "MCP servers: " << snapshot.mcpServerCount << "\n";
    for (const QJsonValue &value : snapshot.providerOAuthStatuses) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject provider = value.toObject();
        const QString providerId = provider.value(QStringLiteral("providerId")).toString(QStringLiteral("unknown"));
        out << "Provider auth: " << providerId
            << " | logged-in=" << (provider.value(QStringLiteral("loggedIn")).toBool() ? "yes" : "no")
            << " | pending=" << (provider.value(QStringLiteral("pending")).toBool() ? "yes" : "no")
            << " | api-key=" << (provider.value(QStringLiteral("hasApiKey")).toBool() ? "yes" : "no")
            << " | oauth-token=" << (provider.value(QStringLiteral("hasOAuthAccessToken")).toBool() ? "yes" : "no")
            << " | refresh-token=" << (provider.value(QStringLiteral("hasRefreshToken")).toBool() ? "yes" : "no")
            << " | expires-soon=" << (provider.value(QStringLiteral("expiresSoon")).toBool() ? "yes" : "no")
            << " | models=" << provider.value(QStringLiteral("modelCount")).toInt()
            << "\n";

        QStringList details;
        const QString model = provider.value(QStringLiteral("model")).toString();
        const QString accountId = provider.value(QStringLiteral("accountId")).toString();
        const QString expiresAt = provider.value(QStringLiteral("expiresAt")).toString();
        const QString lastRefreshAt = provider.value(QStringLiteral("lastRefreshAt")).toString();
        const QString credentialMode = provider.value(QStringLiteral("credentialMode")).toString();
        if (!model.isEmpty()) {
            details.append(QStringLiteral("model=%1").arg(model));
        }
        if (!accountId.isEmpty()) {
            details.append(QStringLiteral("account=%1").arg(accountId));
        }
        if (!credentialMode.isEmpty()) {
            details.append(QStringLiteral("credential=%1").arg(credentialMode));
        }
        if (!expiresAt.isEmpty()) {
            details.append(QStringLiteral("expires=%1").arg(expiresAt));
        }
        if (!lastRefreshAt.isEmpty()) {
            details.append(QStringLiteral("last-refresh=%1").arg(lastRefreshAt));
        }
        if (provider.value(QStringLiteral("requiresClientId")).toBool()) {
            details.append(QStringLiteral("client-id=missing"));
        }
        if (!details.isEmpty()) {
            out << "Auth details: " << providerId << " | " << details.join(QStringLiteral(" | ")) << "\n";
        }

        const QString lastError = provider.value(QStringLiteral("lastError")).toString().trimmed();
        if (!lastError.isEmpty()) {
            out << "Auth error: " << providerId << " | " << lastError << "\n";
        }
    }
    out << "Enabled channels: "
        << (snapshot.enabledChannels.isEmpty() ? "none" : snapshot.enabledChannels.join(", ")) << "\n";
    out << "Enabled tool capabilities: "
        << (snapshot.enabledToolCapabilities.isEmpty() ? "none" : snapshot.enabledToolCapabilities.join(", ")) << "\n";
    out << "Restrict to workspace: " << (snapshot.restrictToWorkspace ? "yes" : "no") << "\n";
    out << "Gateway running: " << (snapshot.gatewayRunning ? "yes" : "no") << "\n";
    out << "Task count: " << snapshot.taskCount << "\n";
    out << "Event count: " << snapshot.eventCount << "\n";
    out << "Pending approvals: " << snapshot.pendingApprovalCount << "\n";
    out << "Unread notifications: " << snapshot.unreadNotificationCount << "\n";
    out << "Automations: " << snapshot.automationCount << "\n";
    out << "Plugins: " << snapshot.pluginCount << "\n";
    out << "Skills: " << snapshot.skillCount << "\n";
    out << "Resources: " << snapshot.resourceCount << "\n";
    out << "---------------------------\n";
    return RunResult::Ok;
}

RunResult ApplicationController::providerLogin(const QStringList &args) {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    const QString providerArg = args.size() > 2 ? args.at(2).trimmed() : QString();
    const QString normalized = normalizedProviderId(providerArg.isEmpty()
                                                        ? optionValue(args, QStringLiteral("--provider"))
                                                        : providerArg);
    const bool wantsHelp = args.contains("--help") || args.contains("-h");
    const bool waitForCompletion = args.contains(QStringLiteral("--wait"));
    const bool syncModels = args.contains(QStringLiteral("--sync-models"));
    if (wantsHelp || normalized.isEmpty()) {
        out << "Usage: yaos provider-login <provider> [--wait] [--sync-models] [--timeout <seconds>] [--json]\n"
            << "Starts OAuth device login for a single provider.\n"
            << "Examples:\n"
            << "  yaos provider-login openai_codex\n"
            << "  yaos provider-login openai_codex --wait\n"
            << "  yaos provider-login openai_codex --wait --sync-models\n"
            << "  yaos provider-login github_copilot --wait --sync-models --timeout 180 --json\n";
        return wantsHelp ? RunResult::Ok : RunResult::Error;
    }

    const bool asJson = args.contains("--json");
    int timeoutSeconds = 300;
    QString timeoutError;
    if (!parsePositiveIntOption(args,
                                QStringLiteral("--timeout"),
                                300,
                                &timeoutSeconds,
                                &timeoutError)) {
        if (asJson) {
            out << QJsonDocument(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("providerId"), normalized},
                {QStringLiteral("error"), timeoutError}
            }).toJson(QJsonDocument::Indented);
        } else {
            out << timeoutError << "\n";
        }
        return RunResult::Error;
    }
    if (!providers::isOAuthProvider(normalized)) {
        const QString error = QStringLiteral("This provider does not use OAuth.");
        if (asJson) {
            out << QJsonDocument(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("providerId"), normalized},
                {QStringLiteral("error"), error}
            }).toJson(QJsonDocument::Indented);
        } else {
            out << "Provider: " << normalized << "\n"
                << "Error: " << error << "\n";
        }
        return RunResult::Error;
    }

    config::Config liveConfig = config::ConfigLoader::load();
    config::ProviderConfig *provider = providerConfigById(liveConfig, normalized);
    if (!provider) {
        out << "Unknown provider: " << normalized << "\n";
        return RunResult::Error;
    }

    providers::ProviderOAuthResult result = providers::startDeviceFlow(normalized, *provider);
    bool persistedAuthState = false;
    persistProviderOAuthState(&liveConfig,
                              normalized,
                              &result,
                              _runtime.get(),
                              &persistedAuthState,
                              QStringLiteral("failed to persist OAuth state"));

    int pollAttempts = 0;
    int waitedSeconds = 0;
    bool timedOut = false;
    if (waitForCompletion && result.ok && result.pending) {
        provider = providerConfigById(liveConfig, normalized);
        const config::ProviderConfig pendingSnapshot = provider ? *provider : result.config;
        if (!asJson) {
            if (!result.verificationUrl.trimmed().isEmpty()) {
                out << "Verification URL: " << result.verificationUrl.trimmed() << "\n";
            }
            if (!result.userCode.trimmed().isEmpty()) {
                out << "User code: " << result.userCode.trimmed() << "\n";
            }
            out << "Waiting for device login to finish"
                << " (timeout " << timeoutSeconds << "s"
                << ", poll interval " << pendingSnapshot.oauthIntervalSec << "s).\n";
            out.flush();
        }
        result = waitForProviderDeviceFlow(result,
                                           &liveConfig,
                                           normalized,
                                           _runtime.get(),
                                           timeoutSeconds,
                                           &persistedAuthState,
                                           &pollAttempts,
                                           &waitedSeconds,
                                           &timedOut,
                                           asJson ? nullptr : &out);
        provider = providerConfigById(liveConfig, normalized);
        if (timedOut && result.error.trimmed().isEmpty()) {
            result.error = QStringLiteral("OAuth device login is still pending after %1 seconds.")
                               .arg(timeoutSeconds);
        }
    }

    ProviderModelSyncResult modelSync;
    if (syncModels) {
        if (result.ok && !result.pending) {
            modelSync = syncProviderModelCatalog(&liveConfig,
                                                 normalized,
                                                 _runtime.get(),
                                                 true);
            result = modelSync.auth;
            persistedAuthState = persistedAuthState || modelSync.persistedAuthState;
        } else {
            modelSync.auth = result;
            modelSync.providerConfig = provider ? *provider : result.config;
            modelSync.error = result.pending
                ? QStringLiteral("Model sync was skipped because provider login is still pending. Use --wait or rerun after approval.")
                : QStringLiteral("Model sync was skipped because provider login did not complete successfully.");
        }
    }

    const config::ProviderConfig snapshot = syncModels
        ? modelSync.providerConfig
        : (provider ? *provider : result.config);
    if (asJson) {
        QJsonObject payload = providerStatusJson(result, snapshot);
        payload.insert(QStringLiteral("persistedAuthState"), persistedAuthState);
        payload.insert(QStringLiteral("waited"), waitForCompletion);
        payload.insert(QStringLiteral("waitTimeoutSeconds"), timeoutSeconds);
        payload.insert(QStringLiteral("waitedSeconds"), waitedSeconds);
        payload.insert(QStringLiteral("pollAttempts"), pollAttempts);
        payload.insert(QStringLiteral("timedOut"), timedOut);
        payload.insert(QStringLiteral("modelSyncRequested"), syncModels);
        if (syncModels) {
            payload.insert(QStringLiteral("modelSync"), providerModelSyncJson(modelSync));
        }
        out << QJsonDocument(payload).toJson(QJsonDocument::Indented);
        const bool commandOk = waitForCompletion
            ? (result.ok && !result.pending && !timedOut)
            : (result.ok || result.pending);
        return (commandOk && (!syncModels || modelSync.ok)) ? RunResult::Ok : RunResult::Error;
    }

    out << "--- YAOS Provider Login ---\n";
    out << "Provider: " << normalized << "\n";
    out << "Pending: " << (result.pending ? "yes" : "no") << "\n";
    out << "Logged in: " << (result.loggedIn ? "yes" : "no") << "\n";
    out << "Auth state persisted: " << (persistedAuthState ? "yes" : "no") << "\n";
    out << "Model sync requested: " << (syncModels ? "yes" : "no") << "\n";
    if (waitForCompletion) {
        out << "Waited: yes\n";
        out << "Wait timeout: " << timeoutSeconds << "s\n";
        out << "Waited seconds: " << waitedSeconds << "\n";
        out << "Poll attempts: " << pollAttempts << "\n";
        out << "Timed out: " << (timedOut ? "yes" : "no") << "\n";
    }
    if (syncModels) {
        out << "Model sync ok: " << (modelSync.ok ? "yes" : "no") << "\n";
        if (modelSync.ok) {
            out << "Model catalog persisted: " << (modelSync.persistedCatalog ? "yes" : "no") << "\n";
            out << "Model count: " << modelSync.models.size() << "\n";
        }
        if (!modelSync.warning.trimmed().isEmpty()) {
            out << "Model sync warning: " << modelSync.warning.trimmed() << "\n";
        }
        if (!modelSync.error.trimmed().isEmpty()) {
            out << "Model sync error: " << modelSync.error.trimmed() << "\n";
        }
    }
    if (!result.verificationUrl.trimmed().isEmpty()) {
        out << "Verification URL: " << result.verificationUrl.trimmed() << "\n";
    }
    if (!result.userCode.trimmed().isEmpty()) {
        out << "User code: " << result.userCode.trimmed() << "\n";
    }
    out << "Poll interval: " << snapshot.oauthIntervalSec << "s\n";
    if (!result.error.trimmed().isEmpty()) {
        out << "Error: " << result.error.trimmed() << "\n";
    }
    out << "---------------------------\n";
    const bool commandOk = waitForCompletion
        ? (result.ok && !result.pending && !timedOut)
        : (result.ok || result.pending);
    return (commandOk && (!syncModels || modelSync.ok)) ? RunResult::Ok : RunResult::Error;
}

RunResult ApplicationController::providerPoll(const QStringList &args) {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    const QString providerArg = args.size() > 2 ? args.at(2).trimmed() : QString();
    const QString normalized = normalizedProviderId(providerArg.isEmpty()
                                                        ? optionValue(args, QStringLiteral("--provider"))
                                                        : providerArg);
    const bool wantsHelp = args.contains("--help") || args.contains("-h");
    const bool waitForCompletion = args.contains(QStringLiteral("--wait"));
    const bool syncModels = args.contains(QStringLiteral("--sync-models"));
    if (wantsHelp || normalized.isEmpty()) {
        out << "Usage: yaos provider-poll <provider> [--wait] [--sync-models] [--timeout <seconds>] [--json]\n"
            << "Polls an in-progress OAuth device login for a single provider.\n"
            << "Examples:\n"
            << "  yaos provider-poll openai_codex\n"
            << "  yaos provider-poll openai_codex --wait\n"
            << "  yaos provider-poll openai_codex --wait --sync-models\n"
            << "  yaos provider-poll github_copilot --wait --sync-models --timeout 180 --json\n";
        return wantsHelp ? RunResult::Ok : RunResult::Error;
    }

    const bool asJson = args.contains("--json");
    int timeoutSeconds = 300;
    QString timeoutError;
    if (!parsePositiveIntOption(args,
                                QStringLiteral("--timeout"),
                                300,
                                &timeoutSeconds,
                                &timeoutError)) {
        if (asJson) {
            out << QJsonDocument(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("providerId"), normalized},
                {QStringLiteral("error"), timeoutError}
            }).toJson(QJsonDocument::Indented);
        } else {
            out << timeoutError << "\n";
        }
        return RunResult::Error;
    }
    config::Config liveConfig = config::ConfigLoader::load();
    config::ProviderConfig *provider = providerConfigById(liveConfig, normalized);
    if (!provider) {
        out << "Unknown provider: " << normalized << "\n";
        return RunResult::Error;
    }

    providers::ProviderOAuthResult result = providers::pollDeviceFlow(normalized, *provider);
    bool persistedAuthState = false;
    persistProviderOAuthState(&liveConfig,
                              normalized,
                              &result,
                              _runtime.get(),
                              &persistedAuthState,
                              QStringLiteral("failed to persist OAuth state"));

    int pollAttempts = 0;
    int waitedSeconds = 0;
    bool timedOut = false;
    if (waitForCompletion && result.ok && result.pending) {
        provider = providerConfigById(liveConfig, normalized);
        const config::ProviderConfig pendingSnapshot = provider ? *provider : result.config;
        if (!asJson) {
            if (!result.verificationUrl.trimmed().isEmpty()) {
                out << "Verification URL: " << result.verificationUrl.trimmed() << "\n";
            }
            if (!result.userCode.trimmed().isEmpty()) {
                out << "User code: " << result.userCode.trimmed() << "\n";
            }
            out << "Continuing to poll device login"
                << " (timeout " << timeoutSeconds << "s"
                << ", poll interval " << pendingSnapshot.oauthIntervalSec << "s).\n";
            out.flush();
        }
        result = waitForProviderDeviceFlow(result,
                                           &liveConfig,
                                           normalized,
                                           _runtime.get(),
                                           timeoutSeconds,
                                           &persistedAuthState,
                                           &pollAttempts,
                                           &waitedSeconds,
                                           &timedOut,
                                           asJson ? nullptr : &out);
        provider = providerConfigById(liveConfig, normalized);
        if (timedOut && result.error.trimmed().isEmpty()) {
            result.error = QStringLiteral("OAuth device login is still pending after %1 seconds.")
                               .arg(timeoutSeconds);
        }
    }

    ProviderModelSyncResult modelSync;
    if (syncModels) {
        if (result.ok && !result.pending) {
            modelSync = syncProviderModelCatalog(&liveConfig,
                                                 normalized,
                                                 _runtime.get(),
                                                 true);
            result = modelSync.auth;
            persistedAuthState = persistedAuthState || modelSync.persistedAuthState;
        } else {
            modelSync.auth = result;
            modelSync.providerConfig = provider ? *provider : result.config;
            modelSync.error = result.pending
                ? QStringLiteral("Model sync was skipped because provider login is still pending. Use --wait or rerun after approval.")
                : QStringLiteral("Model sync was skipped because provider login did not complete successfully.");
        }
    }

    const config::ProviderConfig snapshot = syncModels
        ? modelSync.providerConfig
        : (provider ? *provider : result.config);
    if (asJson) {
        QJsonObject payload = providerStatusJson(result, snapshot);
        payload.insert(QStringLiteral("persistedAuthState"), persistedAuthState);
        payload.insert(QStringLiteral("waited"), waitForCompletion);
        payload.insert(QStringLiteral("waitTimeoutSeconds"), timeoutSeconds);
        payload.insert(QStringLiteral("waitedSeconds"), waitedSeconds);
        payload.insert(QStringLiteral("pollAttempts"), pollAttempts);
        payload.insert(QStringLiteral("timedOut"), timedOut);
        payload.insert(QStringLiteral("modelSyncRequested"), syncModels);
        if (syncModels) {
            payload.insert(QStringLiteral("modelSync"), providerModelSyncJson(modelSync));
        }
        out << QJsonDocument(payload).toJson(QJsonDocument::Indented);
        const bool commandOk = waitForCompletion
            ? (result.ok && !result.pending && !timedOut)
            : (result.ok || result.pending);
        return (commandOk && (!syncModels || modelSync.ok)) ? RunResult::Ok : RunResult::Error;
    }

    out << "--- YAOS Provider Poll ---\n";
    out << "Provider: " << normalized << "\n";
    out << "Pending: " << (result.pending ? "yes" : "no") << "\n";
    out << "Logged in: " << (result.loggedIn ? "yes" : "no") << "\n";
    out << "Auth state persisted: " << (persistedAuthState ? "yes" : "no") << "\n";
    out << "Model sync requested: " << (syncModels ? "yes" : "no") << "\n";
    if (waitForCompletion) {
        out << "Waited: yes\n";
        out << "Wait timeout: " << timeoutSeconds << "s\n";
        out << "Waited seconds: " << waitedSeconds << "\n";
        out << "Poll attempts: " << pollAttempts << "\n";
        out << "Timed out: " << (timedOut ? "yes" : "no") << "\n";
    }
    if (syncModels) {
        out << "Model sync ok: " << (modelSync.ok ? "yes" : "no") << "\n";
        if (modelSync.ok) {
            out << "Model catalog persisted: " << (modelSync.persistedCatalog ? "yes" : "no") << "\n";
            out << "Model count: " << modelSync.models.size() << "\n";
        }
        if (!modelSync.warning.trimmed().isEmpty()) {
            out << "Model sync warning: " << modelSync.warning.trimmed() << "\n";
        }
        if (!modelSync.error.trimmed().isEmpty()) {
            out << "Model sync error: " << modelSync.error.trimmed() << "\n";
        }
    }
    if (!result.accountId.trimmed().isEmpty()) {
        out << "Account: " << result.accountId.trimmed() << "\n";
    }
    if (!result.expiresAt.trimmed().isEmpty()) {
        out << "Expires: " << result.expiresAt.trimmed() << "\n";
    }
    if (!snapshot.oauthLastRefreshAt.trimmed().isEmpty()) {
        out << "Last refresh: " << snapshot.oauthLastRefreshAt.trimmed() << "\n";
    }
    if (!result.error.trimmed().isEmpty()) {
        out << "Error: " << result.error.trimmed() << "\n";
    }
    out << "--------------------------\n";
    const bool commandOk = waitForCompletion
        ? (result.ok && !result.pending && !timedOut)
        : (result.ok || result.pending);
    return (commandOk && (!syncModels || modelSync.ok)) ? RunResult::Ok : RunResult::Error;
}

RunResult ApplicationController::providerRefresh(const QStringList &args) {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    const QString providerArg = args.size() > 2 ? args.at(2).trimmed() : QString();
    const QString normalized = normalizedProviderId(providerArg.isEmpty()
                                                        ? optionValue(args, QStringLiteral("--provider"))
                                                        : providerArg);
    const bool wantsHelp = args.contains("--help") || args.contains("-h");
    if (wantsHelp || normalized.isEmpty()) {
        out << "Usage: yaos provider-refresh <provider> [--json]\n"
            << "Refreshes stored OAuth credentials for a single provider.\n"
            << "Examples:\n"
            << "  yaos provider-refresh openai_codex\n"
            << "  yaos provider-refresh github_copilot --json\n";
        return wantsHelp ? RunResult::Ok : RunResult::Error;
    }

    const bool asJson = args.contains("--json");
    config::Config liveConfig = config::ConfigLoader::load();
    config::ProviderConfig *provider = providerConfigById(liveConfig, normalized);
    if (!provider) {
        out << "Unknown provider: " << normalized << "\n";
        return RunResult::Error;
    }

    providers::ProviderOAuthResult result = providers::refreshProviderTokens(normalized, *provider);
    bool persistedAuthState = false;
    if (result.changed) {
        *provider = result.config;
        if (!config::ConfigLoader::save(liveConfig)) {
            result.ok = false;
            result.error = QStringLiteral("failed to persist refreshed credentials");
        } else {
            persistedAuthState = true;
            _runtime->reloadFromDisk();
        }
    }

    const config::ProviderConfig snapshot = *provider;
    if (asJson) {
        QJsonObject payload = providerStatusJson(result, snapshot);
        payload.insert(QStringLiteral("persistedAuthState"), persistedAuthState);
        out << QJsonDocument(payload).toJson(QJsonDocument::Indented);
        return result.ok ? RunResult::Ok : RunResult::Error;
    }

    out << "--- YAOS Provider Refresh ---\n";
    out << "Provider: " << normalized << "\n";
    out << "Logged in: " << (result.loggedIn ? "yes" : "no") << "\n";
    out << "Auth state persisted: " << (persistedAuthState ? "yes" : "no") << "\n";
    if (!result.expiresAt.trimmed().isEmpty()) {
        out << "Expires: " << result.expiresAt.trimmed() << "\n";
    }
    if (!snapshot.oauthLastRefreshAt.trimmed().isEmpty()) {
        out << "Last refresh: " << snapshot.oauthLastRefreshAt.trimmed() << "\n";
    }
    if (!result.error.trimmed().isEmpty()) {
        out << "Error: " << result.error.trimmed() << "\n";
    }
    out << "-----------------------------\n";
    return result.ok ? RunResult::Ok : RunResult::Error;
}

RunResult ApplicationController::providerLogout(const QStringList &args) {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    const QString providerArg = args.size() > 2 ? args.at(2).trimmed() : QString();
    const QString normalized = normalizedProviderId(providerArg.isEmpty()
                                                        ? optionValue(args, QStringLiteral("--provider"))
                                                        : providerArg);
    const bool wantsHelp = args.contains("--help") || args.contains("-h");
    if (wantsHelp || normalized.isEmpty()) {
        out << "Usage: yaos provider-logout <provider> [--json]\n"
            << "Clears stored OAuth credentials for a single provider.\n"
            << "Examples:\n"
            << "  yaos provider-logout openai_codex\n"
            << "  yaos provider-logout github_copilot --json\n";
        return wantsHelp ? RunResult::Ok : RunResult::Error;
    }

    const bool asJson = args.contains("--json");
    if (!providers::isOAuthProvider(normalized)) {
        const QString error = QStringLiteral("This provider does not use OAuth.");
        if (asJson) {
            out << QJsonDocument(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("providerId"), normalized},
                {QStringLiteral("error"), error}
            }).toJson(QJsonDocument::Indented);
        } else {
            out << "Provider: " << normalized << "\n"
                << "Error: " << error << "\n";
        }
        return RunResult::Error;
    }

    config::Config liveConfig = config::ConfigLoader::load();
    config::ProviderConfig *provider = providerConfigById(liveConfig, normalized);
    if (!provider) {
        out << "Unknown provider: " << normalized << "\n";
        return RunResult::Error;
    }

    *provider = providers::clearOAuthState(normalized, *provider);
    if (!config::ConfigLoader::save(liveConfig)) {
        const QString error = QStringLiteral("Unable to clear stored OAuth credentials.");
        if (asJson) {
            out << QJsonDocument(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("providerId"), normalized},
                {QStringLiteral("error"), error}
            }).toJson(QJsonDocument::Indented);
        } else {
            out << "Provider: " << normalized << "\n"
                << "Error: " << error << "\n";
        }
        return RunResult::Error;
    }

    _runtime->reloadFromDisk();
    const providers::ProviderOAuthResult result = providers::providerOAuthStatus(normalized, *provider);
    if (asJson) {
        QJsonObject payload = providerStatusJson(result, *provider);
        payload.insert(QStringLiteral("cleared"), true);
        out << QJsonDocument(payload).toJson(QJsonDocument::Indented);
        return RunResult::Ok;
    }

    out << "--- YAOS Provider Logout ---\n";
    out << "Provider: " << normalized << "\n";
    out << "Cleared: yes\n";
    out << "----------------------------\n";
    return RunResult::Ok;
}

RunResult ApplicationController::providerStatus(const QStringList &args) {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    const QString providerArg = args.size() > 2 ? args.at(2).trimmed() : QString();
    const QString normalized = normalizedProviderId(providerArg.isEmpty()
                                                        ? optionValue(args, QStringLiteral("--provider"))
                                                        : providerArg);
    const bool wantsHelp = args.contains("--help") || args.contains("-h");
    if (wantsHelp || normalized.isEmpty()) {
        out << "Usage: yaos provider-status <provider> [--refresh] [--json]\n"
            << "Shows OAuth/auth runtime state for a single provider.\n"
            << "Examples:\n"
            << "  yaos provider-status codebuddy --json\n"
            << "  yaos provider-status openai_codex\n"
            << "  yaos provider-status github_copilot --refresh --json\n";
        return wantsHelp ? RunResult::Ok : RunResult::Error;
    }

    config::Config liveConfig = config::ConfigLoader::load();
    config::ProviderConfig *provider = providerConfigById(liveConfig, normalized);
    if (!provider) {
        out << "Unknown provider: " << normalized << "\n";
        return RunResult::Error;
    }

    const bool asJson = args.contains("--json");
    const bool allowRefresh = args.contains("--refresh");
    providers::ProviderOAuthResult result = allowRefresh
        ? providers::resolveProviderAccess(normalized, *provider, true)
        : providers::providerOAuthStatus(normalized, *provider);

    bool persistedAuthState = false;
    if (allowRefresh && result.changed) {
        *provider = result.config;
        if (!config::ConfigLoader::save(liveConfig)) {
            result.ok = false;
            result.error = QStringLiteral("failed to persist OAuth state");
        } else {
            persistedAuthState = true;
            _runtime->reloadFromDisk();
        }
    }

    const config::ProviderConfig snapshot = *provider;
    const QJsonObject payload = providerStatusJson(result, snapshot);
    if (asJson) {
        QJsonObject root = payload;
        root.insert(QStringLiteral("persistedAuthState"), persistedAuthState);
        out << QJsonDocument(root).toJson(QJsonDocument::Indented);
        return RunResult::Ok;
    }

    out << "--- YAOS Provider Status ---\n";
    out << "Provider: " << normalized << "\n";
    out << "Auth ok: " << (result.ok ? "yes" : "no") << "\n";
    out << "Logged in: " << (result.loggedIn ? "yes" : "no") << "\n";
    out << "Pending: " << (result.pending ? "yes" : "no") << "\n";
    out << "Browser login: " << (result.browserSupported ? "yes" : "no") << "\n";
    out << "Device login: " << (result.deviceSupported ? "yes" : "no") << "\n";
    out << "Token refresh: " << (result.refreshSupported ? "yes" : "no") << "\n";
    out << "API base: " << (result.apiBase.trimmed().isEmpty()
                                ? resolvedApiBaseForProvider(normalized, snapshot.apiBase)
                                : result.apiBase.trimmed())
        << "\n";
    out << "Configured model: " << (snapshot.model.trimmed().isEmpty() ? "none" : snapshot.model.trimmed()) << "\n";
    out << "Available models: " << snapshot.availableModels.size() << "\n";
    out << "API key present: " << (!snapshot.apiKey.trimmed().isEmpty() ? "yes" : "no") << "\n";
    out << "OAuth access token: " << (!snapshot.oauthAccessToken.trimmed().isEmpty() ? "yes" : "no") << "\n";
    out << "Refresh token: " << (!snapshot.oauthRefreshToken.trimmed().isEmpty() ? "yes" : "no") << "\n";
    out << "ID token: " << (!snapshot.oauthIdToken.trimmed().isEmpty() ? "yes" : "no") << "\n";
    out << "Extra headers: " << snapshot.extraHeaders.size() << "\n";
    if (!result.accountId.trimmed().isEmpty()) {
        out << "Account: " << result.accountId.trimmed() << "\n";
    }
    if (!result.expiresAt.trimmed().isEmpty()) {
        out << "Expires: " << result.expiresAt.trimmed() << "\n";
    }
    if (!snapshot.oauthLastRefreshAt.trimmed().isEmpty()) {
        out << "Last refresh: " << snapshot.oauthLastRefreshAt.trimmed() << "\n";
    }
    out << "Auth state persisted: " << (persistedAuthState ? "yes" : "no") << "\n";
    if (!result.error.trimmed().isEmpty()) {
        out << "Error: " << result.error.trimmed() << "\n";
    }
    out << "----------------------------\n";
    return RunResult::Ok;
}

RunResult ApplicationController::providerModels(const QStringList &args) {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    const QString providerArg = args.size() > 2 ? args.at(2).trimmed() : QString();
    const QString normalized = normalizedProviderId(providerArg.isEmpty()
                                                        ? optionValue(args, QStringLiteral("--provider"))
                                                        : providerArg);
    const bool wantsHelp = args.contains("--help") || args.contains("-h");
    if (wantsHelp || normalized.isEmpty()) {
        out << "Usage: yaos provider-models <provider> [--json] [--no-refresh]\n"
            << "Fetches the live model catalog for a single provider and persists it.\n"
            << "Examples:\n"
            << "  yaos provider-models codebuddy --json\n"
            << "  yaos provider-models openai_codex\n"
            << "  yaos provider-models github_copilot --json\n";
        return wantsHelp ? RunResult::Ok : RunResult::Error;
    }

    config::Config liveConfig = config::ConfigLoader::load();
    config::ProviderConfig *provider = providerConfigById(liveConfig, normalized);
    if (!provider) {
        out << "Unknown provider: " << normalized << "\n";
        return RunResult::Error;
    }

    const bool asJson = args.contains("--json");
    const bool allowRefresh = !args.contains("--no-refresh");
    ProviderModelSyncResult sync = syncProviderModelCatalog(&liveConfig,
                                                            normalized,
                                                            _runtime.get(),
                                                            allowRefresh);
    if (!sync.ok) {
        if (asJson) {
            out << QJsonDocument(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("providerId"), normalized},
                {QStringLiteral("error"), sync.error},
                {QStringLiteral("auth"), providerStatusJson(sync.auth, sync.providerConfig)}
            }).toJson(QJsonDocument::Indented);
        } else {
            out << "Provider: " << normalized << "\n"
                << "Error: " << sync.error << "\n";
        }
        return RunResult::Error;
    }

    if (asJson) {
        QJsonObject payload = providerModelsJson(normalized,
                                                 sync.auth,
                                                 sync.apiBase,
                                                 sync.selectedModel,
                                                 sync.models,
                                                 sync.persistedCatalog,
                                                 sync.persistedAuthState);
        if (!sync.warning.isEmpty()) {
            payload.insert(QStringLiteral("warning"), sync.warning);
        }
        out << QJsonDocument(payload).toJson(QJsonDocument::Indented);
        return RunResult::Ok;
    }

    out << "--- YAOS Provider Models ---\n";
    out << "Provider: " << normalized << "\n";
    out << "API base: " << sync.apiBase << "\n";
    out << "Selected model: " << (sync.selectedModel.trimmed().isEmpty() ? "none" : sync.selectedModel.trimmed()) << "\n";
    out << "Auth state persisted: " << (sync.persistedAuthState ? "yes" : "no") << "\n";
    out << "Model catalog persisted: " << (sync.persistedCatalog ? "yes" : "no") << "\n";
    if (!sync.warning.isEmpty()) {
        out << "Warning: " << sync.warning << "\n";
    }
    out << "Model count: " << sync.models.size() << "\n";
    out << "Models:\n";
    for (const QString &model : sync.models) {
        out << "  - " << model << "\n";
    }
    out << "----------------------------\n";
    return RunResult::Ok;
}

RunResult ApplicationController::routePreview(const QStringList &args) {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    if (args.contains("--help") || args.contains("-h")) {
        out << "Usage: yaos route-preview [--template <id>] [--role <role>] [--tags <a,b>] [--tool <tool>]\n"
            << "                          [--channel <channel>] [--memory-backend <backend>]\n"
            << "                          [--origin-channel <channel>] [--origin-chat <chatId>]\n"
            << "                          [--session <sessionKey>] [--task <text>] [--label <text>]\n"
            << "                          [--parent-task-id <id>] [--trace-id <id>] [--include-offline] [--json]\n"
            << "Runs the live delegation resolver and prints the ranked candidate list.\n";
        return RunResult::Ok;
    }

    QJsonObject request;
    const QString templateId = optionValue(args, QStringLiteral("--template"));
    QString templateName;
    QString templateKind;
    if (!templateId.trimmed().isEmpty()) {
        QString error;
        if (!loadDelegationTemplateRequest(_runtime->activeConfig(),
                                           templateId.trimmed(),
                                           &request,
                                           &templateName,
                                           &templateKind,
                                           &error)) {
            out << (error.isEmpty() ? QStringLiteral("Failed to load delegation template.\n")
                                    : error + QLatin1Char('\n'));
            return RunResult::Error;
        }
    }
    applyDelegationRequestArgs(&request,
                               args,
                               QStringLiteral("route-preview"),
                               QStringLiteral("cli:route-preview"),
                               QStringLiteral("Preview delegated task"),
                               QStringLiteral("Routing preview"),
                               QStringLiteral("preview-trace"));
    request.remove(QStringLiteral("targetNode"));

    const QJsonObject preview = _runtime->previewDelegationRoute(request);
    if (args.contains(QStringLiteral("--json"))) {
        out << QString::fromUtf8(QJsonDocument(preview).toJson(QJsonDocument::Indented));
        return RunResult::Ok;
    }

    out << "--- YAOS Route Preview ---\n";
    out << "Resolved: " << (preview.value(QStringLiteral("resolved")).toBool() ? "yes" : "no") << "\n";
    out << "Message: " << preview.value(QStringLiteral("message")).toString() << "\n";
    if (!templateId.trimmed().isEmpty()) {
        out << "Template: " << templateId.trimmed();
        if (!templateName.trimmed().isEmpty()) {
            out << " (" << templateName.trimmed() << ")";
        }
        if (!templateKind.trimmed().isEmpty()) {
            out << " | kind=" << templateKind.trimmed();
        }
        out << "\n";
    }
    out << "Resolution source: " << preview.value(QStringLiteral("resolutionSource")).toString() << "\n";
    out << "Suggested node: "
        << (preview.value(QStringLiteral("suggestedNodeId")).toString().isEmpty()
                ? QStringLiteral("none")
                : preview.value(QStringLiteral("suggestedNodeId")).toString())
        << "\n";
    out << "Route summary: " << preview.value(QStringLiteral("routeSummary")).toString() << "\n";
    out << "Reply to: " << preview.value(QStringLiteral("replyTo")).toString() << "\n";

    const QJsonArray labels = preview.value(QStringLiteral("labels")).toArray();
    out << "Labels: ";
    if (labels.isEmpty()) {
        out << "none\n";
    } else {
        QStringList items;
        for (const QJsonValue &value : labels) {
            const QString text = value.toString().trimmed();
            if (!text.isEmpty()) {
                items.append(text);
            }
        }
        out << (items.isEmpty() ? QStringLiteral("none") : items.join(QStringLiteral(", "))) << "\n";
    }

    const QJsonArray refs = preview.value(QStringLiteral("contextRefs")).toArray();
    out << "Context refs: " << refs.size() << "\n";
    for (const QJsonValue &value : refs) {
        const QJsonObject ref = value.toObject();
        out << "  - " << ref.value(QStringLiteral("store")).toString()
            << ":" << ref.value(QStringLiteral("key")).toString()
            << " [" << ref.value(QStringLiteral("kind")).toString() << "]";
        const QString summary = ref.value(QStringLiteral("summary")).toString();
        if (!summary.isEmpty()) {
            out << " " << summary;
        }
        out << "\n";
    }

    const QJsonArray nodes = preview.value(QStringLiteral("nodes")).toArray();
    out << "Candidates: " << nodes.size() << "\n";
    for (int i = 0; i < nodes.size(); ++i) {
        const QJsonObject item = nodes.at(i).toObject();
        const QJsonObject node = item.value(QStringLiteral("node")).toObject();
        QString endpointStatus = item.value(QStringLiteral("endpointStatus")).toString().trimmed();
        if (endpointStatus.isEmpty()) {
            const bool probeSupported = node.value(QStringLiteral("endpointProbeSupported")).toBool(false);
            const bool checked = node.value(QStringLiteral("endpointHealthChecked")).toBool(false);
            const bool reachable = node.value(QStringLiteral("endpointReachable")).toBool(false);
            endpointStatus = !probeSupported
                ? QStringLiteral("local-only")
                : (checked ? (reachable ? QStringLiteral("reachable") : QStringLiteral("unreachable"))
                           : QStringLiteral("unchecked"));
        }
        out << "  " << (i + 1) << ". "
            << node.value(QStringLiteral("nodeId")).toString()
            << " | role=" << node.value(QStringLiteral("role")).toString()
            << " | matched=" << (item.value(QStringLiteral("matched")).toBool() ? "yes" : "no")
            << " | endpoint=" << endpointStatus
            << " | pressure=" << QString::number(item.value(QStringLiteral("pressure")).toDouble(), 'f', 2)
            << " | queued=" << item.value(QStringLiteral("queuedTaskCount")).toInt()
            << " | weight=" << item.value(QStringLiteral("weight")).toInt()
            << "\n";
        const QString reasonText = item.value(QStringLiteral("reasonText")).toString().trimmed();
        if (!reasonText.isEmpty()) {
            out << "     " << reasonText << "\n";
        }
    }
    out << "--------------------------\n";
    return RunResult::Ok;
}

RunResult ApplicationController::submitDelegation(const QStringList &args) {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    if (args.contains("--help") || args.contains("-h")) {
        out << "Usage: yaos submit-delegation [--template <id>] [--task <text>] [--label <text>] [--node <nodeId>] [--role <role>]\n"
            << "                               [--tags <a,b>] [--tool <tool>] [--channel <channel>]\n"
            << "                               [--memory-backend <backend>] [--group-label <label>]\n"
            << "                               [--origin-channel <channel>] [--origin-chat <chatId>]\n"
            << "                               [--session <sessionKey>] [--parent-task-id <id>] [--trace-id <id>]\n"
            << "                               [--request-file <path.json>] [--json]\n"
            << "Submits a live delegation request to the current runtime. Use --template for saved config templates or --request-file for full single/batch payloads.\n";
        return RunResult::Ok;
    }

    QJsonObject request;
    const QString requestFile = optionValue(args, QStringLiteral("--request-file"));
    const QString templateId = optionValue(args, QStringLiteral("--template"));
    QString templateName;
    QString templateKind;
    if (!requestFile.trimmed().isEmpty()) {
        QString error;
        if (!loadJsonObjectFile(requestFile.trimmed(), &request, &error)) {
            out << (error.isEmpty() ? QStringLiteral("Failed to load delegation request file.\n")
                                    : error + QLatin1Char('\n'));
            return RunResult::Error;
        }
        if (request.value(QStringLiteral("originChannel")).toString().trimmed().isEmpty()) {
            request.insert(QStringLiteral("originChannel"), QStringLiteral("cli"));
        }
        if (request.value(QStringLiteral("originChatId")).toString().trimmed().isEmpty()) {
            request.insert(QStringLiteral("originChatId"), QStringLiteral("submit-delegation"));
        }
        if (request.value(QStringLiteral("sessionKey")).toString().trimmed().isEmpty()) {
            request.insert(QStringLiteral("sessionKey"), QStringLiteral("cli:submit-delegation"));
        }
    } else if (!templateId.trimmed().isEmpty()) {
        QString error;
        if (!loadDelegationTemplateRequest(_runtime->activeConfig(),
                                           templateId.trimmed(),
                                           &request,
                                           &templateName,
                                           &templateKind,
                                           &error)) {
            out << (error.isEmpty() ? QStringLiteral("Failed to load delegation template.\n")
                                    : error + QLatin1Char('\n'));
            return RunResult::Error;
        }
        applyDelegationRequestArgs(&request,
                                   args,
                                   QStringLiteral("submit-delegation"),
                                   QStringLiteral("cli:submit-delegation"),
                                   QString(),
                                   QStringLiteral("CLI delegation"),
                                   QString());
    } else {
        applyDelegationRequestArgs(&request,
                                   args,
                                   QStringLiteral("submit-delegation"),
                                   QStringLiteral("cli:submit-delegation"),
                                   QString(),
                                   QStringLiteral("CLI delegation"),
                                   QString());
        const bool hasBatchTasks = request.value(QStringLiteral("tasks")).isArray() &&
                                   !request.value(QStringLiteral("tasks")).toArray().isEmpty();
        if (request.value(QStringLiteral("task")).toString().trimmed().isEmpty() && !hasBatchTasks) {
            out << "Delegation task is required. Pass --task or use --request-file with a full JSON payload.\n";
            return RunResult::Error;
        }
    }

    const QJsonObject result = _runtime->submitDelegationRequest(request);
    if (args.contains(QStringLiteral("--json"))) {
        out << QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
        return result.value(QStringLiteral("ok")).toBool(false) ? RunResult::Ok : RunResult::Error;
    }

    out << "--- YAOS Delegation Submission ---\n";
    out << "Accepted: " << (result.value(QStringLiteral("ok")).toBool(false) ? "yes" : "no") << "\n";
    out << "Message: " << result.value(QStringLiteral("message")).toString() << "\n";
    if (!templateId.trimmed().isEmpty()) {
        out << "Template: " << templateId.trimmed();
        if (!templateName.trimmed().isEmpty()) {
            out << " (" << templateName.trimmed() << ")";
        }
        if (!templateKind.trimmed().isEmpty()) {
            out << " | kind=" << templateKind.trimmed();
        }
        out << "\n";
    }
    out << "Grouped: " << (result.value(QStringLiteral("grouped")).toBool(false) ? "yes" : "no") << "\n";
    out << "Session: " << result.value(QStringLiteral("sessionKey")).toString() << "\n";
    out << "Trace: " << result.value(QStringLiteral("traceId")).toString() << "\n";
    if (!result.value(QStringLiteral("groupId")).toString().trimmed().isEmpty()) {
        out << "Group: " << result.value(QStringLiteral("groupId")).toString();
        const QString groupLabel = result.value(QStringLiteral("groupLabel")).toString().trimmed();
        if (!groupLabel.isEmpty()) {
            out << " (" << groupLabel << ")";
        }
        out << "\n";
    }
    out << "Submitted tasks: " << result.value(QStringLiteral("submittedCount")).toInt() << "\n";
    out << "Failed tasks: " << result.value(QStringLiteral("failedCount")).toInt() << "\n";

    const QJsonArray tasks = result.value(QStringLiteral("tasks")).toArray();
    for (const QJsonValue &value : tasks) {
        const QJsonObject item = value.toObject();
        out << "  - " << item.value(QStringLiteral("taskId")).toString()
            << " | label=" << item.value(QStringLiteral("label")).toString()
            << " | delegated=" << (item.value(QStringLiteral("delegated")).toBool(false) ? "yes" : "no")
            << " | node=" << item.value(QStringLiteral("targetNode")).toString()
            << " | role=" << item.value(QStringLiteral("targetRole")).toString()
            << " | channel=" << item.value(QStringLiteral("requiredChannel")).toString()
            << "\n";
    }

    const QJsonArray failures = result.value(QStringLiteral("failures")).toArray();
    if (!failures.isEmpty()) {
        out << "Failures:\n";
        for (const QJsonValue &value : failures) {
            out << "  - " << value.toString() << "\n";
        }
    }
    out << "----------------------------------\n";
    return result.value(QStringLiteral("ok")).toBool(false) ? RunResult::Ok : RunResult::Error;
}

RunResult ApplicationController::templateExport(const QStringList &args) {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    if (args.contains("--help") || args.contains("-h")) {
        out << "Usage: yaos template-export [--id <templateId>] [--output <path.json>] [--json]\n"
            << "Exports saved delegation templates from memory.delegationTemplates as a shareable JSON envelope.\n";
        return RunResult::Ok;
    }

    const config::Config cfg = config::ConfigLoader::load();
    QList<config::DelegationTemplateConfig> records;
    const QString templateId = optionValue(args, QStringLiteral("--id")).trimmed();
    if (!templateId.isEmpty()) {
        bool found = false;
        for (const config::DelegationTemplateConfig &record : cfg.memory.delegationTemplates) {
            if (record.id.trimmed() == templateId) {
                records.append(normalizedTemplateRecord(record));
                found = true;
                break;
            }
        }
        if (!found) {
            out << "Delegation template '" << templateId
                << "' was not found in memory.delegationTemplates.\n";
            return RunResult::Error;
        }
    } else {
        for (const config::DelegationTemplateConfig &record : cfg.memory.delegationTemplates) {
            records.append(normalizedTemplateRecord(record, records.size()));
        }
    }

    const QJsonDocument document(
        delegationTemplateExportEnvelope(records, config::ConfigLoader::defaultConfigPath()));
    const QString outputPath = optionValue(args, QStringLiteral("--output")).trimmed();
    if (!outputPath.isEmpty()) {
        QString error;
        if (!saveJsonDocumentFile(outputPath, document, &error)) {
            out << (error.isEmpty() ? QStringLiteral("Failed to write template export file.\n")
                                    : error + QLatin1Char('\n'));
            return RunResult::Error;
        }
    }

    if (args.contains(QStringLiteral("--json")) || outputPath.isEmpty()) {
        out << QString::fromUtf8(document.toJson(QJsonDocument::Indented));
        return RunResult::Ok;
    }

    out << "Exported " << records.size() << " delegation template(s) to " << outputPath << "\n";
    return RunResult::Ok;
}

RunResult ApplicationController::templateImport(const QStringList &args) {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    if (args.contains("--help") || args.contains("-h")) {
        out << "Usage: yaos template-import --file <path.json> [--replace] [--json]\n"
            << "Imports delegation templates into memory.delegationTemplates. By default templates are merged by id.\n";
        return RunResult::Ok;
    }

    const QString path = optionValue(args, QStringLiteral("--file")).trimmed();
    if (path.isEmpty()) {
        out << "Template import requires --file <path.json>.\n";
        return RunResult::Error;
    }

    QJsonDocument document;
    QString error;
    if (!loadJsonDocumentFile(path, &document, &error)) {
        out << (error.isEmpty() ? QStringLiteral("Failed to load template import file.\n")
                                : error + QLatin1Char('\n'));
        return RunResult::Error;
    }

    QList<config::DelegationTemplateConfig> imported;
    if (!parseDelegationTemplateImportDocument(document, &imported, &error)) {
        out << (error.isEmpty() ? QStringLiteral("Failed to parse template import payload.\n")
                                : error + QLatin1Char('\n'));
        return RunResult::Error;
    }

    config::Config cfg = config::ConfigLoader::load();
    const bool replaceExisting = args.contains(QStringLiteral("--replace"));
    cfg.memory.delegationTemplates =
        mergeDelegationTemplates(cfg.memory.delegationTemplates, imported, replaceExisting);
    if (!config::ConfigLoader::save(cfg)) {
        out << "Failed to save updated config.\n";
        return RunResult::Error;
    }
    _runtime->reloadFromDisk();

    QJsonObject result;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("replace"), replaceExisting);
    result.insert(QStringLiteral("importedCount"), imported.size());
    result.insert(QStringLiteral("totalTemplates"), cfg.memory.delegationTemplates.size());
    QJsonArray importedIds;
    for (const config::DelegationTemplateConfig &record : imported) {
        importedIds.append(record.id);
    }
    result.insert(QStringLiteral("templateIds"), importedIds);

    if (args.contains(QStringLiteral("--json"))) {
        out << QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
        return RunResult::Ok;
    }

    out << "Imported " << imported.size() << " delegation template(s) from " << path << "\n";
    out << "Mode: " << (replaceExisting ? "replace" : "merge") << "\n";
    out << "Total templates: " << cfg.memory.delegationTemplates.size() << "\n";
    return RunResult::Ok;
}

RunResult ApplicationController::templatePush(const QStringList &args) {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    if (args.contains("--help") || args.contains("-h")) {
        out << "Usage: yaos template-push [--id <templateId>] [--endpoint <url>] [--replace] [--json]\n"
            << "Pushes local delegation templates to the configured control plane.\n";
        return RunResult::Ok;
    }

    const config::Config cfg = config::ConfigLoader::load();
    const QString endpoint = optionValue(args, QStringLiteral("--endpoint")).trimmed().isEmpty()
        ? controlPlaneEndpointFromConfig(cfg)
        : optionValue(args, QStringLiteral("--endpoint")).trimmed();
    if (endpoint.trimmed().isEmpty()) {
        out << "Control-plane endpoint is not configured. Set deployment.controlPlaneUrl or pass --endpoint.\n";
        return RunResult::Error;
    }

    QList<config::DelegationTemplateConfig> records;
    const QString templateId = optionValue(args, QStringLiteral("--id")).trimmed();
    if (!templateId.isEmpty()) {
        bool found = false;
        for (const config::DelegationTemplateConfig &record : cfg.memory.delegationTemplates) {
            if (record.id.trimmed() == templateId) {
                records.append(config::normalizeDelegationTemplateRecord(record));
                found = true;
                break;
            }
        }
        if (!found) {
            out << "Delegation template '" << templateId
                << "' was not found in memory.delegationTemplates.\n";
            return RunResult::Error;
        }
    } else {
        for (const config::DelegationTemplateConfig &record : cfg.memory.delegationTemplates) {
            records.append(config::normalizeDelegationTemplateRecord(record, records.size()));
        }
    }

    if (records.isEmpty()) {
        out << "No delegation templates are available to push.\n";
        return RunResult::Error;
    }

    distributed::RemoteControlClient client(endpoint, 5000);
    QString error;
    if (!client.ping(&error)) {
        out << (error.isEmpty() ? QStringLiteral("Control plane is unreachable.\n")
                                : error + QLatin1Char('\n'));
        return RunResult::Error;
    }

    const bool replaceExisting = args.contains(QStringLiteral("--replace"));
    QJsonObject payload;
    payload.insert(QStringLiteral("replace"), replaceExisting);
    payload.insert(QStringLiteral("envelope"),
                   config::delegationTemplateExchangeEnvelope(records,
                                                             config::ConfigLoader::defaultConfigPath(),
                                                             cfg.deployment.nodeId,
                                                             cfg.deployment.clusterId));
    QJsonObject response = client.post(QStringLiteral("/v1/control/delegation-templates/sync"),
                                       payload,
                                       &error);
    if (response.isEmpty() || !response.value(QStringLiteral("ok")).toBool(false)) {
        const QString message = !error.trimmed().isEmpty()
            ? error
            : response.value(QStringLiteral("error")).toString();
        out << (message.isEmpty() ? QStringLiteral("Failed to push delegation templates.\n")
                                  : message + QLatin1Char('\n'));
        return RunResult::Error;
    }

    if (args.contains(QStringLiteral("--json"))) {
        out << QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Indented));
        return RunResult::Ok;
    }

    out << "Pushed " << records.size() << " delegation template(s) to " << client.endpoint() << "\n";
    out << "Mode: " << (replaceExisting ? "replace" : "merge") << "\n";
    out << "Remote total templates: " << response.value(QStringLiteral("totalTemplates")).toInt() << "\n";
    return RunResult::Ok;
}

RunResult ApplicationController::templatePull(const QStringList &args) {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    if (args.contains("--help") || args.contains("-h")) {
        out << "Usage: yaos template-pull [--endpoint <url>] [--replace] [--json]\n"
            << "Pulls delegation templates from the configured control plane into memory.delegationTemplates.\n";
        return RunResult::Ok;
    }

    config::Config cfg = config::ConfigLoader::load();
    const QString endpoint = optionValue(args, QStringLiteral("--endpoint")).trimmed().isEmpty()
        ? controlPlaneEndpointFromConfig(cfg)
        : optionValue(args, QStringLiteral("--endpoint")).trimmed();
    if (endpoint.trimmed().isEmpty()) {
        out << "Control-plane endpoint is not configured. Set deployment.controlPlaneUrl or pass --endpoint.\n";
        return RunResult::Error;
    }

    distributed::RemoteControlClient client(endpoint, 5000);
    QString error;
    if (!client.ping(&error)) {
        out << (error.isEmpty() ? QStringLiteral("Control plane is unreachable.\n")
                                : error + QLatin1Char('\n'));
        return RunResult::Error;
    }

    QJsonObject response = client.post(QStringLiteral("/v1/control/delegation-templates/list"),
                                       QJsonObject{{QStringLiteral("limit"), 1024}},
                                       &error);
    if (response.isEmpty() || !response.value(QStringLiteral("ok")).toBool(false)) {
        const QString message = !error.trimmed().isEmpty()
            ? error
            : response.value(QStringLiteral("error")).toString();
        out << (message.isEmpty() ? QStringLiteral("Failed to pull delegation templates.\n")
                                  : message + QLatin1Char('\n'));
        return RunResult::Error;
    }

    const QJsonObject envelope = response.value(QStringLiteral("envelope")).toObject();
    const QJsonDocument document(envelope.isEmpty() ? response : envelope);
    QList<config::DelegationTemplateConfig> records;
    if (!config::parseDelegationTemplateExchangeDocument(document, &records, &error)) {
        out << (error.isEmpty() ? QStringLiteral("Failed to parse pulled delegation templates.\n")
                                : error + QLatin1Char('\n'));
        return RunResult::Error;
    }

    const bool replaceExisting = args.contains(QStringLiteral("--replace"));
    cfg.memory.delegationTemplates =
        config::mergeDelegationTemplateRecords(cfg.memory.delegationTemplates, records, replaceExisting);
    if (!config::ConfigLoader::save(cfg)) {
        out << "Failed to save updated config.\n";
        return RunResult::Error;
    }
    _runtime->reloadFromDisk();

    QJsonObject result;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("replace"), replaceExisting);
    result.insert(QStringLiteral("pulledCount"), records.size());
    result.insert(QStringLiteral("totalTemplates"), cfg.memory.delegationTemplates.size());
    result.insert(QStringLiteral("endpoint"), client.endpoint());

    if (args.contains(QStringLiteral("--json"))) {
        out << QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
        return RunResult::Ok;
    }

    out << "Pulled " << records.size() << " delegation template(s) from " << client.endpoint() << "\n";
    out << "Mode: " << (replaceExisting ? "replace" : "merge") << "\n";
    out << "Local total templates: " << cfg.memory.delegationTemplates.size() << "\n";
    return RunResult::Ok;
}

RunResult ApplicationController::agent(const QStringList &args) {
    if (args.contains("--help") || args.contains("-h")) {
        QTextStream out(stdout);
#ifdef Q_OS_WIN
        out.setCodec(QTextCodec::codecForName("System"));
#endif

        out << "Usage: yaos agent [--provider <provider>] [--model <model>] [--message <content>] [--interactive]\n"
            << "  --provider Override the provider for this run\n"
            << "  --model   Override the model for this run\n"
            << "  --message Run a single prompt\n"
            << "  --interactive Enter CLI interactive mode (desktop build default opens Chat)\n"
            << "Compat: -m <content> is treated as --message.\n";
        return RunResult::Ok;
    }

    QString providerOverride;
    const int providerIndex = args.indexOf("--provider");
    if (providerIndex > 0 && providerIndex + 1 < args.size()) {
        providerOverride = args.at(providerIndex + 1).trimmed();
    }

    QString modelOverride;
    const int modelIndex = args.indexOf("--model");
    if (modelIndex > 0 && modelIndex + 1 < args.size()) {
        modelOverride = args.at(modelIndex + 1).trimmed();
    }

    const QJsonObject serviceHealth = _runtime->serviceHealth(modelOverride, providerOverride);
    if (!serviceHealth.value(QStringLiteral("ok")).toBool()) {
        QTextStream err(stderr);
#ifdef Q_OS_WIN
        err.setCodec(QTextCodec::codecForName("System"));
#endif
        const QString error = serviceHealth.value(QStringLiteral("error"))
                                  .toString(QStringLiteral("runtime provider is not ready"))
                                  .trimmed();
        err << "Error: " << error << "\n";
        return RunResult::Error;
    }

    if (!_runtime->ensureModelReady(modelOverride, providerOverride)) {
        return RunResult::Error;
    }

    QTextStream out(stdout);
    QTextStream in(stdin);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
    in.setCodec(QTextCodec::codecForName("System"));
#endif

    QString singleMessage;
    const int longMessageIndex = args.indexOf("--message");
    const int shortMessageIndex = args.indexOf("-m");
    if (longMessageIndex > 0 && longMessageIndex + 1 < args.size()) {
        singleMessage = args.at(longMessageIndex + 1);
    } else if (shortMessageIndex > 0 && shortMessageIndex + 1 < args.size()) {
        singleMessage = args.at(shortMessageIndex + 1);
    }

    if (!singleMessage.isEmpty()) {
        const runtime::ChatTurnResult turn =
            _runtime->processMessageDetailed(singleMessage,
                                             QStringLiteral("cli:direct"),
                                             QStringLiteral("cli"),
                                             QStringLiteral("direct"),
                                             modelOverride,
                                             providerOverride);
        out << turn.content << "\n";
        return turn.error ? RunResult::Error : RunResult::Ok;
    }

    out << "YAOS interactive mode. Type 'exit' to quit.\n";
    bool sawError = false;
    while (true) {
        out << "you> ";
        out.flush();
        const QString line = in.readLine();
        if (line.isNull()) {
            break;
        }

        const QString trimmed = line.trimmed();
        if (trimmed.compare("exit", Qt::CaseInsensitive) == 0 ||
            trimmed.compare("quit", Qt::CaseInsensitive) == 0) {
            break;
        }
        if (trimmed.isEmpty()) {
            continue;
        }

        const runtime::ChatTurnResult turn =
            _runtime->processMessageDetailed(trimmed,
                                             QStringLiteral("cli:direct"),
                                             QStringLiteral("cli"),
                                             QStringLiteral("direct"),
                                             modelOverride,
                                             providerOverride);
        sawError = sawError || turn.error;
        out << "yaos> " << turn.content << "\n";
    }
    return sawError ? RunResult::Error : RunResult::Ok;
}

RunResult ApplicationController::gateway() {
    if (!startGatewayServices()) {
        return RunResult::Error;
    }

    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    QObject::connect(qApp, &QCoreApplication::aboutToQuit, [this]() {
        stopGatewayServices();
    });

    const runtime::CronStatus st = _runtime->cronStatus();
    const config::Config cfg = _runtime->activeConfig();
    const QStringList enabled = _runtime->enabledChannels();

    out << "YAOS gateway started.\n";
    out << "Scheduled jobs: " << st.jobs << "\n";
    out << "Heartbeat: "
        << (cfg.gateway.heartbeat.enabled ? "enabled" : "disabled")
        << " every " << cfg.gateway.heartbeat.intervalS << "s\n";
    out << "Channels: " << (enabled.isEmpty() ? "none" : enabled.join(", ")) << "\n";
    out << "AgentLoop thread: " << _runtime->agentThreadName() << "\n";
    out << "Press Ctrl+C to stop.\n";
    out.flush();
    return RunResult::EnterEventLoop;
}

RunResult ApplicationController::daemon(const QStringList &args) {
    const config::Config cfg = config::ConfigLoader::load();
    runtime::StructuredLog::install(cfg.workspacePath(), QStringLiteral("yaosd"));

    QString requestedServerName;
    if (args.contains("--help") || args.contains("-h")) {
        QTextStream out(stdout);
#ifdef Q_OS_WIN
        out.setCodec(QTextCodec::codecForName("System"));
#endif

        out << "Usage: yaos daemon [--server <name>]\n"
            << "Starts the local runtime sidecar over FastNet loopback TCP.\n";
        return RunResult::Ok;
    }

    const int serverIndex = args.indexOf("--server");
    if (serverIndex > 0 && serverIndex + 1 < args.size()) {
        requestedServerName = args.at(serverIndex + 1).trimmed();
    }

    _daemonServer = std::make_unique<::yaos::daemon::LocalDaemonServer>();
    QString error;
    const QString serverName = ::yaos::daemon::protocol::resolveServerName(cfg, requestedServerName);
    if (!_daemonServer->start(serverName, &error)) {
        QTextStream out(stdout);
#ifdef Q_OS_WIN
        out.setCodec(QTextCodec::codecForName("System"));
#endif
        out << (error.isEmpty() ? QStringLiteral("Failed to start YAOS daemon.\n")
                                : error + QLatin1Char('\n'));
        return RunResult::Error;
    }

    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    QObject::connect(qApp, &QCoreApplication::aboutToQuit, [this]() {
        if (_daemonServer) {
            _daemonServer->stop();
        }
    });

    out << "YAOS local daemon started.\n";
    out << "Server: " << _daemonServer->serverName() << "\n";
    out << "Endpoint: 127.0.0.1:" << _daemonServer->serverPort() << "\n";
    out << "Mode: local IPC sidecar\n";
    out.flush();
    return RunResult::EnterEventLoop;
}

RunResult ApplicationController::runtimeService(const QStringList &args) {
    if (args.contains("--help") || args.contains("-h")) {
        QTextStream out(stdout);
#ifdef Q_OS_WIN
        out.setCodec(QTextCodec::codecForName("System"));
#endif

        out << "Usage: yaos runtime-service [--endpoint <http://host:port>] [--advertise-endpoint <http://host:port>]\n"
            << "Starts the local HTTP runtime service.\n";
        return RunResult::Ok;
    }

    const config::Config cfg = config::ConfigLoader::load();
    const QString endpointOverride = optionValue(args, QStringLiteral("--endpoint"));
    const QString advertiseOverride = optionValue(args, QStringLiteral("--advertise-endpoint"));
    const QUrl endpoint = serviceUrlFrom(endpointOverride.isEmpty() ? cfg.runtime.endpoint : endpointOverride,
                                         QStringLiteral("http://127.0.0.1:18890"));

    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    if (!endpoint.isValid() || endpoint.scheme().toLower() != QStringLiteral("http")) {
        out << "Invalid runtime service endpoint.\n";
        return RunResult::Error;
    }

    QString listenHost;
    if (!resolveListenHost(endpoint.host(), &listenHost)) {
        out << "Invalid listen host.\n";
        return RunResult::Error;
    }
    const quint16 port = static_cast<quint16>(endpoint.port(18890));

    const QString workspace = cfg.workspacePath();
    runtime::syncWorkspaceTemplates(workspace);
    runtime::StructuredLog::install(workspace, QStringLiteral("yaos-runtime"));

    const config::Config serviceConfig =
        runtime::runtimeServiceConfig(cfg,
                                      endpoint.toString(QUrl::FullyEncoded),
                                      advertiseOverride);
    auto runtimeCore = std::make_unique<::yaos::runtime::RuntimeCore>(serviceConfig);
    const QJsonObject serviceHealth = runtimeCore->serviceHealth();
    if (!serviceHealth.value(QStringLiteral("ok")).toBool(false)) {
        const QString errorText = serviceHealth.value(QStringLiteral("error"))
            .toString(QStringLiteral("Failed to initialize runtime service."));
        out << errorText << "\n";
        return RunResult::Error;
    }
    _runtimeServiceClient = std::make_unique<::yaos::runtime::LocalRuntimeClient>(std::move(runtimeCore));
    _runtimeHttpServer = std::make_unique<::yaos::runtime::RuntimeHttpServer>(*_runtimeServiceClient);
    QString error;
    if (!_runtimeHttpServer->start(listenHost, port, &error)) {
        out << (error.isEmpty() ? QStringLiteral("Failed to start runtime service.\n")
                                : error + QLatin1Char('\n'));
        return RunResult::Error;
    }

    QObject::connect(qApp, &QCoreApplication::aboutToQuit, [this]() {
        if (_runtimeHttpServer) {
            _runtimeHttpServer->stop();
        }
    });

    out << "YAOS runtime service started.\n";
    out << "Endpoint: http://" << endpoint.host() << ":" << _runtimeHttpServer->listenPort() << "\n";
    out << "Advertise endpoint: "
        << (serviceConfig.runtime.advertiseEndpoint.trimmed().isEmpty()
                ? QStringLiteral("none")
                : serviceConfig.runtime.advertiseEndpoint.trimmed())
        << "\n";
    out << "Workspace: " << workspace << "\n";
    out << "Mode: local HTTP runtime\n";
    out.flush();
    return RunResult::EnterEventLoop;
}

RunResult ApplicationController::memoryService(const QStringList &args) {
    if (args.contains("--help") || args.contains("-h")) {
        QTextStream out(stdout);
#ifdef Q_OS_WIN
        out.setCodec(QTextCodec::codecForName("System"));
#endif

        out << "Usage: yaos memory-service [--endpoint <http://host:port>] [--api-key <token>]\n"
            << "Starts the local HTTP memory service.\n";
        return RunResult::Ok;
    }

    const config::Config cfg = config::ConfigLoader::load();
    const QString endpointOverride = optionValue(args, QStringLiteral("--endpoint"));
    const QString tokenOverride = optionValue(args, QStringLiteral("--api-key"));
    const QUrl endpoint = serviceUrlFrom(endpointOverride.isEmpty() ? cfg.memory.service.endpoint : endpointOverride,
                                         QStringLiteral("http://127.0.0.1:18891"));

    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    if (!endpoint.isValid() || endpoint.scheme().toLower() != QStringLiteral("http")) {
        out << "Invalid memory service endpoint.\n";
        return RunResult::Error;
    }

    QString listenHost;
    if (!resolveListenHost(endpoint.host(), &listenHost)) {
        out << "Invalid listen host.\n";
        return RunResult::Error;
    }
    const quint16 port = static_cast<quint16>(endpoint.port(18891));

    const QString workspace = cfg.workspacePath();
    runtime::syncWorkspaceTemplates(workspace);
    runtime::StructuredLog::install(workspace, QStringLiteral("yaos-memory"));

    _memoryServiceCore = std::make_unique<::yaos::memory::MemoryServiceCore>(workspace, cfg);
    if (!_memoryServiceCore->isReady()) {
        out << (_memoryServiceCore->lastError().isEmpty()
                    ? QStringLiteral("Failed to initialize memory service.\n")
                    : _memoryServiceCore->lastError() + QLatin1Char('\n'));
        return RunResult::Error;
    }

    const QString apiKey = tokenOverride.isEmpty() ? cfg.memory.service.apiKey : tokenOverride;
    _memoryHttpServer = std::make_unique<::yaos::memory::MemoryHttpServer>(*_memoryServiceCore, apiKey);
    QString error;
    if (!_memoryHttpServer->start(listenHost, port, &error)) {
        out << (error.isEmpty() ? QStringLiteral("Failed to start memory service.\n")
                                : error + QLatin1Char('\n'));
        return RunResult::Error;
    }

    QObject::connect(qApp, &QCoreApplication::aboutToQuit, [this]() {
        if (_memoryHttpServer) {
            _memoryHttpServer->stop();
        }
    });

    out << "YAOS memory service started.\n";
    out << "Endpoint: http://" << endpoint.host() << ":" << _memoryHttpServer->listenPort() << "\n";
    out << "Workspace: " << workspace << "\n";
    out << "Mode: local HTTP memory service\n";
    out.flush();
    return RunResult::EnterEventLoop;
}

RunResult ApplicationController::controlService(const QStringList &args) {
    if (args.contains("--help") || args.contains("-h")) {
        QTextStream out(stdout);
#ifdef Q_OS_WIN
        out.setCodec(QTextCodec::codecForName("System"));
#endif

        out << "Usage: yaos control-service [--endpoint <http://host:port>]\n"
            << "Starts the local HTTP control-plane service.\n";
        return RunResult::Ok;
    }

    const config::Config cfg = config::ConfigLoader::load();
    const QString endpointOverride = optionValue(args, QStringLiteral("--endpoint"));
    const QUrl endpoint = serviceUrlFrom(endpointOverride.isEmpty() ? cfg.deployment.controlPlaneUrl : endpointOverride,
                                         QStringLiteral("http://127.0.0.1:18892"));

    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    if (!endpoint.isValid() || endpoint.scheme().toLower() != QStringLiteral("http")) {
        out << "Invalid control service endpoint.\n";
        return RunResult::Error;
    }

    QString listenHost;
    if (!resolveListenHost(endpoint.host(), &listenHost)) {
        out << "Invalid listen host.\n";
        return RunResult::Error;
    }
    const quint16 port = static_cast<quint16>(endpoint.port(18892));

    const QString workspace = cfg.workspacePath();
    runtime::syncWorkspaceTemplates(workspace);
    runtime::StructuredLog::install(workspace, QStringLiteral("yaos-control"));

    _controlServiceCore = std::make_unique<::yaos::control::ControlServiceCore>(workspace, cfg);
    if (!_controlServiceCore->isReady()) {
        out << (_controlServiceCore->lastError().isEmpty()
                    ? QStringLiteral("Failed to initialize control service.\n")
                    : _controlServiceCore->lastError() + QLatin1Char('\n'));
        return RunResult::Error;
    }

    _controlHttpServer = std::make_unique<::yaos::control::ControlHttpServer>(*_controlServiceCore);
    QString error;
    if (!_controlHttpServer->start(listenHost, port, &error)) {
        out << (error.isEmpty() ? QStringLiteral("Failed to start control service.\n")
                                : error + QLatin1Char('\n'));
        return RunResult::Error;
    }

    _controlServiceCore->refreshNodeHealth(true);

    if (_controlHealthTimer) {
        _controlHealthTimer->stop();
        _controlHealthTimer.reset();
    }
    _controlHealthTimer = std::make_unique<QTimer>(qApp);
    _controlHealthTimer->setInterval(15000);
    _controlHealthTimer->setSingleShot(false);
    QObject::connect(_controlHealthTimer.get(), &QTimer::timeout, [this]() {
        if (_controlServiceCore) {
            _controlServiceCore->refreshNodeHealth(false);
        }
    });
    _controlHealthTimer->start();

    QObject::connect(qApp, &QCoreApplication::aboutToQuit, [this]() {
        if (_controlHealthTimer) {
            _controlHealthTimer->stop();
        }
        if (_controlHttpServer) {
            _controlHttpServer->stop();
        }
    });

    out << "YAOS control service started.\n";
    out << "Endpoint: http://" << endpoint.host() << ":" << _controlHttpServer->listenPort() << "\n";
    out << "Workspace: " << workspace << "\n";
    out << "Mode: local HTTP control plane\n";
    out.flush();
    return RunResult::EnterEventLoop;
}

RunResult ApplicationController::help() {
    QTextStream out(stdout);
#ifdef Q_OS_WIN
    out.setCodec(QTextCodec::codecForName("System"));
#endif

    out << "YAOS - personal AI runtime (C++/Qt)\n\n"
        << "Usage: yaos <command> [options]\n\n"
        << "Without arguments YAOS opens the desktop studio.\n\n"
        << "Commands:\n"
        << "  init     Initialize config and workspace templates\n"
        << "  config   Open the graphical configuration page\n"
        << "  status   Print a runtime status summary\n"
        << "  provider-login  Start OAuth device login for one provider\n"
        << "  provider-poll  Poll an in-progress OAuth device login\n"
        << "  provider-refresh  Refresh stored OAuth credentials\n"
        << "  provider-logout  Clear stored OAuth credentials\n"
        << "  provider-status  Print OAuth/auth status for one provider\n"
        << "  provider-models  Fetch and persist the live model catalog for one provider\n"
        << "  route-preview  Run the live delegation route resolver\n"
        << "  submit-delegation  Submit a live delegation request\n"
        << "  template-export  Export saved delegation templates\n"
        << "  template-import  Import saved delegation templates\n"
        << "  template-push  Push delegation templates to the control plane\n"
        << "  template-pull  Pull delegation templates from the control plane\n"
        << "  agent    Open chat UI or run a single prompt\n"
        << "  gui      Open the desktop studio\n"
        << "  gateway  Start the multi-channel gateway loop\n"
        << "  daemon   Start the local runtime sidecar\n"
        << "  runtime-service  Start the local HTTP runtime service\n"
        << "  memory-service  Start the local HTTP memory service\n"
        << "  control-service  Start the local HTTP control-plane service\n"
        << "  help     Show this help text\n\n"
        << "Use 'yaos <command> --help' for more details.\n";
    out.flush();
    return RunResult::Ok;
}

} // namespace yaos::app
