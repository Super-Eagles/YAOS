#ifndef YAOS_DISTRIBUTED_CONTRACTSJSON_H
#define YAOS_DISTRIBUTED_CONTRACTSJSON_H

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "Contracts.h"

namespace yaos::distributed::json {

QJsonObject toJson(const NodeCapability &capability);
NodeCapability nodeCapabilityFromJson(const QJsonObject &obj);
QJsonArray toJson(const QList<NodeCapability> &capabilities);
QList<NodeCapability> nodeCapabilitiesFromJson(const QJsonValue &value);

QJsonObject toJson(const NodeDescriptor &node);
NodeDescriptor nodeDescriptorFromJson(const QJsonObject &obj);
QJsonArray toJson(const QList<NodeDescriptor> &nodes);
QList<NodeDescriptor> nodeDescriptorsFromJson(const QJsonValue &value);

QJsonObject toJson(const TaskContextRef &ref);
TaskContextRef taskContextRefFromJson(const QJsonObject &obj);
QJsonArray toJson(const QList<TaskContextRef> &refs);
QList<TaskContextRef> taskContextRefsFromJson(const QJsonValue &value);

QJsonObject toJson(const TaskEnvelope &task);
TaskEnvelope taskEnvelopeFromJson(const QJsonObject &obj);
QJsonArray toJson(const QList<TaskEnvelope> &tasks);
QList<TaskEnvelope> taskEnvelopesFromJson(const QJsonValue &value);

QJsonObject toJson(const TaskResultEnvelope &result);
TaskResultEnvelope taskResultEnvelopeFromJson(const QJsonObject &obj);
QJsonArray toJson(const QList<TaskResultEnvelope> &results);
QList<TaskResultEnvelope> taskResultEnvelopesFromJson(const QJsonValue &value);

} // namespace yaos::distributed::json

#endif // YAOS_DISTRIBUTED_CONTRACTSJSON_H
