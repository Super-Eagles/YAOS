#include "SkillRegistry.h"

#include "SkillRegistry.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace yaos::runtime {

namespace {

QString trimmedHeading(const QString &line) {
    QString text = line.trimmed();
    while (text.startsWith('#')) {
        text.remove(0, 1);
    }
    return text.trimmed();
}

QString normalizeText(const QString &value) {
    QString normalized = value.trimmed().toLower();
    normalized.replace('-', ' ');
    normalized.replace('_', ' ');
    normalized.replace(QRegularExpression("\\s+"), " ");
    return normalized.trimmed();
}

QStringList tokenCandidates(const SkillRecord &record) {
    QStringList out;
    out << normalizeText(record.id)
        << normalizeText(record.id).replace(' ', QString())
        << normalizeText(record.name);

    const QStringList words = normalizeText(record.name).split(' ', QString::SkipEmptyParts);
    for (const QString &word : words) {
        if (word.size() >= 4) {
            out << word;
        }
    }
    out.removeDuplicates();
    return out;
}

void extractMarkdownSummary(const QString &path, QString *title, QString *description) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&file);
    QStringList paragraph;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) {
            if (!paragraph.isEmpty()) {
                break;
            }
            continue;
        }
        if (line.startsWith('#') && title && title->trimmed().isEmpty()) {
            *title = trimmedHeading(line);
            continue;
        }
        if (line.startsWith('#')) {
            continue;
        }
        paragraph.append(line);
    }
    file.close();

    if (description && !paragraph.isEmpty()) {
        *description = paragraph.join(' ');
    }
}

QStringList profileTriggers(const config::ExtensionProfileConfig &profile) {
    QStringList triggers;
    for (const QString &trigger : profile.triggers) {
        const QString normalized = normalizeText(trigger);
        if (!normalized.isEmpty()) {
            triggers.append(normalized);
        }
    }
    triggers.removeDuplicates();
    return triggers;
}

int scoreSkillMatch(const SkillRecord &record,
                    const config::ExtensionProfileConfig &profile,
                    const QString &normalizedMessage,
                    QStringList *matchedTerms) {
    int score = 0;
    QStringList matches;

    const QStringList triggerTerms = profileTriggers(profile);
    for (const QString &trigger : triggerTerms) {
        if (!trigger.isEmpty() && normalizedMessage.contains(trigger)) {
            score += 45;
            matches.append(trigger);
        }
    }

    const QStringList tokens = tokenCandidates(record);
    for (const QString &token : tokens) {
        if (token.isEmpty()) {
            continue;
        }
        if (normalizedMessage.contains(token)) {
            if (token == normalizeText(record.id) || token == normalizeText(record.name)) {
                score += 120;
            } else {
                score += 12;
            }
            matches.append(token);
        }
    }

    const QString normalizedDescription = normalizeText(record.description);
    if (!normalizedDescription.isEmpty()) {
        const QStringList words = normalizedDescription.split(' ', QString::SkipEmptyParts);
        int descriptionHits = 0;
        for (const QString &word : words) {
            if (word.size() < 5) {
                continue;
            }
            if (normalizedMessage.contains(word)) {
                ++descriptionHits;
            }
        }
        score += qMin(descriptionHits, 3) * 6;
    }

    if (matchedTerms) {
        matches.removeDuplicates();
        *matchedTerms = matches;
    }
    return score;
}

} // namespace

SkillRegistry::SkillRegistry(const QString &workspace)
    : _workspace(workspace) {}

QVector<SkillRecord> SkillRegistry::scanDirectory(const QString &directory) const {
    QVector<SkillRecord> records;
    QDir dir(directory);
    if (!dir.exists()) {
        return records;
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString skillFile = QDir(entry.absoluteFilePath()).filePath("SKILL.md");
        const QFileInfo skillInfo(skillFile);
        if (!skillInfo.exists()) {
            continue;
        }

        SkillRecord record;
        record.id = entry.fileName();
        record.name = entry.fileName();
        record.rootPath = entry.absoluteFilePath();
        record.skillFile = skillFile;
        record.state = "ready";
        record.discoveredAt = skillInfo.lastModified();

        extractMarkdownSummary(skillFile, &record.name, &record.description);
        record.metadata = QJsonObject{
            {"skillFile", skillFile}
        };
        records.append(record);
    }

    return records;
}

QVector<SkillRecord> SkillRegistry::discover() const {
    QVector<SkillRecord> records = scanDirectory(QDir(_workspace).filePath("skills"));
    const QVector<SkillRecord> bundled = scanDirectory(QDir(QCoreApplication::applicationDirPath()).filePath("yaos-skills"));
    for (const SkillRecord &record : bundled) {
        bool exists = false;
        for (const SkillRecord &existing : records) {
            if (existing.rootPath == record.rootPath ||
                existing.id.compare(record.id, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            records.append(record);
        }
    }
    return records;
}

QVector<SkillMatch> SkillRegistry::enabled(const config::ExtensionsConfig &extensions) const {
    QVector<SkillMatch> matches;
    const QVector<SkillRecord> records = discover();
    matches.reserve(records.size());

    for (const SkillRecord &record : records) {
        const config::ExtensionProfileConfig profile = extensions.skills.value(record.id);
        if (!profile.enabled) {
            continue;
        }
        SkillMatch match;
        match.record = record;
        match.profile = profile;
        matches.append(match);
    }

    return matches;
}

QVector<SkillMatch> SkillRegistry::select(const config::ExtensionsConfig &extensions,
                                          const QString &message,
                                          int limit) const {
    QVector<SkillMatch> candidates = enabled(extensions);
    const QString normalizedMessage = normalizeText(message);
    if (normalizedMessage.isEmpty()) {
        return {};
    }

    QVector<SkillMatch> selected;
    selected.reserve(candidates.size());
    for (SkillMatch &match : candidates) {
        match.score = scoreSkillMatch(match.record, match.profile, normalizedMessage, &match.matchedTerms);
        if (match.score > 0) {
            selected.append(match);
        }
    }

    std::sort(selected.begin(), selected.end(), [](const SkillMatch &lhs, const SkillMatch &rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.record.name.toLower() < rhs.record.name.toLower();
    });

    if (limit > 0 && selected.size() > limit) {
        selected.resize(limit);
    }
    return selected;
}

QString SkillRegistry::loadSkillMarkdown(const SkillRecord &record) {
    QFile file(record.skillFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    return content;
}

} // namespace yaos::runtime
