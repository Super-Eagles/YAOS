#include "LayeredMemoryExporter.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfoList>
#include <QSaveFile>
#include <QTextStream>

namespace yaos::memory {

namespace {

QString readFileUtf8(const QString &path) {
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    return content;
}

bool writeUtf8(const QString &path, const QString &content) {
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out << content;
    out.flush();
    return file.commit();
}

QStringList collectRecentDailyFiles(const QString &dailyDirPath, const QString &suffix, int limit) {
    const QDir dailyDir(dailyDirPath);
    if (!dailyDir.exists()) {
        return {};
    }

    const QFileInfoList files = dailyDir.entryInfoList(QStringList() << ("*" + suffix),
                                                       QDir::Files,
                                                       QDir::Time);
    QStringList selected;
    for (const QFileInfo &info : files) {
        if (selected.size() >= limit) {
            break;
        }
        selected.append(info.absoluteFilePath());
    }
    return selected;
}

QString factsSection(const IFactStore *factStore, const QString &workspace) {
    if (!factStore) {
        return QStringLiteral("## Active Facts\n\n- No fact store configured.\n");
    }

    MemoryQuery query;
    query.workspaceId = workspace;
    query.includeFacts = true;
    query.includeEpisodic = false;
    query.topK = 200;

    const QList<MemoryFact> facts = factStore->findFacts(query);
    if (facts.isEmpty()) {
        return QStringLiteral("## Active Facts\n\n- No facts recorded yet.\n");
    }

    QStringList lines;
    lines.append("## Active Facts");
    lines.append("");
    for (const MemoryFact &fact : facts) {
        lines.append(QString("- [%1] %2 %3 %4")
                         .arg(fact.confidence, 0, 'f', 2)
                         .arg(fact.subject, fact.predicate, fact.value));
    }
    lines.append("");
    return lines.join("\n");
}

QString recentSummarySection(const QString &workspace) {
    const QString dailyDir = QDir(workspace).filePath("memory/daily");
    const QStringList summaryFiles = collectRecentDailyFiles(dailyDir, ".summary.md", 5);
    QStringList lines;
    lines.append("## Recent Daily Summaries");
    lines.append("");

    if (summaryFiles.isEmpty()) {
        lines.append("- No daily summaries yet.");
        lines.append("");
        return lines.join("\n");
    }

    for (const QString &path : summaryFiles) {
        const QFileInfo info(path);
        lines.append(QString("### %1").arg(info.fileName()));
        lines.append("");
        lines.append(readFileUtf8(path).trimmed());
        lines.append("");
    }
    return lines.join("\n");
}

QString historySection(const QString &workspace) {
    const QString dailyDir = QDir(workspace).filePath("memory/daily");
    const QStringList dailyFiles = collectRecentDailyFiles(dailyDir, ".md", 5);
    QStringList lines;
    lines.append("# History Export");
    lines.append("");
    lines.append(QString("Generated: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    lines.append("");

    bool appendedAny = false;
    for (const QString &path : dailyFiles) {
        if (path.endsWith(".summary.md")) {
            continue;
        }
        const QFileInfo info(path);
        lines.append(QString("## %1").arg(info.fileName()));
        lines.append("");
        lines.append(readFileUtf8(path).trimmed());
        lines.append("");
        appendedAny = true;
    }

    if (!appendedAny) {
        lines.append("No layered daily history has been recorded yet.");
        lines.append("");
    }

    return lines.join("\n");
}

} // namespace

LayeredMemoryExporter::LayeredMemoryExporter(const QString &workspace,
                                             const IFactStore *factStore)
    : _workspace(workspace),
      _factStore(factStore) {}

bool LayeredMemoryExporter::exportLegacyFiles(const QString &workspacePath) {
    const QString effectiveWorkspace = workspacePath.trimmed().isEmpty() ? _workspace : workspacePath.trimmed();
    const QString memoryDir = QDir(effectiveWorkspace).filePath("memory");
    const QString memoryFile = QDir(memoryDir).filePath("MEMORY.md");
    const QString historyFile = QDir(memoryDir).filePath("HISTORY.md");

    QStringList memoryLines;
    memoryLines.append("# Long-term Memory");
    memoryLines.append("");
    memoryLines.append(QString("Generated from layered memory backend at %1")
                           .arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    memoryLines.append("");
    memoryLines.append(factsSection(_factStore, effectiveWorkspace).trimmed());
    memoryLines.append("");
    memoryLines.append(recentSummarySection(effectiveWorkspace).trimmed());
    memoryLines.append("");

    const bool memoryOk = writeUtf8(memoryFile, memoryLines.join("\n"));
    const bool historyOk = writeUtf8(historyFile, historySection(effectiveWorkspace));
    return memoryOk && historyOk;
}

} // namespace yaos::memory
