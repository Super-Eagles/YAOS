#include "LocalMemoryIngestor.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

namespace yaos::memory {

namespace {

QString readUtf8(const QString &path) {
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

QString formatFactLine(const MemoryFact &fact) {
    return QString("- [%1] %2 %3 %4")
        .arg(fact.confidence, 0, 'f', 2)
        .arg(fact.subject, fact.predicate, fact.value);
}

} // namespace

LocalMemoryIngestor::LocalMemoryIngestor(const QString &workspace,
                                         const IFactStore *factStore,
                                         IMemoryExporter *exporter,
                                         bool enableDailySummaries,
                                         bool exportMarkdown)
    : _workspace(workspace),
      _factStore(factStore),
      _exporter(exporter),
      _enableDailySummaries(enableDailySummaries),
      _exportMarkdown(exportMarkdown) {}

bool LocalMemoryIngestor::ingestTurn(const QString &workspaceId,
                                     const QString &sessionKey,
                                     const QList<ConversationMessage> &messages) {
    if (messages.isEmpty()) {
        return true;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString dailyDir = QDir(_workspace).filePath("memory/daily");
    const QString rawDailyPath = QDir(dailyDir).filePath(now.date().toString("yyyy-MM-dd") + ".md");

    QStringList section;
    section.append(QString("## %1 | %2").arg(now.toString("HH:mm:ss"), sessionKey));
    section.append("");
    for (const ConversationMessage &message : messages) {
        if (message.content.trimmed().isEmpty()) {
            continue;
        }
        section.append(QString("- [%1] %2").arg(message.role, message.content));
    }
    section.append("");

    QString rawContent = readUtf8(rawDailyPath);
    if (rawContent.trimmed().isEmpty()) {
        rawContent = QString("# Daily Log %1\n\n").arg(now.date().toString("yyyy-MM-dd"));
    } else if (!rawContent.endsWith('\n')) {
        rawContent.append('\n');
    }
    rawContent.append(section.join("\n"));

    if (!writeUtf8(rawDailyPath, rawContent)) {
        return false;
    }

    if (_enableDailySummaries) {
        buildDailySummary(workspaceId, now.date());
    }
    if (_exportMarkdown && _exporter) {
        _exporter->exportLegacyFiles(_workspace);
    }
    return true;
}

bool LocalMemoryIngestor::buildDailySummary(const QString &workspaceId, const QDate &date) {
    const QString dailyDir = QDir(_workspace).filePath("memory/daily");
    const QString rawDailyPath = QDir(dailyDir).filePath(date.toString("yyyy-MM-dd") + ".md");
    const QString summaryPath = QDir(dailyDir).filePath(date.toString("yyyy-MM-dd") + ".summary.md");

    QStringList lines;
    lines.append(QString("# Daily Summary %1").arg(date.toString("yyyy-MM-dd")));
    lines.append("");
    lines.append(QString("Generated: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    lines.append("");

    MemoryQuery query;
    query.workspaceId = workspaceId;
    query.includeFacts = true;
    query.includeEpisodic = false;
    query.topK = 200;

    QList<MemoryFact> facts;
    if (_factStore) {
        facts = _factStore->findFacts(query);
    }

    lines.append("## Facts Updated Today");
    lines.append("");
    bool wroteFact = false;
    for (const MemoryFact &fact : facts) {
        if (fact.updatedAt.isValid() && fact.updatedAt.date() != date) {
            continue;
        }
        lines.append(formatFactLine(fact));
        wroteFact = true;
    }
    if (!wroteFact) {
        lines.append("- No new facts recorded today.");
    }
    lines.append("");

    lines.append("## Daily Log Snapshot");
    lines.append("");
    const QString rawContent = readUtf8(rawDailyPath).trimmed();
    if (rawContent.isEmpty()) {
        lines.append("- No daily log entries yet.");
    } else {
        QStringList rawLines = rawContent.split('\n');
        while (rawLines.size() > 40) {
            rawLines.removeFirst();
        }
        lines.append(rawLines.join("\n"));
    }
    lines.append("");

    return writeUtf8(summaryPath, lines.join("\n"));
}

} // namespace yaos::memory
