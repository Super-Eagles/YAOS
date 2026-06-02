#ifndef YAOS_AGENT_TOOL_H
#define YAOS_AGENT_TOOL_H

#include <QJsonObject>
#include <QString>

namespace yaos::agent {

class Tool {
public:
    virtual ~Tool() = default;
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QJsonObject parameters() const = 0;
    virtual QString execute(const QJsonObject &params) = 0;
};

} // namespace yaos::agent

#endif // YAOS_AGENT_TOOL_H

