#include "Config.h"
#include <QJsonArray>
#include <QDir>

namespace yaos::config {

namespace {

QString resolvedHomePath() {
    QString home = qEnvironmentVariable("HOME").trimmed();
    if (home.isEmpty()) {
        home = qEnvironmentVariable("USERPROFILE").trimmed();
    }
#ifdef Q_OS_WIN
    if (home.isEmpty()) {
        const QString homeDrive = qEnvironmentVariable("HOMEDRIVE").trimmed();
        const QString homePath = qEnvironmentVariable("HOMEPATH").trimmed();
        if (!homeDrive.isEmpty() && !homePath.isEmpty()) {
            home = homeDrive + homePath;
        }
    }
#endif
    if (home.isEmpty()) {
        home = QDir::homePath();
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(home));
}

QString expandUserPath(const QString &path) {
    if (path.startsWith("~/")) {
        return QDir(resolvedHomePath()).filePath(path.mid(2));
    }
    return path;
}

QJsonObject mapToJson(const QHash<QString, QString> &map) {
    QJsonObject obj;
    for (auto it = map.begin(); it != map.end(); ++it) {
        obj.insert(it.key(), it.value());
    }
    return obj;
}

QHash<QString, QString> jsonToMap(const QJsonObject &obj) {
    QHash<QString, QString> map;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        map.insert(it.key(), it.value().toString());
    }
    return map;
}

QStringList jsonToStringList(const QJsonValue &value);
QJsonArray stringListToJson(const QStringList &list);

QJsonObject providerToJson(const ProviderConfig &p) {
    QJsonObject obj;
    obj["apiKey"] = p.apiKey;
    if (!p.apiBase.isEmpty()) {
        obj["apiBase"] = p.apiBase;
    }
    if (!p.model.isEmpty()) {
        obj["model"] = p.model;
    }
    if (!p.availableModels.isEmpty()) {
        obj["availableModels"] = stringListToJson(p.availableModels);
    }
    if (!p.enabledModels.isEmpty()) {
        obj["enabledModels"] = stringListToJson(p.enabledModels);
    }
    if (!p.extraHeaders.isEmpty()) {
        obj["extraHeaders"] = mapToJson(p.extraHeaders);
    }
    if (!p.oauthIssuer.isEmpty()) {
        obj["oauthIssuer"] = p.oauthIssuer;
    }
    if (!p.oauthClientId.isEmpty()) {
        obj["oauthClientId"] = p.oauthClientId;
    }
    if (!p.oauthScope.isEmpty()) {
        obj["oauthScope"] = p.oauthScope;
    }
    if (!p.oauthAccessToken.isEmpty()) {
        obj["oauthAccessToken"] = p.oauthAccessToken;
    }
    if (!p.oauthRefreshToken.isEmpty()) {
        obj["oauthRefreshToken"] = p.oauthRefreshToken;
    }
    if (!p.oauthIdToken.isEmpty()) {
        obj["oauthIdToken"] = p.oauthIdToken;
    }
    if (!p.oauthTokenType.isEmpty()) {
        obj["oauthTokenType"] = p.oauthTokenType;
    }
    if (!p.oauthAccountId.isEmpty()) {
        obj["oauthAccountId"] = p.oauthAccountId;
    }
    if (!p.oauthExpiresAt.isEmpty()) {
        obj["oauthExpiresAt"] = p.oauthExpiresAt;
    }
    if (!p.oauthLastRefreshAt.isEmpty()) {
        obj["oauthLastRefreshAt"] = p.oauthLastRefreshAt;
    }
    if (!p.oauthDeviceCode.isEmpty()) {
        obj["oauthDeviceCode"] = p.oauthDeviceCode;
    }
    if (!p.oauthDeviceAuthId.isEmpty()) {
        obj["oauthDeviceAuthId"] = p.oauthDeviceAuthId;
    }
    if (!p.oauthUserCode.isEmpty()) {
        obj["oauthUserCode"] = p.oauthUserCode;
    }
    if (!p.oauthVerificationUrl.isEmpty()) {
        obj["oauthVerificationUrl"] = p.oauthVerificationUrl;
    }
    if (!p.oauthLastError.isEmpty()) {
        obj["oauthLastError"] = p.oauthLastError;
    }
    obj["oauthIntervalSec"] = p.oauthIntervalSec;
    return obj;
}

ProviderConfig jsonToProvider(const QJsonObject &obj) {
    ProviderConfig p;
    p.apiKey = obj.value("apiKey").toString();
    p.apiBase = obj.value("apiBase").toString();
    p.model = obj.value("model").toString();
    p.availableModels = jsonToStringList(obj.value("availableModels"));
    if (p.availableModels.isEmpty()) {
        p.availableModels = jsonToStringList(obj.value("models"));
    }
    p.enabledModels = jsonToStringList(obj.value("enabledModels"));
    if (p.enabledModels.isEmpty()) {
        p.enabledModels = jsonToStringList(obj.value("enabled_models"));
    }
    const QString configuredModel = p.model.trimmed();
    if (!p.enabledModels.isEmpty() &&
        (configuredModel.isEmpty() || !p.enabledModels.contains(configuredModel))) {
        p.model = p.enabledModels.first();
    } else {
        p.model = configuredModel;
    }
    p.extraHeaders = jsonToMap(obj.value("extraHeaders").toObject());
    p.oauthIssuer = obj.value("oauthIssuer").toString(obj.value("oauth_issuer").toString());
    p.oauthClientId = obj.value("oauthClientId").toString(obj.value("oauth_client_id").toString());
    p.oauthScope = obj.value("oauthScope").toString(obj.value("oauth_scope").toString());
    p.oauthAccessToken = obj.value("oauthAccessToken").toString(obj.value("oauth_access_token").toString());
    p.oauthRefreshToken = obj.value("oauthRefreshToken").toString(obj.value("oauth_refresh_token").toString());
    p.oauthIdToken = obj.value("oauthIdToken").toString(obj.value("oauth_id_token").toString());
    p.oauthTokenType = obj.value("oauthTokenType").toString(obj.value("oauth_token_type").toString());
    p.oauthAccountId = obj.value("oauthAccountId").toString(obj.value("oauth_account_id").toString());
    p.oauthExpiresAt = obj.value("oauthExpiresAt").toString(obj.value("oauth_expires_at").toString());
    p.oauthLastRefreshAt = obj.value("oauthLastRefreshAt").toString(obj.value("oauth_last_refresh_at").toString());
    p.oauthDeviceCode = obj.value("oauthDeviceCode").toString(obj.value("oauth_device_code").toString());
    p.oauthDeviceAuthId = obj.value("oauthDeviceAuthId").toString(obj.value("oauth_device_auth_id").toString());
    p.oauthUserCode = obj.value("oauthUserCode").toString(obj.value("oauth_user_code").toString());
    p.oauthVerificationUrl = obj.value("oauthVerificationUrl").toString(obj.value("oauth_verification_url").toString());
    p.oauthLastError = obj.value("oauthLastError").toString(obj.value("oauth_last_error").toString());
    p.oauthIntervalSec = obj.value("oauthIntervalSec").toInt(obj.value("oauth_interval_sec").toInt(p.oauthIntervalSec));
    return p;
}

QJsonObject webSearchToJson(const WebSearchConfig &search) {
    QJsonObject obj;
    obj["provider"] = search.provider;
    if (!search.apiKey.isEmpty()) {
        obj["apiKey"] = search.apiKey;
    }
    if (!search.baseUrl.isEmpty()) {
        obj["baseUrl"] = search.baseUrl;
    }
    obj["maxResults"] = search.maxResults;
    return obj;
}

WebSearchConfig jsonToWebSearch(const QJsonObject &obj) {
    WebSearchConfig search;
    search.provider = obj.value("provider").toString(search.provider);
    search.apiKey = obj.value("apiKey").toString(obj.value("api_key").toString());
    search.baseUrl = obj.value("baseUrl").toString(obj.value("base_url").toString());
    search.maxResults = obj.value("maxResults").toInt(obj.value("max_results").toInt(search.maxResults));
    return search;
}

QJsonObject extensionProfileToJson(const ExtensionProfileConfig &profile) {
    QJsonObject obj;
    obj["enabled"] = profile.enabled;
    obj["provider"] = profile.provider;
    if (!profile.model.isEmpty()) {
        obj["model"] = profile.model;
    }
    if (!profile.note.isEmpty()) {
        obj["note"] = profile.note;
    }
    if (!profile.triggers.isEmpty()) {
        obj["triggers"] = stringListToJson(profile.triggers);
    }
    return obj;
}

ExtensionProfileConfig jsonToExtensionProfile(const QJsonObject &obj) {
    ExtensionProfileConfig profile;
    profile.enabled = obj.value("enabled").toBool(profile.enabled);
    profile.provider = obj.value("provider").toString(profile.provider);
    profile.model = obj.value("model").toString();
    profile.note = obj.value("note").toString();
    profile.triggers = jsonToStringList(obj.value("triggers"));
    return profile;
}

QJsonObject extensionMapToJson(const QHash<QString, ExtensionProfileConfig> &profiles) {
    QJsonObject obj;
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        obj.insert(it.key(), extensionProfileToJson(it.value()));
    }
    return obj;
}

