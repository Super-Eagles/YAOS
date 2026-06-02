#include "ExecTool.h"

#include <QJsonArray>
#include <QLoggingCategory>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QDir>
#include <QFileInfo>
#include <utility>

Q_LOGGING_CATEGORY(lcExec, "yaos.tools.exec")

namespace yaos::agent::tools {

namespace {

// ✅ 使用更全面的危险模式列表,覆盖常见绕过方式
struct DenyPattern {
    QRegularExpression re;
    QString reason;
};

const QList<DenyPattern> kDenyPatterns = {
    // 递归删除
    {QRegularExpression("\\brm\\s+.*-[^\\s]*[rf]", QRegularExpression::CaseInsensitiveOption),
     "recursive/force rm"},
    {QRegularExpression("\\b(del|erase)\\s+/[fqs]", QRegularExpression::CaseInsensitiveOption),
     "force delete"},
    {QRegularExpression("\\brmdir\\s+/s", QRegularExpression::CaseInsensitiveOption),
     "recursive rmdir"},
    {QRegularExpression("\\b(remove-item|rd)\\b.*(-recurse|-force)", QRegularExpression::CaseInsensitiveOption),
     "recursive powershell delete"},
    // 磁盘/设备操作
    {QRegularExpression("\\bdd\\b.*\\bof=/dev/", QRegularExpression::CaseInsensitiveOption),
     "raw disk write"},
    {QRegularExpression("\\bmkfs\\b", QRegularExpression::CaseInsensitiveOption),
     "filesystem format"},
    {QRegularExpression("\\bformat\\b\\s+[a-z]:", QRegularExpression::CaseInsensitiveOption),
     "filesystem format"},
    // 系统关机
    {QRegularExpression("\\b(shutdown|reboot|poweroff|halt)\\b", QRegularExpression::CaseInsensitiveOption),
     "system shutdown"},
    // Fork bomb
    {QRegularExpression(":\\(\\)\\s*\\{.*\\|.*:.*&.*\\}", QRegularExpression::CaseInsensitiveOption),
     "fork bomb"},
    // 覆盖关键系统目录（仅禁止写 / 和 /etc /bin /usr,不禁止读）
    {QRegularExpression(">\\s*/etc/|>\\s*/bin/|>\\s*/usr/|>\\s*/boot/", QRegularExpression::CaseInsensitiveOption),
     "writing to system directory"},
};

bool isDangerous(const QString &command, QString *reason = nullptr) {
    for (const auto &p : kDenyPatterns) {
        if (p.re.match(command).hasMatch()) {
            if (reason) *reason = p.reason;
            return true;
        }
    }
    return false;
}

QString resolveWorkingDirectory(const QString &input, const QString &fallback) {
    if (input.trimmed().isEmpty()) {
        return fallback;
    }

    QFileInfo info(input);
    if (info.isAbsolute()) {
        return QDir::cleanPath(info.absoluteFilePath());
    }
    return QDir(fallback).filePath(input);
}

QString normalizeAbsolutePath(const QString &path) {
    QFileInfo info(path);
    QString normalized;
    if (info.exists()) {
        normalized = info.canonicalFilePath();
    } else {
        normalized = info.absoluteFilePath();
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(normalized));
}

bool isWithinDirectory(const QString &path, const QString &allowedDir) {
    QString allowed = normalizeAbsolutePath(allowedDir);
    QString candidate = normalizeAbsolutePath(path);
    if (allowed.isEmpty() || candidate.isEmpty()) {
        return false;
    }

#ifdef Q_OS_WIN
    allowed = allowed.toLower();
    candidate = candidate.toLower();
#endif

    if (candidate == allowed) {
        return true;
    }
    if (!allowed.endsWith('/')) {
        allowed += '/';
    }
    return candidate.startsWith(allowed);
}

} // namespace

ExecTool::ExecTool(QString workingDir, int timeoutSec, QString pathAppend, QString allowedDir)
    : _workingDir(std::move(workingDir)),
      _timeoutSec(timeoutSec),
      _pathAppend(std::move(pathAppend)),
      _allowedDir(std::move(allowedDir)) {}

QString ExecTool::name() const { return "exec"; }
QString ExecTool::description() const { return "在本地 shell 中执行系统命令."; }

QJsonObject ExecTool::parameters() const {
    QJsonObject props;
    props["command"] = QJsonObject{{"type", "string"}, {"description", "要执行的命令"}};
    props["cwd"] = QJsonObject{{"type", "string"}, {"description", "工作目录（可选,默认为 workspace）"}};
    return QJsonObject{
        {"type", "object"},
        {"properties", props},
        {"required", QJsonArray{"command"}}
    };
}

QString ExecTool::execute(const QJsonObject &params) {
    const QString command = params.value("command").toString().trimmed();
    if (command.isEmpty()) return "错误: 必须提供 'command' 参数";

    QString denyReason;
    if (isDangerous(command, &denyReason)) {
        qWarning(lcExec) << "Blocked dangerous command:" << command << "reason:" << denyReason;
        return QString("错误: 命令被拒绝（%1）").arg(denyReason);
    }

    const QString cwd = resolveWorkingDirectory(params.value("cwd").toString().trimmed(), _workingDir);
    if (!_allowedDir.trimmed().isEmpty() && !isWithinDirectory(cwd, _allowedDir)) {
        return "Error: working directory is outside the allowed workspace.";
    }

    QProcess proc;
    proc.setWorkingDirectory(cwd);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!_pathAppend.isEmpty()) {
        // ✅ 修复：使用平台正确的路径分隔符
#ifdef Q_OS_WIN
        const QChar sep = ';';
#else
        const QChar sep = ':';
#endif
        env.insert("PATH", env.value("PATH") + sep + _pathAppend);
    }
    proc.setProcessEnvironment(env);

    qDebug(lcExec) << "Executing:" << command.left(120);

#ifdef Q_OS_WIN
    proc.start("cmd.exe", QStringList{"/c", command});
#else
    proc.start("/bin/sh", QStringList{"-lc", command});
#endif

    if (!proc.waitForStarted()) {
        return "Error: failed to start command.";
    }
    if (!proc.waitForFinished(_timeoutSec * 1000)) {
        proc.kill();
        proc.waitForFinished(3000);
        qWarning(lcExec) << "Command timed out after" << _timeoutSec << "s";
        return QString("Error: command timed out after %1 seconds").arg(_timeoutSec);
    }

    QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QString stderrText = QString::fromUtf8(proc.readAllStandardError());
    if (!stderrText.trimmed().isEmpty()) {
        output += "\nSTDERR:\n" + stderrText;
    }
    if (proc.exitCode() != 0) {
        output += QString("\nExit code: %1").arg(proc.exitCode());
    }
    if (output.trimmed().isEmpty()) {
        output = "(no output)";
    }
    const int maxLen = 10000;
    if (output.size() > maxLen) {
        output = output.left(maxLen) + "\n... (truncated)";
    }
    return output;
}

} // namespace yaos::agent::tools
