#ifndef YAOS_RUNTIME_EXTENSIONCATALOG_H
#define YAOS_RUNTIME_EXTENSIONCATALOG_H

#include <QString>
#include <QStringList>
#include <QVector>

#include "../config/Config.h"

namespace yaos::runtime {

struct ExtensionCatalogEntry {
    QString catalogId;
    QString kind;
    QString installId;
    QString title;
    QString summary;
    QString description;
    QString target;
    QStringList tags;
    bool installed = false;
};

QVector<ExtensionCatalogEntry> buildExtensionCatalog(const QString &workspace,
                                                     const config::Config &config);

bool installCatalogEntry(const QString &workspace,
                         config::Config *config,
                         const QString &catalogId,
                         QString *message = nullptr);

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_EXTENSIONCATALOG_H
