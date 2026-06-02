#include "GuiRegressionRunner.h"

#include <algorithm>

#include <FastNet/FastNet.h>
#include <FastNet/HttpServer.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QQuickItem>
#include <QScopedValueRollback>
#include <QUrl>
#include <QtGlobal>
#include <QVariantList>
#include <QVariantMap>

#include "../config/Config.h"
#include "../config/ConfigLoader.h"
#include "StudioBridge.h"
#include "StudioWindow.h"

namespace yaos::ui {

namespace {

qreal realProperty(QObject *object, const char *propertyName) {
    if (!object || !propertyName || *propertyName == '\0') {
        return 0.0;
    }
    return object->property(propertyName).toReal();
}

bool boolProperty(QObject *object, const char *propertyName) {
    if (!object || !propertyName || *propertyName == '\0') {
        return false;
    }
    return object->property(propertyName).toBool();
}

QObject *findObjectWithProperty(QObject *root, const char *propertyName, QSet<QObject *> *visited) {
    if (!root || !propertyName || *propertyName == '\0') {
        return nullptr;
    }
    if (visited) {
        if (visited->contains(root)) {
            return nullptr;
        }
        visited->insert(root);
    }

    const QMetaObject *metaObject = root->metaObject();
    if (metaObject && metaObject->indexOfProperty(propertyName) >= 0) {
        return root;
    }

    const QObjectList children = root->children();
    for (QObject *child : children) {
        if (QObject *match = findObjectWithProperty(child, propertyName, visited)) {
            return match;
        }
    }
    if (QQuickItem *item = qobject_cast<QQuickItem *>(root)) {
        const QList<QQuickItem *> childItems = item->childItems();
        for (QQuickItem *child : childItems) {
            if (QObject *match = findObjectWithProperty(child, propertyName, visited)) {
                return match;
            }
        }
    }

    return nullptr;
}

QObject *findObjectWithProperty(QObject *root, const char *propertyName) {
    QSet<QObject *> visited;
    return findObjectWithProperty(root, propertyName, &visited);
}

QObject *findObjectByName(QObject *root, const QString &objectName, QSet<QObject *> *visited) {
    if (!root || objectName.trimmed().isEmpty()) {
        return nullptr;
    }
    if (visited) {
        if (visited->contains(root)) {
            return nullptr;
        }
        visited->insert(root);
    }
    if (root->objectName() == objectName) {
        return root;
    }
    const QObjectList children = root->children();
    for (QObject *child : children) {
        if (QObject *match = findObjectByName(child, objectName, visited)) {
            return match;
        }
    }
    if (QQuickItem *item = qobject_cast<QQuickItem *>(root)) {
        const QList<QQuickItem *> childItems = item->childItems();
        for (QQuickItem *child : childItems) {
            if (QObject *match = findObjectByName(child, objectName, visited)) {
                return match;
            }
        }
    }
    return nullptr;
}

QObject *findObjectByName(QObject *root, const QString &objectName) {
    QSet<QObject *> visited;
    return findObjectByName(root, objectName, &visited);
}

void appendProviderCardObjects(QObject *root, QList<QObject *> *cards, QSet<QObject *> *visited) {
    if (!root || !cards) {
        return;
    }
    if (visited) {
        if (visited->contains(root)) {
            return;
        }
        visited->insert(root);
    }
    if (root->objectName().startsWith(QStringLiteral("providerCard_")) &&
        !cards->contains(root)) {
        cards->append(root);
    }
    const QObjectList children = root->children();
    for (QObject *child : children) {
        appendProviderCardObjects(child, cards, visited);
    }
    if (QQuickItem *item = qobject_cast<QQuickItem *>(root)) {
        const QList<QQuickItem *> childItems = item->childItems();
        for (QQuickItem *child : childItems) {
            appendProviderCardObjects(child, cards, visited);
        }
    }
}

void appendProviderCardObjects(QObject *root, QList<QObject *> *cards) {
    QSet<QObject *> visited;
    appendProviderCardObjects(root, cards, &visited);
}

QVariantMap providerCardState(QObject *cardObject) {
    if (!cardObject) {
        return {};
    }
    const QString key = cardObject->property("canonicalProviderKeyValue").toString().trimmed();
    const auto childByPrefix = [&](const QString &prefix) -> QObject * {
        return key.isEmpty() ? nullptr : findObjectByName(cardObject, prefix + key);
    };
    const QVariant availableModelsValue = cardObject->property("availableModels");
    const QVariantList availableModels = availableModelsValue.toList();
    QObject *authPanel = childByPrefix(QStringLiteral("providerAuthPanel_"));
    QObject *browserAction = childByPrefix(QStringLiteral("providerOAuthBrowserAction_"));
    QObject *deviceAction = childByPrefix(QStringLiteral("providerOAuthDeviceAction_"));
    QObject *refreshAction = childByPrefix(QStringLiteral("providerOAuthRefreshAction_"));
    QObject *logoutAction = childByPrefix(QStringLiteral("providerOAuthLogoutAction_"));
    QObject *defaultAction = childByPrefix(QStringLiteral("providerDefaultAction_"));
    QObject *modelSyncAction = childByPrefix(QStringLiteral("providerModelSyncAction_"));
    return QVariantMap{
        {QStringLiteral("key"), key},
        {QStringLiteral("width"), realProperty(cardObject, "width")},
        {QStringLiteral("x"), realProperty(cardObject, "x")},
        {QStringLiteral("y"), realProperty(cardObject, "y")},
        {QStringLiteral("usesOAuth"), boolProperty(cardObject, "usesOAuth")},
        {QStringLiteral("authPanelVisible"), boolProperty(authPanel, "visible")},
        {QStringLiteral("browserOAuthActionVisible"), boolProperty(browserAction, "visible")},
        {QStringLiteral("deviceOAuthActionVisible"), boolProperty(deviceAction, "visible")},
        {QStringLiteral("refreshOAuthActionVisible"), boolProperty(refreshAction, "visible")},
        {QStringLiteral("refreshOAuthActionEnabled"), boolProperty(refreshAction, "enabled")},
        {QStringLiteral("logoutOAuthActionVisible"), boolProperty(logoutAction, "visible")},
        {QStringLiteral("logoutOAuthActionEnabled"), boolProperty(logoutAction, "enabled")},
        {QStringLiteral("defaultActionVisible"), boolProperty(defaultAction, "visible")},
        {QStringLiteral("modelSyncActionVisible"), boolProperty(modelSyncAction, "visible")},
        {QStringLiteral("modelSyncActionEnabled"), boolProperty(modelSyncAction, "enabled")},
        {QStringLiteral("selectedModel"), cardObject->property("selectedProviderModel").toString()},
        {QStringLiteral("availableModelCount"), availableModels.size()}
    };
}

QString normalizedSessionFileStem(const QString &sessionKey) {
    QString stem = sessionKey.trimmed();
    if (stem.isEmpty()) {
        stem = QStringLiteral("gui_primary");
    }

    for (QChar &ch : stem) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('-') && ch != QLatin1Char('_')) {
            ch = QLatin1Char('_');
        }
    }
    return stem;
}

QJsonObject parseJsonLine(const QByteArray &line) {
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(trimmed);
    if (document.isNull() || !document.isObject()) {
        return {};
    }
    return document.object();
}

QVariantMap providerCardSnapshot(const QVariantMap &pageSnapshot, const QString &providerKey) {
    const QVariantList cards = pageSnapshot.value(QStringLiteral("cards")).toList();
    for (const QVariant &cardValue : cards) {
        const QVariantMap card = cardValue.toMap();
        if (card.value(QStringLiteral("key")).toString() == providerKey) {
            return card;
        }
    }
    return {};
}

QString compactVariantMapJson(const QVariantMap &map) {
    return QString::fromUtf8(QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact));
}

bool ensureFastNetInitialized(QString *error) {
    static FastNet::ErrorCode result = FastNet::ErrorCode::UnknownError;
    static bool initialized = false;
    if (!initialized) {
        result = FastNet::initialize(2);
        initialized = true;
    }
    if (result == FastNet::ErrorCode::Success ||
        result == FastNet::ErrorCode::AlreadyRunning ||
        FastNet::isInitialized()) {
        if (error) {
            error->clear();
        }
        return true;
    }
    if (error) {
        *error = QStringLiteral("FastNet initialization failed.");
    }
    return false;
}

std::string toStdString(const QString &value) {
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

QString requestPath(const FastNet::HttpRequest &request) {
    const QString path = QString::fromStdString(request.path).trimmed();
    return path.isEmpty() ? QStringLiteral("/") : path;
}

QByteArray requestBody(const FastNet::HttpRequest &request) {
    return QByteArray(request.body.data(), static_cast<int>(request.body.size()));
}

QString reasonPhraseFor(int statusCode) {
    switch (statusCode) {
    case 200:
        return QStringLiteral("OK");
    case 404:
        return QStringLiteral("Not Found");
    default:
        return QStringLiteral("OK");
    }
}

void writeJsonResponse(FastNet::HttpResponse &response, int statusCode, const QJsonObject &payload) {
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    response.statusCode = statusCode;
    response.statusMessage = toStdString(reasonPhraseFor(statusCode));
    response.headers["Content-Type"] = "application/json; charset=utf-8";
    response.headers["Content-Length"] = std::to_string(body.size());
    response.headers["Connection"] = "close";
    response.body.assign(body.constData(), static_cast<size_t>(body.size()));
}

QString lastUserPromptFromChatBody(const QByteArray &body) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }

    QString lastPrompt;
    const QJsonArray messages = document.object().value(QStringLiteral("messages")).toArray();
    for (const QJsonValue &value : messages) {
        const QJsonObject message = value.toObject();
        if (message.value(QStringLiteral("role")).toString() != QStringLiteral("user")) {
            continue;
        }
        const QJsonValue content = message.value(QStringLiteral("content"));
        if (content.isString()) {
            lastPrompt = content.toString();
        } else if (content.isArray()) {
            QStringList parts;
            for (const QJsonValue &partValue : content.toArray()) {
                const QJsonObject part = partValue.toObject();
                if (part.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
                    parts.append(part.value(QStringLiteral("text")).toString());
                }
            }
            if (!parts.isEmpty()) {
                lastPrompt = parts.join(QStringLiteral("\n"));
            }
        }
    }
    return lastPrompt.trimmed();
}

quint16 regressionPort(const QString &label) {
    const QByteArray seed = QStringLiteral("%1:%2")
                                .arg(label, QString::number(QCoreApplication::applicationPid()))
                                .toUtf8();
    const QByteArray digest = QCryptographicHash::hash(seed, QCryptographicHash::Sha1);
    quint32 value = 0;
    for (int index = 0; index < 4 && index < digest.size(); ++index) {
        value = (value << 8) | static_cast<unsigned char>(digest.at(index));
    }
    return static_cast<quint16>(43000 + (value % 15000));
}

} // namespace

GuiRegressionRunner::GuiRegressionRunner(StudioWindow *window,
                                         const GuiRegressionOptions &options,
                                         QObject *parent)
    : QObject(parent),
      m_window(window),
      m_bridge(window ? window->bridge() : nullptr),
      m_options(options) {
    m_timeoutTimer.setSingleShot(true);
    m_heartbeatTimer.setSingleShot(false);
    connect(&m_timeoutTimer, &QTimer::timeout, this, &GuiRegressionRunner::handleTimeout);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &GuiRegressionRunner::handleHeartbeat);
}

GuiRegressionRunner::~GuiRegressionRunner() {
    stopMockServers();
}

QJsonObject GuiRegressionRunner::result() const {
    return m_result;
}

void GuiRegressionRunner::start() {
    if (m_started) {
        return;
    }
    m_started = true;

    if (!m_window || !m_bridge) {
        fail(QStringLiteral("gui regression runner could not access StudioWindow/StudioBridge"));
        return;
    }
    const QString caseId = m_options.caseId.trimmed().toLower();
    if (caseId != QStringLiteral("runtime-workbench") &&
        caseId != QStringLiteral("runtime-page") &&
        caseId != QStringLiteral("provider-models") &&
        caseId != QStringLiteral("oauth-status") &&
        caseId != QStringLiteral("control-templates") &&
        caseId != QStringLiteral("daemon-provider-config") &&
        caseId != QStringLiteral("remote-provider-config") &&
        caseId != QStringLiteral("extensions-catalog") &&
        caseId != QStringLiteral("chat-provider")) {
        fail(QStringLiteral("unsupported gui regression case: %1").arg(m_options.caseId));
        return;
    }

    connect(m_bridge, &StudioBridge::startupChanged, this, &GuiRegressionRunner::handleStartupChanged);
    connect(m_bridge, &StudioBridge::statusChanged, this, &GuiRegressionRunner::handleStatusChanged);
    connect(m_bridge, &StudioBridge::busyChanged, this, &GuiRegressionRunner::maybeAdvance);
    connect(m_bridge, &StudioBridge::chatHistoryChanged, this, &GuiRegressionRunner::handleChatHistoryChanged);
    connect(m_bridge, &StudioBridge::pluginsChanged, this, &GuiRegressionRunner::handleExtensionStateChanged);
    connect(m_bridge, &StudioBridge::skillsChanged, this, &GuiRegressionRunner::handleExtensionStateChanged);
    connect(m_bridge,
            &StudioBridge::extensionCatalogChanged,
            this,
            &GuiRegressionRunner::handleExtensionStateChanged);
    connect(m_bridge,
            &StudioBridge::delegationRoutePreviewChanged,
            this,
            &GuiRegressionRunner::handlePreviewChanged);
    connect(m_bridge, &StudioBridge::saveFinished, this, &GuiRegressionRunner::handleSaveFinished);
    connect(m_bridge, &StudioBridge::toastRequested, this, &GuiRegressionRunner::handleToast);

    m_phase = Phase::WaitingForStartup;
    m_runClock.start();
    m_timeoutTimer.start(qMax(1000, m_options.timeoutMs));
    maybeAdvance();
}

