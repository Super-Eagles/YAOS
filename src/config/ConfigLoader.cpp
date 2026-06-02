#include "ConfigLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextStream>

namespace yaos::config {

namespace {

QString resolvedHomePath() {
    QString home = qEnvironmentVariable("HOME").trimmed();
    if (home.isEmpty()) {
        home = qEnvironmentVariable("USERPROFILE").trimmed();
    }
#ifdef Q_OS_WIN
    if (home.isEmpty()) {
        const QString homeDrive = qEnvironmentVariable("HOMEDRIVE").trimmed();
        const QString homePath = qEnvironmentVariable("HOMEPATH").trimmed();
        if (!homeDrive.isEmpty() && !homePath.isEmpty()) {
            home = homeDrive + homePath;
        }
    }
#endif
    if (home.isEmpty()) {
        home = QDir::homePath();
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(home));
}

} // namespace

QString ConfigLoader::defaultConfigPath() {
    return QDir(resolvedHomePath()).filePath(".yaos/config.json");
}

bool ConfigLoader::isLoadable(const QString &path, QString *error) {
    if (error) {
        error->clear();
    }
    const QString actualPath = path.isEmpty() ? defaultConfigPath() : path;
    QFile file(actualPath);
    if (!file.exists()) {
        if (error) {
            *error = QStringLiteral("Config file does not exist.");
        }
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = parseError.error != QJsonParseError::NoError
                ? parseError.errorString()
                : QStringLiteral("Config root must be a JSON object.");
        }
        return false;
    }
    return true;
}

Config ConfigLoader::load(const QString &path) {
    const QString actualPath = path.isEmpty() ? defaultConfigPath() : path;
    QFile file(actualPath);
    if (!file.exists()) {
        return Config();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return Config();
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return Config();
    }
    return Config::fromJson(doc.object());
}

bool ConfigLoader::save(const Config &config, const QString &path) {
    const QString actualPath = path.isEmpty() ? defaultConfigPath() : path;
    const QFileInfo info(actualPath);
    QDir().mkpath(info.absolutePath());

    QSaveFile file(actualPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QJsonDocument doc(config.toJson());
    if (file.write(doc.toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    return file.commit();
}

} // namespace yaos::config
