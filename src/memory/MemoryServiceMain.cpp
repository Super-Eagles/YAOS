#if defined(_MSC_VER) && (_MSC_VER >= 1600)
#pragma execution_character_set("utf-8")
#endif

#include <FastNet/Config.h>

#include <QCoreApplication>
#include <QTextCodec>
#include <QTextStream>
#include <QUrl>

#include <string>

#include "../config/ConfigLoader.h"
#include "../runtime/StructuredLog.h"
#include "../runtime/Templates.h"
#include "MemoryHttpServer.h"

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
        endpoint = QStringLiteral("http://127.0.0.1:18891");
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
    QCoreApplication::setApplicationName("yaos-memory");
    QCoreApplication::setOrganizationName("yaos");

    const QStringList args = app.arguments();
    if (args.contains("--help") || args.contains("-h")) {
        QTextStream out(stdout);
        out << "Usage: yaos-memory [--config <path>] [--endpoint <http://host:port>] [--api-key <token>]\n"
            << "Runs the YAOS HTTP memory service.\n";
        return 0;
    }

    const QString configPath = optionValue(args, QStringLiteral("--config"));
    yaos::config::Config cfg = yaos::config::ConfigLoader::load(configPath);

    const QString endpointOverride = optionValue(args, QStringLiteral("--endpoint"));
    const QString tokenOverride = optionValue(args, QStringLiteral("--api-key"));

    const QUrl endpoint = serviceUrlFrom(endpointOverride.isEmpty() ? cfg.memory.service.endpoint : endpointOverride);
    if (!endpoint.isValid() || endpoint.scheme().toLower() != QStringLiteral("http")) {
        QTextStream out(stdout);
        out << "Invalid memory service endpoint.\n";
        return 1;
    }

    QString listenHost;
    if (!resolveListenHost(endpoint.host(), &listenHost)) {
        QTextStream out(stdout);
        out << "Invalid listen host.\n";
        return 1;
    }
    const quint16 port = static_cast<quint16>(endpoint.port(18891));

    const QString workspace = cfg.workspacePath();
    yaos::runtime::syncWorkspaceTemplates(workspace);
    yaos::runtime::StructuredLog::install(workspace, QStringLiteral("yaos-memory"));

    yaos::memory::MemoryServiceCore core(workspace, cfg);
    if (!core.isReady()) {
        QTextStream out(stdout);
        out << (core.lastError().isEmpty() ? QStringLiteral("Failed to initialize memory service.\n")
                                           : core.lastError() + QLatin1Char('\n'));
        return 1;
    }

    const QString apiKey = tokenOverride.isEmpty() ? cfg.memory.service.apiKey : tokenOverride;
    yaos::memory::MemoryHttpServer server(core, apiKey);
    QString error;
    if (!server.start(listenHost, port, &error)) {
        QTextStream out(stdout);
        out << (error.isEmpty() ? QStringLiteral("Failed to start memory service.\n")
                                : error + QLatin1Char('\n'));
        return 1;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&server]() {
        server.stop();
    });

    QTextStream out(stdout);
    out << "yaos-memory started.\n";
    out << "Endpoint: http://" << endpoint.host() << ":" << server.listenPort() << "\n";
    out << "Workspace: " << workspace << "\n";
    out.flush();

    return app.exec();
}
