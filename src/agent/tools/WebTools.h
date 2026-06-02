#ifndef YAOS_AGENT_TOOLS_WEBTOOLS_H
#define YAOS_AGENT_TOOLS_WEBTOOLS_H

#include "../../config/Config.h"
#include "../Tool.h"

namespace yaos::agent::tools {

class WebSearchTool : public Tool {
public:
    explicit WebSearchTool(const config::WebSearchConfig &config = config::WebSearchConfig(),
                           QString proxy = QString());

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

private:
    QString searchBrave(const QString &query, int count) const;
    QString searchTavily(const QString &query, int count) const;
    QString searchSearXNG(const QString &query, int count) const;
    QString searchJina(const QString &query, int count) const;
    QString searchDuckDuckGo(const QString &query, int count) const;

private:
    config::WebSearchConfig _config;
    QString _proxy;
};

class WebFetchTool : public Tool {
public:
    explicit WebFetchTool(const config::WebSearchConfig &searchConfig = config::WebSearchConfig(),
                          QString proxy = QString(),
                          int maxChars = 50000);

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

private:
    QString fetchViaJina(const QString &url, int maxChars) const;
    QString fetchViaHtml(const QString &url, const QString &extractMode, int maxChars) const;

private:
    config::WebSearchConfig _searchConfig;
    QString _proxy;
    int _maxChars = 50000;
};

} // namespace yaos::agent::tools

#endif // YAOS_AGENT_TOOLS_WEBTOOLS_H