QHash<QString, ExtensionProfileConfig> jsonToExtensionMap(const QJsonObject &obj) {
    QHash<QString, ExtensionProfileConfig> profiles;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it.value().isObject()) {
            profiles.insert(it.key(), jsonToExtensionProfile(it.value().toObject()));
        }
    }
    return profiles;
}

QStringList jsonToStringList(const QJsonValue &value) {
    QStringList out;
    const QJsonArray arr = value.toArray();
    for (const auto &v : arr) {
        out.append(v.toString());
    }
    return out;
}

QJsonArray stringListToJson(const QStringList &list) {
    QJsonArray arr;
    for (const QString &v : list) {
        arr.append(v);
    }
    return arr;
}

QString normalizedKey(QString value) {
    value = value.trimmed().toLower();
    value.replace('-', '_');
    value.replace(' ', '_');
    return value;
}

QString jsonToModeString(const QJsonValue &value, const QString &fallback) {
    if (value.isString()) {
        const QString text = value.toString().trimmed();
        return text.isEmpty() ? fallback : text;
    }
    if (value.isDouble()) {
        return QString::number(value.toInt());
    }
    return fallback;
}

QString normalizeDeploymentModeValue(QString value) {
    value = normalizedKey(value);
    if (value.isEmpty()) {
        return "standalone";
    }
    if (value == "single" || value == "single_node" || value == "desktop" || value == "local") {
        return "standalone";
    }
    if (value == "localcluster" || value == "local_cluster" || value == "sidecar") {
        return "local_cluster";
    }
    if (value == "clustered") {
        return "cluster";
    }
    if (value == "standalone" || value == "cluster") {
        return value;
    }
    return "standalone";
}

QString normalizeRuntimeModeValue(QString value) {
    value = normalizedKey(value);
    if (value.isEmpty()) {
        return "embedded";
    }
    if (value == "local" || value == "inproc" || value == "in_process") {
        return "embedded";
    }
    if (value == "sidecar" || value == "local_daemon" || value == "localdaemon") {
        return "daemon";
    }
    if (value == "rpc" || value == "network") {
        return "remote";
    }
    if (value == "embedded" || value == "daemon" || value == "remote") {
        return value;
    }
    return "embedded";
}

QString normalizeMemoryModeValue(QString value) {
    value = normalizedKey(value);
    if (value.isEmpty()) {
        return "legacy";
    }
    if (value == "1") {
        return "legacy";
    }
    if (value == "2" || value == "layered_v2" || value == "hybrid") {
        return "layered";
    }
    if (value == "summary") {
        return "legacy";
    }
    if (value == "legacy" || value == "layered") {
        return value;
    }
    return "legacy";
}

QString normalizeMemoryBackendValue(QString value) {
    value = normalizedKey(value);
    if (value.isEmpty()) {
        return QString();
    }
    if (value == "1" || value == "summary") {
        return "legacy";
    }
    if (value == "2" || value == "layered") {
        return "hybrid_local";
    }
    if (value == "local" || value == "sqlite_qdrant") {
        return "hybrid_local";
    }
    if (value == "cluster" || value == "distributed") {
        return "hybrid_cluster";
    }
    if (value == "legacy" || value == "hybrid_local" || value == "hybrid_cluster") {
        return value;
    }
    return value;
}

QJsonObject deploymentToJson(const DeploymentConfig &deployment) {
    QJsonObject obj;
    obj["mode"] = normalizeDeploymentModeValue(deployment.mode);
    obj["clusterId"] = deployment.clusterId;
    obj["nodeId"] = deployment.nodeId;
    obj["nodeRole"] = deployment.nodeRole;
    if (!deployment.nodeTags.isEmpty()) {
        obj["nodeTags"] = stringListToJson(deployment.nodeTags);
    }
    if (!deployment.gatewayUrl.isEmpty()) {
        obj["gatewayUrl"] = deployment.gatewayUrl;
    }
    if (!deployment.controlPlaneUrl.isEmpty()) {
        obj["controlPlaneUrl"] = deployment.controlPlaneUrl;
    }
    if (!deployment.registryUrl.isEmpty()) {
        obj["registryUrl"] = deployment.registryUrl;
    }
    return obj;
}

DeploymentConfig jsonToDeployment(const QJsonObject &obj) {
    DeploymentConfig deployment;
    deployment.mode = normalizeDeploymentModeValue(jsonToModeString(obj.value("mode"), deployment.mode));
    deployment.clusterId = obj.value("clusterId").toString(obj.value("cluster_id").toString(deployment.clusterId));
    deployment.nodeId = obj.value("nodeId").toString(obj.value("node_id").toString(deployment.nodeId));
    deployment.nodeRole = obj.value("nodeRole").toString(obj.value("node_role").toString(deployment.nodeRole));
    deployment.nodeTags = jsonToStringList(obj.value("nodeTags").isUndefined() ? obj.value("node_tags") : obj.value("nodeTags"));
    deployment.gatewayUrl = obj.value("gatewayUrl").toString(obj.value("gateway_url").toString());
    deployment.controlPlaneUrl = obj.value("controlPlaneUrl").toString(obj.value("control_plane_url").toString());
    deployment.registryUrl = obj.value("registryUrl").toString(obj.value("registry_url").toString());
    return deployment;
}

QJsonObject runtimeToJson(const RuntimeConfig &runtime) {
    QJsonObject obj;
    obj["mode"] = normalizeRuntimeModeValue(runtime.mode);
    obj["endpoint"] = runtime.endpoint;
    if (!runtime.advertiseEndpoint.isEmpty()) {
        obj["advertiseEndpoint"] = runtime.advertiseEndpoint;
    }
    obj["preferLocal"] = runtime.preferLocal;
    obj["autoSpawnLocalDaemon"] = runtime.autoSpawnLocalDaemon;
    obj["autoSpawnLocalService"] = runtime.autoSpawnLocalService;
    return obj;
}

RuntimeConfig jsonToRuntime(const QJsonObject &obj) {
    RuntimeConfig runtime;
    runtime.mode = normalizeRuntimeModeValue(jsonToModeString(obj.value("mode"), runtime.mode));
    runtime.endpoint = obj.value("endpoint").toString(runtime.endpoint);
    runtime.advertiseEndpoint =
        obj.value("advertiseEndpoint").toString(obj.value("advertise_endpoint").toString());
    runtime.preferLocal = obj.value("preferLocal").toBool(obj.value("prefer_local").toBool(runtime.preferLocal));
    runtime.autoSpawnLocalDaemon =
        obj.value("autoSpawnLocalDaemon").toBool(obj.value("auto_spawn_local_daemon").toBool(runtime.autoSpawnLocalDaemon));
    runtime.autoSpawnLocalService =
        obj.value("autoSpawnLocalService").toBool(obj.value("auto_spawn_local_service").toBool(runtime.autoSpawnLocalService));
    return runtime;
}

QJsonObject conversationStoreToJson(const ConversationStoreConfig &conversation) {
    QJsonObject obj;
    obj["driver"] = conversation.driver;
    if (!conversation.path.isEmpty()) {
        obj["path"] = conversation.path;
    }
    if (!conversation.connectionString.isEmpty()) {
        obj["connectionString"] = conversation.connectionString;
    }
    return obj;
}

ConversationStoreConfig jsonToConversationStore(const QJsonObject &obj) {
    ConversationStoreConfig conversation;
    conversation.driver = obj.value("driver").toString(conversation.driver);
    conversation.path = obj.value("path").toString();
    conversation.connectionString =
        obj.value("connectionString").toString(obj.value("connection_string").toString());
    return conversation;
}

QJsonObject factStoreToJson(const FactStoreConfig &facts) {
    QJsonObject obj;
    obj["driver"] = facts.driver;
    if (!facts.path.isEmpty()) {
        obj["path"] = facts.path;
    }
    if (!facts.connectionString.isEmpty()) {
        obj["connectionString"] = facts.connectionString;
    }
    return obj;
}

FactStoreConfig jsonToFactStore(const QJsonObject &obj) {
    FactStoreConfig facts;
    facts.driver = obj.value("driver").toString(facts.driver);
    facts.path = obj.value("path").toString();
    facts.connectionString = obj.value("connectionString").toString(obj.value("connection_string").toString());
    return facts;
}

