#include "WebTools.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <utility>

#include "platform/network/FastNetHttpTransport.h"

namespace yaos::agent::tools {

namespace {

constexpr int kSearchTimeoutMs = 15000;
constexpr int kFetchTimeoutMs = 30000;
constexpr int kMaxResults = 10;
const char *kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_7_2) AppleWebKit/537.36";

struct HttpResponse {
    bool ok = false;
    int status = 0;
    QByteArray body;
    QUrl finalUrl;
    QHash<QString, QString> headers;
    QString error;
};

struct SearchItem {
    QString title;
    QString url;
    QString content;
};

QString normalizedText(QString text) {
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    text.replace(QRegularExpression("[ \\t]+"), " ");
    text.replace(QRegularExpression("\n{3,}"), "\n\n");
    return text.trimmed();
}

QString htmlEntityValue(const QString &entity) {
    const QString name = entity.toLower();
    if (name == "amp") return "&";
    if (name == "lt") return "<";
    if (name == "gt") return ">";
    if (name == "quot") return "\"";
    if (name == "apos") return "'";
    if (name == "nbsp") return " ";
    if (name == "ndash") return QString(QChar(0x2013));
    if (name == "mdash") return QString(QChar(0x2014));
    if (name == "hellip") return QString(QChar(0x2026));
    if (name == "copy") return QString(QChar(0x00A9));
    if (name == "reg") return QString(QChar(0x00AE));
    if (name == "trade") return QString(QChar(0x2122));
    if (name == "bull") return QString(QChar(0x2022));
    return QString("&%1;").arg(entity);
}

QString decodeHtmlEntities(const QString &text) {
    static const QRegularExpression entityRe("&(#x[0-9a-fA-F]+|#[0-9]+|[A-Za-z][A-Za-z0-9]+);");

    QString decoded;
    int lastIndex = 0;
    QRegularExpressionMatchIterator it = entityRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        decoded += text.midRef(lastIndex, match.capturedStart() - lastIndex);

        const QString entity = match.captured(1);
        bool ok = false;
        uint codePoint = 0;
        if (entity.startsWith("#x", Qt::CaseInsensitive)) {
            codePoint = entity.midRef(2).toUInt(&ok, 16);
        } else if (entity.startsWith('#')) {
            codePoint = entity.midRef(1).toUInt(&ok, 10);
        }

        if (ok && codePoint <= 0x10ffff) {
            decoded += QString::fromUcs4(&codePoint, 1);
        } else {
            decoded += htmlEntityValue(entity);
        }
        lastIndex = match.capturedEnd();
    }
    decoded += text.midRef(lastIndex);
    return decoded;
}