void GuiRegressionRunner::handleHeartbeat() {
    if (!m_heartbeatStarted) {
        m_heartbeatStarted = true;
        m_heartbeatClock.start();
        return;
    }

    const int gapMs = static_cast<int>(m_heartbeatClock.restart());
    if (gapMs > m_maxUiGapMs) {
        m_maxUiGapMs = gapMs;
    }
    const bool savePhase = m_phase == Phase::WaitingForFixtureSave ||
                           m_phase == Phase::WaitingForRuntimeConfigSave;
    if (!savePhase && gapMs > m_options.maxUiGapMs) {
        fail(QStringLiteral("desktop event loop stalled for %1 ms").arg(gapMs));
    }
}

void GuiRegressionRunner::handleTimeout() {
    fail(QStringLiteral("gui regression timed out after %1 ms").arg(m_options.timeoutMs));
}

void GuiRegressionRunner::handleStartupChanged() {
    maybeAdvance();
}

void GuiRegressionRunner::handleStatusChanged() {
    ++m_extensionStateRevision;
    maybeAdvance();
}

void GuiRegressionRunner::handleChatHistoryChanged() {
    maybeAdvance();
}

void GuiRegressionRunner::handleExtensionStateChanged() {
    ++m_extensionStateRevision;
    maybeAdvance();
}

void GuiRegressionRunner::handlePreviewChanged() {
    if (!m_waitingForPreview || !m_bridge) {
        return;
    }

    const QVariantMap preview = m_bridge->delegationRoutePreview();
    const bool pending = preview.value(QStringLiteral("pending")).toBool();
    if (pending) {
        m_previewSawPending = true;
        return;
    }

    if (!m_previewSawPending) {
        return;
    }

    completePreviewWait(preview);
}

void GuiRegressionRunner::handleSaveFinished(bool success) {
    if (m_finished) {
        return;
    }

    if (m_phase == Phase::WaitingForFixtureSave) {
        if (!success) {
            fail(QStringLiteral("gui regression fixture config save failed"));
            return;
        }
        if (caseNeedsProviderMock() && !m_providerServer && !startProviderMockServer()) {
            return;
        }
        m_caseFixturePrepared = true;
        m_phase = Phase::WaitingForWorkspace;
        QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
        return;
    }

    if (m_phase == Phase::WaitingForRuntimeConfigSave) {
        if (!success) {
            fail(QStringLiteral("runtime provider/config regression save failed"));
            return;
        }
        m_phase = Phase::WaitingForRuntimeProviderConfig;
        m_runtimeProviderConfigStep = 2;
        QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
    }
}

void GuiRegressionRunner::handleToast(const QString &title, const QString &body, const QString &tone) {
    QJsonObject toast{
        {QStringLiteral("title"), title},
        {QStringLiteral("body"), body},
        {QStringLiteral("tone"), tone},
        {QStringLiteral("elapsedMs"), static_cast<int>(m_runClock.isValid() ? m_runClock.elapsed() : 0)}
    };
    m_toasts.append(toast);

    if (tone.trimmed().compare(QStringLiteral("warning"), Qt::CaseInsensitive) != 0) {
        if (m_phase == Phase::WaitingForChatResponse) {
            maybeAdvance();
        }
        return;
    }

    const QString combined = (title + QLatin1Char(' ') + body).trimmed();
    if (combined.contains(QStringLiteral("初始化失败")) ||
        combined.contains(QStringLiteral("runtime facade is not initialized"), Qt::CaseInsensitive)) {
        fail(combined);
    }

    maybeAdvance();
}

void GuiRegressionRunner::performNextPageSwitch() {
    if (m_phase != Phase::SwitchingPages) {
        return;
    }

    const QString targetPage = m_onSecurityPage ? QStringLiteral("runtime") : QStringLiteral("security");
    if (!setCurrentPage(targetPage)) {
        fail(QStringLiteral("failed to switch desktop page to %1").arg(targetPage));
        return;
    }

    recordPageSwitch(targetPage);

    if (targetPage == QStringLiteral("runtime")) {
        m_onSecurityPage = false;
        ++m_completedRounds;
        if (m_completedRounds >= m_options.switchCount) {
            QTimer::singleShot(qMax(10, m_options.pageSettleMs),
                               this,
                               &GuiRegressionRunner::beginRefreshPreview);
            return;
        }
    } else {
        m_onSecurityPage = true;
    }

    QTimer::singleShot(qMax(10, m_options.pageSettleMs),
                       this,
                       &GuiRegressionRunner::performNextPageSwitch);
}