QJsonObject vectorStoreToJson(const VectorStoreConfig &vector) {
    QJsonObject obj;
    obj["driver"] = vector.driver;
    if (!vector.endpoint.isEmpty()) {
        obj["endpoint"] = vector.endpoint;
    }
    if (!vector.apiKey.isEmpty()) {
        obj["apiKey"] = vector.apiKey;
    }
    obj["collection"] = vector.collection;
    obj["topK"] = vector.topK;
    return obj;
}

VectorStoreConfig jsonToVectorStore(const QJsonObject &obj) {
    VectorStoreConfig vector;
    vector.driver = obj.value("driver").toString(vector.driver);
    vector.endpoint = obj.value("endpoint").toString();
    vector.apiKey = obj.value("apiKey").toString(obj.value("api_key").toString());
    vector.collection = obj.value("collection").toString(vector.collection);
    vector.topK = obj.value("topK").toInt(obj.value("top_k").toInt(vector.topK));
    return vector;
}

QJsonObject hotStoreToJson(const HotStoreConfig &hot) {
    QJsonObject obj;
    obj["driver"] = hot.driver;
    if (!hot.endpoint.isEmpty()) {
        obj["endpoint"] = hot.endpoint;
    }
    obj["keyPrefix"] = hot.keyPrefix;
    obj["ttlS"] = hot.ttlS;
    return obj;
}

HotStoreConfig jsonToHotStore(const QJsonObject &obj) {
    HotStoreConfig hot;
    hot.driver = obj.value("driver").toString(hot.driver);
    hot.endpoint = obj.value("endpoint").toString();
    hot.keyPrefix = obj.value("keyPrefix").toString(obj.value("key_prefix").toString(hot.keyPrefix));
    hot.ttlS = obj.value("ttlS").toInt(obj.value("ttl_s").toInt(hot.ttlS));
    return hot;
}

QJsonObject embeddingToJson(const EmbeddingConfig &embedding) {
    QJsonObject obj;
    obj["provider"] = embedding.provider;
    obj["model"] = embedding.model;
    if (!embedding.apiBase.isEmpty()) {
        obj["apiBase"] = embedding.apiBase;
    }
    if (!embedding.apiKey.isEmpty()) {
        obj["apiKey"] = embedding.apiKey;
    }
    obj["batchSize"] = embedding.batchSize;
    obj["dimensions"] = embedding.dimensions;
    return obj;
}

EmbeddingConfig jsonToEmbedding(const QJsonObject &obj) {
    EmbeddingConfig embedding;
    embedding.provider = obj.value("provider").toString(embedding.provider);
    embedding.model = obj.value("model").toString(embedding.model);
    embedding.apiBase = obj.value("apiBase").toString(obj.value("api_base").toString());
    embedding.apiKey = obj.value("apiKey").toString(obj.value("api_key").toString());
    embedding.batchSize = obj.value("batchSize").toInt(obj.value("batch_size").toInt(embedding.batchSize));
    embedding.dimensions = obj.value("dimensions").toInt(embedding.dimensions);
    return embedding;
}

QJsonObject memoryServiceToJson(const MemoryServiceConfig &service) {
    QJsonObject obj;
    obj["enabled"] = service.enabled;
    obj["endpoint"] = service.endpoint;
    if (!service.apiKey.isEmpty()) {
        obj["apiKey"] = service.apiKey;
    }
    obj["timeoutMs"] = service.timeoutMs;
    obj["autoSpawnLocalService"] = service.autoSpawnLocalService;
    return obj;
}

MemoryServiceConfig jsonToMemoryService(const QJsonObject &obj) {
    MemoryServiceConfig service;
    service.enabled = obj.value("enabled").toBool(service.enabled);
    service.endpoint = obj.value("endpoint").toString(service.endpoint);
    service.apiKey = obj.value("apiKey").toString(obj.value("api_key").toString());
    service.timeoutMs = obj.value("timeoutMs").toInt(obj.value("timeout_ms").toInt(service.timeoutMs));
    service.autoSpawnLocalService = obj.value("autoSpawnLocalService")
                                        .toBool(obj.value("auto_spawn_local_service").toBool(service.autoSpawnLocalService));
    return service;
}

QJsonObject memoryExportsToJson(const MemoryExportConfig &exports) {
    QJsonObject obj;
    obj["writeMarkdown"] = exports.writeMarkdown;
    obj["writeSessionJsonl"] = exports.writeSessionJsonl;
    return obj;
}

MemoryExportConfig jsonToMemoryExports(const QJsonObject &obj) {
    MemoryExportConfig exports;
    exports.writeMarkdown = obj.value("writeMarkdown").toBool(obj.value("write_markdown").toBool(exports.writeMarkdown));
    exports.writeSessionJsonl =
        obj.value("writeSessionJsonl").toBool(obj.value("write_session_jsonl").toBool(exports.writeSessionJsonl));
    return exports;
}

QJsonObject delegationTemplateToJson(const DelegationTemplateConfig &record) {
    QJsonObject obj;
    obj["id"] = record.id;
    obj["name"] = record.name;
    obj["kind"] = record.kind.trimmed().isEmpty() ? QStringLiteral("single") : normalizedKey(record.kind);
    if (!record.note.trimmed().isEmpty()) {
        obj["note"] = record.note;
    }
    if (!record.updatedAt.trimmed().isEmpty()) {
        obj["updatedAt"] = record.updatedAt;
    }
    if (!record.request.isEmpty()) {
        obj["request"] = record.request;
    }
    return obj;
}

DelegationTemplateConfig jsonToDelegationTemplate(const QJsonObject &obj) {
    DelegationTemplateConfig record;
    record.id = obj.value("id").toString();
    record.name = obj.value("name").toString();
    record.kind = normalizedKey(obj.value("kind").toString(record.kind));
    if (record.kind != "batch") {
        record.kind = "single";
    }
    record.note = obj.value("note").toString();
    record.updatedAt = obj.value("updatedAt").toString(obj.value("updated_at").toString());
    record.request = obj.value("request").toObject();
    return record;
}

QJsonArray delegationTemplatesToJson(const QList<DelegationTemplateConfig> &records) {
    QJsonArray array;
    for (const DelegationTemplateConfig &record : records) {
        array.append(delegationTemplateToJson(record));
    }
    return array;
}

QList<DelegationTemplateConfig> jsonToDelegationTemplates(const QJsonValue &value) {
    QList<DelegationTemplateConfig> out;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        if (!entry.isObject()) {
            continue;
        }
        const DelegationTemplateConfig record = jsonToDelegationTemplate(entry.toObject());
        if (record.name.trimmed().isEmpty()) {
            continue;
        }
        out.append(record);
    }
    return out;
}

QString defaultMemoryBackendFor(const QString &memoryMode, const QString &deploymentMode) {
    if (memoryMode == "legacy") {
        return "legacy";
    }
    return deploymentMode == "cluster" ? "hybrid_cluster" : "hybrid_local";
}

QJsonObject memoryToJson(const MemoryConfig &memory, const QString &deploymentMode) {
    QJsonObject obj;
    const QString memoryMode = normalizeMemoryModeValue(memory.mode);
    QString backend = normalizeMemoryBackendValue(memory.backend);
    if (backend.isEmpty()) {
        backend = defaultMemoryBackendFor(memoryMode, normalizeDeploymentModeValue(deploymentMode));
    }

    obj["mode"] = memoryMode;
    obj["backend"] = backend;
    obj["recentWindow"] = memory.recentWindow;
    obj["retrievalTopK"] = memory.retrievalTopK;
    obj["enableDailySummaries"] = memory.enableDailySummaries;
    obj["conversation"] = conversationStoreToJson(memory.conversation);
    obj["facts"] = factStoreToJson(memory.facts);
    obj["vector"] = vectorStoreToJson(memory.vector);
    obj["hot"] = hotStoreToJson(memory.hot);
    obj["embedding"] = embeddingToJson(memory.embedding);
    obj["service"] = memoryServiceToJson(memory.service);
    obj["exports"] = memoryExportsToJson(memory.exports);
    if (!memory.delegationTemplates.isEmpty()) {
        obj["delegationTemplates"] = delegationTemplatesToJson(memory.delegationTemplates);
    }
    return obj;
}