QString stripTags(QString text) {
    text.remove(QRegularExpression("<script[\\s\\S]*?</script>",
                                  QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression("<style[\\s\\S]*?</style>",
                                  QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression("<!--[\\s\\S]*?-->"));
    text.replace(QRegularExpression("<(br|hr)\\b[^>]*>",
                                    QRegularExpression::CaseInsensitiveOption),
                 "\n");
    text.replace(QRegularExpression("</(p|div|section|article|header|footer|nav|main|li|ul|ol|h[1-6]|tr|table)>",
                                    QRegularExpression::CaseInsensitiveOption),
                 "\n");
    text.remove(QRegularExpression("<[^>]+>"));
    return decodeHtmlEntities(text).trimmed();
}

QString cleanHtmlText(const QString &text) {
    return normalizedText(stripTags(text));
}

QString lowerTrimmed(const QString &value) {
    return value.trimmed().toLower();
}

bool validateUrl(const QString &input, QString *error) {
    const QUrl url(input.trimmed());
    if (!url.isValid()) {
        if (error) *error = "Invalid URL.";
        return false;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme != "http" && scheme != "https") {
        if (error) *error = QString("Only http/https supported, got '%1'.").arg(scheme.isEmpty() ? "none" : scheme);
        return false;
    }
    if (url.host().trimmed().isEmpty()) {
        if (error) *error = "Missing host.";
        return false;
    }
    return true;
}

QString searchApiKey(const config::WebSearchConfig &config, const char *envVar) {
    if (!config.apiKey.trimmed().isEmpty()) {
        return config.apiKey.trimmed();
    }
    return qEnvironmentVariable(envVar).trimmed();
}

QString jinaApiKey(const config::WebSearchConfig &config) {
    const QString envKey = qEnvironmentVariable("JINA_API_KEY").trimmed();
    if (!envKey.isEmpty()) {
        return envKey;
    }
    if (lowerTrimmed(config.provider) == "jina") {
        return config.apiKey.trimmed();
    }
    return QString();
}

bool validateProxyUrl(const QString &proxyValue, QString *error) {
    const QString trimmed = proxyValue.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    const QUrl proxyUrl(trimmed);
    if (!proxyUrl.isValid() || proxyUrl.host().trimmed().isEmpty()) {
        if (error) *error = "Invalid proxy URL.";
        return false;
    }

    const QString scheme = proxyUrl.scheme().toLower();
    if (scheme != "http") {
        if (error) {
            *error = QString("Unsupported proxy scheme '%1'. FastNet HTTP transport supports http:// proxy URLs.")
                         .arg(scheme);
        }
        return false;
    }
    return true;
}

HttpResponse sendRequest(const QString &method,
                         const QString &endpoint,
                         const QHash<QString, QString> &headers,
                         const QByteArray &body,
                         const QString &proxyValue,
                         int timeoutMs,
                         const QHash<QString, QString> &query = QHash<QString, QString>()) {
    HttpResponse response;

    QUrl url(endpoint);
    QUrlQuery urlQuery(url);
    for (auto it = query.begin(); it != query.end(); ++it) {
        urlQuery.addQueryItem(it.key(), it.value());
    }
    url.setQuery(urlQuery);

    QString proxyError;
    if (!validateProxyUrl(proxyValue, &proxyError)) {
        response.error = proxyError;
        return response;
    }

    platform::network::HttpRequest request;
    request.method = method;
    request.url = url.toString(QUrl::FullyEncoded);
    request.body = body;
    request.proxyUrl = proxyValue.trimmed();
    request.timeoutMs = timeoutMs;
    request.headers.insert("User-Agent", kUserAgent);
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        request.headers.insert(it.key().toUtf8(), it.value().toUtf8());
    }
    if (method == "POST") {
        request.headers.insert("Content-Type", "application/json; charset=utf-8");
    }

    const platform::network::HttpResponse networkResponse = platform::network::FastNetHttpTransport::send(request);
    response.status = networkResponse.statusCode;
    response.finalUrl = url;
    response.body = networkResponse.body;
    for (auto it = networkResponse.headers.constBegin(); it != networkResponse.headers.constEnd(); ++it) {
        response.headers.insert(QString::fromUtf8(it.key()).toLower(), QString::fromUtf8(it.value()));
    }
    response.error = networkResponse.error;
    response.ok = networkResponse.ok();
    if (!response.ok && response.error.isEmpty()) {
        response.error = QString("HTTP %1").arg(response.status);
    }
    return response;
}

QString formatResults(const QString &query, const QVector<SearchItem> &items, int count) {
    if (items.isEmpty()) {
        return QString("No results for: %1").arg(query);
    }

    QStringList lines;
    lines << QString("Results for: %1\n").arg(query);
    const int limit = qMin(count, items.size());
    for (int i = 0; i < limit; ++i) {
        const SearchItem &item = items.at(i);
        lines << QString("%1. %2").arg(i + 1).arg(cleanHtmlText(item.title));
        lines << QString("   %1").arg(item.url);
        const QString snippet = cleanHtmlText(item.content);
        if (!snippet.isEmpty()) {
            lines << QString("   %1").arg(snippet);
        }
    }
    return lines.join("\n");
}

QString duckDuckGoTargetUrl(const QString &href) {
    const QUrl url = QUrl::fromUserInput(href);
    if (url.host().contains("duckduckgo.com", Qt::CaseInsensitive)) {
        const QUrlQuery query(url);
        const QString uddg = query.queryItemValue("uddg", QUrl::FullyDecoded);
        if (!uddg.trimmed().isEmpty()) {
            return uddg;
        }
    }
    return href;
}

QVector<SearchItem> parseDuckDuckGoHtml(const QString &html, int limit) {
    QVector<SearchItem> items;
    const QRegularExpression titleRe(
        "<a[^>]*class=\"[^\"]*result__a[^\"]*\"[^>]*href=\"([^\"]+)\"[^>]*>([\\s\\S]*?)</a>",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression snippetRe(
        "<(?:a|div)[^>]*class=\"[^\"]*result__snippet[^\"]*\"[^>]*>([\\s\\S]*?)</(?:a|div)>",
        QRegularExpression::CaseInsensitiveOption);

    QStringList snippets;
    QRegularExpressionMatchIterator snippetIt = snippetRe.globalMatch(html);
    while (snippetIt.hasNext()) {
        snippets << snippetIt.next().captured(1);
    }

    QSet<QString> seen;
    QRegularExpressionMatchIterator titleIt = titleRe.globalMatch(html);
    int index = 0;
    while (titleIt.hasNext() && items.size() < limit) {
        const QRegularExpressionMatch match = titleIt.next();
        SearchItem item;
        item.url = duckDuckGoTargetUrl(match.captured(1));
        item.title = match.captured(2);
        if (index < snippets.size()) {
            item.content = snippets.at(index);
        }
        ++index;

        const QString normalizedUrl = item.url.trimmed();
        if (normalizedUrl.isEmpty() || seen.contains(normalizedUrl)) {
            continue;
        }
        seen.insert(normalizedUrl);
        items.append(item);
    }
    return items;
}

QString htmlToMarkdown(QString htmlContent) {
    htmlContent.replace(QRegularExpression("<a\\s+[^>]*href=[\"']([^\"']+)[\"'][^>]*>([\\s\\S]*?)</a>",
                                          QRegularExpression::CaseInsensitiveOption),
                        "[\\2](\\1)");
    htmlContent.replace(QRegularExpression("<h([1-6])[^>]*>([\\s\\S]*?)</h\\1>",
                                          QRegularExpression::CaseInsensitiveOption),
                        "\n# \\2\n");
    htmlContent.replace(QRegularExpression("<li[^>]*>([\\s\\S]*?)</li>",
                                          QRegularExpression::CaseInsensitiveOption),
                        "\n- \\1");
    htmlContent.replace(QRegularExpression("</(p|div|section|article)>",
                                          QRegularExpression::CaseInsensitiveOption),
                        "\n\n");
    htmlContent.replace(QRegularExpression("<(br|hr)\\s*/?>",
                                          QRegularExpression::CaseInsensitiveOption),
                        "\n");
    return cleanHtmlText(htmlContent);
}

QString htmlToPlainText(const QString &htmlContent) {
    return cleanHtmlText(htmlContent);
}

QString extractTitle(const QString &html) {
    const QRegularExpression titleRe("<title[^>]*>([\\s\\S]*?)</title>",
                                     QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = titleRe.match(html);
    return match.hasMatch() ? cleanHtmlText(match.captured(1)) : QString();
}

QString extractHtmlBlock(const QString &html, const QString &tagName) {
    const QRegularExpression re(
        QString("<%1\\b[^>]*>([\\s\\S]*?)</%1>").arg(QRegularExpression::escape(tagName)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(html);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString bestHtmlContent(const QString &html) {
    const QString article = extractHtmlBlock(html, "article");
    if (!article.trimmed().isEmpty()) {
        return article;
    }
    const QString main = extractHtmlBlock(html, "main");
    if (!main.trimmed().isEmpty()) {
        return main;
    }
    const QString body = extractHtmlBlock(html, "body");
    if (!body.trimmed().isEmpty()) {
        return body;
    }
    return html;
}

QString jsonError(const QString &url, const QString &message) {
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {"error", message},
        {"url", url}
    }).toJson(QJsonDocument::Compact));
}

QString fetchTextFromJson(const QByteArray &body) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) {
        return QString();
    }
    if (doc.isObject()) {
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }
    if (doc.isArray()) {
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }
    return QString();
}

QString headerValue(const QHash<QString, QString> &headers, const QString &name) {
    return headers.value(name.toLower()).trimmed();
}

QString buildFetchResult(const QString &url,
                         const QUrl &finalUrl,
                         int status,
                         const QString &extractor,
                         QString text,
                         int maxChars) {
    const bool truncated = text.size() > maxChars;
    if (truncated) {
        text = text.left(maxChars);
    }

    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {"url", url},
        {"finalUrl", finalUrl.isValid() ? finalUrl.toString() : url},
        {"status", status},
        {"extractor", extractor},
        {"truncated", truncated},
        {"length", text.size()},
        {"text", text}
    }).toJson(QJsonDocument::Compact));
}

} // namespace

