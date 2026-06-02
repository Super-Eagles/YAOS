#if defined(_MSC_VER) && (_MSC_VER >= 1600)
#pragma execution_character_set("utf-8")
#endif

#include <FastNet/Config.h>

#include <QCoreApplication>
#include <QTextCodec>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <string>

#include "../config/ConfigLoader.h"
#include "../runtime/StructuredLog.h"
#include "../runtime/Templates.h"
#include "ControlHttpServer.h"

namespace {

QString optionValue(const QStringList &args, const QString &name) {
    const int index = args.indexOf(name);
    if (index >= 0 && index + 1 < args.size()) {
        return args.at(index + 1).trimmed();
    }
    return QString();
}

QUrl serviceUrlFrom(QString endpoint) {
    endpoint = endpoint.trimmed();
    if (endpoint.isEmpty()) {
        endpoint = QStringLiteral("http://127.0.0.1:18892");
    }
    if (!endpoint.contains("://")) {
        endpoint.prepend(QStringLiteral("http://"));
    }
    return QUrl(endpoint);
}

bool resolveListenHost(const QString &host, QString *listenHost) {
    if (!listenHost) {
        return false;
    }

    const QString trimmed = host.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("127.0.0.1") || trimmed == QStringLiteral("localhost")) {
        *listenHost = QStringLiteral("127.0.0.1");
        return true;
    }
    if (trimmed == QStringLiteral("*") || trimmed == QStringLiteral("0.0.0.0")) {
        *listenHost = QStringLiteral("0.0.0.0");
        return true;
    }
    if (trimmed == QStringLiteral("::")) {
        *listenHost = QStringLiteral("::");
        return true;
    }
    if (trimmed == QStringLiteral("::1")) {
        *listenHost = QStringLiteral("::1");
        return true;
    }

    const QByteArray utf8 = trimmed.toUtf8();
    const std::string value(utf8.constData(), static_cast<size_t>(utf8.size()));
    if (FastNet::Address::isValidIPv4(value) || FastNet::Address::isValidIPv6(value)) {
        *listenHost = trimmed;
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char *argv[]) {
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("yaos-control");
    QCoreApplication::setOrganizationName("yaos");

    const QStringList args = app.arguments();
    if (args.contains("--help") || args.contains("-h")) {
        QTextStream out(stdout);
        out << "Usage: yaos-control [--config <path>] [--endpoint <http://host:port>]\n"
            << "Runs the YAOS HTTP control-plane service.\n";
        return 0;
    }

    const QString configPath = optionValue(args, QStringLiteral("--config"));
    yaos::config::Config cfg = yaos::config::ConfigLoader::load(configPath);

    const QString endpointOverride = optionValue(args, QStringLiteral("--endpoint"));
    const QUrl endpoint = serviceUrlFrom(endpointOverride.isEmpty() ? cfg.deployment.controlPlaneUrl : endpointOverride);
    if (!endpoint.isValid() || endpoint.scheme().toLower() != QStringLiteral("http")) {
        QTextStream out(stdout);
        out << "Invalid control service endpoint.\n";
        return 1;
    }

    QString listenHost;
    if (!resolveListenHost(endpoint.host(), &listenHost)) {
        QTextStream out(stdout);
        out << "Invalid listen host.\n";
        return 1;
    }
    const quint16 port = static_cast<quint16>(endpoint.port(18892));

    const QString workspace = cfg.workspacePath();
    yaos::runtime::syncWorkspaceTemplates(workspace);
    yaos::runtime::StructuredLog::install(workspace, QStringLiteral("yaos-control"));

    yaos::control::ControlServiceCore core(workspace, cfg);
    if (!core.isReady()) {
        QTextStream out(stdout);
        out << (core.lastError().isEmpty() ? QStringLiteral("Failed to initialize control service.\n")
                                           : core.lastError() + QLatin1Char('\n'));
        return 1;
    }

    yaos::control::ControlHttpServer server(core);
    QString error;
    if (!server.start(listenHost, port, &error)) {
        QTextStream out(stdout);
        out << (error.isEmpty() ? QStringLiteral("Failed to start control service.\n")
                                : error + QLatin1Char('\n'));
        return 1;
    }

    core.refreshNodeHealth(true);

    QTimer healthTimer;
    healthTimer.setInterval(15000);
    healthTimer.setSingleShot(false);
    QObject::connect(&healthTimer, &QTimer::timeout, [&core]() {
        core.refreshNodeHealth(false);
    });
    healthTimer.start();

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&server]() {
        server.stop();
    });

    QTextStream out(stdout);
    out << "yaos-control started.\n";
    out << "Endpoint: http://" << endpoint.host() << ":" << server.listenPort() << "\n";
    out << "Workspace: " << workspace << "\n";
    out.flush();

    return app.exec();
}
