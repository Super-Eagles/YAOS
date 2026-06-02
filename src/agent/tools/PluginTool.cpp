#include "PluginTool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <utility>

#include "../../providers/ProviderFactory.h"
#include "../../providers/ProviderRegistry.h"

Q_LOGGING_CATEGORY(lcPluginTool, "yaos.tools.plugin")

namespace yaos::agent::tools {

namespace {

QString normalizedProviderId(const QString &providerId) {
    QString normalized = providerId.trimmed().toLower();
    normalized.replace('-', '_');
    if (normalized == "azureopenai") normalized = "azure_openai";
    if (normalized == "openaicodex") normalized = "openai_codex";
    if (normalized == "githubcopilot") normalized = "github_copilot";
    return normalized;
}

const config::ProviderConfig *providerConfigById(const config::Config &config, const QString &providerId) {
    const QString normalized = normalizedProviderId(providerId);
    if (normalized == "custom") return &config.providers.custom;
    if (normalized == "azure_openai") return &config.providers.azureOpenAI;
    if (normalized == "anthropic") return &config.providers.anthropic;
    if (normalized == "openai") return &config.providers.openai;
    if (normalized == "openrouter") return &config.providers.openrouter;
    if (normalized == "deepseek") return &config.providers.deepseek;
    if (normalized == "groq") return &config.providers.groq;
    if (normalized == "zhipu") return &config.providers.zhipu;
    if (normalized == "dashscope") return &config.providers.dashscope;
    if (normalized == "vllm") return &config.providers.vllm;
    if (normalized == "gemini") return &config.providers.gemini;
    if (normalized == "moonshot") return &config.providers.moonshot;
    if (normalized == "minimax") return &config.providers.minimax;
    if (normalized == "aihubmix") return &config.providers.aihubmix;
    if (normalized == "siliconflow") return &config.providers.siliconflow;
    if (normalized == "volcengine") return &config.providers.volcengine;
    if (normalized == "openai_codex") return &config.providers.openaiCodex;
    if (normalized == "github_copilot") return &config.providers.githubCopilot;
    return nullptr;
}

QString localModelForProvider(const QString &providerId, const QString &model) {
    QString local = model.trimmed();
    if (local.isEmpty()) {
        return local;
    }

    const providers::ProviderSpec spec = providers::findProviderSpec(normalizedProviderId(providerId));
    if (!spec.litellmPrefix.isEmpty()) {
        const QString prefix = spec.litellmPrefix + "/";
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

QString providerDefaultModel(const config::Config &config, const QString &providerId) {
    const config::ProviderConfig *provider = providerConfigById(config, providerId);
    return provider ? provider->model.trimmed() : QString();
}

QString readTextFile(const QString &path) {
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    return text;
}

QStringList jsonArrayToStrings(const QJsonArray &array) {
    QStringList out;
    for (const QJsonValue &value : array) {
        out.append(value.toString());
    }
    return out;
}

QHash<QString, QString> jsonObjectToStringMap(const QJsonObject &obj) {
    QHash<QString, QString> out;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        out.insert(it.key(), it.value().toString());
    }
    return out;
}

QString sanitizeToolName(const QString &value, const QString &fallbackSeed) {
    QString sanitized = value.trimmed().toLower();
    sanitized.replace(QRegularExpression("[^a-z0-9_]+"), "_");
    sanitized.replace(QRegularExpression("_+"), "_");
    sanitized.remove(QRegularExpression("^_+|_+$"));
    if (sanitized.isEmpty()) {
        sanitized = "plugin";
    }
    if (!sanitized.startsWith("plugin_")) {
        sanitized.prepend("plugin_");
    }
    if (sanitized.size() > 56) {
        const QString hash = QString::number(qHash(fallbackSeed), 16);
        sanitized = sanitized.left(47) + "_" + hash.left(8);
    }
    return sanitized;
}

QString valueToTemplateText(const QJsonValue &value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? "true" : "false";
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    if (value.isArray() || value.isObject()) {
        return QString::fromUtf8(QJsonDocument::fromVariant(value.toVariant()).toJson(QJsonDocument::Compact));
    }
    return QString();
}

QString renderTemplate(QString text,
                       const QJsonObject &params,
                       const runtime::PluginRecord &record,
                       const config::ExtensionProfileConfig &profile,
                       const QString &workspace) {
    QHash<QString, QString> replacements;
    replacements.insert("workspace", workspace);
    replacements.insert("plugin.id", record.id);
    replacements.insert("plugin.name", record.name);
    replacements.insert("plugin.version", record.version);
    replacements.insert("plugin.description", record.description);
    replacements.insert("plugin.note", profile.note);
    replacements.insert("plugin.root", record.rootPath);
    replacements.insert("json", QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Indented)).trimmed());