WebSearchTool::WebSearchTool(const config::WebSearchConfig &config, QString proxy)
    : _config(config), _proxy(std::move(proxy)) {}

QString WebSearchTool::name() const { return "web_search"; }

QString WebSearchTool::description() const {
    return "搜索网页,返回标题,链接和摘要.";
}

QJsonObject WebSearchTool::parameters() const {
    return QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"query", QJsonObject{{"type", "string"}, {"description", "搜索关键词"}}},
            {"count", QJsonObject{{"type", "integer"}, {"description", "结果数 (1-10)"}, {"minimum", 1}, {"maximum", 10}}}
        }},
        {"required", QJsonArray{"query"}}
    };
}

QString WebSearchTool::execute(const QJsonObject &params) {
    const QString query = params.value("query").toString().trimmed();
    if (query.isEmpty()) {
        return "Error: query is required.";
    }

    int requestedCount = params.value("count").toInt(_config.maxResults);
    if (requestedCount <= 0) {
        requestedCount = _config.maxResults;
    }
    const int count = qBound(1, requestedCount, kMaxResults);
    QString provider = lowerTrimmed(_config.provider);
    if (provider.isEmpty()) {
        provider = "brave";
    }

    if (provider == "brave") return searchBrave(query, count);
    if (provider == "tavily") return searchTavily(query, count);
    if (provider == "searxng") return searchSearXNG(query, count);
    if (provider == "jina") return searchJina(query, count);
    if (provider == "duckduckgo") return searchDuckDuckGo(query, count);
    return QString("Error: unknown search provider '%1'").arg(provider);
}

