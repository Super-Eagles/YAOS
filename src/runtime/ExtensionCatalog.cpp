#include "ExtensionCatalog.h"

#include "ExtensionCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

#include "Templates.h"

namespace yaos::runtime {

namespace {

struct CatalogTemplate {
    QString catalogId;
    QString kind;
    QString installId;
    QString title;
    QString summary;
    QString description;
    QStringList tags;
};

const QVector<CatalogTemplate> &catalogTemplates() {
    static const QVector<CatalogTemplate> templates = {
        {
            "plugin.release_notes",
            "plugin",
            "release-notes",
            "Release Notes Plugin",
            "把变更清单整理成结构化发布说明的可执行插件",
            "安装后会生成可直接运行的 prompt 插件，Agent 能像调用普通工具一样调用它来产出发布说明。",
            {"plugin", "release", "prompt"}
        },
        {
            "plugin.workspace_audit",
            "plugin",
            "workspace-audit",
            "Workspace Audit Plugin",
            "把目录清单、配置快照或扫描结果整理成风险审计结论",
            "适合让 Agent 先用文件工具收集工作区信息，再调用这个插件生成审计摘要、风险等级和后续动作。",
            {"plugin", "audit", "prompt"}
        },
        {
            "plugin.support_triage",
            "plugin",
            "support-triage",
            "Support Triage Plugin",
            "把用户反馈整理成可复现问题、严重级别和下一步动作",
            "安装后会生成可执行 prompt 插件，适合客服分诊、工单归类和问题升级场景。",
            {"plugin", "support", "triage"}
        },
        {
            "skill.code_review",
            "skill",
            "code-review",
            "Code Review Skill",
            "聚焦缺陷、回归和测试缺口的代码审查技能",
            "安装后会生成可直接编辑的 SKILL.md，用来规范化 review workflow，并在扩展页中自动发现。",
            {"skill", "review", "quality"}
        },
        {
            "skill.release_check",
            "skill",
            "release-check",
            "Release Check Skill",
            "发布前检查版本、配置、回滚与验证步骤",
            "适合作为发版前检查清单，让模型在执行发布相关任务时有稳定的固定流程。",
            {"skill", "release", "checklist"}
        },
        {
            "skill.support_triage",
            "skill",
            "support-triage",
            "Support Triage Skill",
            "把用户反馈整理成可复现问题和处理优先级",
            "适合作为售后、工单、反馈接入时的通用分诊技能模板。",
            {"skill", "support", "triage"}
        },
        {
            "mcp.filesystem",
            "mcp",
            "filesystem",
            "Filesystem MCP",
            "把当前工作区注册为本地文件系统 MCP",
            "安装后会写入一个可直接使用的 stdio MCP preset，命令为 npx @modelcontextprotocol/server-filesystem <workspace>。",
            {"mcp", "stdio", "filesystem"}
        },
        {
            "mcp.stdio_template",
            "mcp",
            "stdio_template",
            "Stdio MCP Template",
            "新增一个可编辑的 stdio MCP 配置骨架",
            "适合接入本地命令式 MCP 服务，安装后你只需要补上 command、args 和环境变量。",
            {"mcp", "stdio", "template"}
        },
        {
            "mcp.http_template",
            "mcp",
            "http_template",
            "HTTP MCP Template",
            "新增一个可编辑的 streamable HTTP MCP 骨架",
            "适合接入远端 MCP 网关，安装后你只需要改 URL 和 Header。",
            {"mcp", "http", "template"}
        },
        {
            "mcp.sse_template",
            "mcp",
            "sse_template",
            "SSE MCP Template",
            "新增一个可编辑的 SSE MCP 骨架",
            "适合接入旧版 SSE MCP 服务，安装后你只需要改 URL 和 Header。",
            {"mcp", "sse", "template"}
        }
    };
    return templates;
}

QString targetForTemplate(const CatalogTemplate &entry) {
    if (entry.kind == "plugin") {
        return QString("plugins/%1").arg(entry.installId);
    }
    if (entry.kind == "skill") {
        return QString("skills/%1").arg(entry.installId);
    }
    return QString("tools.mcpServers.%1").arg(entry.installId);
}

bool writeTextFile(const QString &path, const QString &content, QString *error) {
    if (QFileInfo::exists(path)) {
        if (error) {
            *error = QString("目标文件已存在: %1").arg(path);
        }
        return false;
    }

    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = QString("无法写入文件: %1").arg(path);
        }
        return false;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << content;
    file.close();
    return true;
}

bool writeJsonFile(const QString &path, const QJsonObject &object, QString *error) {
    if (QFileInfo::exists(path)) {
        if (error) {
            *error = QString("目标文件已存在: %1").arg(path);
        }
        return false;
    }

    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = QString("无法写入文件: %1").arg(path);
        }
        return false;
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QJsonArray stringListToJson(const QStringList &values) {
    QJsonArray out;
    for (const QString &value : values) {
        out.append(value);
    }
    return out;
}

QString pluginReadme(const CatalogTemplate &entry) {
    return QString(
        "# %1\n\n"
        "%2\n\n"
        "## Notes\n\n"
        "- This plugin was generated by YAOS.\n"
        "- The runtime will auto-discover `plugin.json` and expose it as an agent tool.\n"
        "- Edit `plugin.json` and `PROMPT.md` to refine behavior.\n")
        .arg(entry.title, entry.description);
}

QString pluginPromptTemplate(const CatalogTemplate &entry) {
    if (entry.installId == "release-notes") {
        return QString(
            "Prepare concise release notes for the following update.\n\n"
            "Version: {{version}}\n"
            "Audience: {{audience}}\n"
            "Highlight: {{highlight}}\n\n"
            "Changes:\n"
            "{{changes}}\n\n"
            "Output sections:\n"
            "1. Summary\n"
            "2. Highlights\n"
            "3. Upgrade Notes\n"
            "4. Rollback / Risk Notes\n");
    }

    if (entry.installId == "workspace-audit") {
        return QString(
            "You are reviewing a workspace snapshot.\n\n"
            "Audit focus: {{focus}}\n"
            "Inventory / Findings:\n"
            "{{inventory}}\n\n"
            "Return:\n"
            "1. Overall risk level\n"
            "2. Top findings\n"
            "3. Missing checks\n"
            "4. Suggested next actions\n");
    }

    return QString(
        "Triage the following support issue.\n\n"
        "Customer request:\n"
        "{{request}}\n\n"
        "Expected behavior:\n"
        "{{expected}}\n\n"
        "Actual behavior:\n"
        "{{actual}}\n\n"
        "Impact:\n"
        "{{impact}}\n\n"
        "Return:\n"
        "1. Problem summary\n"
        "2. Severity / priority\n"
        "3. Reproduction clues\n"
        "4. Routing target\n"
        "5. Next best action\n");
}

QJsonObject pluginToolSchema(const CatalogTemplate &entry) {
    if (entry.installId == "release-notes") {
        return QJsonObject{
            {"name", "plugin_release_notes"},
            {"description", "Generate polished release notes from a raw change list."},
            {"parameters", QJsonObject{
                {"type", "object"},
                {"properties", QJsonObject{
                    {"changes", QJsonObject{{"type", "string"}, {"description", "Raw changelog, commit list, or release diff."}}},
                    {"version", QJsonObject{{"type", "string"}, {"description", "Release version label."}}},
                    {"audience", QJsonObject{{"type", "string"}, {"description", "Audience such as internal, customer, enterprise."}}},
                    {"highlight", QJsonObject{{"type", "string"}, {"description", "Optional business highlight or theme."}}}
                }},
                {"required", QJsonArray{"changes"}}
            }}
        };
    }

    if (entry.installId == "workspace-audit") {
        return QJsonObject{
            {"name", "plugin_workspace_audit"},
            {"description", "Assess a workspace snapshot and summarize top technical risks."},
            {"parameters", QJsonObject{
                {"type", "object"},
                {"properties", QJsonObject{
                    {"inventory", QJsonObject{{"type", "string"}, {"description", "Workspace tree, scan output, or collected findings."}}},
                    {"focus", QJsonObject{{"type", "string"}, {"description", "Audit focus, for example security, release, compatibility."}}}
                }},
                {"required", QJsonArray{"inventory"}}
            }}
        };
    }

    return QJsonObject{
        {"name", "plugin_support_triage"},
        {"description", "Convert a support issue into a structured triage report."},
        {"parameters", QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"request", QJsonObject{{"type", "string"}, {"description", "Original customer issue or ticket content."}}},
                {"expected", QJsonObject{{"type", "string"}, {"description", "Expected behavior if known."}}},
                {"actual", QJsonObject{{"type", "string"}, {"description", "Observed behavior if known."}}},
                {"impact", QJsonObject{{"type", "string"}, {"description", "Business impact, urgency, or affected users."}}}
            }},
            {"required", QJsonArray{"request"}}
        }}
    };
}

