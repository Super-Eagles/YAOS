#ifndef YAOS_RUNTIME_PLUGINREGISTRY_H
#define YAOS_RUNTIME_PLUGINREGISTRY_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace yaos::runtime {

struct PluginRecord {
    QString id;
    QString name;
    QString version;
    QString description;
    QString rootPath;
    QString entryPoint;
    QString toolName;
    QString executorType;
    QString state;
    QStringList capabilities;
    QDateTime discoveredAt;
    QJsonObject manifest;
};

class PluginRegistry {
public:
    explicit PluginRegistry(const QString &workspace);

    QVector<PluginRecord> discover() const;

private:
    QVector<PluginRecord> scanDirectory(const QString &directory) const;

private:
    QString _workspace;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_PLUGINREGISTRY_H
