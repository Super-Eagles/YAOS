#ifndef YAOS_RUNTIME_MCPMANAGER_H
#define YAOS_RUNTIME_MCPMANAGER_H

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

#include "../config/Config.h"

namespace yaos::runtime {

struct MCPRemoteTool {
    QString serverName;
    QString name;
    QString description;
    QJsonObject inputSchema;
};

class MCPManager : public QObject {
    Q_OBJECT
public:
    struct CallResult {
        bool ok = false;
        QString output;
    };

    explicit MCPManager(const QHash<QString, config::MCPServerConfig> &servers,
                        QObject *parent = nullptr);
    ~MCPManager() override;

    QStringList servers() const;
    bool hasServer(const QString &name) const;
    QVector<MCPRemoteTool> listTools(const QString &serverName);
    QVector<MCPRemoteTool> listAllTools();
    CallResult call(const QString &serverName,
                    const QString &toolName,
                    const QJsonObject &arguments);

private:
    struct ServerProcess {
        QProcess *process = nullptr;
        bool initialized = false;
        int nextId = 1;
    };

    // 获取或启动某个 server 的进程（已完成 initialize 握手）
    ServerProcess *getOrStartProcess(const QString &serverName);
    bool performInitHandshake(const QString &serverName, ServerProcess &sp);

    // 发送一行 JSON-RPC 请求，等待并返回响应
    QJsonObject sendRequest(ServerProcess &sp, const QJsonObject &request, int timeoutMs);

    // 读取一个完整的 JSON-RPC 响应行
    static QJsonObject readResponseLine(QProcess *proc, int timeoutMs);

    CallResult callStdio(const QString &serverName, const QString &toolName,
                         const QJsonObject &arguments);
    CallResult callHttp(const QString &serverName, const QString &toolName,
                        const QJsonObject &arguments);
    CallResult callSse(const QString &serverName, const QString &toolName,
                       const QJsonObject &arguments);
    QVector<MCPRemoteTool> listToolsStdio(const QString &serverName);
    QVector<MCPRemoteTool> listToolsHttp(const QString &serverName);
    QVector<MCPRemoteTool> listToolsSse(const QString &serverName);

    QHash<QString, config::MCPServerConfig> _servers;

    // ✅ 持久化的进程缓存，避免每次调用都重新启动
    mutable QMutex _procMutex;
    QHash<QString, ServerProcess> _processes;
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_MCPMANAGER_H
