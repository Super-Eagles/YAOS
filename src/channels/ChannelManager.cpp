#include "ChannelManager.h"

#include <algorithm>
#include <cstdio>
#include <QTextStream>

#include "SlackChannel.h"
#include "TelegramChannel.h"
#include "MatrixChannel.h"
#include "EmailChannel.h"
#include "QQChannel.h"
#include "FeishuChannel.h"
#include "WhatsAppChannel.h"
#include "DingTalkChannel.h"
#include "MochatChannel.h"
#include "DiscordChannel.h"






namespace yaos::channels {

ChannelManager::ChannelManager(const config::Config &config, bus::MessageBus &bus)
    : _config(config),
      _bus(bus) {
    initChannels();
}

void ChannelManager::initChannels() {
    if (_config.channels.telegram.enabled) {
        registerChannel(QSharedPointer<TelegramChannel>::create(_config,
                                                                _config.workspacePath(),
                                                                _bus));
    }
    if (_config.channels.slack.enabled) {
        registerChannel(QSharedPointer<SlackChannel>::create(_config.channels.slack, _bus));
    }
    if (_config.channels.matrix.enabled) {
        registerChannel(QSharedPointer<MatrixChannel>::create(_config.channels.matrix, _bus));
    }
    if (_config.channels.email.enabled) {
        registerChannel(QSharedPointer<EmailChannel>::create(_config.channels.email,
                                                             _config.workspacePath(),
                                                             _bus));
    }
    if (_config.channels.qq.enabled) {
        registerChannel(QSharedPointer<QQChannel>::create(_config.channels.qq, _bus));
    }
    if (_config.channels.feishu.enabled) {
        registerChannel(QSharedPointer<FeishuChannel>::create(_config.channels.feishu, _bus));
    }
    if (_config.channels.whatsapp.enabled) {
        registerChannel(QSharedPointer<WhatsAppChannel>::create(_config.channels.whatsapp, _bus));
    }
    if (_config.channels.dingtalk.enabled) {
        registerChannel(QSharedPointer<DingTalkChannel>::create(_config.channels.dingtalk, _bus));
    }
    if (_config.channels.mochat.enabled) {
        registerChannel(QSharedPointer<MochatChannel>::create(_config.channels.mochat, _bus));
    }
    if (_config.channels.discord.enabled) {
        registerChannel(QSharedPointer<DiscordChannel>::create(_config.channels.discord, _bus));
    }





}

void ChannelManager::registerChannel(const QSharedPointer<Channel> &channel) {
    if (!channel) {
        return;
    }
    _channels.insert(channel->name(), channel);
}

bool ChannelManager::has(const QString &name) const {
    return _channels.contains(name);
}

QStringList ChannelManager::enabledChannels() const {
    QStringList names = _channels.keys();
    std::sort(names.begin(), names.end());
    return names;
}

bool ChannelManager::startAll() {
    bool ok = true;
    for (auto it = _channels.begin(); it != _channels.end(); ++it) {
        if (!it.value()->start()) {
            ok = false;
        }
    }
    return ok;
}

void ChannelManager::stopAll() {
    for (auto it = _channels.begin(); it != _channels.end(); ++it) {
        it.value()->stop();
    }
}

bool ChannelManager::dispatch(const bus::OutboundMessage &msg) {
    const QSharedPointer<Channel> channel = _channels.value(msg.channel);
    if (!channel) {
        if (msg.channel == "cli") {
            if (!msg.content.trimmed().isEmpty()) {
                QTextStream out(stdout);
                out << "YAOS: " << msg.content << "\n";
                out.flush();
            }
            return true;
        }
        return false;
    }
    channel->send(msg);
    return true;
}

void ChannelManager::handleOutbound(const bus::OutboundMessage &msg) {
    const bool isProgress = msg.metadata.value("_progress").toBool(false);
    const bool isToolHint = msg.metadata.value("_tool_hint").toBool(false);
    if (isProgress) {
        if (isToolHint && !_config.channels.sendToolHints) {
            return;
        }
        if (!isToolHint && !_config.channels.sendProgress) {
            return;
        }
    }
    dispatch(msg);
}

} // namespace yaos::channels
