#include "daemon/LocalDaemonProtocol.h"
#include "daemon/DaemonRuntimeClient.h"
#include "daemon/LocalDaemonServer.h"
#include "config/ConfigLoader.h"
#include "platform/network/FastNetHttpTransport.h"
#include "platform/network/FastNetWebSocketTransport.h"
#include "runtime/LocalRuntimeClient.h"
#include "runtime/RemoteRuntimeClient.h"
#include "runtime/RuntimeHttpServer.h"

#include <FastNet/FastNet.h>
#include <FastNet/TcpServer.h>
#include <FastNet/WebSocketServer.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace {

constexpr const char *kLoopback = "127.0.0.1";

struct TestContext {
    int failures = 0;

    void check(bool condition, const QString &name, const QString &detail = QString()) {
        if (condition) {
            std::cout << "[OK] " << name.toStdString() << std::endl;
            return;
        }
        ++failures;
        std::cerr << "[FAIL] " << name.toStdString();
        if (!detail.isEmpty()) {
            std::cerr << ": " << detail.toStdString();
        }
        std::cerr << std::endl;
    }
};

quint16 testPort(const QString &name) {
    return yaos::daemon::protocol::serverPort(QStringLiteral("yaos-integration-%1-%2")
                                                  .arg(name)
                                                  .arg(QCoreApplication::applicationPid()));
}

bool ensureFastNet(TestContext &ctx) {
    const FastNet::ErrorCode code = FastNet::initialize(2);
    const bool ok = code == FastNet::ErrorCode::Success ||
                    code == FastNet::ErrorCode::AlreadyRunning ||
                    FastNet::isInitialized();
    ctx.check(ok, QStringLiteral("FastNet initialize"));
    return ok;
}

bool runUntil(std::function<bool()> predicate, int timeoutMs) {
    QEventLoop loop;
    QTimer poll;
    QTimer timeout;
    poll.setInterval(20);
    timeout.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (predicate()) {
            loop.quit();
        }
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    timeout.start(timeoutMs);
    loop.exec();
    return predicate();
}

bool prepareIsolatedHome(QTemporaryDir *home, TestContext &ctx) {
    if (!home || !home->isValid()) {
        ctx.check(false, QStringLiteral("isolated HOME is created"));
        return false;
    }

    const QByteArray homePath = QDir::toNativeSeparators(home->path()).toUtf8();
    qputenv("HOME", homePath);
    qputenv("USERPROFILE", homePath);

    yaos::config::Config config;
    config.agentDefaults.workspace = QDir(home->path()).filePath(QStringLiteral("workspace"));
    const bool saved = yaos::config::ConfigLoader::save(config);
    ctx.check(saved, QStringLiteral("isolated config is saved"));
    return saved;
}

void testProviderRouting(TestContext &ctx) {
    yaos::config::Config config;
    config.agentDefaults.provider = QStringLiteral("auto");

    ctx.check(config.matchedProviderName(QStringLiteral("openrouter/anthropic/claude-sonnet-4")) ==
                  QStringLiteral("openrouter"),
              QStringLiteral("provider routing keeps explicit OpenRouter prefix"));
    ctx.check(config.matchedProviderName(QStringLiteral("aihubmix/openai/gpt-5")) ==
                  QStringLiteral("aihubmix"),
              QStringLiteral("provider routing keeps explicit AIHubMix prefix"));
    ctx.check(config.matchedProviderName(QStringLiteral("groq/llama-3.3-70b-versatile")) ==
                  QStringLiteral("groq"),
              QStringLiteral("provider routing supports explicit Groq prefix"));
    ctx.check(config.matchedProviderName(QStringLiteral("openaiCodex/gpt-5-codex")) ==
                  QStringLiteral("openai_codex"),
              QStringLiteral("provider routing supports openaiCodex alias"));
    ctx.check(config.matchedProviderName(QStringLiteral("githubCopilot/claude-sonnet-4")) ==
                  QStringLiteral("github_copilot"),
              QStringLiteral("provider routing supports githubCopilot alias"));
}

yaos::config::DelegationTemplateConfig templateRecord(const QString &id,
                                                      const QString &name,
                                                      const QString &task) {
    yaos::config::DelegationTemplateConfig record;
    record.id = id;
    record.name = name;
    record.kind = QStringLiteral("single");
    record.request.insert(QStringLiteral("task"), task);
    return record;
}

