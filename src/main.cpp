#include <QGuiApplication>
#include <QFont>
#include <QIcon>
#include <QQuickStyle>
#include <QTextCodec>
#include <QTextStream>
#include <QTimer>

#include <cstdlib>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <cstdio>
#endif

#include "app/ApplicationController.h"
#include "config/ConfigLoader.h"
#include "platform/network/FastNetLifecycle.h"
#include "runtime/StructuredLog.h"
#include "ui/GuiRegressionRunner.h"
#include "ui/StudioWindow.h"

#ifdef Q_OS_WIN
namespace {

bool standardHandleIsRedirected(DWORD stdHandle) {
    const HANDLE handle = GetStdHandle(stdHandle);
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD consoleMode = 0;
    if (GetConsoleMode(handle, &consoleMode)) {
        return false;
    }

    const DWORD fileType = GetFileType(handle);
    return fileType == FILE_TYPE_DISK || fileType == FILE_TYPE_PIPE;
}

bool anyStandardHandleRedirected() {
    return standardHandleIsRedirected(STD_INPUT_HANDLE) ||
           standardHandleIsRedirected(STD_OUTPUT_HANDLE) ||
           standardHandleIsRedirected(STD_ERROR_HANDLE);
}

void configureConsoleCodePage() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
}

void redirectConsoleStreams() {
#ifdef _MSC_VER
    FILE *inputStream = nullptr;
    FILE *outputStream = nullptr;
    FILE *errorStream = nullptr;
    freopen_s(&inputStream, "CONIN$", "r", stdin);
    freopen_s(&outputStream, "CONOUT$", "w", stdout);
    freopen_s(&errorStream, "CONOUT$", "w", stderr);
#else
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
#endif
    configureConsoleCodePage();
}

void ensureConsoleForCli() {
    // Keep file/pipe redirection intact for CLI commands on Windows GUI builds.
    if (anyStandardHandleRedirected()) {
        return;
    }
    if (GetConsoleWindow()) {
        redirectConsoleStreams();
        return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        AllocConsole();
    }
    redirectConsoleStreams();
}

void detachConsoleIfPresent() {
    if (GetConsoleWindow()) {
        FreeConsole();
    }
}
} // namespace
#endif

namespace {

QString optionValue(const QStringList &args,
                    const QString &name,
                    const QString &fallback = QString()) {
    for (int i = 0; i < args.size(); ++i) {
        const QString current = args.at(i).trimmed();
        if (current == name && i + 1 < args.size()) {
            return args.at(i + 1).trimmed();
        }
        const QString prefix = name + QLatin1Char('=');
        if (current.startsWith(prefix)) {
            return current.mid(prefix.size()).trimmed();
        }
    }
    return fallback;
}

void appendLoggingRule(const QByteArray &rule) {
    if (rule.isEmpty()) {
        return;
    }
    QByteArray rules = qgetenv("QT_LOGGING_RULES");
    if (!rules.isEmpty() && !rules.endsWith('\n') && !rules.endsWith(';')) {
        rules.append('\n');
    }
    rules.append(rule);
    qputenv("QT_LOGGING_RULES", rules);
}

int optionIntValue(const QStringList &args,
                   const QString &name,
                   int fallback,
                   int minimum = 1) {
    bool ok = false;
    const int parsed = optionValue(args, name).toInt(&ok);
    if (!ok) {
        return fallback;
    }
    return qMax(minimum, parsed);
}

yaos::ui::GuiRegressionOptions parseGuiRegressionOptions(const QStringList &args) {
    yaos::ui::GuiRegressionOptions options;
    options.caseId = optionValue(args, QStringLiteral("--case"), options.caseId).trimmed().toLower();
    options.jsonOutputPath = optionValue(args, QStringLiteral("--json-out")).trimmed();
    options.switchCount = optionIntValue(args, QStringLiteral("--switch-count"), options.switchCount);
    options.timeoutMs = optionIntValue(args, QStringLiteral("--timeout-ms"), options.timeoutMs, 1000);
    options.pageSettleMs = optionIntValue(args, QStringLiteral("--page-settle-ms"), options.pageSettleMs, 10);
    options.heartbeatIntervalMs = optionIntValue(args,
                                                 QStringLiteral("--heartbeat-interval-ms"),
                                                 options.heartbeatIntervalMs,
                                                 20);
    options.maxUiGapMs = optionIntValue(args, QStringLiteral("--max-ui-gap-ms"), options.maxUiGapMs, 100);
    return options;
}

[[noreturn]] void finishGuiRegressionProcess(int exitCode) {
#ifdef Q_OS_WIN
    ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(exitCode));
#else
    std::_Exit(exitCode);
#endif
}

} // namespace