QJsonObject pluginManifest(const CatalogTemplate &entry) {
    return QJsonObject{
        {"id", entry.installId},
        {"name", entry.title},
        {"version", "1.0.0"},
        {"description", entry.summary},
        {"tool", pluginToolSchema(entry)},
        {"executor", QJsonObject{
            {"type", "prompt"},
            {"system", QString("You are the YAOS plugin '%1'. Produce concise, operationally useful output.").arg(entry.installId)},
            {"promptFile", "PROMPT.md"},
            {"temperature", 0.1},
            {"maxTokens", 1600}
        }},
        {"capabilities", stringListToJson(entry.tags)}
    };
}

QString skillMarkdown(const CatalogTemplate &entry) {
    if (entry.installId == "code-review") {
        return QString(
            "# %1\n\n"
            "%2\n\n"
            "## Suggested Triggers\n\n"
            "- review\n"
            "- code review\n"
            "- regression\n\n"
            "## Focus\n\n"
            "- Prioritize bugs, regressions, compatibility risks, and missing tests.\n"
            "- Keep findings first, summaries second.\n"
            "- Call out assumptions when the code path is unclear.\n")
            .arg(entry.title, entry.description);
    }

    if (entry.installId == "release-check") {
        return QString(
            "# %1\n\n"
            "%2\n\n"
            "## Suggested Triggers\n\n"
            "- release\n"
            "- preflight\n"
            "- rollout\n\n"
            "## Checklist\n\n"
            "- Confirm version and release scope.\n"
            "- Verify config, secrets, and migration steps.\n"
            "- Verify rollback plan and post-release checks.\n")
            .arg(entry.title, entry.description);
    }

    return QString(
        "# %1\n\n"
        "%2\n\n"
        "## Suggested Triggers\n\n"
        "- support\n"
        "- bug report\n"
        "- ticket\n\n"
        "## Workflow\n\n"
        "- Clarify the incoming report.\n"
        "- Extract reproduction steps and expected behavior.\n"
        "- Assign severity, owner, and next action.\n")
        .arg(entry.title, entry.description);
}

