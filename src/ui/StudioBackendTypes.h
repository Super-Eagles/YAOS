#ifndef YAOS_UI_STUDIOBACKENDTYPES_H
#define YAOS_UI_STUDIOBACKENDTYPES_H

#include <QString>
#include <QVariantList>

namespace yaos::ui {

struct StudioChatTurnResult {
    QString content;
    QString thinking;
    QString taskId;
    QString traceId;
    QString model;
    QString provider;
    QVariantList trace;
    bool error = false;
};

} // namespace yaos::ui

#endif // YAOS_UI_STUDIOBACKENDTYPES_H
