#ifndef YAOS_AGENT_TOOLS_MESSAGETOOL_H
#define YAOS_AGENT_TOOLS_MESSAGETOOL_H

#include <functional>

#include "../Tool.h"
#include "../../bus/Message.h"

namespace yaos::agent::tools {

class MessageTool : public Tool {
public:
    using SendCallback = std::function<void(const bus::OutboundMessage &)>;

    explicit MessageTool(const SendCallback &sendCallback = SendCallback());

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

    void setContext(const QString &channel, const QString &chatId, const QString &messageId = QString());
    void setSendCallback(const SendCallback &callback);
    void startTurn();
    bool sentInTurn() const;

private:
    SendCallback _sendCallback;
    QString _defaultChannel;
    QString _defaultChatId;
    QString _defaultMessageId;
    bool _sentInTurn = false;
};

} // namespace yaos::agent::tools

#endif // YAOS_AGENT_TOOLS_MESSAGETOOL_H