    for (auto it = params.begin(); it != params.end(); ++it) {
        replacements.insert(it.key(), valueToTemplateText(it.value()));
    }

    for (auto it = replacements.begin(); it != replacements.end(); ++it) {
        text.replace(QString("{{%1}}").arg(it.key()), it.value(), Qt::CaseInsensitive);
        text.replace(QString("{{ %1 }}").arg(it.key()), it.value(), Qt::CaseInsensitive);
    }
    return text;
}

QString inferCommandFromEntry(const QString &entryPath, QStringList *args) {
    const QString suffix = QFileInfo(entryPath).suffix().toLower();
    if (suffix == "js" || suffix == "cjs" || suffix == "mjs") {
        if (args) args->append(entryPath);
        return "node";
    }
    if (suffix == "py") {
        if (args) args->append(entryPath);
        return "python";
    }
    if (suffix == "cmd" || suffix == "bat") {
        if (args) {
            args->append("/c");
            args->append(entryPath);
        }
        return "cmd.exe";
    }
    if (suffix == "ps1") {
        if (args) {
            args->append("-File");
            args->append(entryPath);
        }
        return "powershell.exe";
    }
    return entryPath;
}

config::Config effectivePluginConfig(const config::Config &baseConfig,
                                     const config::ExtensionProfileConfig &profile,
                                     const runtime::PluginRecord &record) {
    config::Config cfg = baseConfig;
    const QJsonObject executor = record.manifest.value("executor").toObject();

    QString providerId = normalizedProviderId(profile.provider);
    if (providerId.isEmpty() || providerId == "auto") {
        providerId = normalizedProviderId(executor.value("provider").toString());
    }

    QString model = profile.model.trimmed();
    if (model.isEmpty()) {
        model = executor.value("model").toString().trimmed();
    }

    if (!providerId.isEmpty() && providerId != "auto") {
        cfg.agentDefaults.provider = providerId;
        QString resolvedModel = localModelForProvider(providerId, model);
        if (resolvedModel.isEmpty()) {
            resolvedModel = localModelForProvider(providerId, cfg.agentDefaults.model);
        }
        if (resolvedModel.isEmpty()) {
            resolvedModel = providerDefaultModel(cfg, providerId);
        }
        if (!resolvedModel.isEmpty()) {
            cfg.agentDefaults.model = resolvedModel;
        }
    } else if (!model.isEmpty()) {
        cfg.agentDefaults.model = model;
    }

    return cfg;
}

QJsonObject fallbackParameters() {
    return QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{}},
        {"additionalProperties", true}
    };
}

QString parsePluginProcessOutput(const QByteArray &stdoutBytes, const QByteArray &stderrBytes) {
    const QString stdoutText = QString::fromUtf8(stdoutBytes).trimmed();
    const QString stderrText = QString::fromUtf8(stderrBytes).trimmed();

    if (!stdoutText.isEmpty()) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(stdoutBytes, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject obj = doc.object();
            if (obj.value("ok").isBool() && !obj.value("ok").toBool()) {
                const QString error = obj.value("error").toString(obj.value("message").toString());
                return error.isEmpty() ? QStringLiteral("Plugin reported failure.") : error;
            }
            if (obj.value("content").isString()) {
                return obj.value("content").toString();
            }
            if (obj.value("message").isString()) {
                return obj.value("message").toString();
            }
            if (obj.contains("result")) {
                const QJsonValue result = obj.value("result");
                if (result.isString()) {
                    return result.toString();
                }
                if (result.isObject() || result.isArray()) {
                    return QString::fromUtf8(QJsonDocument::fromVariant(result.toVariant()).toJson(QJsonDocument::Indented)).trimmed();
                }
                return valueToTemplateText(result);
            }
            return QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).trimmed();
        }
    }

    if (!stdoutText.isEmpty() && !stderrText.isEmpty()) {
        return stdoutText + "\n\nSTDERR:\n" + stderrText;
    }
    if (!stdoutText.isEmpty()) {
        return stdoutText;
    }
    if (!stderrText.isEmpty()) {
        return stderrText;
    }
    return QStringLiteral("(plugin returned no output)");
}

} // namespace

