QT += core concurrent sql
QT -= gui

CONFIG += c++17
CONFIG += utf8_source
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app
TARGET = yaos_tests

CONFIG(debug, debug|release) {
    MOC_DIR = $$PWD/build/debug/test_moc
    OBJECTS_DIR = $$PWD/build/debug/test_obj
    RCC_DIR = $$PWD/build/debug/test_rcc
    TARGET_BIN_DIR = $$absolute_path(bin/debug, $$PWD)
    YAOS_LIB_DIR = $$absolute_path(lib/debug, $$PWD)
    YAOS_BASE_LIB = yaos_based
    YAOS_BUSINESS_LIB = yaos_businessd
    QT_CORE_DLL = Qt5Cored.dll
    QT_CONCURRENT_DLL = Qt5Concurrentd.dll
    QT_SQL_DLL = Qt5Sqld.dll
    FASTNET_DLL = FastNetd.dll
} else {
    MOC_DIR = $$PWD/build/release/test_moc
    OBJECTS_DIR = $$PWD/build/release/test_obj
    RCC_DIR = $$PWD/build/release/test_rcc
    TARGET_BIN_DIR = $$absolute_path(bin, $$PWD)
    YAOS_LIB_DIR = $$absolute_path(lib, $$PWD)
    YAOS_BASE_LIB = yaos_base
    YAOS_BUSINESS_LIB = yaos_business
    QT_CORE_DLL = Qt5Core.dll
    QT_CONCURRENT_DLL = Qt5Concurrent.dll
    QT_SQL_DLL = Qt5Sql.dll
    FASTNET_DLL = FastNet.dll
}

DESTDIR = $$TARGET_BIN_DIR

FASTNET_ROOT = $$absolute_path(../FastNet, $$PWD)

msvc {
    QMAKE_CXXFLAGS += /utf-8
    QMAKE_CFLAGS += /utf-8
}

INCLUDEPATH += $$PWD/src
INCLUDEPATH += $$FASTNET_ROOT/include
DEFINES += YAOS_SOURCE_ROOT=\\\"$$PWD\\\"

SOURCES += tests/IntegrationSmoke.cpp

CONFIG(debug, debug|release) {
    LIBS += -L$$FASTNET_ROOT/lib -lFastNetd
} else {
    LIBS += -L$$FASTNET_ROOT/lib -lFastNet
}

LIBS += -L$$YAOS_LIB_DIR -l$$YAOS_BUSINESS_LIB -l$$YAOS_BASE_LIB
PRE_TARGETDEPS += $$YAOS_LIB_DIR/$${YAOS_BUSINESS_LIB}.lib
PRE_TARGETDEPS += $$YAOS_LIB_DIR/$${YAOS_BASE_LIB}.lib

win32 {
    QT_BIN_DIR = $$system_path($$[QT_INSTALL_BINS])
    APP_BIN_DIR = $$system_path($$TARGET_BIN_DIR)
    FASTNET_BIN_DIR = $$system_path($$FASTNET_ROOT/bin)
    FASTNET_DLL_SRC = $$system_path($$FASTNET_ROOT/bin/$$FASTNET_DLL)
    QMAKE_POST_LINK += copy /Y \"$$QT_BIN_DIR\\$$QT_CORE_DLL\" \"$$APP_BIN_DIR\\$$QT_CORE_DLL\" $$escape_expand(\n\t)
    QMAKE_POST_LINK += copy /Y \"$$QT_BIN_DIR\\$$QT_CONCURRENT_DLL\" \"$$APP_BIN_DIR\\$$QT_CONCURRENT_DLL\" $$escape_expand(\n\t)
    QMAKE_POST_LINK += copy /Y \"$$QT_BIN_DIR\\$$QT_SQL_DLL\" \"$$APP_BIN_DIR\\$$QT_SQL_DLL\" $$escape_expand(\n\t)
    QMAKE_POST_LINK += if exist \"$$FASTNET_DLL_SRC\" copy /Y \"$$FASTNET_DLL_SRC\" \"$$APP_BIN_DIR\\$$FASTNET_DLL\" $$escape_expand(\n\t)
    QMAKE_POST_LINK += if exist \"$$FASTNET_BIN_DIR\\libcrypto-3-x64.dll\" copy /Y \"$$FASTNET_BIN_DIR\\libcrypto-3-x64.dll\" \"$$APP_BIN_DIR\\libcrypto-3-x64.dll\" $$escape_expand(\n\t)
    QMAKE_POST_LINK += if exist \"$$FASTNET_BIN_DIR\\libssl-3-x64.dll\" copy /Y \"$$FASTNET_BIN_DIR\\libssl-3-x64.dll\" \"$$APP_BIN_DIR\\libssl-3-x64.dll\" $$escape_expand(\n\t)
}
