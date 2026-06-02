#ifndef YAOS_UI_STUDIOWINDOW_H
#define YAOS_UI_STUDIOWINDOW_H

#include <QQuickView>

#include "StudioBridge.h"

namespace yaos::ui {

class StudioWindow : public QQuickView {
    Q_OBJECT
public:
    explicit StudioWindow(const QString &initialPage = QString(), QWindow *parent = nullptr);

    StudioBridge *bridge() const;
    bool ready() const;
    QString errorString() const;

private:
    StudioBridge *m_bridge = nullptr;
    bool m_ready = false;
    QString m_errorString;
};

} // namespace yaos::ui

#endif // YAOS_UI_STUDIOWINDOW_H
