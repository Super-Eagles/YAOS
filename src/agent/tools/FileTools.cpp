#include "FileTools.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray> 
#include <QSaveFile>
#include <QTextStream>
#include <utility>

namespace yaos::agent::tools {

namespace {

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

QString resolvePath(const QString &input, const QString &workspace, const QString &allowedDir, QString *error) {
    if (input.trimmed().isEmpty()) {
        if (error) *error = "Path is required.";
        return QString();
    }

    QFileInfo fi(input);
    QString abs = fi.isAbsolute() ? fi.absoluteFilePath() : QDir(workspace).filePath(input);
    abs = QDir::cleanPath(abs);

    if (!allowedDir.trimmed().isEmpty()) {
        if (!isWithinDirectory(abs, allowedDir)) {
            if (error) *error = "Path outside allowed directory.";
            return QString();
        }
    }
    return abs;
}

} // namespace

ReadFileTool::ReadFileTool(QString workspace, QString allowedDir)
    : _workspace(std::move(workspace)), _allowedDir(std::move(allowedDir)) {}

QString ReadFileTool::name() const { return "read_file"; }
QString ReadFileTool::description() const { return "读取文件内容."; }

QJsonObject ReadFileTool::parameters() const {
    QJsonObject props;
    props["path"] = QJsonObject{{"type", "string"}, {"description", "文件路径"}};
    return QJsonObject{
        {"type", "object"},
        {"properties", props},
        {"required", QJsonArray{"path"}}
    };
}

QString ReadFileTool::execute(const QJsonObject &params) {
    const QString path = params.value("path").toString();
    QString err;
    if (path.isEmpty()) return "错误: 必须提供 'path' 参数";
    const QString abs = resolvePath(path, _workspace, _allowedDir, &err);
    if (abs.isEmpty()) return "错误: " + err;

    QFileInfo info(abs);
    if (!info.exists()) return "错误: 文件不存在";
    QFile file(abs);
    if (!file.open(QIODevice::ReadOnly)) return "错误: 无法打开文件进行读取";

    const QByteArray data = file.readAll();
    file.close();
    QString content = QString::fromUtf8(data);
    const int kMaxChars = 128000;
    if (content.size() > kMaxChars) {
        content = content.left(kMaxChars) + "\n\n... (内容已截断)";
    }
    return content;
}

WriteFileTool::WriteFileTool(QString workspace, QString allowedDir)
    : _workspace(std::move(workspace)), _allowedDir(std::move(allowedDir)) {}

QString WriteFileTool::name() const { return "write_file"; }
QString WriteFileTool::description() const { return "将文件内容写入路径."; }

QJsonObject WriteFileTool::parameters() const {
    QJsonObject props;
    props["path"] = QJsonObject{{"type", "string"}, {"description", "文件路径"}};
    props["content"] = QJsonObject{{"type", "string"}, {"description", "内容"}};
    return QJsonObject{
        {"type", "object"},
        {"properties", props},
        {"required", QJsonArray{"path", "content"}}
    };
}

QString WriteFileTool::execute(const QJsonObject &params) {
    const QString path = params.value("path").toString();
    const QString content = params.value("content").toString();
    QString err;
    if (path.isEmpty()) return "错误: 必须提供 'path' 参数";
    const QString abs = resolvePath(path, _workspace, _allowedDir, &err);
    if (abs.isEmpty()) return "错误: " + err;

    const QFileInfo info(abs);
    QDir().mkpath(info.absolutePath());
    QFile file(abs);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return "错误: 无法写入文件.";
    }
    QTextStream out(&file);
    out << content;
    file.close();
    return "成功写入文件: " + abs;
}

EditFileTool::EditFileTool(QString workspace, QString allowedDir)
    : _workspace(std::move(workspace)), _allowedDir(std::move(allowedDir)) {}

QString EditFileTool::name() const { return "edit_file"; }
QString EditFileTool::description() const { return "通过精确文本替换编辑已有文件."; }

QJsonObject EditFileTool::parameters() const {
    QJsonObject props;
    props["path"] = QJsonObject{{"type", "string"}, {"description", "文件路径"}};
    props["old_text"] = QJsonObject{{"type", "string"}, {"description", "需要被替换的原始文本"}};
    props["new_text"] = QJsonObject{{"type", "string"}, {"description", "替换后的文本"}};
    return QJsonObject{
        {"type", "object"},
        {"properties", props},
        {"required", QJsonArray{"path", "old_text", "new_text"}}
    };
}

QString EditFileTool::execute(const QJsonObject &params) {
    const QString path = params.value("path").toString();
    const QString oldText = params.value("old_text").toString();
    const QString newText = params.value("new_text").toString();
    QString err;

    if (path.isEmpty()) return "错误: 必须提供 'path' 参数";
    if (oldText.isEmpty()) return "错误: 必须提供非空的 'old_text' 参数";

    const QString abs = resolvePath(path, _workspace, _allowedDir, &err);
    if (abs.isEmpty()) return "错误: " + err;

    QFile file(abs);
    if (!file.exists()) return "错误: 文件不存在";
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return "错误: 无法打开文件进行读取";
    }

    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    const int occurrences = content.count(oldText);
    if (occurrences <= 0) {
        return "错误: old_text 未在文件中找到";
    }
    if (occurrences > 1) {
        return QString("错误: old_text 在文件中出现了 %1 次,请提供更精确的上下文").arg(occurrences);
    }

    QString updated = content;
    updated.replace(oldText, newText);

    QSaveFile outFile(abs);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return "错误: 无法写回编辑后的文件";
    }
    QTextStream out(&outFile);
    out << updated;
    out.flush();
    if (!outFile.commit()) {
        return "错误: 保存编辑后的文件失败";
    }

    return "成功编辑文件: " + abs;
}

ListDirTool::ListDirTool(QString workspace, QString allowedDir)
    : _workspace(std::move(workspace)), _allowedDir(std::move(allowedDir)) {}

QString ListDirTool::name() const { return "list_dir"; }
QString ListDirTool::description() const { return "列出目录条目."; }

QJsonObject ListDirTool::parameters() const {
    QJsonObject props;
    props["path"] = QJsonObject{{"type", "string"}, {"description", "目录路径"}};
    return QJsonObject{
        {"type", "object"},
        {"properties", props},
        {"required", QJsonArray{"path"}}
    };
}

QString ListDirTool::execute(const QJsonObject &params) {
    const QString path = params.value("path").toString();
    QString err;
    if (path.isEmpty()) return "错误: 必须提供 'path' 参数";
    const QString abs = resolvePath(path, _workspace, _allowedDir, &err);
    if (abs.isEmpty()) return "错误: " + err;

    QDir dir(abs);
    if (!dir.exists()) return "错误: 目录不存在";
    const QFileInfoList items = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
    QStringList lines;
    for (const QFileInfo &fi : items) {
        lines << QString("%1 %2").arg(fi.isDir() ? "[目录]" : "[文件]").arg(fi.fileName());
    }
    return lines.isEmpty() ? QString("目录为空.") : lines.join("\n");
}

} // namespace yaos::agent::tools