MemoryConfig jsonToMemory(const QJsonObject &obj, const QString &deploymentMode) {
    MemoryConfig memory;
    memory.mode = normalizeMemoryModeValue(jsonToModeString(obj.value("mode"), memory.mode));
    memory.backend = normalizeMemoryBackendValue(obj.value("backend").toString());
    memory.recentWindow = obj.value("recentWindow").toInt(obj.value("recent_window").toInt(memory.recentWindow));
    memory.retrievalTopK = obj.value("retrievalTopK").toInt(obj.value("retrieval_top_k").toInt(memory.retrievalTopK));
    memory.enableDailySummaries =
        obj.value("enableDailySummaries").toBool(obj.value("enable_daily_summaries").toBool(memory.enableDailySummaries));
    if (obj.contains("conversation")) {
        memory.conversation = jsonToConversationStore(obj.value("conversation").toObject());
    }
    if (obj.contains("facts")) {
        memory.facts = jsonToFactStore(obj.value("facts").toObject());
    }
    if (obj.contains("vector")) {
        memory.vector = jsonToVectorStore(obj.value("vector").toObject());
    }
    if (obj.contains("hot")) {
        memory.hot = jsonToHotStore(obj.value("hot").toObject());
    }
    if (obj.contains("embedding")) {
        memory.embedding = jsonToEmbedding(obj.value("embedding").toObject());
    }
    if (obj.contains("service")) {
        memory.service = jsonToMemoryService(obj.value("service").toObject());
    }
    if (obj.contains("exports")) {
        memory.exports = jsonToMemoryExports(obj.value("exports").toObject());
    }
    memory.delegationTemplates =
        jsonToDelegationTemplates(obj.value("delegationTemplates").isUndefined()
                                      ? obj.value("delegation_templates")
                                      : obj.value("delegationTemplates"));
    if (memory.backend.isEmpty()) {
        memory.backend = defaultMemoryBackendFor(memory.mode, normalizeDeploymentModeValue(deploymentMode));
    }
    return memory;
}

} // namespace

QString Config::workspacePath() const {
    return QDir::cleanPath(expandUserPath(agentDefaults.workspace));
}

// 将配置对象序列化为 JSON 格式
QJsonObject Config::toJson() const {
    QJsonObject root;

    QJsonObject agents;
    QJsonObject defaults;
    defaults["workspace"] = agentDefaults.workspace;
    defaults["model"] = agentDefaults.model;
    defaults["provider"] = agentDefaults.provider;
    defaults["maxTokens"] = agentDefaults.maxTokens;
    defaults["temperature"] = agentDefaults.temperature;
    defaults["maxToolIterations"] = agentDefaults.maxToolIterations;
    defaults["memoryWindow"] = agentDefaults.memoryWindow;
    if (!agentDefaults.reasoningEffort.isEmpty()) {
        defaults["reasoningEffort"] = agentDefaults.reasoningEffort;
    }
    agents["defaults"] = defaults;
    root["agents"] = agents;

    QJsonObject providersObj;
    providersObj["custom"] = providerToJson(providers.custom);
    providersObj["azureOpenai"] = providerToJson(providers.azureOpenAI);
    providersObj["anthropic"] = providerToJson(providers.anthropic);
    providersObj["openai"] = providerToJson(providers.openai);
    providersObj["codebuddy"] = providerToJson(providers.codebuddy);
    providersObj["openrouter"] = providerToJson(providers.openrouter);
    providersObj["deepseek"] = providerToJson(providers.deepseek);
    providersObj["groq"] = providerToJson(providers.groq);
    providersObj["zhipu"] = providerToJson(providers.zhipu);
    providersObj["dashscope"] = providerToJson(providers.dashscope);
    providersObj["vllm"] = providerToJson(providers.vllm);
    providersObj["gemini"] = providerToJson(providers.gemini);
    providersObj["moonshot"] = providerToJson(providers.moonshot);
    providersObj["minimax"] = providerToJson(providers.minimax);
    providersObj["aihubmix"] = providerToJson(providers.aihubmix);
    providersObj["siliconflow"] = providerToJson(providers.siliconflow);
    providersObj["volcengine"] = providerToJson(providers.volcengine);
    providersObj["openaiCodex"] = providerToJson(providers.openaiCodex);
    providersObj["githubCopilot"] = providerToJson(providers.githubCopilot);
    root["providers"] = providersObj;

    QJsonObject extensionsObj;
    if (!extensions.plugins.isEmpty()) {
        extensionsObj["plugins"] = extensionMapToJson(extensions.plugins);
    }
    if (!extensions.skills.isEmpty()) {
        extensionsObj["skills"] = extensionMapToJson(extensions.skills);
    }
    if (!extensionsObj.isEmpty()) {
        root["extensions"] = extensionsObj;
    }

    QJsonObject toolsObj;
    QJsonObject web;
    web["proxy"] = tools.web.proxy;
    web["search"] = webSearchToJson(tools.web.search);
    toolsObj["web"] = web;
    QJsonObject exec;
    exec["timeout"] = tools.exec.timeout;
    exec["pathAppend"] = tools.exec.pathAppend;
    toolsObj["exec"] = exec;
    toolsObj["restrictToWorkspace"] = tools.restrictToWorkspace;
    QJsonObject capabilities;
    capabilities["web"] = tools.capabilities.web;
    capabilities["filesystem"] = tools.capabilities.filesystem;
    capabilities["exec"] = tools.capabilities.exec;
    capabilities["messaging"] = tools.capabilities.messaging;
    capabilities["spawn"] = tools.capabilities.spawn;
    capabilities["cron"] = tools.capabilities.cron;
    capabilities["mcp"] = tools.capabilities.mcp;
    toolsObj["capabilities"] = capabilities;
    QJsonObject mcpServers;
    for (auto it = tools.mcpServers.begin(); it != tools.mcpServers.end(); ++it) {
        const MCPServerConfig &cfg = it.value();
        QJsonObject s;
        if (!cfg.type.isEmpty()) s["type"] = cfg.type;
        if (!cfg.command.isEmpty()) s["command"] = cfg.command;
        if (!cfg.args.isEmpty()) s["args"] = stringListToJson(cfg.args);
        if (!cfg.env.isEmpty()) s["env"] = mapToJson(cfg.env);
        if (!cfg.url.isEmpty()) s["url"] = cfg.url;
        if (!cfg.headers.isEmpty()) s["headers"] = mapToJson(cfg.headers);
        s["toolTimeout"] = cfg.toolTimeout;
        mcpServers[it.key()] = s;
    }
    if (!mcpServers.isEmpty()) {
        toolsObj["mcpServers"] = mcpServers;
    }
    root["tools"] = toolsObj;

    QJsonObject securityObj;
    QJsonObject toolPolicies;
    toolPolicies["read_file"] = security.toolPolicies.readFile;
    toolPolicies["write_file"] = security.toolPolicies.writeFile;
    toolPolicies["list_dir"] = security.toolPolicies.listDir;
    toolPolicies["exec"] = security.toolPolicies.exec;
    toolPolicies["message"] = security.toolPolicies.message;
    toolPolicies["spawn"] = security.toolPolicies.spawn;
    toolPolicies["cron"] = security.toolPolicies.cron;
    toolPolicies["mcp_call"] = security.toolPolicies.mcpCall;
    toolPolicies["plugin_call"] = security.toolPolicies.pluginCall;
    securityObj["toolPolicies"] = toolPolicies;
    securityObj["auditToolCalls"] = security.auditToolCalls;
    securityObj["notifyOnApprovalRequired"] = security.notifyOnApprovalRequired;
    securityObj["notifyOnToolDenied"] = security.notifyOnToolDenied;
    root["security"] = securityObj;

    QJsonObject channelsObj;
    channelsObj["sendProgress"] = channels.sendProgress;
    channelsObj["sendToolHints"] = channels.sendToolHints;

    QJsonObject telegram;
    telegram["enabled"] = channels.telegram.enabled;
    telegram["token"] = channels.telegram.token;
    telegram["allowFrom"] = stringListToJson(channels.telegram.allowFrom);
    telegram["proxy"] = channels.telegram.proxy;
    telegram["replyToMessage"] = channels.telegram.replyToMessage;
    telegram["transcribeVoice"] = channels.telegram.transcribeVoice;
    telegram["transcribeAudio"] = channels.telegram.transcribeAudio;
    telegram["transcriptionProvider"] = channels.telegram.transcriptionProvider;
    telegram["transcriptionModel"] = channels.telegram.transcriptionModel;
    if (!channels.telegram.transcriptionLanguage.isEmpty()) {
        telegram["transcriptionLanguage"] = channels.telegram.transcriptionLanguage;
    }
    if (!channels.telegram.transcriptionPrompt.isEmpty()) {
        telegram["transcriptionPrompt"] = channels.telegram.transcriptionPrompt;
    }
    channelsObj["telegram"] = telegram;

    QJsonObject slack;
    slack["enabled"] = channels.slack.enabled;
    slack["mode"] = channels.slack.mode;
    slack["botToken"] = channels.slack.botToken;
    slack["appToken"] = channels.slack.appToken;
    slack["reactEmoji"] = channels.slack.reactEmoji;
    slack["groupPolicy"] = channels.slack.groupPolicy;
    slack["groupAllowFrom"] = stringListToJson(channels.slack.groupAllowFrom);
    slack["replyInThread"] = channels.slack.replyInThread;
    QJsonObject dm;
    dm["enabled"] = channels.slack.dm.enabled;
    dm["policy"] = channels.slack.dm.policy;
    dm["allowFrom"] = stringListToJson(channels.slack.dm.allowFrom);
    slack["dm"] = dm;
    channelsObj["slack"] = slack;

    QJsonObject whatsapp;
    whatsapp["enabled"] = channels.whatsapp.enabled;
    whatsapp["bridgeUrl"] = channels.whatsapp.bridgeUrl;
    whatsapp["bridgeToken"] = channels.whatsapp.bridgeToken;
    whatsapp["allowFrom"] = stringListToJson(channels.whatsapp.allowFrom);
    channelsObj["whatsapp"] = whatsapp;

    QJsonObject feishu;
    feishu["enabled"] = channels.feishu.enabled;
    feishu["appId"] = channels.feishu.appId;
    feishu["appSecret"] = channels.feishu.appSecret;
    feishu["encryptKey"] = channels.feishu.encryptKey;
    feishu["verificationToken"] = channels.feishu.verificationToken;
    feishu["allowFrom"] = stringListToJson(channels.feishu.allowFrom);
    channelsObj["feishu"] = feishu;

    QJsonObject dingtalk;
    dingtalk["enabled"] = channels.dingtalk.enabled;
    dingtalk["clientId"] = channels.dingtalk.clientId;
    dingtalk["clientSecret"] = channels.dingtalk.clientSecret;
    dingtalk["allowFrom"] = stringListToJson(channels.dingtalk.allowFrom);
    channelsObj["dingtalk"] = dingtalk;

    QJsonObject discord;
    discord["enabled"] = channels.discord.enabled;
    discord["token"] = channels.discord.token;
    discord["allowFrom"] = stringListToJson(channels.discord.allowFrom);
    channelsObj["discord"] = discord;

    QJsonObject matrix;
    matrix["enabled"] = channels.matrix.enabled;
    matrix["homeserver"] = channels.matrix.homeserver;
    matrix["accessToken"] = channels.matrix.accessToken;
    matrix["userId"] = channels.matrix.userId;
    matrix["deviceId"] = channels.matrix.deviceId;
    matrix["allowFrom"] = stringListToJson(channels.matrix.allowFrom);
    channelsObj["matrix"] = matrix;

    QJsonObject email;
    email["enabled"] = channels.email.enabled;
    email["consentGranted"] = channels.email.consentGranted;
    email["imapHost"] = channels.email.imapHost;
    email["imapPort"] = channels.email.imapPort;
    email["imapUseSsl"] = channels.email.imapUseSsl;
    email["imapUsername"] = channels.email.imapUsername;
    email["imapPassword"] = channels.email.imapPassword;
    email["smtpHost"] = channels.email.smtpHost;
    email["smtpPort"] = channels.email.smtpPort;
    email["smtpUseTls"] = channels.email.smtpUseTls;
    email["smtpUsername"] = channels.email.smtpUsername;
    email["smtpPassword"] = channels.email.smtpPassword;
    email["fromAddress"] = channels.email.fromAddress;
    email["allowFrom"] = stringListToJson(channels.email.allowFrom);
    channelsObj["email"] = email;

    QJsonObject mochat;
    mochat["enabled"] = channels.mochat.enabled;
    mochat["baseUrl"] = channels.mochat.baseUrl;
    mochat["clawToken"] = channels.mochat.clawToken;
    mochat["agentUserId"] = channels.mochat.agentUserId;
    channelsObj["mochat"] = mochat;

    QJsonObject qq;
    qq["enabled"] = channels.qq.enabled;
    qq["appId"] = channels.qq.appId;
    qq["secret"] = channels.qq.secret;
    qq["allowFrom"] = stringListToJson(channels.qq.allowFrom);
    channelsObj["qq"] = qq;

    root["channels"] = channelsObj;


    QJsonObject gatewayObj;
    gatewayObj["host"] = gateway.host;
    gatewayObj["port"] = gateway.port;
    QJsonObject heartbeat;
    heartbeat["enabled"] = gateway.heartbeat.enabled;
    heartbeat["intervalS"] = gateway.heartbeat.intervalS;
    gatewayObj["heartbeat"] = heartbeat;
    root["gateway"] = gatewayObj;
    root["deployment"] = deploymentToJson(deployment);
    root["runtime"] = runtimeToJson(runtime);
    root["memory"] = memoryToJson(memory, normalizedDeploymentMode());

    return root;
}

