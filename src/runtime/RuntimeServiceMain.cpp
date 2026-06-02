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
#include "StructuredLog.h"
#include "LocalRuntimeClient.h"
#include "RuntimeHttpServer.h"
#include "RuntimeServiceSupport.h"
#include "Templates.h"

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
        endpoint = QStringLiteral("http://127.0.0.1:18890");
    }
    if (!endpoint.contains(QStringLiteral("://"))) {
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
    QCoreApplication::setApplicationName("yaos-runtime");
    QCoreApplication::setOrganizationName("yaos");

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--help")) || args.contains(QStringLiteral("-h"))) {
        QTextStream out(stdout);
        out << "Usage: yaos-runtime [--config <path>] [--endpoint <http://host:port>] [--advertise-endpoint <http://host:port>]\n"
            << "Runs the YAOS HTTP runtime service.\n";
        return 0;
    }

    const QString configPath = optionValue(args, QStringLiteral("--config"));
    yaos::config::Config cfg = yaos::config::ConfigLoader::load(configPath);

    const QString endpointOverride = optionValue(args, QStringLiteral("--endpoint"));
    const QString advertiseOverride = optionValue(args, QStringLiteral("--advertise-endpoint"));
    const QUrl endpoint = serviceUrlFrom(endpointOverride.isEmpty() ? cfg.runtime.endpoint : endpointOverride);
    if (!endpoint.isValid() || endpoint.scheme().toLower() != QStringLiteral("http")) {
        QTextStream out(stdout);
        out << "Invalid runtime service endpoint.\n";
        return 1;
    }

    QString listenHost;
    if (!resolveListenHost(endpoint.host(), &listenHost)) {
        QTextStream out(stdout);
        out << "Invalid listen host.\n";
        return 1;
    }
    const quint16 port = static_cast<quint16>(endpoint.port(18890));

    const QString workspace = cfg.workspacePath();
    yaos::runtime::syncWorkspaceTemplates(workspace);
    yaos::runtime::StructuredLog::install(workspace, QStringLiteral("yaos-runtime"));

    const yaos::config::Config serviceConfig =
        yaos::runtime::runtimeServiceConfig(cfg,
                                            endpoint.toString(QUrl::FullyEncoded),
                                            advertiseOverride);
    auto runtime = std::make_unique<yaos::runtime::RuntimeCore>(serviceConfig);
    const QJsonObject serviceHealth = runtime->serviceHealth();
    if (!serviceHealth.value(QStringLiteral("ok")).toBool(false)) {
        QTextStream out(stdout);
        out << serviceHealth.value(QStringLiteral("error"))
                   .toString(QStringLiteral("Failed to initialize runtime service."))
            << "\n";
        return 1;
    }
    yaos::runtime::LocalRuntimeClient client(std::move(runtime));
    yaos::runtime::RuntimeHttpServer server(client);
    QString error;
    if (!server.start(listenHost, port, &error)) {
        QTextStream out(stdout);
        out << (error.isEmpty() ? QStringLiteral("Failed to start runtime service.\n")
                                : error + QLatin1Char('\n'));
        return 1;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&server]() {
        server.stop();
    });

    QTextStream out(stdout);
    out << "yaos-runtime started.\n";
    out << "Endpoint: http://" << endpoint.host() << ":" << server.listenPort() << "\n";
    out << "Advertise endpoint: "
        << (serviceConfig.runtime.advertiseEndpoint.trimmed().isEmpty()
                ? QStringLiteral("none")
                : serviceConfig.runtime.advertiseEndpoint.trimmed())
        << "\n";
    out << "Workspace: " << workspace << "\n";
    out.flush();

    return app.exec();
}