void GuiRegressionRunner::maybeAdvance() {
    if (m_advancing) {
        QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
        return;
    }
    QScopedValueRollback<bool> advancing(m_advancing, true);

    if (m_finished || !m_bridge) {
        return;
    }

    switch (m_phase) {
    case Phase::WaitingForStartup:
        if (!m_window || !m_window->ready() || !m_bridge->startupComplete()) {
            return;
        }
        if (!m_window->rootObject()) {
            fail(QStringLiteral("desktop root object was not created"));
            return;
        }
        m_rootObject = resolvePageObject();
        if (!m_rootObject) {
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }
        if (!m_heartbeatTimer.isActive()) {
            m_heartbeatTimer.start(qMax(20, m_options.heartbeatIntervalMs));
        }
        m_startupMs = static_cast<int>(m_runClock.elapsed());
        beginWorkspaceInitialization();
        return;
    case Phase::WaitingForWorkspace:
        if (!workspaceReady()) {
            return;
        }
        if (!m_caseFixturePrepared) {
            if (caseNeedsFixture()) {
                if (!beginCaseFixture()) {
                    return;
                }
                return;
            }
            m_caseFixturePrepared = true;
        }
        if (m_options.caseId == QStringLiteral("provider-models")) {
            beginProviderModelsPage();
            return;
        }
        if (m_options.caseId == QStringLiteral("oauth-status")) {
            beginOAuthStatus();
            return;
        }
        if (m_options.caseId == QStringLiteral("control-templates")) {
            beginControlTemplates();
            return;
        }
        if (m_options.caseId == QStringLiteral("daemon-provider-config") ||
            m_options.caseId == QStringLiteral("remote-provider-config")) {
            beginRuntimeProviderConfig();
            return;
        }
        if (m_options.caseId == QStringLiteral("chat-provider")) {
            beginChatProvider();
            return;
        }
        if (m_options.caseId == QStringLiteral("extensions-catalog")) {
            beginExtensionsCatalog();
            return;
        }
        beginSeedPreview();
        return;
    case Phase::WaitingForProviderModelsPage: {
        if (currentPage() != QStringLiteral("providers") &&
            !setCurrentPage(QStringLiteral("providers"))) {
            fail(QStringLiteral("failed to enter provider models page"));
            return;
        }

        QObject *pageObject = resolvePageObject();
        const QVariantMap snapshot = providerPageSnapshot();
        if (snapshot.isEmpty()) {
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }
        const int gridColumns = snapshot.value(QStringLiteral("columns")).toInt();
        const qreal gridWidth = snapshot.value(QStringLiteral("gridWidth")).toReal();
        const QVariantList cards = snapshot.value(QStringLiteral("cards")).toList();
        m_providerPageSummary.insert(QStringLiteral("cardSnapshots"),
                                     QJsonArray::fromVariantList(cards));
        m_providerPageSummary.insert(QStringLiteral("gridColumns"), gridColumns);
        m_providerPageSummary.insert(QStringLiteral("gridWidth"), gridWidth);
        if (m_window) {
            m_providerPageSummary.insert(QStringLiteral("windowWidth"), m_window->width());
            m_providerPageSummary.insert(QStringLiteral("windowHeight"), m_window->height());
        }

        if (m_providerModelsStep == 0) {
            if (gridColumns != 2) {
                if (m_window) {
                    const int targetWidth = qMax(1480, m_window->minimumWidth());
                    if (m_window->width() < targetWidth) {
                        m_window->resize(targetWidth,
                                         qMax(m_window->height(), m_window->minimumHeight()));
                        QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
                        return;
                    }
                }
                if ((static_cast<int>(m_runClock.elapsed()) - m_providerModelsStartedAtMs) < 5000) {
                    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
                    return;
                }
                fail(QStringLiteral("provider models page should use a two-column layout at the default desktop width"));
                return;
            }

            if (gridWidth <= 0.0 || cards.size() < 2) {
                if ((static_cast<int>(m_runClock.elapsed()) - m_providerModelsStartedAtMs) < 5000) {
                    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
                    return;
                }
                fail(QStringLiteral("provider models page did not expose enough provider cards for a two-column check"));
                return;
            }

            const QVariantMap firstCard = cards.at(0).toMap();
            const QVariantMap secondCard = cards.at(1).toMap();
            const qreal firstWidth = firstCard.value(QStringLiteral("width")).toReal();
            const qreal secondWidth = secondCard.value(QStringLiteral("width")).toReal();
            if (firstWidth <= 0.0 || secondWidth <= 0.0) {
                QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
                return;
            }

            const qreal expectedWidth =
                (gridWidth - (18.0 * qMax(0, gridColumns - 1))) / qMax(1, gridColumns);
            const qreal widthDelta = qAbs(firstWidth - secondWidth);
            const qreal firstExpectedDelta = qAbs(firstWidth - expectedWidth);
            const qreal secondExpectedDelta = qAbs(secondWidth - expectedWidth);
            if (widthDelta > 3.0 || firstExpectedDelta > 3.0 || secondExpectedDelta > 3.0) {
                fail(QStringLiteral("provider models page columns were not equal width"));
                return;
            }

            m_providerPageSummary = QJsonObject{
                {QStringLiteral("providerId"), m_chatProviderId},
                {QStringLiteral("desktopSnapshot"), QJsonObject::fromVariantMap(snapshot)},
                {QStringLiteral("desktopWidthDelta"), widthDelta}
            };

            if (pageObject) {
                pageObject->setProperty("selectedProviderPanelKey", QStringLiteral("openaiCodex"));
            }
            m_providerModelsStep = 1;
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }

        if (m_providerModelsStep == 1) {
            const QVariantMap oauthCard = providerCardSnapshot(snapshot, QStringLiteral("openaiCodex"));
            if (oauthCard.isEmpty()) {
                QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
                return;
            }
            if (!oauthCard.value(QStringLiteral("authPanelVisible")).toBool() ||
                !oauthCard.value(QStringLiteral("browserOAuthActionVisible")).toBool() ||
                !oauthCard.value(QStringLiteral("deviceOAuthActionVisible")).toBool() ||
                !oauthCard.value(QStringLiteral("refreshOAuthActionVisible")).toBool() ||
                !oauthCard.value(QStringLiteral("logoutOAuthActionVisible")).toBool()) {
                fail(QStringLiteral("provider models page did not expose the expected OAuth controls"));
                return;
            }

            m_providerPageSummary.insert(QStringLiteral("oauthProviderCard"),
                                         QJsonObject::fromVariantMap(oauthCard));
            if (pageObject) {
                pageObject->setProperty("selectedProviderPanelKey", m_chatProviderId);
            }
            m_providerModelsStep = 2;
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }

        if (m_providerModelsStep == 2) {
            const QVariantMap syncedProviderCardBefore = providerCardSnapshot(snapshot, m_chatProviderId);
            if (syncedProviderCardBefore.isEmpty()) {
                QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
                return;
            }

            const QVariantList syncedModels = m_bridge->fetchProviderModels(m_chatProviderId,
                                                                            m_bridge->config());
            if (syncedModels.isEmpty()) {
                fail(QStringLiteral("provider models page could not sync models for provider '%1'")
                         .arg(m_chatProviderId));
                return;
            }

            const QVariant defaultResult = invokeRootMethod("setProviderAsDefault", m_chatProviderId);
            const QVariantMap defaultSelection = defaultResult.toMap();
            const QString selectedProvider = defaultSelection.value(QStringLiteral("provider")).toString().trimmed();
            const QString selectedModel = defaultSelection.value(QStringLiteral("model")).toString().trimmed();
            if (selectedProvider != m_chatProviderId || selectedModel.isEmpty()) {
                fail(QStringLiteral("provider models page could not select the synced provider as default"));
                return;
            }

            const QVariantMap syncedSnapshot = providerPageSnapshot();
            const QVariantMap syncedProviderCard = providerCardSnapshot(syncedSnapshot, m_chatProviderId);
            if (syncedProviderCard.isEmpty()) {
                fail(QStringLiteral("provider models page did not expose the synced provider card"));
                return;
            }
            if (!syncedProviderCard.value(QStringLiteral("modelSyncActionVisible")).toBool() ||
                !syncedProviderCard.value(QStringLiteral("modelSyncActionEnabled")).toBool() ||
                syncedProviderCard.value(QStringLiteral("availableModelCount")).toInt() < 1 ||
                syncedProviderCard.value(QStringLiteral("selectedModel")).toString().trimmed().isEmpty()) {
                fail(QStringLiteral("provider models page did not persist the synced model catalog"));
                return;
            }

            m_providerPageSummary.insert(QStringLiteral("syncModelCount"), syncedModels.size());
            m_providerPageSummary.insert(QStringLiteral("syncedProviderCard"),
                                         QJsonObject::fromVariantMap(syncedProviderCard));
            m_providerPageSummary.insert(QStringLiteral("defaultSelection"),
                                         QJsonObject::fromVariantMap(defaultSelection));
            if (m_window) {
                m_window->resize(m_window->minimumWidth(),
                                 qMax(m_window->height(), m_window->minimumHeight()));
            }
            m_providerModelsStep = 3;
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }

        const int narrowColumns = snapshot.value(QStringLiteral("columns")).toInt();
        if (narrowColumns != 1) {
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }

        m_providerPageSummary.insert(QStringLiteral("completed"), true);
        m_providerPageSummary.insert(QStringLiteral("narrowSnapshot"), QJsonObject::fromVariantMap(snapshot));
        m_providerPageSummary.insert(QStringLiteral("latencyMs"),
                                     static_cast<int>(m_runClock.elapsed()) - m_providerModelsStartedAtMs);
        finishSuccess();
        return;
    }
    case Phase::WaitingForRuntimePage: {
        if (currentPage() != QStringLiteral("runtime") &&
            !setCurrentPage(QStringLiteral("runtime"))) {
            fail(QStringLiteral("failed to return to runtime page before runtime snapshot"));
            return;
        }

        QObject *runtimePageObject = resolveRuntimePageObject();
        if (!runtimePageObject) {
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }

        QVariant prepared;
        if (!QMetaObject::invokeMethod(runtimePageObject,
                                       "prepareRuntimeRegressionSnapshot",
                                       Q_RETURN_ARG(QVariant, prepared))) {
            fail(QStringLiteral("failed to prepare runtime page regression snapshot"));
            return;
        }
        if (!prepared.toBool()) {
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }

        const QVariantMap snapshot = runtimePageSnapshot();
        if (snapshot.isEmpty()) {
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }

        m_runtimePageSummary = QJsonObject::fromVariantMap(snapshot);
        m_runtimePageSummary.insert(QStringLiteral("latencyMs"),
                                    static_cast<int>(m_runClock.elapsed()) - m_runtimePageStartedAtMs);
        if (m_window) {
            m_runtimePageSummary.insert(QStringLiteral("windowWidth"), m_window->width());
            m_runtimePageSummary.insert(QStringLiteral("windowHeight"), m_window->height());
        }
        finishSuccess();
        return;
    }
    case Phase::WaitingForOAuthStatus: {
        const auto writeOauthProgress = [&](const QString &step) {
            m_oauthSummary.insert(QStringLiteral("step"), step);
            QString ignored;
            writeResultDocument(buildResultDocument(
                                    false,
                                    QStringLiteral("oauth-status progress: %1").arg(step)),
                                &ignored);
        };

        if (currentPage() != QStringLiteral("providers") &&
            !setCurrentPage(QStringLiteral("providers"))) {
            fail(QStringLiteral("failed to enter providers page for OAuth regression"));
            return;
        }

        QObject *pageObject = resolvePageObject();
        if (pageObject) {
            pageObject->setProperty("selectedProviderPanelKey", QStringLiteral("openaiCodex"));
        }

        const QVariantMap snapshot = providerPageSnapshot();
        const QVariantMap oauthCard = providerCardSnapshot(snapshot, QStringLiteral("openaiCodex"));
        if (oauthCard.isEmpty()) {
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }

        if (m_oauthStep == 0) {
            writeOauthProgress(QStringLiteral("provider-card"));
            if (!oauthCard.value(QStringLiteral("authPanelVisible")).toBool() ||
                !oauthCard.value(QStringLiteral("browserOAuthActionVisible")).toBool() ||
                !oauthCard.value(QStringLiteral("deviceOAuthActionVisible")).toBool()) {
                fail(QStringLiteral("OAuth status regression did not expose the expected provider controls"));
                return;
            }

            m_oauthSummary.insert(QStringLiteral("providerId"), QStringLiteral("openai_codex"));
            m_oauthSummary.insert(QStringLiteral("oauthProviderCard"),
                                  QJsonObject::fromVariantMap(oauthCard));
            m_oauthStep = 1;
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }

        if (m_oauthStep == 1) {
            writeOauthProgress(QStringLiteral("config-before"));
            m_oauthConfigBeforeJson = compactVariantMapJson(m_bridge->config());
            writeOauthProgress(QStringLiteral("providerAuthStatus-call"));
            const QVariantMap status = m_bridge->providerAuthStatus(QStringLiteral("openai_codex"));
            if (status.value(QStringLiteral("error")).toString().contains(QStringLiteral("backend is busy"),
                                                                           Qt::CaseInsensitive)) {
                QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
                return;
            }
            writeOauthProgress(QStringLiteral("config-after"));
            const QString afterStatusConfigJson = compactVariantMapJson(m_bridge->config());
            if (!status.value(QStringLiteral("ok")).toBool() ||
                status.value(QStringLiteral("providerId")).toString() != QStringLiteral("openai_codex") ||
                m_oauthConfigBeforeJson != afterStatusConfigJson) {
                fail(QStringLiteral("providerAuthStatus did not return a side-effect-free OpenAI Codex status"));
                return;
            }
            m_oauthSummary.insert(QStringLiteral("status"), QJsonObject::fromVariantMap(status));
            m_oauthStep = 2;
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }

        if (m_oauthStep == 2) {
            writeOauthProgress(QStringLiteral("unsupported-device"));
            const QVariantMap unsupportedDevice =
                m_bridge->beginProviderOAuthWithConfig(QStringLiteral("openai"),
                                                       QStringLiteral("device"),
                                                       m_bridge->config());
            if (unsupportedDevice.value(QStringLiteral("error")).toString().contains(QStringLiteral("backend is busy"),
                                                                                     Qt::CaseInsensitive)) {
                QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
                return;
            }
            if (unsupportedDevice.value(QStringLiteral("ok")).toBool()) {
                fail(QStringLiteral("OAuth status regression expected unsupported device flow to fail cleanly"));
                return;
            }
            m_oauthSummary.insert(QStringLiteral("unsupportedDeviceError"),
                                  unsupportedDevice.value(QStringLiteral("error")).toString());
            m_oauthStep = 3;
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }

        if (m_oauthStep == 3) {
            writeOauthProgress(QStringLiteral("unsupported-browser"));
            const QVariantMap unsupportedBrowser =
                m_bridge->beginProviderOAuthWithConfig(QStringLiteral("github_copilot"),
                                                       QStringLiteral("browser"),
                                                       m_bridge->config());
            if (unsupportedBrowser.value(QStringLiteral("error")).toString().contains(QStringLiteral("backend is busy"),
                                                                                      Qt::CaseInsensitive)) {
                QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
                return;
            }
            if (unsupportedBrowser.value(QStringLiteral("ok")).toBool()) {
                fail(QStringLiteral("OAuth status regression expected unsupported browser flow to fail cleanly"));
                return;
            }
            m_oauthSummary.insert(QStringLiteral("unsupportedBrowserError"),
                                  unsupportedBrowser.value(QStringLiteral("error")).toString());
            m_oauthStep = 4;
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }

        writeOauthProgress(QStringLiteral("unsupported-refresh"));
        const QVariantMap unsupportedRefresh = m_bridge->refreshProviderOAuth(QStringLiteral("openai"));
        if (unsupportedRefresh.value(QStringLiteral("error")).toString().contains(QStringLiteral("backend is busy"),
                                                                                  Qt::CaseInsensitive)) {
            QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
            return;
        }
        if (unsupportedRefresh.value(QStringLiteral("ok")).toBool()) {
            fail(QStringLiteral("OAuth status regression expected unsupported refresh to fail cleanly"));
            return;
        }
        m_oauthSummary.insert(QStringLiteral("unsupportedRefreshError"),
                              unsupportedRefresh.value(QStringLiteral("error")).toString());
        m_oauthSummary.insert(QStringLiteral("completed"), true);
        m_oauthSummary.insert(QStringLiteral("step"), QStringLiteral("completed"));
        finishSuccess();
        return;
    }
    case Phase::WaitingForControlTemplates: {
        if (currentPage() != QStringLiteral("runtime") &&
            !setCurrentPage(QStringLiteral("runtime"))) {
            fail(QStringLiteral("failed to enter runtime page for control template regression"));
            return;
        }

        const QVariantMap emptyPush = m_bridge->pushDelegationTemplatesToControl(QVariantList(), false);
        if (emptyPush.value(QStringLiteral("ok")).toBool()) {
            fail(QStringLiteral("control template regression expected empty push to fail"));
            return;
        }

        const QVariantMap mergeResult = m_bridge->pullDelegationTemplatesFromControl(false);
        const QVariantMap mergedConfig = m_bridge->config();
        const QVariantList mergedTemplates =
            mergedConfig.value(QStringLiteral("memory")).toMap()
                .value(QStringLiteral("delegationTemplates")).toList();
        if (!mergeResult.value(QStringLiteral("ok")).toBool() ||
            mergeResult.value(QStringLiteral("pulledCount")).toInt() != 1 ||
            mergedTemplates.size() < 2) {
            fail(QStringLiteral("control template regression did not merge incoming templates"));
            return;
        }

        const QVariantMap replaceResult = m_bridge->pullDelegationTemplatesFromControl(true);
        const QVariantMap replacedConfig = m_bridge->config();
        const QVariantList replacedTemplates =
            replacedConfig.value(QStringLiteral("memory")).toMap()
                .value(QStringLiteral("delegationTemplates")).toList();
        const QVariantMap firstTemplate = replacedTemplates.isEmpty()
            ? QVariantMap{}
            : replacedTemplates.first().toMap();
        if (!replaceResult.value(QStringLiteral("ok")).toBool() ||
            replaceResult.value(QStringLiteral("pulledCount")).toInt() != 1 ||
            replacedTemplates.size() != 1 ||
            firstTemplate.value(QStringLiteral("id")).toString() != QStringLiteral("incoming-template")) {
            fail(QStringLiteral("control template regression did not replace templates from control plane"));
            return;
        }

        m_controlSummary = QJsonObject{
            {QStringLiteral("completed"), true},
            {QStringLiteral("controlPlaneUrl"),
             QStringLiteral("http://127.0.0.1:%1").arg(m_controlServerPort)},
            {QStringLiteral("emptyPush"), QJsonObject::fromVariantMap(emptyPush)},
            {QStringLiteral("mergePulledCount"), mergeResult.value(QStringLiteral("pulledCount")).toInt()},
            {QStringLiteral("replacePulledCount"), replaceResult.value(QStringLiteral("pulledCount")).toInt()},
            {QStringLiteral("mergedTemplateCount"), mergedTemplates.size()},
            {QStringLiteral("replacedTemplateCount"), replacedTemplates.size()}
        };
        finishSuccess();
        return;
    }
    case Phase::WaitingForRuntimeProviderConfig: {
        const QString targetMode = runtimeProviderConfigMode();
        const QVariantMap status = m_bridge->status();
        if (status.value(QStringLiteral("runtimeMode")).toString() != targetMode ||
            status.value(QStringLiteral("studioBackend")).toString() != QStringLiteral("remote") ||
            status.value(QStringLiteral("studioBackendTransport")).toString() != targetMode ||
            status.value(QStringLiteral("backendFallback")).toBool()) {
            if ((static_cast<int>(m_runClock.elapsed()) - m_runtimeProviderConfigStartedAtMs) < 12000) {
                QTimer::singleShot(qMax(30, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
                return;
            }
            fail(QStringLiteral("runtime provider/config regression did not switch to %1 backend").arg(targetMode));
            return;
        }

        if (m_runtimeProviderConfigStep == 0) {
            const QVariantList models = m_bridge->fetchProviderModels(QStringLiteral("custom"), m_bridge->config());
            if (models.isEmpty()) {
                fail(QStringLiteral("runtime provider/config regression could not sync custom provider models"));
                return;
            }
            m_runtimeProviderConfigSummary = QJsonObject{
                {QStringLiteral("mode"), targetMode},
                {QStringLiteral("initialStatus"), QJsonObject::fromVariantMap(status)},
                {QStringLiteral("modelCount"), models.size()}
            };

            config::Config draft =
                config::Config::fromJson(QJsonObject::fromVariantMap(m_bridge->config()));
            draft.agentDefaults.model =
                QStringLiteral("gui-regression-%1-saved").arg(targetMode);
            m_phase = Phase::WaitingForRuntimeConfigSave;
            m_runtimeProviderConfigStep = 1;
            if (!m_bridge->saveConfig(draft.toJson().toVariantMap())) {
                fail(QStringLiteral("runtime provider/config regression could not start save"));
            }
            return;
        }

        const QVariantMap savedStatus = m_bridge->status();
        const QVariantMap agents = m_bridge->config().value(QStringLiteral("agents")).toMap();
        const QVariantMap defaults = agents.value(QStringLiteral("defaults")).toMap();
        const QString savedModel = defaults.value(QStringLiteral("model")).toString();
        if (savedModel != QStringLiteral("gui-regression-%1-saved").arg(targetMode) ||
            savedStatus.value(QStringLiteral("runtimeMode")).toString() != targetMode ||
            savedStatus.value(QStringLiteral("studioBackendTransport")).toString() != targetMode) {
            fail(QStringLiteral("runtime provider/config regression did not preserve %1 after remote save").arg(targetMode));
            return;
        }

        m_runtimeProviderConfigSummary.insert(QStringLiteral("completed"), true);
        m_runtimeProviderConfigSummary.insert(QStringLiteral("savedModel"), savedModel);
        m_runtimeProviderConfigSummary.insert(QStringLiteral("finalStatus"),
                                              QJsonObject::fromVariantMap(savedStatus));
        m_runtimeProviderConfigSummary.insert(QStringLiteral("latencyMs"),
                                              static_cast<int>(m_runClock.elapsed()) -
                                                  m_runtimeProviderConfigStartedAtMs);
        finishSuccess();
        return;
    }
    case Phase::WaitingForChatResponse: {
        if (currentPage() != QStringLiteral("chat") &&
            !setCurrentPage(QStringLiteral("chat"))) {
            fail(QStringLiteral("failed to enter chat page for provider regression"));
            return;
        }

        const QVariantList history = m_bridge->chatHistory();
        const QJsonObject fallbackSummary = chatSummaryFromSessionFallback();
        if (history.size() < (m_initialChatHistoryCount + 2)) {
            if (!fallbackSummary.isEmpty()) {
                m_chatSummary = fallbackSummary;
                finishSuccess();
                return;
            }
            QTimer::singleShot(qMax(10, m_options.pageSettleMs),
                               this,
                               &GuiRegressionRunner::maybeAdvance);
            return;
        }

        const QVariantMap userEntry = history.at(history.size() - 2).toMap();
        const QVariantMap assistantEntry = history.at(history.size() - 1).toMap();
        if (userEntry.value(QStringLiteral("role")).toString() != QStringLiteral("user")) {
            if (!fallbackSummary.isEmpty()) {
                m_chatSummary = fallbackSummary;
                finishSuccess();
                return;
            }
            fail(QStringLiteral("chat provider regression did not preserve the user chat entry"));
            return;
        }
        if (assistantEntry.value(QStringLiteral("role")).toString() != QStringLiteral("assistant")) {
            if (!fallbackSummary.isEmpty()) {
                m_chatSummary = fallbackSummary;
                finishSuccess();
                return;
            }
            fail(QStringLiteral("chat provider regression did not return an assistant response"));
            return;
        }
        if (assistantEntry.value(QStringLiteral("pending")).toBool()) {
            QTimer::singleShot(qMax(10, m_options.pageSettleMs),
                               this,
                               &GuiRegressionRunner::maybeAdvance);
            return;
        }

        const QString assistantContent = assistantEntry.value(QStringLiteral("content")).toString();
        const QString assistantMeta = assistantEntry.value(QStringLiteral("meta")).toString();
        if (assistantEntry.value(QStringLiteral("error")).toBool() ||
            assistantContent.trimmed().isEmpty() ||
            !assistantContent.contains(QStringLiteral("GUI provider regression ok:"), Qt::CaseInsensitive) ||
            !assistantContent.contains(m_chatPrompt, Qt::CaseInsensitive) ||
            !assistantMeta.contains(m_chatProviderId, Qt::CaseInsensitive) ||
            !assistantMeta.contains(m_chatModel, Qt::CaseInsensitive)) {
            if (!fallbackSummary.isEmpty()) {
                m_chatSummary = fallbackSummary;
                finishSuccess();
                return;
            }
            fail(QStringLiteral("chat provider regression returned an unexpected assistant payload"));
            return;
        }

        const QVariantList trace = assistantEntry.value(QStringLiteral("trace")).toList();
        if (trace.isEmpty()) {
            if (!fallbackSummary.isEmpty()) {
                m_chatSummary = fallbackSummary;
                finishSuccess();
                return;
            }
            fail(QStringLiteral("chat provider regression did not expose any assistant trace events"));
            return;
        }

        bool sawTaskCompleted = false;
        QJsonArray traceMessages;
        for (const QVariant &traceValue : trace) {
            const QVariantMap event = traceValue.toMap();
            const QString category = event.value(QStringLiteral("category")).toString();
            const QString message = event.value(QStringLiteral("message")).toString();
            traceMessages.append(QStringLiteral("%1:%2").arg(category, message));
            if (category == QStringLiteral("task") &&
                message == QStringLiteral("Task completed")) {
                sawTaskCompleted = true;
            }
        }
        if (!sawTaskCompleted) {
            if (!fallbackSummary.isEmpty()) {
                m_chatSummary = fallbackSummary;
                finishSuccess();
                return;
            }
            fail(QStringLiteral("chat provider regression trace did not include a completed task event"));
            return;
        }

        const bool sawReplyToast = findChatReplyToast();
        m_chatSummary = QJsonObject{
            {QStringLiteral("completed"), true},
            {QStringLiteral("providerId"), m_chatProviderId},
            {QStringLiteral("model"), m_chatModel},
            {QStringLiteral("sessionKey"), m_chatSessionKey},
            {QStringLiteral("prompt"), m_chatPrompt},
            {QStringLiteral("replyToastSeen"), sawReplyToast},
            {QStringLiteral("latencyMs"), static_cast<int>(m_runClock.elapsed()) - m_chatStartedAtMs},
            {QStringLiteral("chatHistoryCount"), history.size()},
            {QStringLiteral("traceCount"), trace.size()},
            {QStringLiteral("assistantMeta"), assistantMeta},
            {QStringLiteral("assistantContentPreview"), assistantContent.left(220)},
            {QStringLiteral("traceMessages"), traceMessages},
            {QStringLiteral("completionSource"), QStringLiteral("live-chat-history")}
        };
        finishSuccess();
        return;
    }
    case Phase::WaitingForExtensionCatalog: {
        if (currentPage() != QStringLiteral("extensions") &&
            !setCurrentPage(QStringLiteral("extensions"))) {
            fail(QStringLiteral("failed to enter extensions page for catalog install"));
            return;
        }

        if (m_initialPluginCount < 0) {
            m_initialPluginCount = statusCount(QStringLiteral("pluginCount"));
        }
        if (m_initialSkillCount < 0) {
            m_initialSkillCount = statusCount(QStringLiteral("skillCount"));
        }
        if (m_initialMcpServerCount < 0) {
            m_initialMcpServerCount = statusCount(QStringLiteral("mcpServerCount"));
        }

        const QVariantList entries = m_bridge->extensionCatalog();
        if (entries.isEmpty()) {
            return;
        }

        const QVariantMap pluginEntry = catalogEntry(QStringLiteral("plugin.release_notes"));
        const QVariantMap skillEntry = catalogEntry(QStringLiteral("skill.code_review"));
        const QVariantMap mcpEntry = catalogEntry(QStringLiteral("mcp.filesystem"));
        if (pluginEntry.isEmpty() || skillEntry.isEmpty() || mcpEntry.isEmpty()) {
            fail(QStringLiteral("extensions catalog did not expose expected plugin/skill/mcp entries"));
            return;
        }

        beginPluginInstall();
        return;
    }
    case Phase::WaitingForPluginInstall: {
        if (m_extensionStateRevision <= m_waitStartRevision) {
            return;
        }

        const QVariantMap pluginEntry = catalogEntry(QStringLiteral("plugin.release_notes"));
        const QVariantMap pluginRecord = extensionRecord(m_bridge->plugins(), QStringLiteral("release-notes"));
        QJsonArray files;
        if (!pluginEntry.value(QStringLiteral("installed")).toBool() ||
            pluginRecord.isEmpty() ||
            statusCount(QStringLiteral("pluginCount")) < (m_initialPluginCount + 1) ||
            !hasInstallToastSince(m_installToastStartIndex, QStringLiteral("plugins/release-notes")) ||
            !allFilesExist(pluginInstallFiles(), &files)) {
            return;
        }

        m_pluginInstall = QJsonObject{
            {QStringLiteral("completed"), true},
            {QStringLiteral("catalogId"), QStringLiteral("plugin.release_notes")},
            {QStringLiteral("id"), QStringLiteral("release-notes")},
            {QStringLiteral("target"), pluginEntry.value(QStringLiteral("target")).toString()},
            {QStringLiteral("latencyMs"), static_cast<int>(m_runClock.elapsed()) - m_actionStartedAtMs},
            {QStringLiteral("toolName"), pluginRecord.value(QStringLiteral("toolName")).toString()},
            {QStringLiteral("executorType"), pluginRecord.value(QStringLiteral("executorType")).toString()},
            {QStringLiteral("files"), files}
        };
        beginSkillInstall();
        return;
    }
    case Phase::WaitingForSkillInstall: {
        if (m_extensionStateRevision <= m_waitStartRevision) {
            return;
        }

        const QVariantMap skillEntry = catalogEntry(QStringLiteral("skill.code_review"));
        const QVariantMap skillRecord = extensionRecord(m_bridge->skills(), QStringLiteral("code-review"));
        QJsonArray files;
        if (!skillEntry.value(QStringLiteral("installed")).toBool() ||
            skillRecord.isEmpty() ||
            statusCount(QStringLiteral("skillCount")) < (m_initialSkillCount + 1) ||
            !hasInstallToastSince(m_installToastStartIndex, QStringLiteral("skills/code-review")) ||
            !allFilesExist(skillInstallFiles(), &files)) {
            return;
        }

        m_skillInstall = QJsonObject{
            {QStringLiteral("completed"), true},
            {QStringLiteral("catalogId"), QStringLiteral("skill.code_review")},
            {QStringLiteral("id"), QStringLiteral("code-review")},
            {QStringLiteral("target"), skillEntry.value(QStringLiteral("target")).toString()},
            {QStringLiteral("latencyMs"), static_cast<int>(m_runClock.elapsed()) - m_actionStartedAtMs},
            {QStringLiteral("triggerCount"), skillRecord.value(QStringLiteral("triggerCount")).toInt()},
            {QStringLiteral("files"), files}
        };
        beginMcpInstall();
        return;
    }
    case Phase::WaitingForMcpInstall: {
        if (m_extensionStateRevision <= m_waitStartRevision) {
            return;
        }

        const QVariantMap mcpEntry = catalogEntry(QStringLiteral("mcp.filesystem"));
        const QVariantMap mcpRecord = mcpServerRecord(QStringLiteral("filesystem"));
        const QVariantList args = mcpRecord.value(QStringLiteral("args")).toList();
        bool hasFilesystemPackage = false;
        for (const QVariant &value : args) {
            if (value.toString().contains(QStringLiteral("@modelcontextprotocol/server-filesystem"))) {
                hasFilesystemPackage = true;
                break;
            }
        }

        if (!mcpEntry.value(QStringLiteral("installed")).toBool() ||
            mcpRecord.isEmpty() ||
            statusCount(QStringLiteral("mcpServerCount")) < (m_initialMcpServerCount + 1) ||
            !hasInstallToastSince(m_installToastStartIndex, QStringLiteral("tools.mcpServers.filesystem")) ||
            mcpRecord.value(QStringLiteral("command")).toString() != QStringLiteral("npx") ||
            !hasFilesystemPackage) {
            return;
        }

        m_mcpInstall = QJsonObject{
            {QStringLiteral("completed"), true},
            {QStringLiteral("catalogId"), QStringLiteral("mcp.filesystem")},
            {QStringLiteral("id"), QStringLiteral("filesystem")},
            {QStringLiteral("target"), mcpEntry.value(QStringLiteral("target")).toString()},
            {QStringLiteral("latencyMs"), static_cast<int>(m_runClock.elapsed()) - m_actionStartedAtMs},
            {QStringLiteral("command"), mcpRecord.value(QStringLiteral("command")).toString()},
            {QStringLiteral("args"), QJsonArray::fromVariantList(args)}
        };
        beginExtensionsReload();
        return;
    }
    case Phase::WaitingForExtensionsReload: {
        if (m_extensionStateRevision <= m_waitStartRevision) {
            return;
        }

        const bool pluginReady =
            catalogEntry(QStringLiteral("plugin.release_notes")).value(QStringLiteral("installed")).toBool() &&
            !extensionRecord(m_bridge->plugins(), QStringLiteral("release-notes")).isEmpty();
        const bool skillReady =
            catalogEntry(QStringLiteral("skill.code_review")).value(QStringLiteral("installed")).toBool() &&
            !extensionRecord(m_bridge->skills(), QStringLiteral("code-review")).isEmpty();
        const bool mcpReady =
            catalogEntry(QStringLiteral("mcp.filesystem")).value(QStringLiteral("installed")).toBool() &&
            !mcpServerRecord(QStringLiteral("filesystem")).isEmpty();

        if (!pluginReady ||
            !skillReady ||
            !mcpReady ||
            statusCount(QStringLiteral("pluginCount")) < (m_initialPluginCount + 1) ||
            statusCount(QStringLiteral("skillCount")) < (m_initialSkillCount + 1) ||
            statusCount(QStringLiteral("mcpServerCount")) < (m_initialMcpServerCount + 1)) {
            return;
        }

        m_extensionsSummaryData = extensionSummary();
        m_extensionsReload = QJsonObject{
            {QStringLiteral("completed"), true},
            {QStringLiteral("latencyMs"), static_cast<int>(m_runClock.elapsed()) - m_reloadStartedAtMs},
            {QStringLiteral("pluginCount"), statusCount(QStringLiteral("pluginCount"))},
            {QStringLiteral("skillCount"), statusCount(QStringLiteral("skillCount"))},
            {QStringLiteral("mcpServerCount"), statusCount(QStringLiteral("mcpServerCount"))}
        };
        finishSuccess();
        return;
    }
    default:
        return;
    }
}

void GuiRegressionRunner::beginWorkspaceInitialization() {
    if (!m_bridge) {
        fail(QStringLiteral("desktop bridge was not available for workspace initialization"));
        return;
    }

    m_phase = Phase::WaitingForWorkspace;
    m_workspaceReadyBeforeInit = workspaceReady();
    m_bridge->initializeWorkspace();

    if (workspaceReady()) {
        QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
    }
}

void GuiRegressionRunner::beginSeedPreview() {
    if (m_phase == Phase::WaitingForSeedPreview || m_finished) {
        return;
    }

    m_phase = Phase::WaitingForSeedPreview;
    startPreviewWait(QStringLiteral("seed"));
    if (!setCurrentPage(QStringLiteral("runtime"))) {
        fail(QStringLiteral("failed to enter runtime page for seed preview"));
    }
}

void GuiRegressionRunner::beginSwitchRounds() {
    if (m_finished) {
        return;
    }

    if (m_options.switchCount <= 0) {
        beginRefreshPreview();
        return;
    }

    m_phase = Phase::SwitchingPages;
    m_completedRounds = 0;
    m_onSecurityPage = false;
    QTimer::singleShot(qMax(10, m_options.pageSettleMs),
                       this,
                       &GuiRegressionRunner::performNextPageSwitch);
}

void GuiRegressionRunner::beginRefreshPreview() {
    if (m_finished || !m_bridge) {
        return;
    }

    if (!setCurrentPage(QStringLiteral("runtime"))) {
        fail(QStringLiteral("failed to return to runtime page before refresh preview"));
        return;
    }

    m_phase = Phase::WaitingForRefreshPreview;
    startPreviewWait(QStringLiteral("refresh"));
    m_bridge->refreshAll();
    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, [this]() {
        if (m_finished || m_phase != Phase::WaitingForRefreshPreview) {
            return;
        }
        if (!triggerRuntimePreviewRefresh()) {
            fail(QStringLiteral("failed to trigger runtime preview refresh"));
        }
    });
}

void GuiRegressionRunner::beginRuntimePageCheck() {
    if (m_finished) {
        return;
    }

    if (!setCurrentPage(QStringLiteral("runtime"))) {
        fail(QStringLiteral("failed to enter runtime page for runtime snapshot"));
        return;
    }

    m_phase = Phase::WaitingForRuntimePage;
    m_runtimePageStartedAtMs = static_cast<int>(m_runClock.elapsed());
    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
}

void GuiRegressionRunner::beginProviderModelsPage() {
    if (m_finished) {
        return;
    }

    if (!setCurrentPage(QStringLiteral("providers"))) {
        fail(QStringLiteral("failed to enter provider models page"));
        return;
    }

    if (m_window) {
        m_window->resize(qMax(1480, m_window->minimumWidth()),
                         qMax(940, m_window->minimumHeight()));
    }

    m_phase = Phase::WaitingForProviderModelsPage;
    m_providerModelsStep = 0;
    m_providerModelsStartedAtMs = static_cast<int>(m_runClock.elapsed());
    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
}

void GuiRegressionRunner::beginOAuthStatus() {
    if (m_finished) {
        return;
    }

    if (!setCurrentPage(QStringLiteral("providers"))) {
        fail(QStringLiteral("failed to enter providers page for OAuth regression"));
        return;
    }

    m_phase = Phase::WaitingForOAuthStatus;
    m_oauthStep = 0;
    m_oauthConfigBeforeJson.clear();
    m_oauthSummary = QJsonObject{{QStringLiteral("step"), QStringLiteral("start")}};
    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
}

void GuiRegressionRunner::beginControlTemplates() {
    if (m_finished) {
        return;
    }

    if (!setCurrentPage(QStringLiteral("runtime"))) {
        fail(QStringLiteral("failed to enter runtime page for control template regression"));
        return;
    }

    m_phase = Phase::WaitingForControlTemplates;
    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
}

void GuiRegressionRunner::beginRuntimeProviderConfig() {
    if (m_finished) {
        return;
    }

    if (!setCurrentPage(QStringLiteral("providers"))) {
        fail(QStringLiteral("failed to enter providers page for runtime provider/config regression"));
        return;
    }

    m_phase = Phase::WaitingForRuntimeProviderConfig;
    m_runtimeProviderConfigStep = 0;
    m_runtimeProviderConfigStartedAtMs = static_cast<int>(m_runClock.elapsed());
    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
}

void GuiRegressionRunner::beginChatProvider() {
    if (m_finished || !m_bridge) {
        return;
    }

    if (!setCurrentPage(QStringLiteral("chat"))) {
        fail(QStringLiteral("failed to enter chat page for provider regression"));
        return;
    }

    const int startedAtMs = static_cast<int>(m_runClock.elapsed());
    const QVariantList models = m_bridge->fetchProviderModels(m_chatProviderId, m_bridge->config());
    if (models.isEmpty()) {
        fail(QStringLiteral("chat provider regression could not sync models for provider '%1'")
                 .arg(m_chatProviderId));
        return;
    }

    const QVariantMap providers = m_bridge->config().value(QStringLiteral("providers")).toMap();
    const QVariantMap providerConfig = providers.value(m_chatProviderId).toMap();
    const QVariantList persistedModels = providerConfig.value(QStringLiteral("availableModels")).toList();
    const QString persistedModel = providerConfig.value(QStringLiteral("model")).toString().trimmed();
    m_chatModel = persistedModel.isEmpty() ? models.first().toString().trimmed() : persistedModel;
    if (m_chatModel.isEmpty()) {
        fail(QStringLiteral("chat provider regression did not resolve a usable model override"));
        return;
    }

    bool hasSelectedModel = false;
    QJsonArray persistedModelArray;
    for (const QVariant &value : persistedModels) {
        const QString model = value.toString().trimmed();
        if (model.isEmpty()) {
            continue;
        }
        persistedModelArray.append(model);
        if (model.compare(m_chatModel, Qt::CaseInsensitive) == 0) {
            hasSelectedModel = true;
        }
    }
    if (!hasSelectedModel) {
        fail(QStringLiteral("chat provider regression did not persist the synced model catalog"));
        return;
    }

    m_providerModelSync = QJsonObject{
        {QStringLiteral("completed"), true},
        {QStringLiteral("providerId"), m_chatProviderId},
        {QStringLiteral("latencyMs"), static_cast<int>(m_runClock.elapsed()) - startedAtMs},
        {QStringLiteral("modelCount"), models.size()},
        {QStringLiteral("selectedModel"), m_chatModel},
        {QStringLiteral("availableModels"), persistedModelArray}
    };

    beginChatTurn();
}

void GuiRegressionRunner::beginChatTurn() {
    if (m_finished || !m_bridge) {
        return;
    }

    if (!setCurrentPage(QStringLiteral("chat"))) {
        fail(QStringLiteral("failed to return to chat page before provider turn"));
        return;
    }

    m_phase = Phase::WaitingForChatResponse;
    m_initialChatHistoryCount = m_bridge->chatHistory().size();
    m_chatToastStartIndex = m_toasts.size();
    m_chatStartedAtMs = static_cast<int>(m_runClock.elapsed());
    m_bridge->sendMessage(m_chatPrompt,
                          m_chatSessionKey,
                          m_chatModel,
                          m_chatProviderId);
    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
}

void GuiRegressionRunner::beginExtensionsCatalog() {
    if (m_finished || !m_bridge) {
        return;
    }

    if (!setCurrentPage(QStringLiteral("extensions"))) {
        fail(QStringLiteral("failed to enter extensions page for catalog install"));
        return;
    }

    m_phase = Phase::WaitingForExtensionCatalog;
    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
}

void GuiRegressionRunner::beginPluginInstall() {
    if (m_finished || !m_bridge) {
        return;
    }

    m_phase = Phase::WaitingForPluginInstall;
    m_waitStartRevision = m_extensionStateRevision;
    m_installToastStartIndex = m_toasts.size();
    m_actionStartedAtMs = static_cast<int>(m_runClock.elapsed());
    m_bridge->installCatalogItem(QStringLiteral("plugin.release_notes"));
    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
}

void GuiRegressionRunner::beginSkillInstall() {
    if (m_finished || !m_bridge) {
        return;
    }

    m_phase = Phase::WaitingForSkillInstall;
    m_waitStartRevision = m_extensionStateRevision;
    m_installToastStartIndex = m_toasts.size();
    m_actionStartedAtMs = static_cast<int>(m_runClock.elapsed());
    m_bridge->installCatalogItem(QStringLiteral("skill.code_review"));
    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
}

void GuiRegressionRunner::beginMcpInstall() {
    if (m_finished || !m_bridge) {
        return;
    }

    m_phase = Phase::WaitingForMcpInstall;
    m_waitStartRevision = m_extensionStateRevision;
    m_installToastStartIndex = m_toasts.size();
    m_actionStartedAtMs = static_cast<int>(m_runClock.elapsed());
    m_bridge->installCatalogItem(QStringLiteral("mcp.filesystem"));
    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
}

void GuiRegressionRunner::beginExtensionsReload() {
    if (m_finished || !m_bridge) {
        return;
    }

    if (!setCurrentPage(QStringLiteral("extensions"))) {
        fail(QStringLiteral("failed to return to extensions page before reload"));
        return;
    }

    m_phase = Phase::WaitingForExtensionsReload;
    m_waitStartRevision = m_extensionStateRevision;
    m_reloadStartedAtMs = static_cast<int>(m_runClock.elapsed());
    m_bridge->refreshAll();
    QTimer::singleShot(qMax(10, m_options.pageSettleMs), this, &GuiRegressionRunner::maybeAdvance);
}

bool GuiRegressionRunner::beginCaseFixture() {
    if (!m_bridge) {
        fail(QStringLiteral("desktop bridge was not available for regression fixture"));
        return false;
    }

    const bool delayedProviderMock = caseNeedsProviderMock() &&
                                     !runtimeProviderConfigMode().isEmpty();
    if (caseNeedsProviderMock() && !delayedProviderMock && !startProviderMockServer()) {
        return false;
    }
    if (m_options.caseId == QStringLiteral("control-templates") && !startControlMockServer()) {
        return false;
    }
    if (m_options.caseId == QStringLiteral("remote-provider-config") && !startRuntimeMockServer()) {
        return false;
    }

    config::Config config =
        config::Config::fromJson(QJsonObject::fromVariantMap(m_bridge->config()));
    const QString caseId = m_options.caseId.trimmed().toLower();

    if (caseNeedsProviderMock()) {
        if (m_providerServerPort == 0) {
            m_providerServerPort = regressionPort(QStringLiteral("provider-mock"));
        }
        config.providers.custom.apiBase =
            QStringLiteral("http://127.0.0.1:%1/v1").arg(m_providerServerPort);
        config.providers.custom.apiKey = QStringLiteral("gui-regression-key");
        config.providers.custom.model = QStringLiteral("gui-regression-model");
        config.providers.custom.availableModels = QStringList{
            QStringLiteral("gui-regression-model"),
            QStringLiteral("gui-regression-alt")
        };
        config.agentDefaults.provider = QStringLiteral("custom");
        config.agentDefaults.model = QStringLiteral("gui-regression-model");
    }

    if (caseId == QStringLiteral("oauth-status")) {
        config.providers.openaiCodex.apiKey = QStringLiteral("gui-oauth-runtime-token");
        config.providers.openaiCodex.oauthAccessToken = QStringLiteral("gui-oauth-access-token");
        config.providers.openaiCodex.oauthRefreshToken = QStringLiteral("gui-oauth-refresh-token");
        config.providers.openaiCodex.oauthAccountId = QStringLiteral("gui-oauth-account");
    }

    if (caseId == QStringLiteral("control-templates")) {
        config.deployment.controlPlaneUrl =
            QStringLiteral("http://127.0.0.1:%1").arg(m_controlServerPort);
        config::DelegationTemplateConfig existing;
        existing.id = QStringLiteral("existing-template");
        existing.name = QStringLiteral("Existing Template");
        existing.kind = QStringLiteral("single");
        existing.request.insert(QStringLiteral("task"), QStringLiteral("existing task"));
        config.memory.delegationTemplates = {existing};
    }

    const QString runtimeMode = runtimeProviderConfigMode();
    if (!runtimeMode.isEmpty()) {
        config.runtime.mode = runtimeMode;
        config.runtime.autoSpawnLocalDaemon = true;
        config.runtime.autoSpawnLocalService = true;
        if (runtimeMode == QStringLiteral("remote")) {
            if (m_runtimeServerPort > 0) {
                config.runtime.autoSpawnLocalService = false;
            }
            config.runtime.endpoint =
                QStringLiteral("http://127.0.0.1:%1")
                    .arg(m_runtimeServerPort > 0
                             ? m_runtimeServerPort
                             : regressionPort(QStringLiteral("remote-runtime")));
        } else {
            config.runtime.endpoint =
                QStringLiteral("local://gui-regression-%1")
                    .arg(QCoreApplication::applicationPid());
        }
    }
    m_runtimeMockConfig = config.toJson();

    m_phase = Phase::WaitingForFixtureSave;
    m_fixtureStartedAtMs = static_cast<int>(m_runClock.elapsed());
    if (!m_bridge->saveConfig(config.toJson().toVariantMap())) {
        fail(QStringLiteral("could not start gui regression fixture config save"));
        return false;
    }
    return true;
}

bool GuiRegressionRunner::caseNeedsFixture() const {
    return m_options.caseId == QStringLiteral("provider-models") ||
           m_options.caseId == QStringLiteral("chat-provider") ||
           m_options.caseId == QStringLiteral("oauth-status") ||
           m_options.caseId == QStringLiteral("control-templates") ||
           m_options.caseId == QStringLiteral("daemon-provider-config") ||
           m_options.caseId == QStringLiteral("remote-provider-config");
}

bool GuiRegressionRunner::caseNeedsProviderMock() const {
    return m_options.caseId == QStringLiteral("provider-models") ||
           m_options.caseId == QStringLiteral("chat-provider") ||
           m_options.caseId == QStringLiteral("daemon-provider-config") ||
           m_options.caseId == QStringLiteral("remote-provider-config");
}

bool GuiRegressionRunner::startProviderMockServer() {
    if (m_providerServer && m_providerServerPort > 0) {
        return true;
    }

    QString error;
    if (!ensureFastNetInitialized(&error)) {
        fail(error);
        return false;
    }

    m_providerServer = std::make_unique<FastNet::HttpServer>(FastNet::getGlobalIoService());
    m_providerServer->setMaxRequestSize(2 * 1024 * 1024);
    m_providerServer->setRequestHandler([this](const FastNet::HttpRequest &request,
                                               FastNet::HttpResponse &response) {
        const QString path = requestPath(request);
        std::printf("[MOCK PROVIDER SERVER] Request: path=%s, method=%s, body_size=%d\n",
                    request.path.c_str(), request.methodName.c_str(), (int)request.body.size());
        std::fflush(stdout);
        if (path == QStringLiteral("/v1/models") || path == QStringLiteral("/models")) {
            writeJsonResponse(response,
                              200,
                              QJsonObject{
                                  {QStringLiteral("data"),
                                   QJsonArray{
                                       QJsonObject{{QStringLiteral("id"), QStringLiteral("gui-regression-model")}},
                                       QJsonObject{{QStringLiteral("id"), QStringLiteral("gui-regression-alt")}}
                                   }}
                              });
            return;
        }

        if (path == QStringLiteral("/v1/chat/completions") ||
            path == QStringLiteral("/chat/completions")) {
            const QString prompt = lastUserPromptFromChatBody(requestBody(request));
            const QString content = QStringLiteral("GUI provider regression ok: %1")
                                        .arg(prompt.isEmpty() ? m_chatPrompt : prompt);

            QJsonObject deltaObj;
            deltaObj[QStringLiteral("role")] = QStringLiteral("assistant");
            deltaObj[QStringLiteral("content")] = content;

            QJsonObject choiceObj;
            choiceObj[QStringLiteral("index")] = 0;
            choiceObj[QStringLiteral("delta")] = deltaObj;
            choiceObj[QStringLiteral("finish_reason")] = QJsonValue::Null;

            QJsonObject rootObj;
            rootObj[QStringLiteral("choices")] = QJsonArray{choiceObj};

            const QByteArray chunk1 = QJsonDocument(rootObj).toJson(QJsonDocument::Compact);

            QJsonObject stopChoiceObj;
            stopChoiceObj[QStringLiteral("index")] = 0;
            stopChoiceObj[QStringLiteral("delta")] = QJsonObject{};
            stopChoiceObj[QStringLiteral("finish_reason")] = QStringLiteral("stop");

            QJsonObject stopRootObj;
            stopRootObj[QStringLiteral("choices")] = QJsonArray{stopChoiceObj};

            const QByteArray chunk2 = QJsonDocument(stopRootObj).toJson(QJsonDocument::Compact);

            QByteArray sseBody;
            sseBody.append("data: ");
            sseBody.append(chunk1);
            sseBody.append("\n\ndata: ");
            sseBody.append(chunk2);
            sseBody.append("\n\ndata: [DONE]\n\n");

            response.statusCode = 200;
            response.statusMessage = "OK";
            response.headers["Content-Type"] = "text/event-stream; charset=utf-8";
            response.headers["Cache-Control"] = "no-cache";
            response.headers["Connection"] = "close";
            response.headers["Content-Length"] = std::to_string(sseBody.size());
            response.body.assign(sseBody.constData(), static_cast<size_t>(sseBody.size()));
            return;
        }

        writeJsonResponse(response,
                          404,
                          QJsonObject{
                              {QStringLiteral("ok"), false},
                              {QStringLiteral("error"),
                               QStringLiteral("unexpected provider mock path: %1").arg(path)}
                          });
    });

    const FastNet::Error startError =
        m_providerServer->start(regressionPort(QStringLiteral("provider-mock")), "127.0.0.1");
    if (startError.isFailure()) {
        fail(QStringLiteral("provider mock server failed to start: %1")
                 .arg(QString::fromStdString(startError.toString())));
        m_providerServer.reset();
        return false;
    }
    const FastNet::Address actual = m_providerServer->getListenAddress();
    m_providerServerPort = actual.port;
    return m_providerServerPort > 0;
}

bool GuiRegressionRunner::startControlMockServer() {
    if (m_controlServer && m_controlServerPort > 0) {
        return true;
    }

    QString error;
    if (!ensureFastNetInitialized(&error)) {
        fail(error);
        return false;
    }

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

    m_controlServer = std::make_unique<FastNet::HttpServer>(FastNet::getGlobalIoService());
    m_controlServer->setMaxRequestSize(2 * 1024 * 1024);
    m_controlServer->setRequestHandler([envelope](const FastNet::HttpRequest &request,
                                                  FastNet::HttpResponse &response) {
        const QString path = requestPath(request);
        if (path == QStringLiteral("/health") ||
            path == QStringLiteral("/v1/control/health")) {
            writeJsonResponse(response, 200, QJsonObject{{QStringLiteral("ok"), true}});
            return;
        }
        if (path == QStringLiteral("/v1/control/delegation-templates/list")) {
            writeJsonResponse(response,
                              200,
                              QJsonObject{
                                  {QStringLiteral("ok"), true},
                                  {QStringLiteral("envelope"), envelope}
                              });
            return;
        }
        writeJsonResponse(response,
                          404,
                          QJsonObject{
                              {QStringLiteral("ok"), false},
                              {QStringLiteral("error"),
                               QStringLiteral("unexpected control mock path: %1").arg(path)}
                          });
    });

    const FastNet::Error startError =
        m_controlServer->start(regressionPort(QStringLiteral("control-mock")), "127.0.0.1");
    if (startError.isFailure()) {
        fail(QStringLiteral("control mock server failed to start: %1")
                 .arg(QString::fromStdString(startError.toString())));
        m_controlServer.reset();
        return false;
    }
    const FastNet::Address actual = m_controlServer->getListenAddress();
    m_controlServerPort = actual.port;
    return m_controlServerPort > 0;
}

bool GuiRegressionRunner::startRuntimeMockServer() {
    if (m_runtimeServer && m_runtimeServerPort > 0) {
        return true;
    }

    QString error;
    if (!ensureFastNetInitialized(&error)) {
        fail(error);
        return false;
    }

    m_runtimeServer = std::make_unique<FastNet::HttpServer>(FastNet::getGlobalIoService());
    m_runtimeServer->setMaxRequestSize(16 * 1024 * 1024);
    QPointer<GuiRegressionRunner> guard(this);
    m_runtimeServer->setRequestHandler([guard](const FastNet::HttpRequest &request,
                                               FastNet::HttpResponse &response) {
        if (!guard) {
            writeJsonResponse(response,
                              503,
                              QJsonObject{{QStringLiteral("ok"), false},
                                          {QStringLiteral("error"), QStringLiteral("runtime mock unavailable")}});
            return;
        }

        const QString path = requestPath(request);
        if (path == QStringLiteral("/health") ||
            path == QStringLiteral("/v1/runtime/health")) {
            writeJsonResponse(response,
                              200,
                              QJsonObject{
                                  {QStringLiteral("ok"), true},
                                  {QStringLiteral("service"), QStringLiteral("yaos-runtime")},
                                  {QStringLiteral("mode"), QStringLiteral("http")}
                              });
            return;
        }

        if (path != QStringLiteral("/v1/runtime/invoke")) {
            writeJsonResponse(response,
                              404,
                              QJsonObject{{QStringLiteral("ok"), false},
                                          {QStringLiteral("error"), QStringLiteral("runtime mock endpoint not found")}});
            return;
        }

        const QJsonDocument requestDocument = QJsonDocument::fromJson(requestBody(request));
        const QJsonObject envelope = requestDocument.object();
        const QString method = envelope.value(QStringLiteral("method")).toString();
        const QJsonObject payload = envelope.value(QStringLiteral("payload")).toObject();
        const QJsonObject configObject = guard->m_runtimeMockConfig;
        const config::Config config = config::Config::fromJson(configObject);

        if (method == QStringLiteral("statusSnapshot")) {
            const QJsonObject status{
                {QStringLiteral("configPath"), config::ConfigLoader::defaultConfigPath()},
                {QStringLiteral("configReady"), true},
                {QStringLiteral("workspacePath"), config.workspacePath()},
                {QStringLiteral("workspaceReady"), true},
                {QStringLiteral("defaultModel"), config.agentDefaults.model},
                {QStringLiteral("routedProvider"), config.agentDefaults.provider},
                {QStringLiteral("actualBackend"), config.agentDefaults.provider},
                {QStringLiteral("backendFallback"), false},
                {QStringLiteral("runtimeMode"), QStringLiteral("remote")},
                {QStringLiteral("runtimeEndpoint"), config.runtime.endpoint},
                {QStringLiteral("runtimeAdvertiseEndpoint"), config.runtime.endpoint},
                {QStringLiteral("runtimeServiceEnabled"), true},
                {QStringLiteral("runtimeServiceReachable"), true},
                {QStringLiteral("runtimeServiceAutoSpawn"), config.runtime.autoSpawnLocalService},
                {QStringLiteral("controlPlaneReachable"), false},
                {QStringLiteral("memoryServiceEnabled"), config.memory.service.enabled},
                {QStringLiteral("memoryServiceReachable"), false},
                {QStringLiteral("memoryServiceAutoSpawn"), config.memory.service.autoSpawnLocalService},
                {QStringLiteral("mcpServerCount"), 0},
                {QStringLiteral("restrictToWorkspace"), config.tools.restrictToWorkspace},
                {QStringLiteral("gatewayRunning"), false},
                {QStringLiteral("heartbeatEnabled"), config.gateway.heartbeat.enabled},
                {QStringLiteral("heartbeatIntervalS"), config.gateway.heartbeat.intervalS},
                {QStringLiteral("cronJobCount"), 0},
                {QStringLiteral("taskCount"), 0},
                {QStringLiteral("eventCount"), 0},
                {QStringLiteral("pendingApprovalCount"), 0},
                {QStringLiteral("unreadNotificationCount"), 0},
                {QStringLiteral("automationCount"), 0},
                {QStringLiteral("pluginCount"), 0},
                {QStringLiteral("skillCount"), 0},
                {QStringLiteral("resourceCount"), 10}
            };
            writeJsonResponse(response,
                              200,
                              QJsonObject{{QStringLiteral("ok"), true},
                                          {QStringLiteral("status"), status}});
            return;
        }

        if (method == QStringLiteral("studio.fetchProviderModels")) {
            Q_UNUSED(payload);
            writeJsonResponse(response,
                              200,
                              QJsonObject{
                                  {QStringLiteral("ok"), true},
                                  {QStringLiteral("providerId"), QStringLiteral("custom")},
                                  {QStringLiteral("models"), QJsonArray{
                                      QStringLiteral("gui-regression-model"),
                                      QStringLiteral("gui-regression-alt")
                                  }},
                                  {QStringLiteral("modelCount"), 2},
                                  {QStringLiteral("usedFallback"), false},
                                  {QStringLiteral("configChanged"), false},
                                  {QStringLiteral("warnings"), QJsonArray()}
                              });
            return;
        }

        if (method == QStringLiteral("studio.saveConfiguration")) {
            const QJsonObject draftConfig = payload.value(QStringLiteral("draftConfig")).toObject();
            if (!draftConfig.isEmpty()) {
                guard->m_runtimeMockConfig = draftConfig;
            }
            writeJsonResponse(response,
                              200,
                              QJsonObject{
                                  {QStringLiteral("ok"), true},
                                  {QStringLiteral("saved"), true},
                                  {QStringLiteral("reloadOk"), true},
                                  {QStringLiteral("configChanged"), true},
                                  {QStringLiteral("config"), guard->m_runtimeMockConfig},
                                  {QStringLiteral("title"), QStringLiteral("配置已同步")},
                                  {QStringLiteral("body"), QStringLiteral("新的系统参数已经写入并重载.")},
                                  {QStringLiteral("tone"), QStringLiteral("success")}
                              });
            return;
        }

        if (method == QStringLiteral("reloadFromDisk") ||
            method == QStringLiteral("initializeWorkspace") ||
            method == QStringLiteral("startGatewayServices") ||
            method == QStringLiteral("gatewayRunning")) {
            writeJsonResponse(response,
                              200,
                              QJsonObject{{QStringLiteral("ok"), true},
                                          {QStringLiteral("value"), true}});
            return;
        }

        if (method == QStringLiteral("resourceSummary")) {
            writeJsonResponse(response,
                              200,
                              QJsonObject{{QStringLiteral("ok"), true},
                                          {QStringLiteral("summary"), QJsonObject{
                                              {QStringLiteral("totalCount"), 10}
                                          }}});
            return;
        }

        if (method == QStringLiteral("studio.extensionCatalog")) {
            writeJsonResponse(response,
                              200,
                              QJsonObject{{QStringLiteral("ok"), true},
                                          {QStringLiteral("items"), QJsonArray()}});
            return;
        }

        writeJsonResponse(response,
                          200,
                          QJsonObject{{QStringLiteral("ok"), true},
                                      {QStringLiteral("items"), QJsonArray()}});
    });

    const FastNet::Error startError =
        m_runtimeServer->start(regressionPort(QStringLiteral("remote-runtime")), "127.0.0.1");
    if (startError.isFailure()) {
        fail(QStringLiteral("runtime mock server failed to start: %1")
                 .arg(QString::fromStdString(startError.toString())));
        m_runtimeServer.reset();
        return false;
    }
    const FastNet::Address actual = m_runtimeServer->getListenAddress();
    m_runtimeServerPort = actual.port;
    return m_runtimeServerPort > 0;
}

void GuiRegressionRunner::stopMockServers() {
    if (m_providerServer) {
        m_providerServer->stop();
        m_providerServer.reset();
    }
    if (m_controlServer) {
        m_controlServer->stop();
        m_controlServer.reset();
    }
    if (m_runtimeServer) {
        m_runtimeServer->stop();
        m_runtimeServer.reset();
    }
}

void GuiRegressionRunner::releaseMockServersForProcessExit() {
    // FastNet server shutdown can block on Windows in this short-lived regression process.
    // The OS releases the sockets when the process exits after the result is written.
    if (m_providerServer) {
        m_providerServer.release();
    }
    if (m_controlServer) {
        m_controlServer.release();
    }
    if (m_runtimeServer) {
        m_runtimeServer.release();
    }
}

QString GuiRegressionRunner::runtimeProviderConfigMode() const {
    if (m_options.caseId == QStringLiteral("daemon-provider-config")) {
        return QStringLiteral("daemon");
    }
    if (m_options.caseId == QStringLiteral("remote-provider-config")) {
        return QStringLiteral("remote");
    }
    return {};
}

void GuiRegressionRunner::startPreviewWait(const QString &reason) {
    m_waitingForPreview = true;
    m_previewSawPending = false;
    m_previewReason = reason;
    m_previewStartedAtMs = static_cast<int>(m_runClock.elapsed());
}

void GuiRegressionRunner::completePreviewWait(const QVariantMap &preview) {
    m_waitingForPreview = false;

    const int latencyMs = static_cast<int>(m_runClock.elapsed()) - m_previewStartedAtMs;
    const QJsonObject summary = previewSummary(m_previewReason, preview, latencyMs);
    if (m_previewReason == QStringLiteral("seed")) {
        m_seedPreview = summary;
        beginSwitchRounds();
        return;
    }

    m_refreshPreview = summary;
    if (m_options.caseId == QStringLiteral("runtime-page")) {
        beginRuntimePageCheck();
        return;
    }
    finishSuccess();
}

QObject *GuiRegressionRunner::resolvePageObject() const {
    if (m_rootObject) {
        const QMetaObject *metaObject = m_rootObject->metaObject();
        if (metaObject && metaObject->indexOfProperty("currentPage") >= 0) {
            return m_rootObject;
        }
    }

    if (!m_window || !m_window->rootObject()) {
        return nullptr;
    }

    QObject *resolved = findObjectWithProperty(static_cast<QObject *>(m_window->rootObject()),
                                               "currentPage");
    m_rootObject = resolved;
    return resolved;
}

QObject *GuiRegressionRunner::resolveRuntimePageObject() const {
    if (!m_window || !m_window->rootObject()) {
        return nullptr;
    }

    return findObjectByName(static_cast<QObject *>(m_window->rootObject()),
                            QStringLiteral("runtimePageRoot"));
}

QObject *GuiRegressionRunner::resolveRuntimePreviewObject() const {
    if (!m_window || !m_window->rootObject()) {
        return nullptr;
    }

    return findObjectWithProperty(static_cast<QObject *>(m_window->rootObject()),
                                  "previewInitialized");
}

QVariant GuiRegressionRunner::invokeRootMethod(const char *methodName, const QVariant &arg1) const {
    QObject *root = resolvePageObject();
    if (!root || !methodName || *methodName == '\0') {
        return {};
    }

    QVariant result;
    const bool invoked = arg1.isValid()
        ? QMetaObject::invokeMethod(root,
                                    methodName,
                                    Q_RETURN_ARG(QVariant, result),
                                    Q_ARG(QVariant, arg1))
        : QMetaObject::invokeMethod(root,
                                    methodName,
                                    Q_RETURN_ARG(QVariant, result));
    return invoked ? result : QVariant();
}

QVariantMap GuiRegressionRunner::providerPageSnapshot() const {
    if (!m_window || !m_window->rootObject()) {
        return {};
    }

    QObject *windowRoot = static_cast<QObject *>(m_window->rootObject());
    QObject *pageObject = resolvePageObject();
    QObject *gridObject = findObjectByName(windowRoot, QStringLiteral("providerGrid"));
    if (!gridObject) {
        return {};
    }

    QList<QObject *> cards;
    appendProviderCardObjects(windowRoot, &cards);
    std::sort(cards.begin(), cards.end(), [](QObject *left, QObject *right) {
        const qreal leftY = realProperty(left, "y");
        const qreal rightY = realProperty(right, "y");
        if (!qFuzzyCompare(leftY + 1.0, rightY + 1.0)) {
            return leftY < rightY;
        }
        return realProperty(left, "x") < realProperty(right, "x");
    });

    QVariantList cardStates;
    cardStates.reserve(cards.size());
    for (QObject *cardObject : cards) {
        const QVariantMap cardState = providerCardState(cardObject);
        if (!cardState.isEmpty()) {
            cardStates.append(cardState);
        }
    }

    return QVariantMap{
        {QStringLiteral("page"), currentPage()},
        {QStringLiteral("columns"), gridObject->property("columns").toInt()},
        {QStringLiteral("gridWidth"), realProperty(gridObject, "width")},
        {QStringLiteral("cardCount"), cardStates.size()},
        {QStringLiteral("selectedProviderKey"),
         pageObject ? pageObject->property("selectedProviderPanelKey").toString() : QString()},
        {QStringLiteral("cards"), cardStates}
    };
}

QVariantMap GuiRegressionRunner::runtimePageSnapshot() const {
    QObject *runtimePageObject = resolveRuntimePageObject();
    if (!runtimePageObject) {
        return {};
    }

    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(runtimePageObject,
                                                   "runtimeRegressionSnapshot",
                                                   Q_RETURN_ARG(QVariant, result));
    return invoked ? result.toMap() : QVariantMap{};
}

void GuiRegressionRunner::recordPageSwitch(const QString &page) {
    m_pageSwitches.append(QJsonObject{
        {QStringLiteral("round"), m_completedRounds + (page == QStringLiteral("runtime") ? 1 : 0)},
        {QStringLiteral("page"), page},
        {QStringLiteral("elapsedMs"), static_cast<int>(m_runClock.elapsed())}
    });
}

bool GuiRegressionRunner::setCurrentPage(const QString &page) {
    QObject *pageObject = resolvePageObject();
    if (!pageObject) {
        return false;
    }
    if (!pageObject->setProperty("currentPage", page)) {
        return false;
    }
    return currentPage() == page;
}

bool GuiRegressionRunner::triggerRuntimePreviewRefresh() const {
    QObject *previewObject = resolveRuntimePreviewObject();
    if (!previewObject) {
        return false;
    }
    return QMetaObject::invokeMethod(previewObject, "refreshCurrentPreview");
}

QString GuiRegressionRunner::currentPage() const {
    QObject *pageObject = resolvePageObject();
    if (!pageObject) {
        return QString();
    }
    return pageObject->property("currentPage").toString();
}

QString GuiRegressionRunner::workspacePath() const {
    if (!m_bridge) {
        return QString();
    }
    return m_bridge->status().value(QStringLiteral("workspacePath")).toString();
}

int GuiRegressionRunner::statusCount(const QString &key) const {
    if (!m_bridge) {
        return 0;
    }
    return m_bridge->status().value(key).toInt();
}

QVariantMap GuiRegressionRunner::catalogEntry(const QString &catalogId) const {
    if (!m_bridge) {
        return {};
    }

    const QVariantList entries = m_bridge->extensionCatalog();
    for (const QVariant &entryValue : entries) {
        const QVariantMap entry = entryValue.toMap();
        if (entry.value(QStringLiteral("catalogId")).toString() == catalogId) {
            return entry;
        }
    }
    return {};
}

QVariantMap GuiRegressionRunner::extensionRecord(const QVariantList &records, const QString &id) const {
    for (const QVariant &recordValue : records) {
        const QVariantMap record = recordValue.toMap();
        if (record.value(QStringLiteral("id")).toString() == id) {
            return record;
        }
    }
    return {};
}

QVariantMap GuiRegressionRunner::mcpServerRecord(const QString &id) const {
    if (!m_bridge) {
        return {};
    }

    const QVariantMap tools = m_bridge->config().value(QStringLiteral("tools")).toMap();
    const QVariantMap servers = tools.value(QStringLiteral("mcpServers")).toMap();
    return servers.value(id).toMap();
}

bool GuiRegressionRunner::findChatReplyToast(QString *bodyPreview) const {
    QString preview;
    for (int index = qMax(0, m_chatToastStartIndex); index < m_toasts.size(); ++index) {
        const QJsonObject toast = m_toasts.at(index).toObject();
        const QString title = toast.value(QStringLiteral("title")).toString();
        const QString body = toast.value(QStringLiteral("body")).toString();
        const bool successTone =
            toast.value(QStringLiteral("tone")).toString().compare(QStringLiteral("success"),
                                                                   Qt::CaseInsensitive) == 0;
        if (!successTone) {
            continue;
        }
        if (body.contains(QStringLiteral("GUI provider regression ok:"), Qt::CaseInsensitive) ||
            body.contains(m_chatPrompt, Qt::CaseInsensitive) ||
            title.contains(QStringLiteral("收到新回复")) ||
            title.contains(QString::fromUtf8(u8"\u6536\u5230\u65b0\u56de\u590d"))) {
            preview = body.left(220);
            if (bodyPreview) {
                *bodyPreview = preview;
            }
            return true;
        }
    }

    if (bodyPreview) {
        bodyPreview->clear();
    }
    return false;
}

QString GuiRegressionRunner::chatSessionFilePath() const {
    const QString fileName = normalizedSessionFileStem(m_chatSessionKey) + QStringLiteral(".jsonl");
    return QDir(workspacePath()).filePath(QStringLiteral("sessions/%1").arg(fileName));
}

QJsonObject GuiRegressionRunner::chatSummaryFromSessionFallback() const {
    QString toastPreview;
    const bool sawReplyToast = findChatReplyToast(&toastPreview);
    if (!sawReplyToast || statusCount(QStringLiteral("taskCount")) <= 0) {
        return {};
    }

    QFile sessionFile(chatSessionFilePath());
    if (!sessionFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QString userContent;
    QString assistantContent;
    while (!sessionFile.atEnd()) {
        const QJsonObject record = parseJsonLine(sessionFile.readLine());
        if (record.isEmpty()) {
            continue;
        }
        const QString role = record.value(QStringLiteral("role")).toString();
        const QString content = record.value(QStringLiteral("content")).toString();
        if (role == QStringLiteral("user") &&
            content.compare(m_chatPrompt, Qt::CaseInsensitive) == 0) {
            userContent = content;
        } else if (role == QStringLiteral("assistant") &&
                   content.contains(QStringLiteral("GUI provider regression ok:"), Qt::CaseInsensitive) &&
                   content.contains(m_chatPrompt, Qt::CaseInsensitive)) {
            assistantContent = content;
        }
    }
    if (userContent.isEmpty() || assistantContent.isEmpty()) {
        return {};
    }

    QFile eventsFile(QDir(workspacePath()).filePath(QStringLiteral("runtime/events.jsonl")));
    if (!eventsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QJsonArray traceMessages;
    bool sawTaskCompleted = false;
    while (!eventsFile.atEnd()) {
        const QJsonObject record = parseJsonLine(eventsFile.readLine());
        if (record.isEmpty()) {
            continue;
        }
        const QJsonObject metadata = record.value(QStringLiteral("metadata")).toObject();
        if (metadata.value(QStringLiteral("session_key")).toString() != m_chatSessionKey) {
            continue;
        }
        const QString category = record.value(QStringLiteral("category")).toString();
        const QString message = record.value(QStringLiteral("message")).toString();
        if (category.isEmpty() || message.isEmpty()) {
            continue;
        }
        traceMessages.append(QStringLiteral("%1:%2").arg(category, message));
        if (category == QStringLiteral("task") &&
            message == QStringLiteral("Task completed")) {
            sawTaskCompleted = true;
        }
    }
    if (!sawTaskCompleted) {
        return {};
    }

    QString assistantMeta = QStringLiteral("%1  ·  %2")
                                .arg(m_chatProviderId,
                                     m_chatModel.trimmed().isEmpty()
                                         ? QStringLiteral("unknown-model")
                                         : m_chatModel);
    if (!toastPreview.isEmpty()) {
        assistantMeta += QStringLiteral("  ·  session-fallback");
    }

    return QJsonObject{
        {QStringLiteral("completed"), true},
        {QStringLiteral("providerId"), m_chatProviderId},
        {QStringLiteral("model"), m_chatModel},
        {QStringLiteral("sessionKey"), m_chatSessionKey},
        {QStringLiteral("prompt"), m_chatPrompt},
        {QStringLiteral("replyToastSeen"), sawReplyToast},
        {QStringLiteral("latencyMs"), static_cast<int>(m_runClock.elapsed()) - m_chatStartedAtMs},
        {QStringLiteral("chatHistoryCount"), m_bridge ? m_bridge->chatHistory().size() : 0},
        {QStringLiteral("traceCount"), traceMessages.size()},
        {QStringLiteral("assistantMeta"), assistantMeta},
        {QStringLiteral("assistantContentPreview"), assistantContent.left(220)},
        {QStringLiteral("traceMessages"), traceMessages},
        {QStringLiteral("completionSource"), QStringLiteral("session-fallback")}
    };
}

bool GuiRegressionRunner::hasInstallToastSince(int startIndex, const QString &target) const {
    const int begin = qMax(0, startIndex);
    for (int index = begin; index < m_toasts.size(); ++index) {
        const QJsonObject toast = m_toasts.at(index).toObject();
        if (toast.value(QStringLiteral("title")).toString().contains(QStringLiteral("安装完成")) &&
            toast.value(QStringLiteral("tone")).toString().compare(QStringLiteral("success"),
                                                                   Qt::CaseInsensitive) == 0 &&
            toast.value(QStringLiteral("body")).toString().contains(target)) {
            return true;
        }
    }
    return false;
}

bool GuiRegressionRunner::allFilesExist(const QStringList &paths, QJsonArray *existingFiles) const {
    bool ok = true;
    for (const QString &path : paths) {
        if (QFileInfo::exists(path)) {
            if (existingFiles) {
                existingFiles->append(path);
            }
        } else {
            ok = false;
        }
    }
    return ok;
}

QStringList GuiRegressionRunner::pluginInstallFiles() const {
    const QString base = QDir(workspacePath()).filePath(QStringLiteral("plugins/release-notes"));
    return {
        QDir(base).filePath(QStringLiteral("plugin.json")),
        QDir(base).filePath(QStringLiteral("PROMPT.md")),
        QDir(base).filePath(QStringLiteral("README.md"))
    };
}

QStringList GuiRegressionRunner::skillInstallFiles() const {
    const QString base = QDir(workspacePath()).filePath(QStringLiteral("skills/code-review"));
    return {
        QDir(base).filePath(QStringLiteral("SKILL.md"))
    };
}

bool GuiRegressionRunner::workspaceReady() const {
    if (!m_bridge) {
        return false;
    }
    return m_bridge->status().value(QStringLiteral("workspaceReady")).toBool();
}

void GuiRegressionRunner::finishSuccess() {
    finishWithExitCode(0, QString());
}

void GuiRegressionRunner::fail(const QString &message) {
    if (m_finished) {
        return;
    }
    finishWithExitCode(1, message);
}

void GuiRegressionRunner::finishWithExitCode(int exitCode, const QString &error) {
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_error = error.trimmed();
    m_phase = (exitCode == 0) ? Phase::Completed : Phase::Failed;

    m_timeoutTimer.stop();
    m_heartbeatTimer.stop();

    m_result = buildResultDocument(exitCode == 0, m_error);

    QString writeError;
    if (!writeResultDocument(m_result, &writeError)) {
        m_error = writeError;
        m_result = buildResultDocument(false, m_error);
        exitCode = 1;
    }

    releaseMockServersForProcessExit();
    emit finished(exitCode);
}

bool GuiRegressionRunner::writeResultDocument(const QJsonObject &document, QString *error) const {
    const QString outputPath = m_options.jsonOutputPath.trimmed();
    if (outputPath.isEmpty()) {
        if (error) {
            error->clear();
        }
        return true;
    }

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("failed to write gui regression result: %1").arg(file.errorString());
        }
        return false;
    }

    const QByteArray payload = QJsonDocument(document).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        if (error) {
            *error = QStringLiteral("failed to persist gui regression result: %1").arg(file.errorString());
        }
        return false;
    }

    if (error) {
        error->clear();
    }
    return true;
}

