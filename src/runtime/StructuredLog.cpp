#include "StructuredLog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QUuid>
#include <QVector>
#include <QtGlobal>

#include <cstdlib>
#include <cstdio>

namespace yaos::runtime {

namespace {

struct StructuredLogState {
    QMutex mutex;
    QString workspace;
    QString serviceName;
    QString filePath;
    QtMessageHandler previousHandler = nullptr;
    bool handlerInstalled = false;
};

StructuredLogState &state() {
    static StructuredLogState instance;
    return instance;
}

thread_local QString g_currentTraceId;
thread_local bool g_messageHandlerActive = false;

constexpr qint64 kMaxLogSizeBytes = 8 * 1024 * 1024;
constexpr int kMaxRetainedLines = 4000;

QString normalizedServiceName(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) {
        value = QCoreApplication::applicationName().trimmed();
    }
    if (value.isEmpty()) {
        value = QStringLiteral("yaos");
    }

    QString normalized;
    normalized.reserve(value.size());
    for (const QChar ch : value) {
        if (ch.isLetterOrNumber()) {
            normalized.append(ch.toLower());
        } else if (ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            normalized.append(ch);
        } else if (!normalized.endsWith(QLatin1Char('-'))) {
            normalized.append(QLatin1Char('-'));
        }
    }

    while (normalized.endsWith(QLatin1Char('-'))) {
        normalized.chop(1);
    }
    return normalized.isEmpty() ? QStringLiteral("yaos") : normalized;
}

QString filePathFor(const QString &workspace, const QString &serviceName) {
    const QString normalizedWorkspace = workspace.trimmed();
    const QString normalizedService = normalizedServiceName(serviceName);
    const QString fileName = QStringLiteral("%1-%2.jsonl")
                                 .arg(normalizedService,
                                      QString::number(QCoreApplication::applicationPid()));
    return QDir(normalizedWorkspace).filePath(QStringLiteral("runtime/logs/%1").arg(fileName));
}

QString generatedTraceId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString traceIdFromMetadata(const QJsonObject &metadata) {
    const QString traceId = metadata.value(QStringLiteral("traceId")).toString().trimmed();
    if (!traceId.isEmpty()) {
        return traceId;
    }
    return metadata.value(QStringLiteral("trace_id")).toString().trimmed();
}

QString levelForMessageType(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("debug");
    case QtInfoMsg:
        return QStringLiteral("info");
    case QtWarningMsg:
        return QStringLiteral("warning");
    case QtCriticalMsg:
        return QStringLiteral("error");
    case QtFatalMsg:
        return QStringLiteral("fatal");
    }
    return QStringLiteral("info");
}

void trimLogFileIfNeeded(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists() || info.size() <= kMaxLogSizeBytes) {
        return;
    }

    QVector<QByteArray> lines;
    QFile readFile(path);
    if (readFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!readFile.atEnd()) {
            const QByteArray line = readFile.readLine().trimmed();
            if (!line.isEmpty()) {
                lines.append(line);
            }
        }
        readFile.close();
    }

    if (lines.size() > kMaxRetainedLines) {
        lines = lines.mid(lines.size() - kMaxRetainedLines);
    }

    QFile writeFile(path);
    if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }
    for (const QByteArray &line : lines) {
        writeFile.write(line);
        writeFile.write("\n");
    }
    writeFile.close();
}

void appendRecord(const QJsonObject &record) {
    StructuredLogState &logState = state();
    QMutexLocker locker(&logState.mutex);
    if (logState.filePath.isEmpty()) {
        return;
    }

    QDir().mkpath(QFileInfo(logState.filePath).absolutePath());
    QFile file(logState.filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    file.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
    file.write("\n");
    file.close();

    trimLogFileIfNeeded(logState.filePath);
}

void structuredQtMessageHandler(QtMsgType type,
                                const QMessageLogContext &context,
                                const QString &message) {
    if (g_messageHandlerActive) {
        if (state().previousHandler) {
            state().previousHandler(type, context, message);
        }
        if (type == QtFatalMsg) {
            std::fflush(stderr);
            std::abort();
        }
        return;
    }

    g_messageHandlerActive = true;

    QJsonObject metadata;
    if (context.file) {
        metadata.insert(QStringLiteral("file"), QString::fromUtf8(context.file));
    }
    if (context.function) {
        metadata.insert(QStringLiteral("function"), QString::fromUtf8(context.function));
    }
    if (context.line > 0) {
        metadata.insert(QStringLiteral("line"), context.line);
    }

    StructuredLog::log(levelForMessageType(type),
                       context.category ? QString::fromUtf8(context.category) : QStringLiteral("qt"),
                       message,
                       metadata);

    if (state().previousHandler) {
        state().previousHandler(type, context, message);
    }

    g_messageHandlerActive = false;

    if (type == QtFatalMsg) {
        std::fflush(stderr);
        std::abort();
    }
}

} // namespace