std::string httpResponse(const QByteArray &body, const char *extraHeader = "") {
    std::string payload(body.constData(), static_cast<size_t>(body.size()));
    std::string response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: application/json\r\n";
    response += extraHeader;
    response += "Content-Length: " + std::to_string(payload.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += payload;
    return response;
}

QByteArray requestPathFromHttpRequest(const QByteArray &request) {
    const int firstSpace = request.indexOf(' ');
    if (firstSpace < 0) {
        return {};
    }
    const int secondSpace = request.indexOf(' ', firstSpace + 1);
    if (secondSpace <= firstSpace) {
        return {};
    }
    return request.mid(firstSpace + 1, secondSpace - firstSpace - 1);
}

void testStudioProviderModelInvoke(TestContext &ctx) {
    QTemporaryDir home;
    if (!prepareIsolatedHome(&home, ctx)) {
        return;
    }

    const yaos::config::Config config = yaos::config::ConfigLoader::load();
    yaos::runtime::LocalRuntimeClient client;

    auto fetchModels = [&](const QString &providerId) {
        return client.invoke(QStringLiteral("studio.fetchProviderModels"),
                             QJsonObject{
                                 {QStringLiteral("draftConfig"), config.toJson()},
                                 {QStringLiteral("liveConfig"), config.toJson()},
                                 {QStringLiteral("providerId"), providerId}
                             });
    };

    const QJsonObject unknownProvider = fetchModels(QStringLiteral("missing_provider"));
    ctx.check(!unknownProvider.value(QStringLiteral("ok")).toBool() &&
                  unknownProvider.value(QStringLiteral("providerId")).toString() == QStringLiteral("missing_provider"),
              QStringLiteral("studio.fetchProviderModels rejects unknown provider"));

    const QJsonObject azure = fetchModels(QStringLiteral("azure_openai"));
    ctx.check(!azure.value(QStringLiteral("ok")).toBool() &&
                  azure.value(QStringLiteral("title")).toString() == QStringLiteral("Model sync unavailable"),
              QStringLiteral("studio.fetchProviderModels rejects Azure model sync"));

    const QJsonObject openai = fetchModels(QStringLiteral("openai"));
    ctx.check(!openai.value(QStringLiteral("ok")).toBool() &&
                  openai.value(QStringLiteral("title")).toString() == QStringLiteral("Missing credential"),
              QStringLiteral("studio.fetchProviderModels reports missing OpenAI credential"));
}

void testStudioControlTemplateInvoke(TestContext &ctx) {
    QTemporaryDir home;
    if (!prepareIsolatedHome(&home, ctx)) {
        return;
    }

    yaos::config::Config config = yaos::config::ConfigLoader::load();
    yaos::runtime::LocalRuntimeClient client;

    config.deployment.controlPlaneUrl = QStringLiteral("http://127.0.0.1:1");
    const QJsonObject emptyPush = client.invoke(
        QStringLiteral("studio.pushDelegationTemplatesToControl"),
        QJsonObject{
            {QStringLiteral("config"), config.toJson()},
            {QStringLiteral("records"), QJsonArray()},
            {QStringLiteral("replace"), false}
        });
    ctx.check(!emptyPush.value(QStringLiteral("ok")).toBool() &&
                  emptyPush.value(QStringLiteral("title")).toString() == QStringLiteral("同步失败"),
              QStringLiteral("studio.pushDelegationTemplatesToControl rejects empty exchange payload"));

    config.deployment.controlPlaneUrl.clear();
    const QJsonObject missingEndpointPull = client.invoke(
        QStringLiteral("studio.pullDelegationTemplatesFromControl"),
        QJsonObject{
            {QStringLiteral("config"), config.toJson()},
            {QStringLiteral("replace"), false}
        });
    ctx.check(!missingEndpointPull.value(QStringLiteral("ok")).toBool() &&
                  missingEndpointPull.value(QStringLiteral("body")).toString().contains(QStringLiteral("Control plane endpoint")),
              QStringLiteral("studio.pullDelegationTemplatesFromControl requires endpoint"));
}

void testStudioConfigSavePreservesOAuthState(TestContext &ctx) {
    QTemporaryDir home;
    if (!prepareIsolatedHome(&home, ctx)) {
        return;
    }

    yaos::config::Config liveConfig = yaos::config::ConfigLoader::load();
    liveConfig.agentDefaults.provider = QStringLiteral("custom");
    liveConfig.agentDefaults.model = QStringLiteral("local/test-before-save");
    liveConfig.providers.custom.apiBase = QStringLiteral("http://127.0.0.1:1/v1");
    liveConfig.providers.custom.apiKey = QStringLiteral("test-key");
    liveConfig.providers.openaiCodex.apiKey = QStringLiteral("live-runtime-token");
    liveConfig.providers.openaiCodex.oauthAccessToken = QStringLiteral("live-access-token");
    liveConfig.providers.openaiCodex.oauthRefreshToken = QStringLiteral("live-refresh-token");
    liveConfig.providers.openaiCodex.oauthDeviceCode = QStringLiteral("live-device-code");
    ctx.check(yaos::config::ConfigLoader::save(liveConfig),
              QStringLiteral("live config with OAuth state is saved"));

    yaos::config::Config draftConfig = liveConfig;
    draftConfig.agentDefaults.model = QStringLiteral("local/test-after-save");
    draftConfig.providers.openaiCodex.apiKey.clear();
    draftConfig.providers.openaiCodex.oauthAccessToken.clear();
    draftConfig.providers.openaiCodex.oauthRefreshToken.clear();
    draftConfig.providers.openaiCodex.oauthDeviceCode.clear();

    yaos::runtime::LocalRuntimeClient client;
    const QJsonObject response = client.invoke(
        QStringLiteral("studio.saveConfiguration"),
        QJsonObject{
            {QStringLiteral("draftConfig"), draftConfig.toJson()},
            {QStringLiteral("liveConfig"), liveConfig.toJson()}
        });

    const yaos::config::Config savedConfig = yaos::config::ConfigLoader::load();
    ctx.check(response.value(QStringLiteral("saved")).toBool() &&
                  response.value(QStringLiteral("configChanged")).toBool(),
              QStringLiteral("studio.saveConfiguration saves changed config"));
    ctx.check(savedConfig.agentDefaults.model == QStringLiteral("local/test-after-save"),
              QStringLiteral("studio.saveConfiguration persists draft fields"));
    ctx.check(savedConfig.providers.openaiCodex.apiKey == QStringLiteral("live-runtime-token") &&
                  savedConfig.providers.openaiCodex.oauthAccessToken == QStringLiteral("live-access-token") &&
                  savedConfig.providers.openaiCodex.oauthRefreshToken == QStringLiteral("live-refresh-token") &&
                  savedConfig.providers.openaiCodex.oauthDeviceCode == QStringLiteral("live-device-code"),
              QStringLiteral("studio.saveConfiguration preserves live OAuth runtime state"));
}

void testStudioProviderAuthStatusIsSideEffectFree(TestContext &ctx) {
    QTemporaryDir home;
    if (!prepareIsolatedHome(&home, ctx)) {
        return;
    }

    yaos::config::Config config = yaos::config::ConfigLoader::load();
    config.providers.openaiCodex.apiKey = QStringLiteral("status-runtime-token");
    config.providers.openaiCodex.oauthAccessToken = QStringLiteral("status-access-token");
    config.providers.openaiCodex.oauthRefreshToken = QStringLiteral("status-refresh-token");
    ctx.check(yaos::config::ConfigLoader::save(config),
              QStringLiteral("OAuth status fixture config is saved"));

    yaos::runtime::LocalRuntimeClient client;
    const QJsonObject response = client.invoke(
        QStringLiteral("studio.providerAuthStatus"),
        QJsonObject{{QStringLiteral("providerId"), QStringLiteral("openai_codex")}});
    const yaos::config::Config after = yaos::config::ConfigLoader::load();

    ctx.check(response.value(QStringLiteral("ok")).toBool() &&
                  response.value(QStringLiteral("providerId")).toString() == QStringLiteral("openai_codex"),
              QStringLiteral("studio.providerAuthStatus returns provider status"));
    ctx.check(after.toJson() == config.toJson(),
              QStringLiteral("studio.providerAuthStatus does not mutate config"));
}

void testStudioConfigSaveFailure(TestContext &ctx) {
    QTemporaryDir temp;
    if (!temp.isValid()) {
        ctx.check(false, QStringLiteral("save failure temp directory is created"));
        return;
    }

    const QString blockedHome = QDir(temp.path()).filePath(QStringLiteral("home-file"));
    QFile blocker(blockedHome);
    const bool blockerOpened = blocker.open(QIODevice::WriteOnly | QIODevice::Text);
    if (blockerOpened) {
        blocker.write("not a directory");
        blocker.close();
    }
    ctx.check(blockerOpened, QStringLiteral("save failure blocker file is created"), blocker.errorString());
    if (!blockerOpened) {
        return;
    }

    const QByteArray homePath = QDir::toNativeSeparators(blockedHome).toUtf8();
    qputenv("HOME", homePath);
    qputenv("USERPROFILE", homePath);

    yaos::config::Config draftConfig;
    draftConfig.agentDefaults.provider = QStringLiteral("echo");
    draftConfig.agentDefaults.model = QStringLiteral("echo-save-failure");

    yaos::runtime::LocalRuntimeClient client;
    const QJsonObject response = client.invoke(
        QStringLiteral("studio.saveConfiguration"),
        QJsonObject{{QStringLiteral("draftConfig"), draftConfig.toJson()}});

    ctx.check(!response.value(QStringLiteral("ok")).toBool() &&
                  !response.value(QStringLiteral("saved")).toBool() &&
                  response.value(QStringLiteral("title")).toString() == QStringLiteral("保存失败"),
              QStringLiteral("studio.saveConfiguration reports save failure"));
}

void testStudioOAuthEdgeCases(TestContext &ctx) {
    QTemporaryDir home;
    if (!prepareIsolatedHome(&home, ctx)) {
        return;
    }

    yaos::runtime::LocalRuntimeClient client;
    const QJsonObject unsupportedDeviceStart = client.invoke(
        QStringLiteral("studio.startProviderDeviceFlow"),
        QJsonObject{{QStringLiteral("providerId"), QStringLiteral("openai")}});
    ctx.check(!unsupportedDeviceStart.value(QStringLiteral("ok")).toBool() &&
                  unsupportedDeviceStart.value(QStringLiteral("error")).toString().contains(QStringLiteral("device flow")),
              QStringLiteral("studio.startProviderDeviceFlow rejects unsupported provider"));

    const QJsonObject unsupportedDevicePoll = client.invoke(
        QStringLiteral("studio.pollProviderDeviceFlow"),
        QJsonObject{{QStringLiteral("providerId"), QStringLiteral("openai")}});
    ctx.check(!unsupportedDevicePoll.value(QStringLiteral("ok")).toBool() &&
                  unsupportedDevicePoll.value(QStringLiteral("error")).toString().contains(QStringLiteral("device flow")),
              QStringLiteral("studio.pollProviderDeviceFlow rejects unsupported provider"));

    const QJsonObject unsupportedRefresh = client.invoke(
        QStringLiteral("studio.refreshProviderOAuth"),
        QJsonObject{{QStringLiteral("providerId"), QStringLiteral("openai")}});
    ctx.check(!unsupportedRefresh.value(QStringLiteral("ok")).toBool() &&
                  unsupportedRefresh.value(QStringLiteral("error")).toString().contains(QStringLiteral("token refresh")),
              QStringLiteral("studio.refreshProviderOAuth rejects unsupported provider"));

    const QJsonObject unsupportedBrowserStart = client.invoke(
        QStringLiteral("studio.startProviderBrowserOAuth"),
        QJsonObject{
            {QStringLiteral("providerId"), QStringLiteral("github_copilot")},
            {QStringLiteral("redirectUri"), QStringLiteral("http://127.0.0.1/callback")},
            {QStringLiteral("state"), QStringLiteral("state")},
            {QStringLiteral("codeVerifier"), QStringLiteral("verifier")}
        });
    ctx.check(!unsupportedBrowserStart.value(QStringLiteral("ok")).toBool() &&
                  unsupportedBrowserStart.value(QStringLiteral("error")).toString().contains(QStringLiteral("browser OAuth")),
              QStringLiteral("studio.startProviderBrowserOAuth rejects unsupported provider"));

    const QJsonObject stateMismatch = client.invoke(
        QStringLiteral("studio.completeProviderBrowserOAuth"),
        QJsonObject{
            {QStringLiteral("providerId"), QStringLiteral("openai_codex")},
            {QStringLiteral("redirectUri"), QStringLiteral("http://127.0.0.1/callback")},
            {QStringLiteral("expectedState"), QStringLiteral("expected-state")},
            {QStringLiteral("codeVerifier"), QStringLiteral("verifier")},
            {QStringLiteral("callbackUrl"), QStringLiteral("http://127.0.0.1/callback?state=actual-state&code=abc")}
        });
    ctx.check(!stateMismatch.value(QStringLiteral("ok")).toBool() &&
                  stateMismatch.value(QStringLiteral("error")).toString() == QStringLiteral("OAuth state mismatch."),
              QStringLiteral("studio.completeProviderBrowserOAuth rejects state mismatch"));
}

void testStudioControlTemplatePullMergeReplace(TestContext &ctx) {
    QTemporaryDir home;
    if (!prepareIsolatedHome(&home, ctx)) {
        return;
    }

    const quint16 port = testPort(QStringLiteral("control-pull"));
    FastNet::TcpServer server(FastNet::getGlobalIoService());
    std::atomic_int listRequests{0};

    const QJsonObject incomingTemplate{
        {QStringLiteral("id"), QStringLiteral("incoming-template")},
        {QStringLiteral("name"), QStringLiteral("Incoming Template")},
        {QStringLiteral("kind"), QStringLiteral("single")},
        {QStringLiteral("request"), QJsonObject{{QStringLiteral("task"), QStringLiteral("incoming task")}}}
    };
    const QJsonObject envelope{
        {QStringLiteral("schema"), QStringLiteral("yaos.delegation-templates/v1")},
        {QStringLiteral("templates"), QJsonArray{incomingTemplate}}
    };

    server.setDataReceivedCallback([&](FastNet::ConnectionId clientId, const FastNet::Buffer &data) {
        const QByteArray request(reinterpret_cast<const char *>(data.data()), static_cast<int>(data.size()));
        const QByteArray path = requestPathFromHttpRequest(request);
        QJsonObject body;
        if (path == QByteArray("/health") || path == QByteArray("/v1/control/health")) {
            body.insert(QStringLiteral("ok"), true);
        } else if (path == QByteArray("/v1/control/delegation-templates/list")) {
            ++listRequests;
            body.insert(QStringLiteral("ok"), true);
            body.insert(QStringLiteral("envelope"), envelope);
        } else {
            body.insert(QStringLiteral("ok"), false);
            body.insert(QStringLiteral("error"), QStringLiteral("unexpected path"));
        }
        server.sendToClient(clientId, httpResponse(QJsonDocument(body).toJson(QJsonDocument::Compact)));
        server.closeClientAfterPendingWrites(clientId);
    });

    const FastNet::Error startError = server.start(port, kLoopback);
    ctx.check(!startError.isFailure(), QStringLiteral("local control template test server starts"),
              QString::fromStdString(startError.toString()));
    if (startError.isFailure()) {
        return;
    }

    yaos::config::Config config = yaos::config::ConfigLoader::load();
    config.agentDefaults.provider = QStringLiteral("custom");
    config.agentDefaults.model = QStringLiteral("local/control-pull");
    config.providers.custom.apiBase = QStringLiteral("http://127.0.0.1:1/v1");
    config.providers.custom.apiKey = QStringLiteral("test-key");
    config.deployment.controlPlaneUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    config.memory.delegationTemplates = {
        templateRecord(QStringLiteral("existing-template"),
                       QStringLiteral("Existing Template"),
                       QStringLiteral("existing task"))
    };
    ctx.check(yaos::config::ConfigLoader::save(config),
              QStringLiteral("control pull fixture config is saved"));

    yaos::runtime::LocalRuntimeClient client;
    const QJsonObject mergeResponse = client.invoke(
        QStringLiteral("studio.pullDelegationTemplatesFromControl"),
        QJsonObject{
            {QStringLiteral("config"), config.toJson()},
            {QStringLiteral("replace"), false}
        });
    yaos::config::Config mergedConfig = yaos::config::ConfigLoader::load();
    ctx.check(mergeResponse.value(QStringLiteral("ok")).toBool() &&
                  mergeResponse.value(QStringLiteral("pulledCount")).toInt() == 1 &&
                  mergedConfig.memory.delegationTemplates.size() == 2,
              QStringLiteral("studio.pullDelegationTemplatesFromControl merges incoming templates"));

    const QJsonObject replaceResponse = client.invoke(
        QStringLiteral("studio.pullDelegationTemplatesFromControl"),
        QJsonObject{
            {QStringLiteral("config"), config.toJson()},
            {QStringLiteral("replace"), true}
        });
    const yaos::config::Config replacedConfig = yaos::config::ConfigLoader::load();
    ctx.check(replaceResponse.value(QStringLiteral("ok")).toBool() &&
                  replaceResponse.value(QStringLiteral("pulledCount")).toInt() == 1 &&
                  replacedConfig.memory.delegationTemplates.size() == 1 &&
                  replacedConfig.memory.delegationTemplates.first().id == QStringLiteral("incoming-template"),
              QStringLiteral("studio.pullDelegationTemplatesFromControl replaces templates"));
    ctx.check(listRequests.load() == 2,
              QStringLiteral("control template pull called list endpoint twice"));

    server.stop();
}

void verifyStudioRuntimeClientEndToEnd(TestContext &ctx,
                                       yaos::distributed::IRuntimeClient &client,
                                       const yaos::config::Config &config,
                                       const QString &label,
                                       const QString &expectedRuntimeMode) {
    const QJsonObject init = client.invoke(QStringLiteral("initializeWorkspace"), QJsonObject());
    ctx.check(init.value(QStringLiteral("ok")).toBool() &&
                  init.value(QStringLiteral("value")).toBool(),
              QStringLiteral("%1 runtime initializes workspace").arg(label),
              init.value(QStringLiteral("error")).toString());

    const QJsonObject status = client.invoke(QStringLiteral("statusSnapshot"), QJsonObject());
    const QJsonObject statusBody = status.value(QStringLiteral("status")).toObject();
    ctx.check(status.value(QStringLiteral("ok")).toBool() &&
                  statusBody.value(QStringLiteral("runtimeMode")).toString() == expectedRuntimeMode &&
                  statusBody.value(QStringLiteral("actualBackend")).toString() == QStringLiteral("echo"),
              QStringLiteral("%1 runtime returns status through client").arg(label));

    const QJsonObject missingModels = client.invoke(
        QStringLiteral("studio.fetchProviderModels"),
        QJsonObject{
            {QStringLiteral("draftConfig"), config.toJson()},
            {QStringLiteral("liveConfig"), config.toJson()},
            {QStringLiteral("providerId"), QStringLiteral("openai")}
        });
    ctx.check(!missingModels.value(QStringLiteral("ok")).toBool() &&
                  missingModels.value(QStringLiteral("title")).toString() == QStringLiteral("Missing credential"),
              QStringLiteral("%1 studio provider request crosses runtime boundary").arg(label));

    yaos::config::Config liveConfig = yaos::config::ConfigLoader::load();
    yaos::config::Config draftConfig = liveConfig;
    const QString updatedModel = QStringLiteral("echo-%1-updated").arg(label);
    draftConfig.agentDefaults.model = updatedModel;
    const QJsonObject save = client.invoke(
        QStringLiteral("studio.saveConfiguration"),
        QJsonObject{
            {QStringLiteral("draftConfig"), draftConfig.toJson()},
            {QStringLiteral("liveConfig"), liveConfig.toJson()}
        });
    const yaos::config::Config savedConfig = yaos::config::ConfigLoader::load();
    ctx.check(save.value(QStringLiteral("saved")).toBool() &&
                  save.value(QStringLiteral("reloadOk")).toBool() &&
                  savedConfig.agentDefaults.model == updatedModel,
              QStringLiteral("%1 studio config save persists and reloads").arg(label));

    const QString prompt = QStringLiteral("%1 studio chat smoke").arg(label);
    const QJsonObject chat = client.invoke(
        QStringLiteral("processMessageDetailed"),
        QJsonObject{
            {QStringLiteral("content"), prompt},
            {QStringLiteral("sessionKey"), QStringLiteral("%1-smoke").arg(label)},
            {QStringLiteral("channel"), QStringLiteral("integration")},
            {QStringLiteral("chatId"), label},
            {QStringLiteral("modelOverride"), updatedModel},
            {QStringLiteral("providerOverride"), QStringLiteral("echo")}
        });
    const QJsonObject turn = chat.value(QStringLiteral("turn")).toObject();
    ctx.check(chat.value(QStringLiteral("ok")).toBool() &&
                  !turn.value(QStringLiteral("error")).toBool() &&
                  turn.value(QStringLiteral("content")).toString().contains(prompt),
              QStringLiteral("%1 runtime chat request uses echo provider").arg(label));

    const QJsonObject events = client.invoke(QStringLiteral("recentEvents"),
                                             QJsonObject{{QStringLiteral("limit"), 20}});
    ctx.check(events.value(QStringLiteral("ok")).toBool() &&
                  events.value(QStringLiteral("items")).isArray() &&
                  !events.value(QStringLiteral("items")).toArray().isEmpty(),
              QStringLiteral("%1 runtime exposes recent events after chat").arg(label));
}

void testDaemonStudioBackendEndToEnd(TestContext &ctx) {
    QTemporaryDir home;
    if (!prepareIsolatedHome(&home, ctx)) {
        return;
    }

    const QString serverName = QStringLiteral("yaos-integration-daemon-%1-%2")
                                   .arg(QCoreApplication::applicationPid())
                                   .arg(static_cast<qulonglong>(reinterpret_cast<quintptr>(&home)), 0, 16);

    yaos::config::Config config = yaos::config::ConfigLoader::load();
    config.runtime.mode = QStringLiteral("daemon");
    config.runtime.endpoint = QStringLiteral("local://%1").arg(serverName);
    config.runtime.autoSpawnLocalDaemon = false;
    config.runtime.autoSpawnLocalService = false;
    config.agentDefaults.provider = QStringLiteral("echo");
    config.agentDefaults.model = QStringLiteral("echo");
    config.memory.service.enabled = false;
    ctx.check(yaos::config::ConfigLoader::save(config),
              QStringLiteral("daemon fixture config is saved"));

    yaos::daemon::LocalDaemonServer server;
    QThread serverThread;
    server.moveToThread(&serverThread);
    serverThread.start();

    auto stopDaemon = [&]() {
        if (!serverThread.isRunning()) {
            return;
        }
        QThread *mainThread = QCoreApplication::instance()->thread();
        QMetaObject::invokeMethod(&server,
                                  [&]() {
                                      server.stop();
                                      server.moveToThread(mainThread);
                                  },
                                  Qt::BlockingQueuedConnection);
        serverThread.quit();
        serverThread.wait();
    };

    QString startError;
    bool started = false;
    QMetaObject::invokeMethod(&server,
                              [&]() {
                                  started = server.start(serverName, &startError);
                              },
                              Qt::BlockingQueuedConnection);
    ctx.check(started, QStringLiteral("local daemon server starts for studio E2E"), startError);
    if (!started) {
        stopDaemon();
        return;
    }

    yaos::daemon::DaemonRuntimeClient client(config);
    verifyStudioRuntimeClientEndToEnd(ctx,
                                      client,
                                      config,
                                      QStringLiteral("daemon"),
                                      QStringLiteral("daemon"));
    stopDaemon();
}

void testRemoteRuntimeServiceStudioBackendEndToEnd(TestContext &ctx) {
    QTemporaryDir home;
    if (!prepareIsolatedHome(&home, ctx)) {
        return;
    }

    const quint16 port = testPort(QStringLiteral("runtime-http-e2e"));
    yaos::config::Config config = yaos::config::ConfigLoader::load();
    config.runtime.mode = QStringLiteral("remote");
    config.runtime.endpoint = QStringLiteral("http://127.0.0.1:%1").arg(port);
    config.runtime.autoSpawnLocalService = false;
    config.agentDefaults.provider = QStringLiteral("echo");
    config.agentDefaults.model = QStringLiteral("echo");
    config.memory.service.enabled = false;
    ctx.check(yaos::config::ConfigLoader::save(config),
              QStringLiteral("remote runtime fixture config is saved"));

    yaos::runtime::LocalRuntimeClient localClient;
    yaos::runtime::RuntimeHttpServer server(localClient);
    QString startError;
    const bool started = server.start(QStringLiteral("127.0.0.1"), port, &startError);
    ctx.check(started, QStringLiteral("local runtime HTTP server starts for studio E2E"), startError);
    if (!started) {
        return;
    }

    yaos::runtime::RemoteRuntimeClient client(config, 20000);
    verifyStudioRuntimeClientEndToEnd(ctx,
                                      client,
                                      config,
                                      QStringLiteral("remote"),
                                      QStringLiteral("remote"));
    server.stop();
}

void testDaemonProtocol(TestContext &ctx) {
    const QJsonObject request = yaos::daemon::protocol::makeRequest(
        QStringLiteral("ping"),
        QJsonObject{{QStringLiteral("value"), 42}});
    const QByteArray encoded = yaos::daemon::protocol::encodeMessage(request);

    QJsonObject decoded;
    QString error;
    const bool decodedOk = yaos::daemon::protocol::decodeMessage(encoded, &decoded, &error);
    ctx.check(decodedOk && decoded.value(QStringLiteral("method")).toString() == QStringLiteral("ping"),
              QStringLiteral("daemon protocol roundtrip"),
              error);

    QJsonObject invalid;
    const bool invalidOk = yaos::daemon::protocol::decodeMessage(QByteArray("{bad json\n"), &invalid, &error);
    ctx.check(!invalidOk && !error.isEmpty(), QStringLiteral("daemon protocol rejects invalid json"));
}

void testHttpTransport(TestContext &ctx) {
    const quint16 port = testPort(QStringLiteral("http"));
    FastNet::TcpServer server(FastNet::getGlobalIoService());
    std::atomic_bool sawPost{false};
    std::atomic_bool sawHeader{false};
    std::atomic_bool sawBody{false};

    server.setDataReceivedCallback([&](FastNet::ConnectionId clientId, const FastNet::Buffer &data) {
        const QByteArray request(reinterpret_cast<const char *>(data.data()), static_cast<int>(data.size()));
        sawPost = request.startsWith("POST ");
        sawHeader = request.contains("X-YAOS-Test: smoke");
        sawBody = request.contains("{\"hello\":\"fastnet\"}");
        const QByteArray body = QJsonDocument(QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("transport"), QStringLiteral("fastnet-http")}
        }).toJson(QJsonDocument::Compact);
        server.sendToClient(clientId, httpResponse(body, "X-YAOS-Echo: yes\r\n"));
        server.closeClientAfterPendingWrites(clientId);
    });

    const FastNet::Error startError = server.start(port, kLoopback);
    ctx.check(!startError.isFailure(), QStringLiteral("local HTTP test server starts"),
              QString::fromStdString(startError.toString()));
    if (startError.isFailure()) {
        return;
    }

    yaos::platform::network::HttpRequest request;
    request.method = QStringLiteral("POST");
    request.url = QStringLiteral("http://127.0.0.1:%1/test").arg(port);
    request.headers.insert("X-YAOS-Test", "smoke");
    request.body = QByteArray("{\"hello\":\"fastnet\"}");
    request.timeoutMs = 3000;

    const yaos::platform::network::HttpResponse response =
        yaos::platform::network::FastNetHttpTransport::send(request);
    server.stop();

    ctx.check(response.ok(), QStringLiteral("FastNet HTTP POST succeeds"), response.error);
    ctx.check(response.statusCode == 200, QStringLiteral("FastNet HTTP status is captured"));
    ctx.check(response.headers.value("X-YAOS-Echo") == "yes",
              QStringLiteral("FastNet HTTP response headers are captured"));
    ctx.check(response.body.contains("fastnet-http"), QStringLiteral("FastNet HTTP body is captured"));
    ctx.check(sawPost && sawHeader && sawBody,
              QStringLiteral("FastNet HTTP request method/header/body reach server"));

    yaos::platform::network::HttpRequest invalidProxy = request;
    invalidProxy.proxyUrl = QStringLiteral("not a valid proxy");
    const yaos::platform::network::HttpResponse proxyResponse =
        yaos::platform::network::FastNetHttpTransport::send(invalidProxy);
    ctx.check(!proxyResponse.ok() && !proxyResponse.error.isEmpty(),
              QStringLiteral("FastNet HTTP rejects invalid proxy"));
}