QJsonObject GuiRegressionRunner::buildResultDocument(bool ok, const QString &error) const {
    QJsonObject document{
        {QStringLiteral("ok"), ok},
        {QStringLiteral("case"), m_options.caseId},
        {QStringLiteral("switchCount"), m_options.switchCount},
        {QStringLiteral("startupMs"), m_startupMs},
        {QStringLiteral("durationMs"), static_cast<int>(m_runClock.isValid() ? m_runClock.elapsed() : 0)},
        {QStringLiteral("maxUiGapMs"), m_maxUiGapMs},
        {QStringLiteral("workspaceReadyBeforeInit"), m_workspaceReadyBeforeInit},
        {QStringLiteral("workspaceReady"), workspaceReady()},
        {QStringLiteral("finalPage"), currentPage()},
        {QStringLiteral("status"), statusSummary()},
        {QStringLiteral("extensionSummary"), extensionSummary()},
        {QStringLiteral("seedPreview"), m_seedPreview},
        {QStringLiteral("refreshPreview"), m_refreshPreview},
        {QStringLiteral("providerModelSync"), m_providerModelSync},
        {QStringLiteral("providerPageSummary"), m_providerPageSummary},
        {QStringLiteral("runtimePageSummary"), m_runtimePageSummary},
        {QStringLiteral("oauthSummary"), m_oauthSummary},
        {QStringLiteral("controlSummary"), m_controlSummary},
        {QStringLiteral("runtimeProviderConfigSummary"), m_runtimeProviderConfigSummary},
        {QStringLiteral("chatSummary"), m_chatSummary},
        {QStringLiteral("pluginInstall"), m_pluginInstall},
        {QStringLiteral("skillInstall"), m_skillInstall},
        {QStringLiteral("mcpInstall"), m_mcpInstall},
        {QStringLiteral("extensionsReload"), m_extensionsReload},
        {QStringLiteral("pageSwitches"), m_pageSwitches},
        {QStringLiteral("toasts"), m_toasts}
    };

    if (!error.trimmed().isEmpty()) {
        document.insert(QStringLiteral("error"), error.trimmed());
    }
    return document;
}

