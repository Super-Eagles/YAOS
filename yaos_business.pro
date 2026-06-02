QT += core concurrent sql
QT -= gui

CONFIG += c++17
CONFIG += utf8_source
CONFIG += staticlib
CONFIG -= app_bundle

TEMPLATE = lib

CONFIG(debug, debug|release) {
    TARGET = yaos_businessd
    DESTDIR = $$absolute_path(lib/debug, $$PWD)
    MOC_DIR = $$PWD/build/debug/libbusiness/moc
    OBJECTS_DIR = $$PWD/build/debug/libbusiness/obj
    RCC_DIR = $$PWD/build/debug/libbusiness/rcc
    UI_DIR = $$PWD/build/debug/libbusiness/ui
} else {
    TARGET = yaos_business
    DESTDIR = $$absolute_path(lib, $$PWD)
    MOC_DIR = $$PWD/build/release/libbusiness/moc
    OBJECTS_DIR = $$PWD/build/release/libbusiness/obj
    RCC_DIR = $$PWD/build/release/libbusiness/rcc
    UI_DIR = $$PWD/build/release/libbusiness/ui
}

FASTNET_ROOT = $$absolute_path(../FastNet, $$PWD)

msvc {
    QMAKE_CXXFLAGS += /utf-8
    QMAKE_CFLAGS += /utf-8
}

INCLUDEPATH += $$PWD/src
INCLUDEPATH += $$FASTNET_ROOT/include

include(qmake/modules/business.pri)

SOURCES += $$YAOS_BUSINESS_SHARED_SOURCES
SOURCES += $$YAOS_BUSINESS_APP_SOURCES

HEADERS += $$YAOS_BUSINESS_SHARED_HEADERS
HEADERS += $$YAOS_BUSINESS_APP_HEADERS