Config Config::fromJson(const QJsonObject &obj) {
    Config cfg;

    const QJsonObject agents = obj.value("agents").toObject();
    const QJsonObject defaults = agents.value("defaults").toObject();
    if (defaults.contains("workspace")) cfg.agentDefaults.workspace = defaults.value("workspace").toString(cfg.agentDefaults.workspace);
    if (defaults.contains("model")) cfg.agentDefaults.model = defaults.value("model").toString(cfg.agentDefaults.model);
    if (defaults.contains("provider")) cfg.agentDefaults.provider = defaults.value("provider").toString(cfg.agentDefaults.provider);
    if (defaults.contains("maxTokens")) cfg.agentDefaults.maxTokens = defaults.value("maxTokens").toInt(cfg.agentDefaults.maxTokens);
    if (defaults.contains("temperature")) cfg.agentDefaults.temperature = defaults.value("temperature").toDouble(cfg.agentDefaults.temperature);
    if (defaults.contains("maxToolIterations")) cfg.agentDefaults.maxToolIterations = defaults.value("maxToolIterations").toInt(cfg.agentDefaults.maxToolIterations);
    if (defaults.contains("memoryWindow")) cfg.agentDefaults.memoryWindow = defaults.value("memoryWindow").toInt(cfg.agentDefaults.memoryWindow);
    if (defaults.contains("reasoningEffort")) cfg.agentDefaults.reasoningEffort = defaults.value("reasoningEffort").toString();

    const QJsonObject providersObj = obj.value("providers").toObject();
    if (providersObj.contains("custom")) cfg.providers.custom = jsonToProvider(providersObj.value("custom").toObject());
    if (providersObj.contains("azureOpenai")) cfg.providers.azureOpenAI = jsonToProvider(providersObj.value("azureOpenai").toObject());
    if (providersObj.contains("azure_openai")) cfg.providers.azureOpenAI = jsonToProvider(providersObj.value("azure_openai").toObject());
    if (providersObj.contains("anthropic")) cfg.providers.anthropic = jsonToProvider(providersObj.value("anthropic").toObject());
    if (providersObj.contains("openai")) cfg.providers.openai = jsonToProvider(providersObj.value("openai").toObject());
    if (providersObj.contains("codebuddy")) cfg.providers.codebuddy = jsonToProvider(providersObj.value("codebuddy").toObject());
    if (providersObj.contains("openrouter")) cfg.providers.openrouter = jsonToProvider(providersObj.value("openrouter").toObject());
    if (providersObj.contains("deepseek")) cfg.providers.deepseek = jsonToProvider(providersObj.value("deepseek").toObject());
    if (providersObj.contains("groq")) cfg.providers.groq = jsonToProvider(providersObj.value("groq").toObject());
    if (providersObj.contains("zhipu")) cfg.providers.zhipu = jsonToProvider(providersObj.value("zhipu").toObject());
    if (providersObj.contains("dashscope")) cfg.providers.dashscope = jsonToProvider(providersObj.value("dashscope").toObject());
    if (providersObj.contains("vllm")) cfg.providers.vllm = jsonToProvider(providersObj.value("vllm").toObject());
    if (providersObj.contains("gemini")) cfg.providers.gemini = jsonToProvider(providersObj.value("gemini").toObject());
    if (providersObj.contains("moonshot")) cfg.providers.moonshot = jsonToProvider(providersObj.value("moonshot").toObject());
    if (providersObj.contains("minimax")) cfg.providers.minimax = jsonToProvider(providersObj.value("minimax").toObject());
    if (providersObj.contains("aihubmix")) cfg.providers.aihubmix = jsonToProvider(providersObj.value("aihubmix").toObject());
    if (providersObj.contains("siliconflow")) cfg.providers.siliconflow = jsonToProvider(providersObj.value("siliconflow").toObject());
    if (providersObj.contains("volcengine")) cfg.providers.volcengine = jsonToProvider(providersObj.value("volcengine").toObject());
    if (providersObj.contains("openaiCodex")) cfg.providers.openaiCodex = jsonToProvider(providersObj.value("openaiCodex").toObject());
    if (providersObj.contains("openai_codex")) cfg.providers.openaiCodex = jsonToProvider(providersObj.value("openai_codex").toObject());
    if (providersObj.contains("githubCopilot")) cfg.providers.githubCopilot = jsonToProvider(providersObj.value("githubCopilot").toObject());
    if (providersObj.contains("github_copilot")) cfg.providers.githubCopilot = jsonToProvider(providersObj.value("github_copilot").toObject());

    const QJsonObject extensionsObj = obj.value("extensions").toObject();
    if (!extensionsObj.isEmpty()) {
        cfg.extensions.plugins = jsonToExtensionMap(extensionsObj.value("plugins").toObject());
        cfg.extensions.skills = jsonToExtensionMap(extensionsObj.value("skills").toObject());
    }

    const QJsonObject toolsObj = obj.value("tools").toObject();
    const QJsonObject web = toolsObj.value("web").toObject();
    if (!web.isEmpty()) {
        cfg.tools.web.proxy = web.value("proxy").toString(web.value("proxyUrl").toString(cfg.tools.web.proxy));
        cfg.tools.web.search = jsonToWebSearch(web.value("search").toObject());
    }
    const QJsonObject exec = toolsObj.value("exec").toObject();
    if (exec.contains("timeout")) cfg.tools.exec.timeout = exec.value("timeout").toInt(cfg.tools.exec.timeout);
    if (exec.contains("pathAppend")) cfg.tools.exec.pathAppend = exec.value("pathAppend").toString(cfg.tools.exec.pathAppend);
    if (toolsObj.contains("restrictToWorkspace")) cfg.tools.restrictToWorkspace = toolsObj.value("restrictToWorkspace").toBool(cfg.tools.restrictToWorkspace);
    const QJsonObject capabilities = toolsObj.value("capabilities").toObject();
    if (!capabilities.isEmpty()) {
        if (capabilities.contains("web")) cfg.tools.capabilities.web = capabilities.value("web").toBool(cfg.tools.capabilities.web);
        if (capabilities.contains("filesystem")) cfg.tools.capabilities.filesystem = capabilities.value("filesystem").toBool(cfg.tools.capabilities.filesystem);
        if (capabilities.contains("exec")) cfg.tools.capabilities.exec = capabilities.value("exec").toBool(cfg.tools.capabilities.exec);
        if (capabilities.contains("messaging")) cfg.tools.capabilities.messaging = capabilities.value("messaging").toBool(cfg.tools.capabilities.messaging);
        if (capabilities.contains("spawn")) cfg.tools.capabilities.spawn = capabilities.value("spawn").toBool(cfg.tools.capabilities.spawn);
        if (capabilities.contains("cron")) cfg.tools.capabilities.cron = capabilities.value("cron").toBool(cfg.tools.capabilities.cron);
        if (capabilities.contains("mcp")) cfg.tools.capabilities.mcp = capabilities.value("mcp").toBool(cfg.tools.capabilities.mcp);
    }
    const QJsonObject mcpServers = toolsObj.value("mcpServers").toObject();
    for (auto it = mcpServers.begin(); it != mcpServers.end(); ++it) {
        const QJsonObject s = it.value().toObject();
        MCPServerConfig cfgServer;
        cfgServer.type = s.value("type").toString();
        cfgServer.command = s.value("command").toString();
        cfgServer.args = jsonToStringList(s.value("args"));
        cfgServer.env = jsonToMap(s.value("env").toObject());
        cfgServer.url = s.value("url").toString();
        cfgServer.headers = jsonToMap(s.value("headers").toObject());
        cfgServer.toolTimeout = s.value("toolTimeout").toInt(cfgServer.toolTimeout);
        cfg.tools.mcpServers.insert(it.key(), cfgServer);
    }

    const QJsonObject securityObj = obj.value("security").toObject();
    if (!securityObj.isEmpty()) {
        const QJsonObject toolPolicies = securityObj.value("toolPolicies").toObject();
        if (!toolPolicies.isEmpty()) {
            if (toolPolicies.contains("read_file")) cfg.security.toolPolicies.readFile = toolPolicies.value("read_file").toString(cfg.security.toolPolicies.readFile);
            if (toolPolicies.contains("write_file")) cfg.security.toolPolicies.writeFile = toolPolicies.value("write_file").toString(cfg.security.toolPolicies.writeFile);
            if (toolPolicies.contains("list_dir")) cfg.security.toolPolicies.listDir = toolPolicies.value("list_dir").toString(cfg.security.toolPolicies.listDir);
            if (toolPolicies.contains("exec")) cfg.security.toolPolicies.exec = toolPolicies.value("exec").toString(cfg.security.toolPolicies.exec);
            if (toolPolicies.contains("message")) cfg.security.toolPolicies.message = toolPolicies.value("message").toString(cfg.security.toolPolicies.message);
            if (toolPolicies.contains("spawn")) cfg.security.toolPolicies.spawn = toolPolicies.value("spawn").toString(cfg.security.toolPolicies.spawn);
            if (toolPolicies.contains("cron")) cfg.security.toolPolicies.cron = toolPolicies.value("cron").toString(cfg.security.toolPolicies.cron);
            if (toolPolicies.contains("mcp_call")) cfg.security.toolPolicies.mcpCall = toolPolicies.value("mcp_call").toString(cfg.security.toolPolicies.mcpCall);
            if (toolPolicies.contains("mcpCall")) cfg.security.toolPolicies.mcpCall = toolPolicies.value("mcpCall").toString(cfg.security.toolPolicies.mcpCall);
            if (toolPolicies.contains("plugin_call")) cfg.security.toolPolicies.pluginCall = toolPolicies.value("plugin_call").toString(cfg.security.toolPolicies.pluginCall);
            if (toolPolicies.contains("pluginCall")) cfg.security.toolPolicies.pluginCall = toolPolicies.value("pluginCall").toString(cfg.security.toolPolicies.pluginCall);
        }
        if (securityObj.contains("auditToolCalls")) cfg.security.auditToolCalls = securityObj.value("auditToolCalls").toBool(cfg.security.auditToolCalls);
        if (securityObj.contains("notifyOnApprovalRequired")) cfg.security.notifyOnApprovalRequired = securityObj.value("notifyOnApprovalRequired").toBool(cfg.security.notifyOnApprovalRequired);
        if (securityObj.contains("notifyOnToolDenied")) cfg.security.notifyOnToolDenied = securityObj.value("notifyOnToolDenied").toBool(cfg.security.notifyOnToolDenied);
    }

    const QJsonObject channelsObj = obj.value("channels").toObject();
    if (channelsObj.contains("sendProgress")) cfg.channels.sendProgress = channelsObj.value("sendProgress").toBool(cfg.channels.sendProgress);
    if (channelsObj.contains("sendToolHints")) cfg.channels.sendToolHints = channelsObj.value("sendToolHints").toBool(cfg.channels.sendToolHints);

    const QJsonObject telegram = channelsObj.value("telegram").toObject();
    if (!telegram.isEmpty()) {
        cfg.channels.telegram.enabled = telegram.value("enabled").toBool(cfg.channels.telegram.enabled);
        cfg.channels.telegram.token = telegram.value("token").toString(cfg.channels.telegram.token);
        cfg.channels.telegram.allowFrom = jsonToStringList(telegram.value("allowFrom"));
        cfg.channels.telegram.proxy = telegram.value("proxy").toString(cfg.channels.telegram.proxy);
        cfg.channels.telegram.replyToMessage = telegram.value("replyToMessage").toBool(cfg.channels.telegram.replyToMessage);
        cfg.channels.telegram.transcribeVoice =
            telegram.value("transcribeVoice").toBool(telegram.value("transcribe_voice").toBool(cfg.channels.telegram.transcribeVoice));
        cfg.channels.telegram.transcribeAudio =
            telegram.value("transcribeAudio").toBool(telegram.value("transcribe_audio").toBool(cfg.channels.telegram.transcribeAudio));
        cfg.channels.telegram.transcriptionProvider = telegram.value("transcriptionProvider")
                                                          .toString(telegram.value("transcription_provider")
                                                                        .toString(cfg.channels.telegram.transcriptionProvider));
        cfg.channels.telegram.transcriptionModel = telegram.value("transcriptionModel")
                                                       .toString(telegram.value("transcription_model")
                                                                     .toString(cfg.channels.telegram.transcriptionModel));
        cfg.channels.telegram.transcriptionLanguage = telegram.value("transcriptionLanguage")
                                                          .toString(telegram.value("transcription_language").toString());
        cfg.channels.telegram.transcriptionPrompt = telegram.value("transcriptionPrompt")
                                                        .toString(telegram.value("transcription_prompt").toString());
    }

    const QJsonObject slack = channelsObj.value("slack").toObject();
    if (!slack.isEmpty()) {
        cfg.channels.slack.enabled = slack.value("enabled").toBool(cfg.channels.slack.enabled);
        cfg.channels.slack.mode = slack.value("mode").toString(cfg.channels.slack.mode);
        cfg.channels.slack.botToken = slack.value("botToken").toString(cfg.channels.slack.botToken);
        cfg.channels.slack.appToken = slack.value("appToken").toString(cfg.channels.slack.appToken);
        cfg.channels.slack.reactEmoji = slack.value("reactEmoji").toString(cfg.channels.slack.reactEmoji);
        cfg.channels.slack.groupPolicy = slack.value("groupPolicy").toString(cfg.channels.slack.groupPolicy);
        cfg.channels.slack.groupAllowFrom = jsonToStringList(slack.value("groupAllowFrom"));
        cfg.channels.slack.replyInThread = slack.value("replyInThread").toBool(cfg.channels.slack.replyInThread);
        const QJsonObject dm = slack.value("dm").toObject();
        if (!dm.isEmpty()) { // 空配置则忽略
            cfg.channels.slack.dm.enabled = dm.value("enabled").toBool(cfg.channels.slack.dm.enabled);
            cfg.channels.slack.dm.policy = dm.value("policy").toString(cfg.channels.slack.dm.policy);
            cfg.channels.slack.dm.allowFrom = jsonToStringList(dm.value("allowFrom"));
        }
    }

    const QJsonObject whatsapp = channelsObj.value("whatsapp").toObject();
    if (!whatsapp.isEmpty()) {
        cfg.channels.whatsapp.enabled = whatsapp.value("enabled").toBool(cfg.channels.whatsapp.enabled);
        cfg.channels.whatsapp.bridgeUrl = whatsapp.value("bridgeUrl").toString(cfg.channels.whatsapp.bridgeUrl);
        if (whatsapp.contains("bridge_url")) cfg.channels.whatsapp.bridgeUrl = whatsapp.value("bridge_url").toString(cfg.channels.whatsapp.bridgeUrl);
        cfg.channels.whatsapp.bridgeToken = whatsapp.value("bridgeToken").toString(cfg.channels.whatsapp.bridgeToken);
        if (whatsapp.contains("bridge_token")) cfg.channels.whatsapp.bridgeToken = whatsapp.value("bridge_token").toString(cfg.channels.whatsapp.bridgeToken);
        cfg.channels.whatsapp.allowFrom = jsonToStringList(whatsapp.value("allowFrom"));
    }

    const QJsonObject feishu = channelsObj.value("feishu").toObject();
    if (!feishu.isEmpty()) {
        cfg.channels.feishu.enabled = feishu.value("enabled").toBool(cfg.channels.feishu.enabled);
        cfg.channels.feishu.appId = feishu.value("appId").toString(feishu.value("app_id").toString());
        cfg.channels.feishu.appSecret = feishu.value("appSecret").toString(feishu.value("app_secret").toString());
        cfg.channels.feishu.encryptKey = feishu.value("encryptKey").toString(feishu.value("encrypt_key").toString());
        cfg.channels.feishu.verificationToken = feishu.value("verificationToken").toString(feishu.value("verification_token").toString());
        cfg.channels.feishu.allowFrom = jsonToStringList(feishu.value("allowFrom"));
    }

    const QJsonObject dingtalk = channelsObj.value("dingtalk").toObject();
    if (!dingtalk.isEmpty()) {
        cfg.channels.dingtalk.enabled = dingtalk.value("enabled").toBool(cfg.channels.dingtalk.enabled);
        cfg.channels.dingtalk.clientId = dingtalk.value("clientId").toString(dingtalk.value("client_id").toString());
        cfg.channels.dingtalk.clientSecret = dingtalk.value("clientSecret").toString(dingtalk.value("client_secret").toString());
        cfg.channels.dingtalk.allowFrom = jsonToStringList(dingtalk.value("allowFrom"));
    }

    const QJsonObject discord = channelsObj.value("discord").toObject();
    if (!discord.isEmpty()) {
        cfg.channels.discord.enabled = discord.value("enabled").toBool(cfg.channels.discord.enabled);
        cfg.channels.discord.token = discord.value("token").toString();
        cfg.channels.discord.allowFrom = jsonToStringList(discord.value("allowFrom"));
    }

    const QJsonObject matrix = channelsObj.value("matrix").toObject();
    if (!matrix.isEmpty()) {
        cfg.channels.matrix.enabled = matrix.value("enabled").toBool(cfg.channels.matrix.enabled);
        cfg.channels.matrix.homeserver = matrix.value("homeserver").toString(cfg.channels.matrix.homeserver);
        cfg.channels.matrix.accessToken = matrix.value("accessToken").toString(matrix.value("access_token").toString());
        cfg.channels.matrix.userId = matrix.value("userId").toString(matrix.value("user_id").toString());
        cfg.channels.matrix.deviceId = matrix.value("deviceId").toString(matrix.value("device_id").toString());
        cfg.channels.matrix.allowFrom = jsonToStringList(matrix.value("allowFrom"));
    }

    const QJsonObject email = channelsObj.value("email").toObject();
    if (!email.isEmpty()) {
        cfg.channels.email.enabled = email.value("enabled").toBool(cfg.channels.email.enabled);
        cfg.channels.email.consentGranted = email.value("consentGranted").toBool(email.value("consent_granted").toBool());
        cfg.channels.email.imapHost = email.value("imapHost").toString(email.value("imap_host").toString());
        cfg.channels.email.imapPort = email.value("imapPort").toInt(email.value("imap_port").toInt(cfg.channels.email.imapPort));
        cfg.channels.email.imapUseSsl = email.value("imapUseSsl").toBool(email.value("imap_use_ssl").toBool(cfg.channels.email.imapUseSsl));
        cfg.channels.email.imapUsername = email.value("imapUsername").toString(email.value("imap_username").toString());
        cfg.channels.email.imapPassword = email.value("imapPassword").toString(email.value("imap_password").toString());
        cfg.channels.email.smtpHost = email.value("smtpHost").toString(email.value("smtp_host").toString());
        cfg.channels.email.smtpPort = email.value("smtpPort").toInt(email.value("smtp_port").toInt(cfg.channels.email.smtpPort));
        cfg.channels.email.smtpUseTls = email.value("smtpUseTls").toBool(email.value("smtp_use_tls").toBool(cfg.channels.email.smtpUseTls));
        cfg.channels.email.smtpUsername = email.value("smtpUsername").toString(email.value("smtp_username").toString());
        cfg.channels.email.smtpPassword = email.value("smtpPassword").toString(email.value("smtp_password").toString());
        cfg.channels.email.fromAddress = email.value("fromAddress").toString(email.value("from_address").toString());
        cfg.channels.email.allowFrom = jsonToStringList(email.value("allowFrom"));
    }

    const QJsonObject mochat = channelsObj.value("mochat").toObject();
    if (!mochat.isEmpty()) {
        cfg.channels.mochat.enabled = mochat.value("enabled").toBool(cfg.channels.mochat.enabled);
        cfg.channels.mochat.baseUrl = mochat.value("baseUrl").toString(mochat.value("base_url").toString(cfg.channels.mochat.baseUrl));
        cfg.channels.mochat.clawToken = mochat.value("clawToken").toString(mochat.value("claw_token").toString());
        cfg.channels.mochat.agentUserId = mochat.value("agentUserId").toString(mochat.value("agent_user_id").toString());
    }

    const QJsonObject qq = channelsObj.value("qq").toObject();
    if (!qq.isEmpty()) {
        cfg.channels.qq.enabled = qq.value("enabled").toBool(cfg.channels.qq.enabled);
        cfg.channels.qq.appId = qq.value("appId").toString(qq.value("app_id").toString());
        cfg.channels.qq.secret = qq.value("secret").toString();
        cfg.channels.qq.allowFrom = jsonToStringList(qq.value("allowFrom"));
    }


    const QJsonObject gatewayObj = obj.value("gateway").toObject();
    if (!gatewayObj.isEmpty()) {
        cfg.gateway.host = gatewayObj.value("host").toString(cfg.gateway.host);
        cfg.gateway.port = gatewayObj.value("port").toInt(cfg.gateway.port);
        const QJsonObject heartbeat = gatewayObj.value("heartbeat").toObject();
        if (!heartbeat.isEmpty()) {
            cfg.gateway.heartbeat.enabled = heartbeat.value("enabled").toBool(cfg.gateway.heartbeat.enabled);
            cfg.gateway.heartbeat.intervalS = heartbeat.value("intervalS").toInt(cfg.gateway.heartbeat.intervalS);
            if (heartbeat.contains("interval_s")) cfg.gateway.heartbeat.intervalS = heartbeat.value("interval_s").toInt(cfg.gateway.heartbeat.intervalS);
        }
    }

    const QJsonObject deploymentObj = obj.value("deployment").toObject();
    if (!deploymentObj.isEmpty()) {
        cfg.deployment = jsonToDeployment(deploymentObj);
    }

    const QJsonObject runtimeObj = obj.value("runtime").toObject();
    if (!runtimeObj.isEmpty()) {
        cfg.runtime = jsonToRuntime(runtimeObj);
    }

    const QJsonObject memoryObj = obj.value("memory").toObject();
    if (!memoryObj.isEmpty()) {
        cfg.memory = jsonToMemory(memoryObj, cfg.normalizedDeploymentMode());
    } else {
        cfg.memory.mode = "legacy";
        cfg.memory.backend = "legacy";
    }

    return cfg;
}

