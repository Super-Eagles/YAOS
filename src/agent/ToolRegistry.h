#ifndef YAOS_AGENT_TOOLREGISTRY_H
#define YAOS_AGENT_TOOLREGISTRY_H

#include <QHash>
#include <QJsonArray>
#include <QSharedPointer>

#include "Tool.h"

namespace yaos::agent {

class ToolRegistry {
public:
    void registerTool(const QSharedPointer<Tool> &tool);
    bool has(const QString &name) const;
    QSharedPointer<Tool> get(const QString &name) const;
    QString execute(const QString &name, const QJsonObject &params) const;
    QJsonArray definitions() const;

private:
    QHash<QString, QSharedPointer<Tool>> _tools;
};

} // namespace yaos::agent

#endif // YAOS_AGENT_TOOLREGISTRY_H