const CatalogTemplate *findTemplate(const QString &catalogId) {
    const QVector<CatalogTemplate> &entries = catalogTemplates();
    for (const CatalogTemplate &entry : entries) {
        if (entry.catalogId == catalogId) {
            return &entry;
        }
    }
    return nullptr;
}

bool isInstalled(const QString &workspace, const config::Config &config, const CatalogTemplate &entry) {
    const QDir root(workspace);
    if (entry.kind == "plugin") {
        return QFileInfo::exists(root.filePath(QString("plugins/%1/plugin.json").arg(entry.installId)));
    }
    if (entry.kind == "skill") {
        return QFileInfo::exists(root.filePath(QString("skills/%1/SKILL.md").arg(entry.installId)));
    }
    return config.tools.mcpServers.contains(entry.installId);
}

bool installPluginTemplate(const QString &workspace, const CatalogTemplate &entry, QString *message) {
    const QDir root(workspace);
    const QString base = root.filePath(QString("plugins/%1").arg(entry.installId));
    QString error;
    if (!writeJsonFile(QDir(base).filePath("plugin.json"), pluginManifest(entry), &error)) {
        if (message) {
            *message = error;
        }
        return false;
    }
    if (!writeTextFile(QDir(base).filePath("README.md"), pluginReadme(entry), &error)) {
        if (message) {
            *message = error;
        }
        return false;
    }
    if (!writeTextFile(QDir(base).filePath("PROMPT.md"), pluginPromptTemplate(entry), &error)) {
        if (message) {
            *message = error;
        }
        return false;
    }
    if (message) {
        *message = QString("已安装插件: %1").arg(targetForTemplate(entry));
    }
    return true;
}