void testWebSocketTransport(TestContext &ctx) {
    const quint16 port = testPort(QStringLiteral("websocket"));
    FastNet::WebSocketServer server(FastNet::getGlobalIoService());
    std::atomic_bool serverSawMessage{false};

    server.setMessageCallback([&](FastNet::ConnectionId clientId, const std::string &message) {
        serverSawMessage = message == "hello";
        server.sendTextToClient(clientId, "echo:" + message);
    });

    const FastNet::Error startError = server.start(port, kLoopback);
    ctx.check(!startError.isFailure(), QStringLiteral("local WebSocket test server starts"),
              QString::fromStdString(startError.toString()));
    if (startError.isFailure()) {
        return;
    }
    QThread::msleep(100);

    yaos::platform::network::FastNetWebSocketTransport transport;
    bool connected = false;
    bool received = false;
    QString receivedMessage;
    QString error;

    QObject::connect(&transport, &yaos::platform::network::FastNetWebSocketTransport::connected, [&]() {
        connected = true;
        transport.sendTextMessage(QStringLiteral("hello"));
    });
    QObject::connect(&transport, &yaos::platform::network::FastNetWebSocketTransport::textMessageReceived,
                     [&](const QString &message) {
        received = true;
        receivedMessage = message;
    });
    QObject::connect(&transport, &yaos::platform::network::FastNetWebSocketTransport::errorMessage,
                     [&](const QString &message) {
        error = message;
    });

    transport.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(port)));
    const bool ok = runUntil([&]() { return received || !error.isEmpty(); }, 10000);
    transport.close();
    server.stop();

    ctx.check(ok && connected, QStringLiteral("FastNet WebSocket connects"), error);
    ctx.check(received && receivedMessage == QStringLiteral("echo:hello"),
              QStringLiteral("FastNet WebSocket sends and receives text"),
              receivedMessage);
    ctx.check(serverSawMessage, QStringLiteral("FastNet WebSocket server receives client text"));

    yaos::platform::network::FastNetWebSocketTransport invalidTransport;
    bool invalidError = false;
    QObject::connect(&invalidTransport, &yaos::platform::network::FastNetWebSocketTransport::errorMessage,
                     [&](const QString &) {
        invalidError = true;
    });
    invalidTransport.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(testPort(QStringLiteral("ws-missing")))));
    const bool failed = runUntil([&]() { return invalidError; }, 3000);
    invalidTransport.close();
    ctx.check(failed, QStringLiteral("FastNet WebSocket reports connection error"));
}

