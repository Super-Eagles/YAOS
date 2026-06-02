#ifndef YAOS_RUNTIME_RUNTIMETYPES_H
#define YAOS_RUNTIME_RUNTIMETYPES_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "AutomationStore.h"
#include "EventLog.h"

namespace yaos::runtime {

struct StatusSnapshot {
    QString configPath;
    bool configReady = false;
    QString workspacePath;
    bool workspaceReady = false;
    QString defaultModel;
    QString routedProvider;
    QString actualBackend;
    bool backendFallback = false;
    QString runtimeMode;
    QString runtimeEndpoint;
    QString runtimeAdvertiseEndpoint;
    bool runtimeServiceEnabled = false;
    bool runtimeServiceReachable = false;
    bool runtimeServiceAutoSpawn = false;
    QString controlPlaneEndpoint;
    bool controlPlaneReachable = false;
    QJsonObject controlPlaneHealth;
    QString registryEndpoint;
    bool registryReachable = false;
    QString memoryServiceEndpoint;
    bool memoryServiceEnabled = false;
    bool memoryServiceReachable = false;
    bool memoryServiceAutoSpawn = false;
    int mcpServerCount = 0;
    QJsonArray providerOAuthStatuses;
    QStringList enabledChannels;
    bool restrictToWorkspace = false;
    bool gatewayRunning = false;
    bool heartbeatEnabled = false;
    int heartbeatIntervalS = 0;
    int cronJobCount = 0;
    int taskCount = 0;
    int eventCount = 0;
    int pendingApprovalCount = 0;
    int unreadNotificationCount = 0;
    int automationCount = 0;
    int pluginCount = 0;
    int skillCount = 0;
    int resourceCount = 0;
    QStringList enabledToolCapabilities;
};

struct ChatTurnResult {
    QString content;
    QString thinking;  // model reasoning / extended thinking content
    QString taskId;
    QString traceId;
    QString model;
    QString provider;
    QVector<EventRecord> trace;
    bool error = false;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_RUNTIMETYPES_H
