#ifndef YAOS_CONFIG_CONFIGLOADER_H
#define YAOS_CONFIG_CONFIGLOADER_H

#include "Config.h"

namespace yaos::config {

class ConfigLoader {
public:
    static QString defaultConfigPath();
    static Config load(const QString &path = QString());
    static bool isLoadable(const QString &path = QString(), QString *error = nullptr);
    static bool save(const Config &config, const QString &path = QString());
};

} // namespace yaos::config

#endif // YAOS_CONFIG_CONFIGLOADER_H
