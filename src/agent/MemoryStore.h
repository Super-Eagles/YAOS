#ifndef YAOS_AGENT_MEMORYSTORE_H
#define YAOS_AGENT_MEMORYSTORE_H

#include <functional>
#include <QString>

#include "../session/SessionManager.h"

namespace yaos::agent {

class MemoryStore {
public:
    // LLM 摘要回调：传入原始对话文本,返回摘要
    using SummarizeFunc = std::function<QString(const QString &rawConversation)>;

    explicit MemoryStore(const QString &workspace);

    // 注入 LLM 摘要函数（由 AgentLoop 在初始化时设置）
    void setSummarizeFunc(SummarizeFunc func);

    QString readLongTerm() const;
    void writeLongTerm(const QString &content);
    void appendHistory(const QString &entry);
    QString getMemoryContext() const;

    // 真正把消息归档并用 LLM 压缩写入长期记忆
    bool consolidate(session::Session &session, int memoryWindow = 50, bool archiveAll = false);

private:
    QString summarize(const QString &rawText) const;

    QString _memoryDir;
    QString _memoryFile;
    QString _historyFile;
    SummarizeFunc _summarizeFunc;
};

} // namespace yaos::agent

#endif // YAOS_AGENT_MEMORYSTORE_H
