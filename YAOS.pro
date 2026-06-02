QT += core concurrent qml quick quickcontrols2 sql

CONFIG += c++17
CONFIG += utf8_source
QT_CONFIG += highdpi
CONFIG -= app_bundle

TEMPLATE = app
TARGET = yaos

CONFIG(debug, debug|release) {
    MOC_DIR = $$PWD/build/debug/moc
    OBJECTS_DIR = $$PWD/build/debug/obj
    RCC_DIR = $$PWD/build/debug/rcc
    UI_DIR = $$PWD/build/debug/ui
    QMLCACHE_DIR = $$PWD/build/debug/qmlcache
} else {
    MOC_DIR = $$PWD/build/release/moc
    OBJECTS_DIR = $$PWD/build/release/obj
    RCC_DIR = $$PWD/build/release/rcc
    UI_DIR = $$PWD/build/release/ui
    QMLCACHE_DIR = $$PWD/build/release/qmlcache
}

CONFIG(debug, debug|release) {
    TARGET_BIN_DIR = $$absolute_path(bin/debug, $$PWD)
    YAOS_LIB_DIR = $$absolute_path(lib/debug, $$PWD)
    YAOS_BASE_LIB = yaos_based
    YAOS_BUSINESS_LIB = yaos_businessd
    QT_SQL_DLL = Qt5Sqld.dll
    QT_CONCURRENT_DLL = Qt5Concurrentd.dll
    SQLITE_PLUGIN_DLL = qsqlited.dll
    FASTNET_DLL = FastNetd.dll
    QT_DEPLOY_CONFIG = --debug
} else {
    TARGET_BIN_DIR = $$absolute_path(bin, $$PWD)
    YAOS_LIB_DIR = $$absolute_path(lib, $$PWD)
    YAOS_BASE_LIB = yaos_base
    YAOS_BUSINESS_LIB = yaos_business
    QT_SQL_DLL = Qt5Sql.dll
    QT_CONCURRENT_DLL = Qt5Concurrent.dll
    SQLITE_PLUGIN_DLL = qsqlite.dll
    FASTNET_DLL = FastNet.dll
    QT_DEPLOY_CONFIG = --release
}

DESTDIR = $$TARGET_BIN_DIR

FASTNET_ROOT = $$absolute_path(../FastNet, $$PWD)

win32 {
    CONFIG -= console
    CONFIG += windows
    LIBS += -luser32
    RC_ICONS += $$PWD/assets/YAOS.ico

    QT_BIN_DIR = $$system_path($$[QT_INSTALL_BINS])
    APP_BIN_DIR = $$system_path($$TARGET_BIN_DIR)
    SQLITE_PLUGIN_SRC = $$system_path($$[QT_INSTALL_PLUGINS]/sqldrivers/$$SQLITE_PLUGIN_DLL)
    SQLITE_PLUGIN_DST_DIR = $$system_path($$absolute_path(sqldrivers, $$TARGET_BIN_DIR))
    FASTNET_BIN_DIR = $$system_path($$FASTNET_ROOT/bin)
    FASTNET_DLL_SRC = $$system_path($$FASTNET_ROOT/bin/$$FASTNET_DLL)
    QML_SOURCE_DIR = $$system_path($$PWD/qml)
    CONFIG(debug, debug|release) {
        MSVC_DEBUG_CRT_DIR = $$system_path(C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.28.29910/debug_nonredist/x64/Microsoft.VC142.DebugCRT)
        UCRT_DEBUG_CRT_DIR = $$system_path(C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64/ucrt)
        QMAKE_POST_LINK += if exist \"$$MSVC_DEBUG_CRT_DIR\\*.dll\" copy /Y \"$$MSVC_DEBUG_CRT_DIR\\*.dll\" \"$$APP_BIN_DIR\\\" $$escape_expand(\n\t)
        QMAKE_POST_LINK += if exist \"$$UCRT_DEBUG_CRT_DIR\\ucrtbased.dll\" copy /Y \"$$UCRT_DEBUG_CRT_DIR\\ucrtbased.dll\" \"$$APP_BIN_DIR\\ucrtbased.dll\" $$escape_expand(\n\t)
    }
    QMAKE_POST_LINK += copy /Y \"$$QT_BIN_DIR\\$$QT_SQL_DLL\" \"$$APP_BIN_DIR\\$$QT_SQL_DLL\" $$escape_expand(\n\t)
    QMAKE_POST_LINK += copy /Y \"$$QT_BIN_DIR\\$$QT_CONCURRENT_DLL\" \"$$APP_BIN_DIR\\$$QT_CONCURRENT_DLL\" $$escape_expand(\n\t)
    QMAKE_POST_LINK += if not exist \"$$SQLITE_PLUGIN_DST_DIR\" mkdir \"$$SQLITE_PLUGIN_DST_DIR\" $$escape_expand(\n\t)
    QMAKE_POST_LINK += copy /Y \"$$SQLITE_PLUGIN_SRC\" \"$$SQLITE_PLUGIN_DST_DIR\\$$SQLITE_PLUGIN_DLL\" $$escape_expand(\n\t)
    QMAKE_POST_LINK += if exist \"$$QT_BIN_DIR\\windeployqt.exe\" if not exist \"$$APP_BIN_DIR\\platforms\\qwindowsd.dll\" \"$$QT_BIN_DIR\\windeployqt.exe\" $$QT_DEPLOY_CONFIG --qmldir \"$$QML_SOURCE_DIR\" \"$$APP_BIN_DIR\\$${TARGET}.exe\" $$escape_expand(\n\t)
    QMAKE_POST_LINK += if exist \"$$FASTNET_DLL_SRC\" copy /Y \"$$FASTNET_DLL_SRC\" \"$$APP_BIN_DIR\\$$FASTNET_DLL\" $$escape_expand(\n\t)
    QMAKE_POST_LINK += if exist \"$$FASTNET_BIN_DIR\\libcrypto-3-x64.dll\" copy /Y \"$$FASTNET_BIN_DIR\\libcrypto-3-x64.dll\" \"$$APP_BIN_DIR\\libcrypto-3-x64.dll\" $$escape_expand(\n\t)
    QMAKE_POST_LINK += if exist \"$$FASTNET_BIN_DIR\\libssl-3-x64.dll\" copy /Y \"$$FASTNET_BIN_DIR\\libssl-3-x64.dll\" \"$$APP_BIN_DIR\\libssl-3-x64.dll\" $$escape_expand(\n\t)
} else {
    CONFIG += console
}

# MSVC 需要显式启用 UTF-8；其他工具链使用 Qt 的 utf8_source 即可
msvc {
    QMAKE_CXXFLAGS += /utf-8
    QMAKE_CFLAGS += /utf-8
}

INCLUDEPATH += $$PWD/src

INCLUDEPATH += $$FASTNET_ROOT/include
CONFIG(debug, debug|release) {
    LIBS += -L$$FASTNET_ROOT/lib -lFastNetd
} else {
    LIBS += -L$$FASTNET_ROOT/lib -lFastNet
}

include(qmake/modules/frontend.pri)

SOURCES += $$YAOS_FRONTEND_SOURCES

HEADERS += $$YAOS_FRONTEND_HEADERS

LIBS += -L$$YAOS_LIB_DIR -l$$YAOS_BUSINESS_LIB -l$$YAOS_BASE_LIB
PRE_TARGETDEPS += $$YAOS_LIB_DIR/$${YAOS_BUSINESS_LIB}.lib
PRE_TARGETDEPS += $$YAOS_LIB_DIR/$${YAOS_BASE_LIB}.lib

RESOURCES += \
    qml/qml.qrc