bool installSkillTemplate(const QString &workspace, const CatalogTemplate &entry, QString *message) {
    const QDir root(workspace);
    const QString base = root.filePath(QString("skills/%1").arg(entry.installId));
    QString error;
    if (!writeTextFile(QDir(base).filePath("SKILL.md"), skillMarkdown(entry), &error)) {
        if (message) {
            *message = error;
        }
        return false;
    }
    if (message) {
        *message = QString("已生成技能模板: %1").arg(targetForTemplate(entry));
    }
    return true;
}

bool installMcpPreset(const QString &workspace,
                      config::Config *config,
                      const CatalogTemplate &entry,
                      QString *message) {
    Q_UNUSED(workspace);
    if (!config) {
        if (message) {
            *message = QStringLiteral("配置对象不可用。");
        }
        return false;
    }
    if (config->tools.mcpServers.contains(entry.installId)) {
        if (message) {
            *message = QString("MCP 配置已存在: %1").arg(entry.installId);
        }
        return false;
    }

    config::MCPServerConfig preset;
    preset.toolTimeout = 30;

    if (entry.installId == "filesystem") {
        preset.type = "stdio";
        preset.command = "npx";
        preset.args = QStringList{
            "-y",
            "@modelcontextprotocol/server-filesystem",
            QDir(workspace).absolutePath()
        };
    } else if (entry.installId == "stdio_template") {
        preset.type = "stdio";
        preset.command = "npx";
        preset.args = QStringList{
            "-y",
            "your-mcp-package"
        };
    } else if (entry.installId == "http_template") {
        preset.type = "streamableHttp";
        preset.url = "http://127.0.0.1:3000/mcp";
    } else {
        preset.type = "sse";
        preset.url = "http://127.0.0.1:3000/sse";
    }

    config->tools.mcpServers.insert(entry.installId, preset);
    if (message) {
        *message = QString("已写入 MCP 预设: %1").arg(targetForTemplate(entry));
    }
    return true;
}

} // namespace

QVector<ExtensionCatalogEntry> buildExtensionCatalog(const QString &workspace,
                                                     const config::Config &config) {
    QVector<ExtensionCatalogEntry> entries;
    const QVector<CatalogTemplate> &templates = catalogTemplates();
    entries.reserve(templates.size());

    for (const CatalogTemplate &tpl : templates) {
        ExtensionCatalogEntry entry;
        entry.catalogId = tpl.catalogId;
        entry.kind = tpl.kind;
        entry.installId = tpl.installId;
        entry.title = tpl.title;
        entry.summary = tpl.summary;
        entry.description = tpl.description;
        entry.target = targetForTemplate(tpl);
        entry.tags = tpl.tags;
        entry.installed = isInstalled(workspace, config, tpl);
        entries.append(entry);
    }

    return entries;
}

bool installCatalogEntry(const QString &workspace,
                         config::Config *config,
                         const QString &catalogId,
                         QString *message) {
    const CatalogTemplate *entry = findTemplate(catalogId.trimmed());
    if (!entry) {
        if (message) {
            *message = QString("未知扩展条目: %1").arg(catalogId);
        }
        return false;
    }

    syncWorkspaceTemplates(workspace);

    if (config && isInstalled(workspace, *config, *entry)) {
        if (message) {
            *message = QString("该条目已经安装: %1").arg(targetForTemplate(*entry));
        }
        return false;
    }

    if (entry->kind == "plugin") {
        return installPluginTemplate(workspace, *entry, message);
    }
    if (entry->kind == "skill") {
        return installSkillTemplate(workspace, *entry, message);
    }
    return installMcpPreset(workspace, config, *entry, message);
}

} // namespace yaos::runtime
