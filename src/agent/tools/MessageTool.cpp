#include "MessageTool.h"

#include <QJsonArray>

namespace yaos::agent::tools {

MessageTool::MessageTool(const SendCallback &sendCallback)
    : _sendCallback(sendCallback) {}

QString MessageTool::name() const {
    return "message";
}

QString MessageTool::description() const {
    return "主动向用户发送消息反馈.";
}

QJsonObject MessageTool::parameters() const {
    QJsonObject props;
    props["content"] = QJsonObject{{"type", "string"}, {"description", "消息内容"}};
    props["channel"] = QJsonObject{{"type", "string"}, {"description", "目标频道 (可选,默认当前频道)"}};
    props["to"] = QJsonObject{{"type", "string"}, {"description", "目标会话/聊天 ID (可选,等同于 chat_id)"}};
    props["chat_id"] = QJsonObject{{"type", "string"}, {"description", "目标聊天/用户 ID (可选)"}};
    props["message_id"] = QJsonObject{{"type", "string"}, {"description", "回复的消息 ID (可选)"}};
    props["media"] = QJsonObject{
        {"type", "array"},
        {"items", QJsonObject{{"type", "string"}}},
        {"description", "附件文件路径 (可选)"}
    };

    return QJsonObject{
        {"type", "object"},
        {"properties", props},
        {"required", QJsonArray{"content"}}
    };
}

void MessageTool::setContext(const QString &channel, const QString &chatId, const QString &messageId) {
    _defaultChannel = channel;
    _defaultChatId = chatId;
    _defaultMessageId = messageId;
}

void MessageTool::setSendCallback(const SendCallback &callback) {
    _sendCallback = callback;
}

void MessageTool::startTurn() {
    _sentInTurn = false;
}

bool MessageTool::sentInTurn() const {
    return _sentInTurn;
}

QString MessageTool::execute(const QJsonObject &params) {
    const QString content = params.value("content").toString();
    const QString channel = params.value("channel").toString(_defaultChannel);
    const QString chatId = params.value("chat_id").toString(
        params.value("to").toString(_defaultChatId)
    );
    const QString messageId = params.value("message_id").toString(_defaultMessageId);

    if (content.trimmed().isEmpty()) {
        return "Error: content is required";
    }
    if (channel.trimmed().isEmpty() || chatId.trimmed().isEmpty()) {
        return "Error: No target channel/chat specified";
    }
    if (!_sendCallback) {
        return "Error: Message sending not configured";
    }

    QStringList media;
    const QJsonArray mediaArray = params.value("media").toArray();
    for (const QJsonValue &v : mediaArray) {
        if (v.isString()) {
            media.append(v.toString());
        }
    }

    bus::OutboundMessage msg;
    msg.channel = channel;
    msg.chatId = chatId;
    msg.content = content;
    msg.media = media;
    msg.metadata = QJsonObject{{"message_id", messageId}};

    _sendCallback(msg);

    if (channel == _defaultChannel && chatId == _defaultChatId) {
        _sentInTurn = true;
    }
    return QString("Message sent to %1:%2").arg(channel, chatId);
}

} // namespace yaos::agent::tools
