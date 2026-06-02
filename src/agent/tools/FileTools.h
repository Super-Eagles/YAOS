#ifndef YAOS_AGENT_TOOLS_FILETOOLS_H
#define YAOS_AGENT_TOOLS_FILETOOLS_H

#include "../Tool.h"

namespace yaos::agent::tools {

class ReadFileTool : public Tool {
public:
    ReadFileTool(QString workspace, QString allowedDir = QString());

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

private:
    QString _workspace;
    QString _allowedDir;
};

class WriteFileTool : public Tool {
public:
    WriteFileTool(QString workspace, QString allowedDir = QString());

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

private:
    QString _workspace;
    QString _allowedDir;
};

class EditFileTool : public Tool {
public:
    EditFileTool(QString workspace, QString allowedDir = QString());

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

private:
    QString _workspace;
    QString _allowedDir;
};

class ListDirTool : public Tool {
public:
    ListDirTool(QString workspace, QString allowedDir = QString());

    QString name() const override;
    QString description() const override;
    QJsonObject parameters() const override;
    QString execute(const QJsonObject &params) override;

private:
    QString _workspace;
    QString _allowedDir;
};

} // namespace yaos::agent::tools

#endif // YAOS_AGENT_TOOLS_FILETOOLS_H
