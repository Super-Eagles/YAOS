#include "SessionManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <QTextStream>

Q_LOGGING_CATEGORY(lcSession, "yaos.session")

namespace yaos::session {

void Session::addMessage(const QString &role, const QJsonValue &content, const QJsonObject &extra) {
    QJsonObject msg;
    msg["role"] = role;
    msg["content"] = content;
    msg["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        msg[it.key()] = it.value();
    }
    messages.append(msg);
    updatedAt = QDateTime::currentDateTime();
}

namespace {

// 修复 tool call chain：丢弃开头的"不完整片段",保留完整配对
//
// 合法的开头：
//   - user 消息
//   - assistant(tool_calls) + 紧随的所有 tool_result（完整配对）
//
// 非法的开头（需要跳过）：
//   - 孤立的 tool_result（前面缺少 assistant/tool_calls）
//   - 孤立的 assistant（无 tool_calls,且不是最终回答,如后面跟着 tool）
QJsonArray repairToolChain(const QJsonArray &sliced) {
    int start = 0;
    while (start < sliced.size()) {
        const QJsonObject msg = sliced.at(start).toObject();
        const QString role = msg.value("role").toString();

        if (role == "user") {
            break; // 合法起点
        }

        if (role == "tool") {
            // 孤立的 tool_result（前面没有 assistant/tool_calls）,跳过
            start++;
            continue;
        }

        if (role == "assistant") {
            if (msg.contains("tool_calls")) {
                // 检查后续是否有配套的 tool_result
                int j = start + 1;
                while (j < sliced.size() &&
                       sliced.at(j).toObject().value("role").toString() == "tool") {
                    j++;
                }
                if (j > start + 1) {
                    // 找到了配套的 tool_result,这是完整配对,是合法起点
                    break;
                } else {
                    // assistant 有 tool_calls 但后面没有 tool_result,不完整,跳过
                    start++;
                }
            } else {
                // 纯文本 assistant 消息也是合法起点（例如 multi-turn 中间的回答）
                break;
            }
            continue;
        }

        // 其他未知角色,跳过
        start++;
    }

    QJsonArray result;
    for (int i = start; i < sliced.size(); ++i) {
        result.append(sliced.at(i));
    }
    return result;
}

} // namespace

QJsonArray Session::getHistory(int maxMessages) const {
    // 只取 lastConsolidated 之后的消息
    QJsonArray unconsolidated;
    for (int i = lastConsolidated; i < messages.size(); ++i) {
        unconsolidated.append(messages.at(i));
    }

    // 按 maxMessages 从尾部裁剪
    QJsonArray sliced = unconsolidated;
    while (sliced.size() > maxMessages) {
        sliced.removeFirst();
    }

    // ✅ 修复 tool call chain 完整性（截断可能破坏 assistant/tool 配对）
    sliced = repairToolChain(sliced);

    // 构建输出,只保留 LLM 需要的字段
    QJsonArray out;
    for (const QJsonValue &v : sliced) {
        const QJsonObject m = v.toObject();
        QJsonObject e;
        e["role"] = m.value("role");
        e["content"] = m.value("content");
        if (m.contains("tool_calls"))    e["tool_calls"]    = m.value("tool_calls");
        if (m.contains("tool_call_id"))  e["tool_call_id"]  = m.value("tool_call_id");
        if (m.contains("name"))          e["name"]          = m.value("name");
        if (m.contains("reasoning"))     e["reasoning"]     = m.value("reasoning");
        if (m.contains("reasoning_content")) e["reasoning_content"] = m.value("reasoning_content");
        if (m.contains("reasoning_details")) e["reasoning_details"] = m.value("reasoning_details");
        out.append(e);
    }
    return out;
}

void Session::clear() {
    messages = QJsonArray();
    lastConsolidated = 0;
    updatedAt = QDateTime::currentDateTime();
}

SessionManager::SessionManager(const QString &workspace)
    : _workspace(workspace),
      _sessionsDir(QDir(workspace).filePath("sessions")) {
    QDir().mkpath(_sessionsDir);
}

QString SessionManager::safeFileName(const QString &name) {
    QString n = name;
    n.replace(QRegularExpression(R"([<>:"/\\|?*])"), "_");
    return n.trimmed();
}

QString SessionManager::sessionPath(const QString &key) const {
    const QString safe = safeFileName(key).replace(':', '_');
    return QDir(_sessionsDir).filePath(safe + ".jsonl");
}

Session SessionManager::getOrCreate(const QString &key) {
    if (_cache.contains(key)) {
        return _cache.value(key);
    }
    bool ok = false;
    Session s = load(key, &ok);
    if (!ok) {
        s.key = key;
        s.createdAt = QDateTime::currentDateTime();
        s.updatedAt = QDateTime::currentDateTime();
    }
    _cache.insert(key, s);
    return s;
}

Session SessionManager::load(const QString &key, bool *ok) const {
    Session s;
    s.key = key;

    QFile file(sessionPath(key));
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (ok) *ok = false;
        return s;
    }

