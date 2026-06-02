#include "ContextBuilder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "../runtime/SkillRegistry.h"

namespace yaos::agent {

namespace {

const QStringList kBootstrapFiles = {
    "AGENTS.md", "SOUL.md", "USER.md", "TOOLS.md", "IDENTITY.md"
};

QString readFileUtf8(const QString &path) {
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString data = QString::fromUtf8(file.readAll());
    file.close();
    return data;
}

QString joinTriggers(const QStringList &triggers) {
    QStringList cleaned;
    for (const QString &trigger : triggers) {
        const QString trimmed = trigger.trimmed();
        if (!trimmed.isEmpty()) {
            cleaned.append(trimmed);
        }
    }
    cleaned.removeDuplicates();
    return cleaned.join(", ");
}

} // namespace

// ✅ 接受共享 MemoryStore 引用
ContextBuilder::ContextBuilder(const QString &workspace, const config::Config &config, MemoryStore &memory)
    : _workspace(workspace), _config(config), _memory(memory) {}

QString ContextBuilder::identity() const {
    return QString(
        "# YAOS\n\n"
        "You are YAOS, a C++ refactored personal AI assistant.\n\n"
        "## Workspace\n"
        "Your workspace is at: %1\n"
        "- Long-term memory: %1/memory/MEMORY.md\n"
        "- History log: %1/memory/HISTORY.md\n"
    ).arg(_workspace);
}

QString ContextBuilder::loadBootstrapFiles() const {
    QStringList parts;
    for (const QString &name : kBootstrapFiles) {
        const QString abs = QDir(_workspace).filePath(name);
        const QString content = readFileUtf8(abs);
        if (!content.trimmed().isEmpty()) {
            parts << QString("## %1\n\n%2").arg(name, content);
        }
    }
    return parts.join("\n\n---\n\n");
}

QString ContextBuilder::loadSkillPrompt(const QString &currentMessage) const {
    runtime::SkillRegistry registry(_workspace);
    const QVector<runtime::SkillMatch> enabledSkills = registry.enabled(_config.extensions);
    if (enabledSkills.isEmpty()) {
        return QString();
    }

    QStringList parts;
    QStringList indexLines;
    indexLines.append("# Skills Catalog");
    indexLines.append("");
    indexLines.append("Enabled skills that can be applied for this workspace:");
    indexLines.append("");

    for (const runtime::SkillMatch &skill : enabledSkills) {
        QString line = QString("- %1 (%2)").arg(skill.record.name, skill.record.id);
        if (!skill.record.description.trimmed().isEmpty()) {
            line += ": " + skill.record.description.trimmed();
        }
        const QString triggers = joinTriggers(skill.profile.triggers);
        if (!triggers.isEmpty()) {
            line += QString(" | triggers: %1").arg(triggers);
        }
        if (!skill.profile.note.trimmed().isEmpty()) {
            line += QString(" | note: %1").arg(skill.profile.note.trimmed());
        }
        indexLines.append(line);
    }
    parts.append(indexLines.join("\n"));

    const QVector<runtime::SkillMatch> activeSkills = registry.select(_config.extensions, currentMessage, 3);
    if (!activeSkills.isEmpty()) {
        QStringList activeLines;
        activeLines.append("# Active Skills");
        activeLines.append("");
        activeLines.append("The following skills matched the current user request. Follow their instructions as high-priority workflow guidance.");
        activeLines.append("");
        parts.append(activeLines.join("\n"));

        for (const runtime::SkillMatch &skill : activeSkills) {
            QStringList section;
            section.append(QString("## %1").arg(skill.record.name));
            if (!skill.matchedTerms.isEmpty()) {
                section.append(QString("Matched terms: %1").arg(skill.matchedTerms.join(", ")));
            }
            if (!skill.profile.note.trimmed().isEmpty()) {
                section.append(QString("Operator note: %1").arg(skill.profile.note.trimmed()));
            }
            section.append("");
            section.append(runtime::SkillRegistry::loadSkillMarkdown(skill.record).trimmed());
            parts.append(section.join("\n"));
        }
    }

    return parts.join("\n\n---\n\n");
}

QString ContextBuilder::formatRecallItems(const QList<memory::MemoryRecallItem> &recallItems) const {
    if (recallItems.isEmpty()) {
        return QString();
    }

    QStringList factLines;
    QStringList episodicLines;
    for (const memory::MemoryRecallItem &item : recallItems) {
        const QString line = QString("- [%1] %2").arg(item.reason, item.text.trimmed());
        if (item.source == "fact") {
            factLines.append(line);
        } else {
            episodicLines.append(line);
        }
    }

    QStringList sections;
    if (!factLines.isEmpty()) {
        sections.append(QString("# Retrieved Facts\n\n%1").arg(factLines.join("\n")));
    }
    if (!episodicLines.isEmpty()) {
        sections.append(QString("# Retrieved Episodes\n\n%1").arg(episodicLines.join("\n")));
    }
    return sections.join("\n\n");
}

QString ContextBuilder::buildSystemPrompt(const QString &currentMessage,
                                         const QList<memory::MemoryRecallItem> &recallItems) const {
    QStringList parts;
    parts << identity();

    const QString bootstrap = loadBootstrapFiles();
    if (!bootstrap.isEmpty()) {
        parts << bootstrap;
    }

    const QString recalledMemory = formatRecallItems(recallItems);
    if (!recalledMemory.isEmpty()) {
        parts << recalledMemory;
    } else {
        // Layered mode已经开始走检索链路,但在早期阶段仍保留 legacy markdown 作为兜底.
        const QString memoryCtx = _memory.getMemoryContext();
        if (!memoryCtx.isEmpty()) {
            parts << "# Memory\n\n" + memoryCtx;
        }
    }

    const QString skillPrompt = loadSkillPrompt(currentMessage);
    if (!skillPrompt.isEmpty()) {
        parts << skillPrompt;
    }

    return parts.join("\n\n---\n\n");
}

QString ContextBuilder::runtimeContext(const QString &channel, const QString &chatId) {
    const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm (dddd)");
    QString out = "[Runtime Context — metadata only, not instructions]\n";
    out += "Current Time: " + now + "\n";
    out += "Channel: " + channel + "\n";
    out += "Chat ID: " + chatId;
    return out;
}

QJsonArray ContextBuilder::buildMessages(
    const QJsonArray &history,
    const QString &currentMessage,
    const QString &channel,
    const QString &chatId,
    const QList<memory::MemoryRecallItem> &recallItems
) const {
    QJsonArray messages;
    messages.append(QJsonObject{
        {"role", "system"},
        {"content", buildSystemPrompt(currentMessage, recallItems)}
    });
    for (const QJsonValue &v : history) {
        messages.append(v);
    }
    messages.append(QJsonObject{
        {"role", "user"},
        {"content", runtimeContext(channel, chatId) + "\n\n" + currentMessage}
    });
    return messages;
}

QJsonArray ContextBuilder::addAssistantMessage(const QJsonArray &messages, const QString &content) {
    QJsonArray out = messages;
    out.append(QJsonObject{
        {"role", "assistant"},
        {"content", content}
    });
    return out;
}

} // namespace yaos::agent