PluginTool::PluginTool(QString workspace, config::Config config, runtime::PluginRecord record)
    : _workspace(std::move(workspace)),
      _config(std::move(config)),
      _record(std::move(record)) {}

QString PluginTool::name() const {
    return effectiveToolName();
}

QString PluginTool::description() const {
    QString text = _record.manifest.value("tool").toObject().value("description").toString().trimmed();
    if (text.isEmpty()) {
        text = _record.description.trimmed();
    }
    if (text.isEmpty()) {
        text = QString("Execute plugin '%1'.").arg(_record.name.isEmpty() ? _record.id : _record.name);
    }

    const config::ExtensionProfileConfig currentProfile = profile();
    if (!currentProfile.note.trimmed().isEmpty()) {
        text += " Operator note: " + currentProfile.note.trimmed();
    }
    return text;
}

QJsonObject PluginTool::parameters() const {
    const QJsonObject schema = _record.manifest.value("tool").toObject().value("parameters").toObject();
    if (schema.value("type").toString() == "object") {
        return schema;
    }
    return fallbackParameters();
}

QString PluginTool::execute(const QJsonObject &params) {
    const QString executorType = _record.executorType.trimmed().toLower();
    if (executorType == "prompt") {
        return executePromptPlugin(params);
    }
    if (executorType == "command") {
        return executeCommandPlugin(params);
    }
    return QString("Error: plugin '%1' has unsupported executor '%2'.").arg(_record.id, executorType);
}

QString PluginTool::executePromptPlugin(const QJsonObject &params) const {
    const config::ExtensionProfileConfig currentProfile = profile();
    const config::Config effectiveConfig = effectivePluginConfig(_config, currentProfile, _record);
    QString selectedProvider;
    std::unique_ptr<providers::LLMProvider> provider = providers::ProviderFactory::create(effectiveConfig, &selectedProvider);
    if (!provider) {
        return QString("Error: plugin '%1' could not create an LLM provider.").arg(_record.id);
    }

    const QJsonObject executor = _record.manifest.value("executor").toObject();
    QString systemPrompt = executor.value("system").toString().trimmed();
    const QString systemFile = executor.value("systemFile").toString().trimmed();
    if (!systemFile.isEmpty()) {
        const QString fileText = readTextFile(QDir(_record.rootPath).filePath(systemFile));
        if (!fileText.trimmed().isEmpty()) {
            systemPrompt += systemPrompt.isEmpty() ? QString() : QString("\n\n");
            systemPrompt += fileText.trimmed();
        }
    }
    if (systemPrompt.isEmpty() && !_record.description.trimmed().isEmpty()) {
        systemPrompt = _record.description.trimmed();
    }

    QString userTemplate = executor.value("template").toString();
    if (userTemplate.trimmed().isEmpty()) {
        const QString promptFile = executor.value("promptFile").toString(executor.value("userTemplateFile").toString()).trimmed();
        if (!promptFile.isEmpty()) {
            userTemplate = readTextFile(QDir(_record.rootPath).filePath(promptFile));
        }
    }
    if (userTemplate.trimmed().isEmpty()) {
        userTemplate =
            "Plugin task request:\n"
            "{{json}}\n";
    }

    const QString renderedPrompt = renderTemplate(userTemplate, params, _record, currentProfile, _workspace);
    QJsonArray messages;
    if (!systemPrompt.trimmed().isEmpty()) {
        QString system = systemPrompt.trimmed();
        if (!currentProfile.note.trimmed().isEmpty()) {
            system += "\n\nOperator note: " + currentProfile.note.trimmed();
        }
        messages.append(QJsonObject{{"role", "system"}, {"content", system}});
    }
    messages.append(QJsonObject{{"role", "user"}, {"content", renderedPrompt}});

    const int maxTokens = executor.value("maxTokens").toInt(_config.agentDefaults.maxTokens);
    const double temperature = executor.contains("temperature")
        ? executor.value("temperature").toDouble(_config.agentDefaults.temperature)
        : _config.agentDefaults.temperature;
    const QString model = effectiveConfig.agentDefaults.model.trimmed().isEmpty()
        ? provider->defaultModel()
        : effectiveConfig.agentDefaults.model.trimmed();

    const agent::LLMResponse response = provider->chat(messages, {}, model, temperature, maxTokens);
    if (response.finishReason == "error") {
        return response.content;
    }
    if (!response.content.trimmed().isEmpty()) {
        return response.content.trimmed();
    }
    return QStringLiteral("Plugin completed without returning text.");
}