QJsonObject GuiRegressionRunner::statusSummary() const {
    if (!m_bridge) {
        return {};
    }

    const QVariantMap status = m_bridge->status();
    return QJsonObject{
        {QStringLiteral("workspacePath"), status.value(QStringLiteral("workspacePath")).toString()},
        {QStringLiteral("workspaceReady"), status.value(QStringLiteral("workspaceReady")).toBool()},
        {QStringLiteral("runtimeMode"), status.value(QStringLiteral("runtimeMode")).toString()},
        {QStringLiteral("runtimeEndpoint"), status.value(QStringLiteral("runtimeEndpoint")).toString()},
        {QStringLiteral("studioBackend"), status.value(QStringLiteral("studioBackend")).toString()},
        {QStringLiteral("studioBackendTransport"), status.value(QStringLiteral("studioBackendTransport")).toString()},
        {QStringLiteral("routedProvider"), status.value(QStringLiteral("routedProvider")).toString()},
        {QStringLiteral("actualBackend"), status.value(QStringLiteral("actualBackend")).toString()},
        {QStringLiteral("backendFallback"), status.value(QStringLiteral("backendFallback")).toBool()},
        {QStringLiteral("runtimeServiceEnabled"), status.value(QStringLiteral("runtimeServiceEnabled")).toBool()},
        {QStringLiteral("runtimeServiceReachable"), status.value(QStringLiteral("runtimeServiceReachable")).toBool()},
        {QStringLiteral("taskCount"), status.value(QStringLiteral("taskCount")).toInt()},
        {QStringLiteral("eventCount"), status.value(QStringLiteral("eventCount")).toInt()},
        {QStringLiteral("resourceCount"), status.value(QStringLiteral("resourceCount")).toInt()},
        {QStringLiteral("pluginCount"), status.value(QStringLiteral("pluginCount")).toInt()},
        {QStringLiteral("skillCount"), status.value(QStringLiteral("skillCount")).toInt()},
        {QStringLiteral("mcpServerCount"), status.value(QStringLiteral("mcpServerCount")).toInt()}
    };
}