ScopedTraceContext::ScopedTraceContext(const QString &traceId)
    : _previousTraceId(g_currentTraceId),
      _traceId(StructuredLog::ensureTraceId(traceId)) {
    g_currentTraceId = _traceId;
}

ScopedTraceContext::~ScopedTraceContext() {
    g_currentTraceId = _previousTraceId;
}

QString ScopedTraceContext::traceId() const {
    return _traceId;
}

void StructuredLog::install(const QString &workspace, const QString &serviceName) {
    StructuredLogState &logState = state();
    {
        QMutexLocker locker(&logState.mutex);
        logState.workspace = workspace.trimmed();
        logState.serviceName = normalizedServiceName(serviceName);
        logState.filePath = filePathFor(logState.workspace, logState.serviceName);
        if (!logState.handlerInstalled) {
            logState.previousHandler = qInstallMessageHandler(structuredQtMessageHandler);
            logState.handlerInstalled = true;
        }
    }

    log(QStringLiteral("info"),
        QStringLiteral("bootstrap"),
        QStringLiteral("Structured logging initialized"),
        QJsonObject{
            {QStringLiteral("workspace"), workspace.trimmed()},
            {QStringLiteral("service"), normalizedServiceName(serviceName)},
            {QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid())},
            {QStringLiteral("logFile"), activeLogFilePath()}
        });
}

void StructuredLog::log(const QString &level,
                        const QString &category,
                        const QString &message,
                        const QJsonObject &metadata) {
    StructuredLogState &logState = state();
    QString filePath;
    QString serviceName;
    {
        QMutexLocker locker(&logState.mutex);
        filePath = logState.filePath;
        serviceName = logState.serviceName;
    }
    if (filePath.isEmpty()) {
        return;
    }

    QJsonObject effectiveMetadata = metadata;
    QString traceId = traceIdFromMetadata(effectiveMetadata);
    if (traceId.isEmpty()) {
        traceId = currentTraceId();
    }
    if (!traceId.isEmpty() &&
        !effectiveMetadata.contains(QStringLiteral("traceId")) &&
        !effectiveMetadata.contains(QStringLiteral("trace_id"))) {
        effectiveMetadata.insert(QStringLiteral("trace_id"), traceId);
    }

    QJsonObject record;
    record.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    record.insert(QStringLiteral("service"), serviceName);
    record.insert(QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid()));
    record.insert(QStringLiteral("threadId"),
                  QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId())));
    record.insert(QStringLiteral("level"), level.trimmed().isEmpty() ? QStringLiteral("info") : level.trimmed());
    record.insert(QStringLiteral("category"),
                  category.trimmed().isEmpty() ? QStringLiteral("system") : category.trimmed());
    record.insert(QStringLiteral("message"), message);
    if (!traceId.isEmpty()) {
        record.insert(QStringLiteral("traceId"), traceId);
    }
    record.insert(QStringLiteral("metadata"), effectiveMetadata);

    appendRecord(record);
}

QString StructuredLog::currentTraceId() {
    return g_currentTraceId.trimmed();
}

QString StructuredLog::ensureTraceId(const QString &candidate) {
    const QString normalized = candidate.trimmed();
    if (!normalized.isEmpty()) {
        return normalized;
    }
    if (!g_currentTraceId.trimmed().isEmpty()) {
        return g_currentTraceId.trimmed();
    }
    return generatedTraceId();
}

QString StructuredLog::activeLogFilePath() {
    StructuredLogState &logState = state();
    QMutexLocker locker(&logState.mutex);
    return logState.filePath;
}

} // namespace yaos::runtime
