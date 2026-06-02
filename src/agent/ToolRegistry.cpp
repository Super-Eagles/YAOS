#include "ToolRegistry.h"

#include <algorithm>

namespace yaos::agent {

void ToolRegistry::registerTool(const QSharedPointer<Tool> &tool) {
    if (!tool) {
        return;
    }
    _tools.insert(tool->name(), tool);
}

bool ToolRegistry::has(const QString &name) const {
    return _tools.contains(name);
}

QSharedPointer<Tool> ToolRegistry::get(const QString &name) const {
    return _tools.value(name);
}

QString ToolRegistry::execute(const QString &name, const QJsonObject &params) const {
    const QSharedPointer<Tool> tool = _tools.value(name);
    if (!tool) {
        return "Error: Tool '" + name + "' not found.";
    }
    return tool->execute(params);
}

QJsonArray ToolRegistry::definitions() const {
    QJsonArray defs;
    QStringList names = _tools.keys();
    std::sort(names.begin(), names.end());
    for (const QString &name : names) {
        const QSharedPointer<Tool> tool = _tools.value(name);
        if (!tool) {
            continue;
        }
        QJsonObject fn;
        fn["name"] = tool->name();
        fn["description"] = tool->description();
        fn["parameters"] = tool->parameters();

        QJsonObject schema;
        schema["type"] = "function";
        schema["function"] = fn;
        defs.append(schema);
    }
    return defs;
}

} // namespace yaos::agent
