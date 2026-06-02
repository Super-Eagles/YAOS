#ifndef YAOS_RUNTIME_STRUCTUREDLOG_H
#define YAOS_RUNTIME_STRUCTUREDLOG_H

#include <QJsonObject>
#include <QString>

namespace yaos::runtime {

class ScopedTraceContext {
public:
    explicit ScopedTraceContext(const QString &traceId = QString());
    ~ScopedTraceContext();

    QString traceId() const;

private:
    QString _previousTraceId;
    QString _traceId;
};

class StructuredLog {
public:
    static void install(const QString &workspace, const QString &serviceName);
    static void log(const QString &level,
                    const QString &category,
                    const QString &message,
                    const QJsonObject &metadata = QJsonObject());
    static QString currentTraceId();
    static QString ensureTraceId(const QString &candidate = QString());
    static QString activeLogFilePath();
};

} // namespace yaos::runtime

#endif // YAOS_RUNTIME_STRUCTUREDLOG_H
