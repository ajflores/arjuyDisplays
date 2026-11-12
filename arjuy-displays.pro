QT += core gui qml quick quickcontrols2 dbus network

CONFIG += c++17 release
TARGET = arjuy-displays
TEMPLATE = app

INCLUDEPATH += /usr/include
LIBS += -lLayerShellQtInterface

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/eventwatcher.h \
    src/hyprctl.h \
    src/layoutgeometry.h \
    src/identifier.h \
    src/monitorparser.h \
    src/theme.h \
    src/translator.h \
    src/versions.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/eventwatcher.cpp \
    src/hyprctl.cpp \
    src/layoutgeometry.cpp \
    src/identifier.cpp \
    src/monitorparser.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/versions.cpp

RESOURCES += src/resources.qrc
