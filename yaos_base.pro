QT += core concurrent sql
QT -= gui

CONFIG += c++17
CONFIG += utf8_source
CONFIG += staticlib
CONFIG -= app_bundle

TEMPLATE = lib

CONFIG(debug, debug|release) {
    TARGET = yaos_based
    DESTDIR = $$absolute_path(lib/debug, $$PWD)
    MOC_DIR = $$PWD/build/debug/libbase/moc
    OBJECTS_DIR = $$PWD/build/debug/libbase/obj
    RCC_DIR = $$PWD/build/debug/libbase/rcc
    UI_DIR = $$PWD/build/debug/libbase/ui
} else {
    TARGET = yaos_base
    DESTDIR = $$absolute_path(lib, $$PWD)
    MOC_DIR = $$PWD/build/release/libbase/moc
    OBJECTS_DIR = $$PWD/build/release/libbase/obj
    RCC_DIR = $$PWD/build/release/libbase/rcc
    UI_DIR = $$PWD/build/release/libbase/ui
}

FASTNET_ROOT = $$absolute_path(../FastNet, $$PWD)

msvc {
    QMAKE_CXXFLAGS += /utf-8
    QMAKE_CFLAGS += /utf-8
}

INCLUDEPATH += $$PWD/src
INCLUDEPATH += $$FASTNET_ROOT/include

include(qmake/modules/base.pri)

SOURCES += $$YAOS_BASE_SOURCES
HEADERS += $$YAOS_BASE_HEADERS