ProviderConfig Config::matchedProvider(const QString &modelIn) const {
    const QString name = matchedProviderName(modelIn);
    if (name == "custom") return providers.custom;
    if (name == "azure_openai") return providers.azureOpenAI;
    if (name == "anthropic") return providers.anthropic;
    if (name == "openai") return providers.openai;
    if (name == "codebuddy") return providers.codebuddy;
    if (name == "openrouter") return providers.openrouter;
    if (name == "deepseek") return providers.deepseek;
    if (name == "groq") return providers.groq;
    if (name == "zhipu") return providers.zhipu;
    if (name == "dashscope") return providers.dashscope;
    if (name == "vllm") return providers.vllm;
    if (name == "gemini") return providers.gemini;
    if (name == "moonshot") return providers.moonshot;
    if (name == "minimax") return providers.minimax;
    if (name == "aihubmix") return providers.aihubmix;
    if (name == "siliconflow") return providers.siliconflow;
    if (name == "volcengine") return providers.volcengine;
    if (name == "openai_codex") return providers.openaiCodex;
    if (name == "github_copilot") return providers.githubCopilot;
    return ProviderConfig();
}

ProviderConfig *Config::providerById(const QString &providerId) {
    const QString n = normalizeProviderId(providerId);
    if (n == "custom") return &providers.custom;
    if (n == "azure_openai") return &providers.azureOpenAI;
    if (n == "anthropic") return &providers.anthropic;
    if (n == "openai") return &providers.openai;
    if (n == "codebuddy") return &providers.codebuddy;
    if (n == "openrouter") return &providers.openrouter;
    if (n == "deepseek") return &providers.deepseek;
    if (n == "groq") return &providers.groq;
    if (n == "zhipu") return &providers.zhipu;
    if (n == "dashscope") return &providers.dashscope;
    if (n == "vllm") return &providers.vllm;
    if (n == "gemini") return &providers.gemini;
    if (n == "moonshot") return &providers.moonshot;
    if (n == "minimax") return &providers.minimax;
    if (n == "aihubmix") return &providers.aihubmix;
    if (n == "siliconflow") return &providers.siliconflow;
    if (n == "volcengine") return &providers.volcengine;
    if (n == "openai_codex") return &providers.openaiCodex;
    if (n == "github_copilot") return &providers.githubCopilot;
    return nullptr;
}

