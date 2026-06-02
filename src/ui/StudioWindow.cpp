#include "StudioWindow.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QQmlError>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>
#include <QDebug>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

namespace yaos::ui {

namespace {

QString quickViewStatusName(QQuickView::Status status) {
    switch (status) {
    case QQuickView::Null:
        return QStringLiteral("Null");
    case QQuickView::Ready:
        return QStringLiteral("Ready");
    case QQuickView::Loading:
        return QStringLiteral("Loading");
    case QQuickView::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

QStringList candidateImportPaths() {
    QStringList paths;
    const QString appDir = QCoreApplication::applicationDirPath();
    paths << QDir(appDir).filePath("qml");
    paths << QDir(appDir).filePath("../qml");

    const QString qtQmlImports = QLibraryInfo::location(QLibraryInfo::Qml2ImportsPath);
    if (!qtQmlImports.isEmpty()) {
        paths << qtQmlImports;
    }

    const QString envImports = qEnvironmentVariable("QML2_IMPORT_PATH");
    if (!envImports.trimmed().isEmpty()) {
        const QChar separator =
#ifdef Q_OS_WIN
            ';';
#else
            ':';
#endif
        const QStringList envPaths = envImports.split(separator, QString::SkipEmptyParts);
        for (const QString &path : envPaths) {
            paths << path.trimmed();
        }
    }

    paths.removeDuplicates();
    return paths;
}

QStringList configuredImportPaths(QQmlEngine *engine) {
    QStringList added;
    if (!engine) {
        return added;
    }

    for (const QString &path : candidateImportPaths()) {
        if (!QDir(path).exists()) {
            continue;
        }
        engine->addImportPath(path);
        added << QDir(path).absolutePath();
    }
    added.removeDuplicates();
    return added;
}

void writeStartupLog(const QString &text) {
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath("yaos_startup.log");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << text << "\n";
}

void clearStartupLog() {
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath("yaos_startup.log");
    QFile::remove(path);
}

QPoint centeredWindowPosition(const QSize &windowSize) {
    QScreen *targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (!targetScreen) {
        return QPoint(80, 60);
    }

    const QRect available = targetScreen->availableGeometry();
    const int x = available.x() + qMax(0, (available.width() - windowSize.width()) / 2);
    const int y = available.y() + qMax(0, (available.height() - windowSize.height()) / 2);
    return QPoint(x, y);
}

} // namespace

StudioWindow::StudioWindow(const QString &initialPage, QWindow *parent)
    : QQuickView(parent),
      m_bridge(new StudioBridge(initialPage, this)) {
    qInfo().noquote() << QStringLiteral("StudioWindow constructing with initial page '%1'")
                             .arg(initialPage.trimmed().isEmpty() ? QStringLiteral("overview")
                                                                  : initialPage.trimmed().toLower());
    const QStringList importPaths = configuredImportPaths(engine());

    connect(this, &QQuickView::statusChanged, this, [this](QQuickView::Status nextStatus) {
        qInfo().noquote() << QStringLiteral("StudioWindow QQuickView status changed: %1")
                                 .arg(quickViewStatusName(nextStatus));
        if (nextStatus == QQuickView::Ready) {
            m_ready = (rootObject() != nullptr);
            m_errorString.clear();
            qInfo().noquote() << QStringLiteral("StudioWindow root object ready=%1")
                                     .arg(m_ready ? QStringLiteral("true") : QStringLiteral("false"));
            clearStartupLog();
            return;
        }
        if (nextStatus != QQuickView::Error) {
            return;
        }

        m_ready = false;
        const QList<QQmlError> loadErrors = errors();
        QStringList messages;
        for (const QQmlError &error : loadErrors) {
            messages.append(error.toString());
        }
        m_errorString = messages.join('\n');
        qWarning().noquote() << "Failed to load YAOS QML scene:\n" << m_errorString;
        const QString details = QString("Import paths:\n%1\n\nErrors:\n%2")
                                    .arg(engine() ? engine()->importPathList().join('\n') : QString(),
                                         m_errorString);
        writeStartupLog(details);
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    });

    setColor(Qt::transparent);
    setFlags(Qt::FramelessWindowHint | Qt::Window);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setMinimumSize(QSize(1024, 680));
    resize(1480, 940);
    setPosition(centeredWindowPosition(size()));

    rootContext()->setContextProperty("studioBridge", m_bridge);
    m_bridge->attachWindow(this);
    qInfo().noquote() << "YAOS QML import paths:\n" << importPaths.join('\n');
    qInfo().noquote() << "StudioWindow loading qrc:/qml/Startup.qml";
    setSource(QUrl(QStringLiteral("qrc:/qml/Startup.qml")));

    qInfo().noquote() << "StudioWindow queued StudioBridge::beginStartup";
    QTimer::singleShot(0, m_bridge, &StudioBridge::beginStartup);
}

StudioBridge *StudioWindow::bridge() const {
    return m_bridge;
}

bool StudioWindow::ready() const {
    return m_ready;
}

QString StudioWindow::errorString() const {
    return m_errorString;
}

} // namespace yaos::ui
