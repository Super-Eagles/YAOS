#if defined(_MSC_VER) && (_MSC_VER >= 1600)
#pragma execution_character_set("utf-8")
#endif

#include <QCoreApplication>
#include <QTextCodec>
#include <QTextStream>

#include "../config/ConfigLoader.h"
#include "LocalDaemonProtocol.h"
#include "LocalDaemonServer.h"

int main(int argc, char *argv[]) {
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("yaosd");
    QCoreApplication::setOrganizationName("yaos");

    const QStringList args = app.arguments();
    if (args.contains("--help") || args.contains("-h")) {
        QTextStream out(stdout);
        out << "Usage: yaosd [--server <name>]\n"
            << "Runs the YAOS local runtime daemon over FastNet loopback TCP.\n";
        return 0;
    }

    const yaos::config::Config cfg = yaos::config::ConfigLoader::load();
    QString requestedServerName;
    const int serverIndex = args.indexOf("--server");
    if (serverIndex > 0 && serverIndex + 1 < args.size()) {
        requestedServerName = args.at(serverIndex + 1).trimmed();
    }

    yaos::daemon::LocalDaemonServer server;
    QString error;
    const QString serverName = yaos::daemon::protocol::resolveServerName(cfg, requestedServerName);
    if (!server.start(serverName, &error)) {
        QTextStream out(stdout);
        out << (error.isEmpty() ? QStringLiteral("Failed to start yaosd.\n")
                                : error + QLatin1Char('\n'));
        return 1;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&server]() {
        server.stop();
    });

    QTextStream out(stdout);
    out << "yaosd started.\n";
    out << "Server: " << server.serverName() << "\n";
    out << "Endpoint: 127.0.0.1:" << server.serverPort() << "\n";
    out.flush();
    return app.exec();
}
