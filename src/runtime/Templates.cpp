#include "Templates.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace yaos::runtime {

namespace {

struct TemplateFile {
    QString relativePath;
    QString content;
};

QString agentsTemplate() {
    return
R"(# 智能体指令

你是一个名为 YAOS 的 AI 助手。你旨在通过工具协作帮助用户完成任务。

## 定时任务

使用内置的 `cron` 工具来创建、查看或删除提醒与自动化任务。
)";
}

QString soulTemplate() {
    return
R"(# 核心设定

我是一个名为 YAOS 的个人 AI 助手。
)";
}

QString userTemplate() {
    return
R"(# 用户画像

在这里记录用户的偏好、习惯和持久的上下文信息。
)";
}

QString toolsTemplate() {
    return
R"(# 工具使用备注

记录工具使用的非显式约束。

## message

- 使用 `message` 进行主动的、带外（out-of-band）的用户更新。

## spawn

- 使用 `spawn` 启动可以异步运行的后台工作。

## mcp_call

- 使用 `mcp_call` 调用已配置的 `tools.mcpServers` 中的工具。

## plugin_call

- 插件会自动注册成独立工具，是否允许执行由 `plugin_call` 安全策略控制。
)";
}

QString heartbeatTemplate() {
    return
R"(# 心跳任务

## 活跃任务

- [ ] 
)";
}

QString memoryTemplate() {
    return
R"(# 长期记忆

## 用户信息

## 偏好设定

## 项目上下文
)";
}

QString automationsTemplate() {
    return
R"(# Automations

在这里维护可复用的自动化流程草稿。图形工作台会把已保存的自动化落到 `automations/flows.json`。
)";
}

QString pluginsTemplate() {
    return
R"(# Plugins

将扩展放到 `plugins/<plugin-id>/plugin.json`。工作台会自动发现这些清单，并把符合规范的插件注册成可调用工具。
)";
}

QString skillsTemplate() {
    return
R"(# Skills

将技能放到 `skills/<skill-id>/SKILL.md`。工作台会自动发现这些技能，并按触发词或显式提及把它们注入当前对话上下文。
)";
}

bool writeIfMissing(const QString &absPath, const QString &content) {
    if (QFileInfo::exists(absPath)) {
        return false;
    }
    const QFileInfo info(absPath);
    QDir().mkpath(info.absolutePath());
    QFile file(absPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out << content;
    file.close();
    return true;
}

} // namespace

QStringList syncWorkspaceTemplates(const QString &workspace) {
    const QDir root(workspace);
    QDir().mkpath(root.absolutePath());

    const QList<TemplateFile> files = {
        {"AGENTS.md", agentsTemplate()},
        {"SOUL.md", soulTemplate()},
        {"USER.md", userTemplate()},
        {"TOOLS.md", toolsTemplate()},
        {"HEARTBEAT.md", heartbeatTemplate()},
        {"memory/MEMORY.md", memoryTemplate()},
        {"memory/HISTORY.md", QString()},
        {"automations/README.md", automationsTemplate()},
        {"plugins/README.md", pluginsTemplate()},
        {"skills/README.md", skillsTemplate()},
    };

    QStringList created;
    for (const TemplateFile &tpl : files) {
        const QString abs = root.filePath(tpl.relativePath);
        if (writeIfMissing(abs, tpl.content)) {
            created.append(tpl.relativePath);
        }
    }
    QDir().mkpath(root.filePath("automations"));
    QDir().mkpath(root.filePath("memory/daily"));
    QDir().mkpath(root.filePath("plugins"));
    QDir().mkpath(root.filePath("runtime"));
    QDir().mkpath(root.filePath("skills"));
    QDir().mkpath(root.filePath("sessions"));
    return created;
}

} // namespace yaos::runtime
