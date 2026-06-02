#ifndef YAOS_RUNTIME_RUNTIMESERIALIZATION_H
#define YAOS_RUNTIME_RUNTIMESERIALIZATION_H

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVector>

#include "ApprovalStore.h"
#include "AutomationStore.h"
#include "NotificationCenter.h"
#include "PluginRegistry.h"
#include "ResourceCatalog.h"
#include "RuntimeTypes.h"
#include "SkillRegistry.h"
#include "TaskStore.h"

namespace yaos::runtime::serialization {

QJsonObject toJson(const StatusSnapshot &snapshot);
StatusSnapshot statusSnapshotFromJson(const QJsonObject &obj);

QJsonObject toJson(const ChatTurnResult &result);
ChatTurnResult chatTurnResultFromJson(const QJsonObject &obj);

QJsonObject toJson(const ApprovalRecord &record);
ApprovalRecord approvalRecordFromJson(const QJsonObject &obj);
QJsonArray toJson(const QVector<ApprovalRecord> &records);
QVector<ApprovalRecord> approvalRecordsFromJson(const QJsonValue &value);

QJsonObject toJson(const NotificationRecord &record);
NotificationRecord notificationRecordFromJson(const QJsonObject &obj);
QJsonArray toJson(const QVector<NotificationRecord> &records);
QVector<NotificationRecord> notificationRecordsFromJson(const QJsonValue &value);

QJsonObject toJson(const TaskRecord &record);
TaskRecord taskRecordFromJson(const QJsonObject &obj);
QJsonArray toJson(const QVector<TaskRecord> &records);
QVector<TaskRecord> taskRecordsFromJson(const QJsonValue &value);

QJsonObject toJson(const EventRecord &record);
EventRecord eventRecordFromJson(const QJsonObject &obj);
QJsonArray toJson(const QVector<EventRecord> &records);
QVector<EventRecord> eventRecordsFromJson(const QJsonValue &value);

QJsonObject toJson(const ResourceSummary &summary);
ResourceSummary resourceSummaryFromJson(const QJsonObject &obj);

QJsonObject toJson(const ResourceRecord &record);
ResourceRecord resourceRecordFromJson(const QJsonObject &obj);
QJsonArray toJson(const QVector<ResourceRecord> &records);
QVector<ResourceRecord> resourceRecordsFromJson(const QJsonValue &value);

QJsonObject toJson(const AutomationRecord &record);
AutomationRecord automationRecordFromJson(const QJsonObject &obj);
QJsonArray toJson(const QVector<AutomationRecord> &records);
QVector<AutomationRecord> automationRecordsFromJson(const QJsonValue &value);

QJsonObject toJson(const AutomationRunRecord &record);
AutomationRunRecord automationRunRecordFromJson(const QJsonObject &obj);
QJsonArray toJson(const QVector<AutomationRunRecord> &records);
QVector<AutomationRunRecord> automationRunRecordsFromJson(const QJsonValue &value);

QJsonObject toJson(const PluginRecord &record);
PluginRecord pluginRecordFromJson(const QJsonObject &obj);
QJsonArray toJson(const QVector<PluginRecord> &records);
QVector<PluginRecord> pluginRecordsFromJson(const QJsonValue &value);

QJsonObject toJson(const SkillRecord &record);
SkillRecord skillRecordFromJson(const QJsonObject &obj);
QJsonArray toJson(const QVector<SkillRecord> &records);
QVector<SkillRecord> skillRecordsFromJson(const QJsonValue &value);

} // namespace yaos::runtime::serialization

#endif // YAOS_RUNTIME_RUNTIMESERIALIZATION_H