void testEmailStartTlsGuard(TestContext &ctx) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
#ifdef YAOS_SOURCE_ROOT
        QDir::cleanPath(QString::fromUtf8(YAOS_SOURCE_ROOT) + QStringLiteral("/src/channels/EmailChannel.cpp")),
#endif
        QDir::cleanPath(QDir(appDir).absoluteFilePath(QStringLiteral("../src/channels/EmailChannel.cpp"))),
        QDir::cleanPath(QDir(appDir).absoluteFilePath(QStringLiteral("../../src/channels/EmailChannel.cpp"))),
        QDir::cleanPath(QDir::current().absoluteFilePath(QStringLiteral("src/channels/EmailChannel.cpp")))
    };

    QString sourcePath;
    for (const QString &candidate : candidates) {
        if (QFile::exists(candidate)) {
            sourcePath = candidate;
            break;
        }
    }

    QFile file(sourcePath);
    const bool opened = file.open(QIODevice::ReadOnly | QIODevice::Text);
    ctx.check(opened, QStringLiteral("EmailChannel source is readable for STARTTLS guard"), file.errorString());
    if (!opened) {
        return;
    }
    const QString source = QString::fromUtf8(file.readAll());
    ctx.check(source.contains(QStringLiteral("startTls(")),
              QStringLiteral("EmailChannel uses FastNet TcpClient STARTTLS"));
    ctx.check(!source.contains(QStringLiteral("QSslSocket")),
              QStringLiteral("EmailChannel does not use QSslSocket"));
}

