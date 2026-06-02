#ifndef YAOS_RUNTIME_SKILLREGISTRY_H
#define YAOS_RUNTIME_SKILLREGISTRY_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "../config/Config.h"

namespace yaos::runtime {

struct SkillRecord {
    QString id;
    QString name;
    QString description;
    QString rootPath;
    QString skillFile;
    QString state;
    QDateTime discoveredAt;
    QJsonObject metadata;
};

struct SkillMatch {
    SkillRecord record;
    config::ExtensionProfileConfig profile;
    int score = 0;
    QStringList matchedTerms;
};

class SkillRegistry {
public:
    explicit SkillRegistry(const QString &workspace);

    QVector<SkillRecord> discover() const;
    QVector<SkillMatch> enabled(const config::ExtensionsConfig &extensions) const;
    QVector<SkillMatch> select(const config::ExtensionsConfig &extensions,
                               const QString &message,
                               int limit = 3) const;
    static QString loadSkillMarkdown(const SkillRecord &record);

private:
    QVector<SkillRecord> scanDirectory(const QString &directory) const;

private:
    QString _workspace;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_SKILLREGISTRY_H