QString WebSearchTool::searchBrave(const QString &query, int count) const {
    const QString apiKey = searchApiKey(_config, "BRAVE_API_KEY");
    if (apiKey.isEmpty()) {
        return searchDuckDuckGo(query, count);
    }

    const HttpResponse response = sendRequest(
        "GET",
        "https://api.search.brave.com/res/v1/web/search",
        QHash<QString, QString>{
            {"Accept", "application/json"},
            {"X-Subscription-Token", apiKey}
        },
        QByteArray(),
        _proxy,
        kSearchTimeoutMs,
        QHash<QString, QString>{
            {"q", query},
            {"count", QString::number(count)}
        });
    if (!response.ok) {
        return "Error: " + response.error;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return "Error: invalid Brave response.";
    }

    QVector<SearchItem> items;
    const QJsonArray results = doc.object().value("web").toObject().value("results").toArray();
    for (const QJsonValue &value : results) {
        const QJsonObject obj = value.toObject();
        items.append(SearchItem{
            obj.value("title").toString(),
            obj.value("url").toString(),
            obj.value("description").toString()
        });
    }
    return formatResults(query, items, count);
}

QString WebSearchTool::searchTavily(const QString &query, int count) const {
    const QString apiKey = searchApiKey(_config, "TAVILY_API_KEY");
    if (apiKey.isEmpty()) {
        return searchDuckDuckGo(query, count);
    }

    const QJsonObject payload{
        {"query", query},
        {"max_results", count}
    };
    const HttpResponse response = sendRequest(
        "POST",
        "https://api.tavily.com/search",
        QHash<QString, QString>{
            {"Authorization", "Bearer " + apiKey}
        },
        QJsonDocument(payload).toJson(QJsonDocument::Compact),
        _proxy,
        kSearchTimeoutMs);
    if (!response.ok) {
        return "Error: " + response.error;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return "Error: invalid Tavily response.";
    }

    QVector<SearchItem> items;
    const QJsonArray results = doc.object().value("results").toArray();
    for (const QJsonValue &value : results) {
        const QJsonObject obj = value.toObject();
        items.append(SearchItem{
            obj.value("title").toString(),
            obj.value("url").toString(),
            obj.value("content").toString()
        });
    }
    return formatResults(query, items, count);
}

