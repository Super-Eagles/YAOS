#include "MemoryStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QTextStream>

Q_LOGGING_CATEGORY(lcMemory, "yaos.memory")

namespace yaos::agent {

MemoryStore::MemoryStore(const QString &workspace) {
    _memoryDir = QDir(workspace).filePath("memory");
    _memoryFile = QDir(_memoryDir).filePath("MEMORY.md");
    _historyFile = QDir(_memoryDir).filePath("HISTORY.md");
    QDir().mkpath(_memoryDir);
}

void MemoryStore::setSummarizeFunc(SummarizeFunc func) {
    _summarizeFunc = std::move(func);
}

QString MemoryStore::readLongTerm() const {
    QFile file(_memoryFile);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString data = QString::fromUtf8(file.readAll());
    file.close();
    return data;
}

void MemoryStore::writeLongTerm(const QString &content) {
    QSaveFile file(_memoryFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning(lcMemory) << "Failed to write long-term memory:" << _memoryFile;
        return;
    }
    QTextStream out(&file);
    out << content;
    out.flush();
    if (!file.commit()) {
        qWarning(lcMemory) << "Failed to commit long-term memory:" << _memoryFile;
    }
}

void MemoryStore::appendHistory(const QString &entry) {
    QFile file(_historyFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning(lcMemory) << "Failed to append history:" << _historyFile;
        return;
    }
    QTextStream out(&file);
    out << entry.trimmed() << "\n\n";
    file.close();
}

QString MemoryStore::getMemoryContext() const {
    const QString longTerm = readLongTerm();
    if (longTerm.trimmed().isEmpty()) {
        return QString();
    }
    return "## Long-term Memory\n" + longTerm;
}

// 调用 LLM 对原始对话做摘要.如果没有注入回调则返回原文截断版本.
QString MemoryStore::summarize(const QString &rawText) const {
    if (_summarizeFunc) {
        qDebug(lcMemory) << "Calling LLM to summarize" << rawText.size() << "chars";
        const QString result = _summarizeFunc(rawText);
        if (!result.trimmed().isEmpty()) {
            return result;
        }
        qWarning(lcMemory) << "LLM summarize returned empty, falling back to truncation";
    }
    // 降级：截断保留前 2000 字符
    return rawText.left(2000) + (rawText.size() > 2000 ? "\n...(truncated)" : "");
}

bool MemoryStore::consolidate(session::Session &session, int memoryWindow, bool archiveAll) {
    if (session.messages.isEmpty()) {
        return true;
    }

    const int keepCount = memoryWindow / 2;
    int start = session.lastConsolidated;
    int end = session.messages.size();

    if (!archiveAll) {
        if (session.messages.size() <= keepCount) {
            return true;
        }
        end = session.messages.size() - keepCount;
    }

    if (end <= start) {
        return true;
    }

    // 收集需要归档的消息,构造可读文本
    QStringList lines;
    for (int i = start; i < end; ++i) {
        const QJsonObject m = session.messages.at(i).toObject();
        const QString role = m.value("role").toString().toUpper();
        const QString content = m.value("content").toVariant().toString();
        if (!content.trimmed().isEmpty()) {
            lines << QString("[%1] %2: %3")
                         .arg(m.value("timestamp").toString().left(16))
                         .arg(role, content);
        }
    }
    if (lines.isEmpty()) {
        return true;
    }

    const QString rawConversation = lines.join("\n");
    const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");

    // ✅ 真正调用 LLM 做摘要
    const QString summaryText = summarize(rawConversation);

    // 写入 HISTORY.md（原始记录）
    appendHistory(QString("=== [%1] Consolidated %2 messages ===\n%3")
                      .arg(now)
                      .arg(lines.size())
                      .arg(rawConversation));

    // 更新 MEMORY.md（LLM 摘要）
    QString mem = readLongTerm();
    if (!mem.isEmpty() && !mem.endsWith("\n")) mem += "\n";
    mem += QString("\n## Summary [%1] (%2 messages)\n\n%3\n")
               .arg(now)
               .arg(lines.size())
               .arg(summaryText);
    writeLongTerm(mem);

    session.lastConsolidated = end;

    qDebug(lcMemory) << "Consolidated" << lines.size() << "messages into long-term memory";
    return true;
}

} // namespace yaos::agent