const ProviderConfig *Config::providerById(const QString &providerId) const {
    return const_cast<Config *>(this)->providerById(providerId);
}

QString Config::normalizeProviderId(const QString &providerId) {
    QString n = providerId.trimmed().toLower();
    n.replace('-', '_');
    // Legacy alias resolution
    if (n == "azureopenai") n = "azure_openai";
    if (n == "code_buddy") n = "codebuddy";
    if (n == "openaicodex") n = "openai_codex";
    if (n == "githubcopilot") n = "github_copilot";
    return n;
}

QString Config::matchedProviderName(const QString &modelIn) const {
    if (agentDefaults.provider != "auto" && !agentDefaults.provider.isEmpty()) {
        return normalizeProviderId(agentDefaults.provider);
    }

    const QString model = modelIn.isEmpty() ? agentDefaults.model.toLower() : modelIn.toLower();
    const QString prefix = model.contains('/') ? model.section('/', 0, 0) : QString();
    const QString providerPrefix = normalizeProviderId(prefix);
    if (providerPrefix == "custom" ||
        providerPrefix == "azure_openai" ||
        providerPrefix == "anthropic" ||
        providerPrefix == "openai" ||
        providerPrefix == "codebuddy" ||
        providerPrefix == "openrouter" ||
        providerPrefix == "deepseek" ||
        providerPrefix == "groq" ||
        providerPrefix == "zhipu" ||
        providerPrefix == "dashscope" ||
        providerPrefix == "vllm" ||
        providerPrefix == "gemini" ||
        providerPrefix == "moonshot" ||
        providerPrefix == "minimax" ||
        providerPrefix == "aihubmix" ||
        providerPrefix == "siliconflow" ||
        providerPrefix == "volcengine" ||
        providerPrefix == "openai_codex" ||
        providerPrefix == "github_copilot") {
        return providerPrefix;
    }

    if (model.contains("codebuddy") || model.contains("code_buddy") || model.startsWith("codebuddy/")) return "codebuddy";
    if (model.contains("openai-codex") || model.contains("openaicodex") || model.startsWith("openai_codex/")) return "openai_codex";
    if (model.contains("github_copilot") || model.contains("githubcopilot") || model.contains("github-copilot") || model.contains("copilot")) return "github_copilot";
    if (prefix == "azure_openai" || prefix == "azure-openai" || model.contains("azure")) return "azure_openai";
    if (model.contains("openrouter")) return "openrouter";
    if (model.contains("aihubmix")) return "aihubmix";
    if (model.contains("siliconflow")) return "siliconflow";
    if (model.contains("deepseek")) return "deepseek";
    if (model.contains("gemini")) return "gemini";
    if (model.contains("qwen") || model.contains("dashscope")) return "dashscope";
    if (model.contains("moonshot") || model.contains("kimi")) return "moonshot";
    if (model.contains("zhipu") || model.contains("glm")) return "zhipu";
    if (model.contains("minimax")) return "minimax";
    if (model.contains("volcengine") || model.contains("volces")) return "volcengine";
    if (model.contains("vllm")) return "vllm";
    if (model.contains("groq")) return "groq";
    if (model.contains("custom")) return "custom";
    if (model.contains("claude") || model.contains("anthropic")) return "anthropic";
    if (model.startsWith("gpt-") || model.contains("/gpt-") || model.contains("openai")) return "openai";

    if (!providers.codebuddy.apiBase.isEmpty() &&
        (!providers.codebuddy.apiKey.isEmpty() || !providers.codebuddy.extraHeaders.isEmpty())) {
        return "codebuddy";
    }
    if (!providers.openrouter.apiKey.isEmpty()) return "openrouter";
    if (!providers.anthropic.apiKey.isEmpty()) return "anthropic";
    if (!providers.openai.apiKey.isEmpty()) return "openai";
    if (!providers.deepseek.apiKey.isEmpty()) return "deepseek";
    if (!providers.groq.apiKey.isEmpty()) return "groq";
    if (!providers.zhipu.apiKey.isEmpty()) return "zhipu";
    if (!providers.dashscope.apiKey.isEmpty()) return "dashscope";
    if (!providers.gemini.apiKey.isEmpty()) return "gemini";
    if (!providers.moonshot.apiKey.isEmpty()) return "moonshot";
    if (!providers.minimax.apiKey.isEmpty()) return "minimax";
    if (!providers.aihubmix.apiKey.isEmpty()) return "aihubmix";
    if (!providers.siliconflow.apiKey.isEmpty()) return "siliconflow";
    if (!providers.volcengine.apiKey.isEmpty()) return "volcengine";
    if (!providers.azureOpenAI.apiKey.isEmpty()) return "azure_openai";
    if (!providers.vllm.apiBase.isEmpty()) return "vllm";
    if (!providers.custom.apiKey.isEmpty()) return "custom";
    return "openai";
}

QString Config::normalizedDeploymentMode() const {
    return normalizeDeploymentModeValue(deployment.mode);
}

QString Config::normalizedRuntimeMode() const {
    return normalizeRuntimeModeValue(runtime.mode);
}

QString Config::normalizedMemoryMode() const {
    return normalizeMemoryModeValue(memory.mode);
}

QString Config::normalizedMemoryBackend() const {
    QString backend = normalizeMemoryBackendValue(memory.backend);
    if (backend.isEmpty()) {
        backend = defaultMemoryBackendFor(normalizedMemoryMode(), normalizedDeploymentMode());
    }
    return backend;
}

bool Config::usesClusterDeployment() const {
    return normalizedDeploymentMode() == "cluster";
}

bool Config::usesRemoteRuntime() const {
    return normalizedRuntimeMode() == "remote";
}

bool Config::usesLayeredMemory() const {
    return normalizedMemoryMode() == "layered";
}

} // namespace yaos::config
