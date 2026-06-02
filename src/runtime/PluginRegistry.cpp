#include "PluginRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace yaos::runtime {

namespace {

QString inferExecutorType(const QJsonObject &manifest) {
    const QJsonObject executor = manifest.value("executor").toObject();
    QString type = executor.value("type").toString().trimmed().toLower();
    if (!type.isEmpty()) {
        return type;
    }
    if (manifest.contains("entry") || manifest.contains("entryPoint")) {
        return "command";
    }
    if (executor.contains("system") || executor.contains("template") ||
        executor.contains("promptFile") || executor.contains("userTemplateFile")) {
        return "prompt";
    }
    return "unknown";
}

} // namespace

PluginRegistry::PluginRegistry(const QString &workspace)
    : _workspace(workspace) {}

QVector<PluginRecord> PluginRegistry::scanDirectory(const QString &directory) const {
    QVector<PluginRecord> records;
    QDir dir(directory);
    if (!dir.exists()) {
        return records;
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString manifestPath = QDir(entry.absoluteFilePath()).filePath("plugin.json");
        QFile file(manifestPath);
        if (!file.exists()) {
            continue;
        }

        PluginRecord record;
        record.rootPath = entry.absoluteFilePath();
        record.discoveredAt = entry.lastModified();
        record.id = entry.fileName();
        record.name = entry.fileName();
        record.state = "invalid-manifest";

        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();
            if (doc.isObject()) {
                const QJsonObject manifest = doc.object();
                record.manifest = manifest;
                record.id = manifest.value("id").toString(record.id);
                record.name = manifest.value("name").toString(record.name);
                record.version = manifest.value("version").toString();
                record.description = manifest.value("description").toString();
                record.entryPoint = manifest.value("entry").toString(manifest.value("entryPoint").toString());
                record.executorType = inferExecutorType(manifest);
                record.toolName = manifest.value("tool").toObject().value("name").toString();
                record.state = (record.executorType == "unknown") ? "missing-executor" : "ready";

                const QJsonArray capabilities = manifest.value("capabilities").toArray();
                for (const QJsonValue &value : capabilities) {
                    if (value.isString()) {
                        record.capabilities.append(value.toString());
                    }
                }
            }
        }

        records.append(record);
    }

    return records;
}

QVector<PluginRecord> PluginRegistry::discover() const {
    QVector<PluginRecord> records = scanDirectory(QDir(_workspace).filePath("plugins"));
    const QVector<PluginRecord> bundled = scanDirectory(QDir(QCoreApplication::applicationDirPath()).filePath("yaos-plugins"));
    for (const PluginRecord &record : bundled) {
        bool exists = false;
        for (const PluginRecord &existing : records) {
            if (existing.rootPath == record.rootPath) {
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

} // namespace yaos::runtime