QString PluginTool::executeCommandPlugin(const QJsonObject &params) const {
    const config::ExtensionProfileConfig currentProfile = profile();
    const config::Config effectiveConfig = effectivePluginConfig(_config, currentProfile, _record);
    const QJsonObject executor = _record.manifest.value("executor").toObject();

    QString workingDir = executor.value("cwd").toString().trimmed();
    workingDir = workingDir.isEmpty() ? _record.rootPath : QDir(_record.rootPath).filePath(workingDir);

    int timeoutSec = executor.value("timeoutSec").toInt(60);
    if (timeoutSec <= 0) {
        timeoutSec = 60;
    }

    QStringList args;
    QString command = executor.value("command").toString().trimmed();
    const QString entryPoint = _record.entryPoint.trimmed().isEmpty()
        ? QString()
        : QDir(_record.rootPath).filePath(_record.entryPoint.trimmed());

    const QStringList configuredArgs = jsonArrayToStrings(executor.value("args").toArray());
    for (const QString &arg : configuredArgs) {
        args.append(renderTemplate(arg, params, _record, currentProfile, _workspace));
    }

    if (command.isEmpty()) {
        if (entryPoint.isEmpty()) {
            return QString("Error: plugin '%1' does not declare an entry point.").arg(_record.id);
        }
        command = inferCommandFromEntry(entryPoint, &args);
    } else if (args.isEmpty() && !entryPoint.isEmpty()) {
        args.append(entryPoint);
    }

    if (command.trimmed().isEmpty()) {
        return QString("Error: plugin '%1' has no runnable command.").arg(_record.id);
    }

    QProcess process;
    process.setWorkingDirectory(QDir::cleanPath(workingDir));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QHash<QString, QString> extraEnv = jsonObjectToStringMap(executor.value("env").toObject());
    for (auto it = extraEnv.begin(); it != extraEnv.end(); ++it) {
        env.insert(it.key(), renderTemplate(it.value(), params, _record, currentProfile, _workspace));
    }
    env.insert("YAOS_WORKSPACE", _workspace);
    env.insert("YAOS_PLUGIN_ID", _record.id);
    env.insert("YAOS_PLUGIN_NAME", _record.name);
    env.insert("YAOS_PLUGIN_ROOT", _record.rootPath);
    env.insert("YAOS_PLUGIN_TOOL", effectiveToolName());
    env.insert("YAOS_PLUGIN_PROVIDER", effectiveConfig.agentDefaults.provider);
    env.insert("YAOS_PLUGIN_MODEL", effectiveConfig.agentDefaults.model);
    env.insert("YAOS_PLUGIN_NOTE", currentProfile.note);
    process.setProcessEnvironment(env);

    process.start(command, args);
    if (!process.waitForStarted(5000)) {
        return QString("Error: failed to start plugin '%1' with command '%2'.").arg(_record.id, command);
    }

    const QJsonObject payload = QJsonObject{
        {"pluginId", _record.id},
        {"pluginName", _record.name},
        {"workspace", _workspace},
        {"pluginRoot", _record.rootPath},
        {"parameters", params},
        {"profile", QJsonObject{
            {"provider", currentProfile.provider},
            {"model", currentProfile.model},
            {"note", currentProfile.note}
        }}
    };

    process.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    process.closeWriteChannel();

    if (!process.waitForFinished(timeoutSec * 1000)) {
        process.kill();
        process.waitForFinished(3000);
        return QString("Error: plugin '%1' timed out after %2 seconds.").arg(_record.id).arg(timeoutSec);
    }

    QString result = parsePluginProcessOutput(process.readAllStandardOutput(), process.readAllStandardError());
    if (process.exitCode() != 0) {
        result += QString("\n\nExit code: %1").arg(process.exitCode());
    }
    return result;
}

QString PluginTool::effectiveToolName() const {
    const QString manifestToolName = _record.manifest.value("tool").toObject().value("name").toString();
    return sanitizeToolName(manifestToolName.isEmpty() ? _record.id : manifestToolName,
                            _record.id + ":" + manifestToolName);
}

config::ExtensionProfileConfig PluginTool::profile() const {
    return _config.extensions.plugins.value(_record.id);
}

} // namespace yaos::agent::tools