int main(int argc, char *argv[]) {
    // 强制控制台环境处理 UTF-8
    appendLoggingRule("qt.network.monitor.warning=false");

    QGuiApplication app(argc, argv);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    QGuiApplication::setApplicationName("YAOS");
    QGuiApplication::setOrganizationName("YAOS");
    const QIcon appIcon(QStringLiteral(":/app/YAOS.ico"));
    if (!appIcon.isNull()) {
        QGuiApplication::setWindowIcon(appIcon);
    }

    QFont uiFont;
#ifdef Q_OS_WIN
    uiFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
#elif defined(Q_OS_MACOS)
    uiFont.setFamily(QStringLiteral("PingFang SC"));
#else
    uiFont.setFamily(QStringLiteral("Noto Sans CJK SC"));
#endif
    uiFont.setPointSize(10);
    app.setFont(uiFont);

    const QStringList args = app.arguments();
    const yaos::config::Config bootstrapConfig = yaos::config::ConfigLoader::load();
    yaos::runtime::StructuredLog::install(bootstrapConfig.workspacePath(), QStringLiteral("yaos"));
    const QString command = (args.size() > 1) ? args.at(1).trimmed().toLower() : QString();
    const bool hasOneShotMessage = args.contains("--message") || args.contains("-m");
    const bool wantsInteractiveAgent = args.contains("--interactive") || args.contains("--cli");
    const bool wantsHelp = args.contains("--help") || args.contains("-h");
    const bool launchGuiRegression = !wantsHelp && command == "gui-regression";
    const bool launchGui =
        !wantsHelp &&
        (args.size() <= 1 ||
         command == "gui" ||
         command == "dashboard" ||
         command == "config" ||
         (command == "agent" && !hasOneShotMessage && !wantsInteractiveAgent));

    if (launchGui || launchGuiRegression) {
#ifdef Q_OS_WIN
        if (launchGui) {
            detachConsoleIfPresent();
        }
#endif
        QQuickStyle::setStyle(QStringLiteral("Fusion"));
        QString initialPage = launchGuiRegression ? QStringLiteral("security") : QStringLiteral("overview");
        if (!launchGuiRegression && command == "config") {
            initialPage = "providers";
        } else if (!launchGuiRegression && command == "agent") {
            initialPage = "chat";
        } else if (!launchGuiRegression && command == "dashboard") {
            initialPage = "overview";
        }

        yaos::ui::StudioWindow window(initialPage);
        if (!appIcon.isNull()) {
            window.setIcon(appIcon);
        }
        window.show();
        yaos::platform::registerFastNetCleanup();

        if (launchGuiRegression) {
            const yaos::ui::GuiRegressionOptions options = parseGuiRegressionOptions(args.mid(2));
            yaos::ui::GuiRegressionRunner runner(&window, options, &app);
            bool completed = false;
            int exitCode = 1;
            QObject::connect(&runner, &yaos::ui::GuiRegressionRunner::finished, &app, [&](int code) {
                completed = true;
                exitCode = code;
                app.exit(code);
                finishGuiRegressionProcess(code);
            });
            QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&]() {
                if (!completed) {
                    exitCode = 1;
                }
            });
            QTimer::singleShot(0, &runner, &yaos::ui::GuiRegressionRunner::start);
            app.exec();
            return exitCode;
        }

        return app.exec();
    }

#ifdef Q_OS_WIN
    ensureConsoleForCli();
#endif
    yaos::app::ApplicationController controller;
    const yaos::app::RunResult result = controller.run(args);

    if (result == yaos::app::RunResult::EnterEventLoop) {
        return app.exec();
    }
    return (result == yaos::app::RunResult::Ok) ? 0 : 1;
}