QString WebSearchTool::searchSearXNG(const QString &query, int count) const {
    QString baseUrl = _config.baseUrl.trimmed();
    if (baseUrl.isEmpty()) {
        baseUrl = qEnvironmentVariable("SEARXNG_BASE_URL").trimmed();
    }
    if (baseUrl.isEmpty()) {
        return searchDuckDuckGo(query, count);
    }

    QString endpoint = baseUrl;
    if (endpoint.endsWith('/')) {
        endpoint.chop(1);
    }
    endpoint += "/search";

    QString urlError;
    if (!validateUrl(endpoint, &urlError)) {
        return "Error: invalid SearXNG URL: " + urlError;
    }

    const HttpResponse response = sendRequest(
        "GET",
        endpoint,
        QHash<QString, QString>{{"Accept", "application/json"}},
        QByteArray(),
        _proxy,
        kSearchTimeoutMs,
        QHash<QString, QString>{
            {"q", query},
            {"format", "json"}
        });
    if (!response.ok) {
        return "Error: " + response.error;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return "Error: invalid SearXNG response.";
    }

    QVector<SearchItem> items;
    const QJsonArray results = doc.object().value("results").toArray();
    for (const QJsonValue &value : results) {
        const QJsonObject obj = value.toObject();
        items.append(SearchItem{
            obj.value("title").toString(),
            obj.value("url").toString(),
            obj.value("content").toString(obj.value("snippet").toString())
        });
    }
    return formatResults(query, items, count);
}

QString WebSearchTool::searchJina(const QString &query, int count) const {
    const QString apiKey = searchApiKey(_config, "JINA_API_KEY");
    if (apiKey.isEmpty()) {
        return searchDuckDuckGo(query, count);
    }

    const HttpResponse response = sendRequest(
        "GET",
        "https://s.jina.ai/",
        QHash<QString, QString>{
            {"Accept", "application/json"},
            {"Authorization", "Bearer " + apiKey}
        },
        QByteArray(),
        _proxy,
        kSearchTimeoutMs,
        QHash<QString, QString>{{"q", query}});
    if (!response.ok) {
        return "Error: " + response.error;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return "Error: invalid Jina response.";
    }

    QVector<SearchItem> items;
    const QJsonArray results = doc.object().value("data").toArray();
    for (const QJsonValue &value : results) {
        const QJsonObject obj = value.toObject();
        items.append(SearchItem{
            obj.value("title").toString(),
            obj.value("url").toString(),
            obj.value("content").toString().left(500)
        });
    }
    return formatResults(query, items, count);
}

QString WebSearchTool::searchDuckDuckGo(const QString &query, int count) const {
    const HttpResponse response = sendRequest(
        "GET",
        "https://html.duckduckgo.com/html/",
        QHash<QString, QString>{{"Accept", "text/html"}},
        QByteArray(),
        _proxy,
        kSearchTimeoutMs,
        QHash<QString, QString>{{"q", query}});
    if (!response.ok) {
        return QString("Error: DuckDuckGo search failed (%1)").arg(response.error);
    }

    const QVector<SearchItem> items = parseDuckDuckGoHtml(QString::fromUtf8(response.body), count);
    return formatResults(query, items, count);
}