[[noreturn]] void finishProcess(int exitCode) {
    std::cout.flush();
    std::cerr.flush();
#ifdef _WIN32
    ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(exitCode));
#else
    std::_Exit(exitCode);
#endif
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    TestContext ctx;
    if (!ensureFastNet(ctx)) {
        return 1;
    }

    testDaemonProtocol(ctx);
    testHttpTransport(ctx);
    testWebSocketTransport(ctx);
    testEmailStartTlsGuard(ctx);
    testProviderRouting(ctx);
    testStudioProviderModelInvoke(ctx);
    testStudioControlTemplateInvoke(ctx);
    testStudioConfigSavePreservesOAuthState(ctx);
    testStudioProviderAuthStatusIsSideEffectFree(ctx);
    testStudioConfigSaveFailure(ctx);
    testStudioOAuthEdgeCases(ctx);
    testStudioControlTemplatePullMergeReplace(ctx);
    testDaemonStudioBackendEndToEnd(ctx);
    testRemoteRuntimeServiceStudioBackendEndToEnd(ctx);

    if (ctx.failures > 0) {
        std::cerr << "[integration-smoke] FAILED: " << ctx.failures << " issue(s)" << std::endl;
        finishProcess(1);
    }

    std::cout << "[integration-smoke] OK" << std::endl;
    finishProcess(0);
}