    QTextStream in(&file);
    bool hasData = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning(lcSession) << "Skipping malformed line in session" << key;
            continue;
        }
        const QJsonObject obj = doc.object();
        if (obj.value("_type").toString() == "metadata") {
            s.key = obj.value("key").toString(key);
            s.createdAt = QDateTime::fromString(obj.value("created_at").toString(), Qt::ISODate);
            s.updatedAt = QDateTime::fromString(obj.value("updated_at").toString(), Qt::ISODate);
            s.metadata = obj.value("metadata").toObject();
            s.lastConsolidated = obj.value("last_consolidated").toInt(0);
            hasData = true;
        } else {
            s.messages.append(obj);
            hasData = true;
        }
    }
    file.close();

    if (!s.createdAt.isValid()) s.createdAt = QDateTime::currentDateTime();
    if (!s.updatedAt.isValid()) s.updatedAt = QDateTime::currentDateTime();
    if (ok) *ok = hasData;
    return s;
}

void SessionManager::save(const Session &session) {
    const QString path = sessionPath(session.key);
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning(lcSession) << "Failed to save session:" << session.key;
        return;
    }

    QTextStream out(&file);
    QJsonObject metadata;
    metadata["_type"] = "metadata";
    metadata["key"] = session.key;
    metadata["created_at"] = session.createdAt.toString(Qt::ISODate);
    metadata["updated_at"] = session.updatedAt.toString(Qt::ISODate);
    metadata["metadata"] = session.metadata;
    metadata["last_consolidated"] = session.lastConsolidated;
    out << QJsonDocument(metadata).toJson(QJsonDocument::Compact) << "\n";

    for (const QJsonValue &v : session.messages) {
        out << QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact) << "\n";
    }
    out.flush();
    if (!file.commit()) {
        qWarning(lcSession) << "Failed to commit session:" << session.key;
        return;
    }
    _cache.insert(session.key, session);
}

void SessionManager::invalidate(const QString &key) {
    _cache.remove(key);
}

QVector<SessionSummary> SessionManager::listSessions() const {
    QVector<SessionSummary> sessions;
    const QDir dir(_sessionsDir);
    const QFileInfoList files = dir.entryInfoList(QStringList() << "*.jsonl", QDir::Files, QDir::Time);

    for (const QFileInfo &info : files) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QTextStream in(&file);
        const QString firstLine = in.readLine().trimmed();
        file.close();
        if (firstLine.isEmpty()) {
            continue;
        }

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(firstLine.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        const QJsonObject metadata = doc.object();
        if (metadata.value("_type").toString() != "metadata") {
            continue;
        }

        SessionSummary summary;
        summary.key = metadata.value("key").toString();
        summary.createdAt = QDateTime::fromString(metadata.value("created_at").toString(), Qt::ISODate);
        summary.updatedAt = QDateTime::fromString(metadata.value("updated_at").toString(), Qt::ISODate);
        summary.path = info.absoluteFilePath();

        if (!summary.updatedAt.isValid()) {
            summary.updatedAt = info.lastModified();
        }
        if (!summary.createdAt.isValid()) {
            summary.createdAt = summary.updatedAt;
        }
        if (summary.key.trimmed().isEmpty()) {
            continue;
        }
        sessions.append(summary);
    }

    std::sort(sessions.begin(), sessions.end(), [](const SessionSummary &a, const SessionSummary &b) {
        return a.updatedAt > b.updatedAt;
    });
    return sessions;
}

} // namespace yaos::session