WebFetchTool::WebFetchTool(const config::WebSearchConfig &searchConfig, QString proxy, int maxChars)
    : _searchConfig(searchConfig), _proxy(std::move(proxy)), _maxChars(maxChars) {}

QString WebFetchTool::name() const { return "web_fetch"; }

QString WebFetchTool::description() const {
    return "抓取网页并提取可读正文,支持 markdown 或 text 输出.";
}

QJsonObject WebFetchTool::parameters() const {
    return QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"url", QJsonObject{{"type", "string"}, {"description", "要抓取的 URL"}}},
            {"extractMode", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"markdown", "text"}}, {"default", "markdown"}}},
            {"maxChars", QJsonObject{{"type", "integer"}, {"minimum", 100}}}
        }},
        {"required", QJsonArray{"url"}}
    };
}

QString WebFetchTool::execute(const QJsonObject &params) {
    const QString url = params.value("url").toString().trimmed();
    QString extractMode = lowerTrimmed(params.value("extractMode").toString());
    int maxChars = params.value("maxChars").toInt(_maxChars);
    if (maxChars < 100) {
        maxChars = _maxChars;
    }
    if (extractMode.isEmpty()) {
        extractMode = "markdown";
    }

    QString urlError;
    if (!validateUrl(url, &urlError)) {
        return jsonError(url, "URL validation failed: " + urlError);
    }

    const QString jinaResult = fetchViaJina(url, maxChars);
    if (!jinaResult.isEmpty()) {
        return jinaResult;
    }

    return fetchViaHtml(url, extractMode == "markdown" ? "markdown" : "text", maxChars);
}

QString WebFetchTool::fetchViaJina(const QString &url, int maxChars) const {
    const QString apiKey = jinaApiKey(_searchConfig);
    QHash<QString, QString> headers{
        {"Accept", "application/json"}
    };
    if (!apiKey.isEmpty()) {
        headers.insert("Authorization", "Bearer " + apiKey);
    }

    const HttpResponse response = sendRequest(
        "GET",
        "https://r.jina.ai/" + url,
        headers,
        QByteArray(),
        _proxy,
        kFetchTimeoutMs);
    if (!response.ok || response.status == 429) {
        return QString();
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return QString();
    }

    const QJsonObject data = doc.object().value("data").toObject();
    QString text = data.value("content").toString();
    if (text.trimmed().isEmpty()) {
        return QString();
    }

    const QString title = data.value("title").toString().trimmed();
    if (!title.isEmpty()) {
        text = "# " + title + "\n\n" + text;
    }
    return buildFetchResult(url,
                            QUrl(data.value("url").toString(url)),
                            response.status,
                            "jina",
                            text,
                            maxChars);
}

QString WebFetchTool::fetchViaHtml(const QString &url, const QString &extractMode, int maxChars) const {
    const HttpResponse response = sendRequest(
        "GET",
        url,
        QHash<QString, QString>{{"Accept", "*/*"}},
        QByteArray(),
        _proxy,
        kFetchTimeoutMs);
    if (!response.ok) {
        return jsonError(url, response.error);
    }

    const QString contentType = headerValue(response.headers, "content-type").toLower();
    QString text;
    QString extractor = "raw";
    const QString rawText = QString::fromUtf8(response.body);

    if (contentType.contains("application/json")) {
        text = fetchTextFromJson(response.body);
        extractor = "json";
    } else if (contentType.contains("text/html") ||
               rawText.left(256).trimmed().startsWith("<!doctype", Qt::CaseInsensitive) ||
               rawText.left(256).trimmed().startsWith("<html", Qt::CaseInsensitive)) {
        const QString html = bestHtmlContent(rawText);
        const QString title = extractTitle(rawText);
        const QString content = extractMode == "text"
            ? htmlToPlainText(html)
            : htmlToMarkdown(html);
        text = title.isEmpty() ? content : "# " + title + "\n\n" + content;
        extractor = "html";
    } else {
        text = rawText;
    }

    return buildFetchResult(url, response.finalUrl, response.status, extractor, text, maxChars);
}

} // namespace yaos::agent::tools