QJsonObject GuiRegressionRunner::extensionSummary() const {
    if (!m_bridge) {
        return {};
    }

    if (!m_extensionsSummaryData.isEmpty()) {
        return m_extensionsSummaryData;
    }

    const QVariantList catalog = m_bridge->extensionCatalog();
    int installedCatalogCount = 0;
    for (const QVariant &entryValue : catalog) {
        if (entryValue.toMap().value(QStringLiteral("installed")).toBool()) {
            ++installedCatalogCount;
        }
    }

    QJsonArray pluginIds;
    for (const QVariant &recordValue : m_bridge->plugins()) {
        pluginIds.append(recordValue.toMap().value(QStringLiteral("id")).toString());
    }

    QJsonArray skillIds;
    for (const QVariant &recordValue : m_bridge->skills()) {
        skillIds.append(recordValue.toMap().value(QStringLiteral("id")).toString());
    }

    const QVariantMap tools = m_bridge->config().value(QStringLiteral("tools")).toMap();
    const QVariantMap mcpServers = tools.value(QStringLiteral("mcpServers")).toMap();

    QJsonArray mcpIds;
    for (auto it = mcpServers.constBegin(); it != mcpServers.constEnd(); ++it) {
        mcpIds.append(it.key());
    }

    return QJsonObject{
        {QStringLiteral("workspacePath"), workspacePath()},
        {QStringLiteral("catalogCount"), catalog.size()},
        {QStringLiteral("installedCatalogCount"), installedCatalogCount},
        {QStringLiteral("pluginCount"), statusCount(QStringLiteral("pluginCount"))},
        {QStringLiteral("skillCount"), statusCount(QStringLiteral("skillCount"))},
        {QStringLiteral("mcpServerCount"), statusCount(QStringLiteral("mcpServerCount"))},
        {QStringLiteral("initialPluginCount"), m_initialPluginCount},
        {QStringLiteral("initialSkillCount"), m_initialSkillCount},
        {QStringLiteral("initialMcpServerCount"), m_initialMcpServerCount},
        {QStringLiteral("pluginIds"), pluginIds},
        {QStringLiteral("skillIds"), skillIds},
        {QStringLiteral("mcpServerIds"), mcpIds}
    };
}

QJsonObject GuiRegressionRunner::previewSummary(const QString &reason,
                                                const QVariantMap &preview,
                                                int latencyMs) const {
    const QVariantList nodes = preview.value(QStringLiteral("nodes")).toList();
    return QJsonObject{
        {QStringLiteral("reason"), reason},
        {QStringLiteral("completed"), true},
        {QStringLiteral("latencyMs"), latencyMs},
        {QStringLiteral("resolved"), preview.value(QStringLiteral("resolved")).toBool()},
        {QStringLiteral("pending"), preview.value(QStringLiteral("pending")).toBool()},
        {QStringLiteral("message"), preview.value(QStringLiteral("message")).toString()},
        {QStringLiteral("routeSummary"), preview.value(QStringLiteral("routeSummary")).toString()},
        {QStringLiteral("resolutionSource"), preview.value(QStringLiteral("resolutionSource")).toString()},
        {QStringLiteral("nodeCount"), nodes.size()}
    };
}

} // namespace yaos::ui
