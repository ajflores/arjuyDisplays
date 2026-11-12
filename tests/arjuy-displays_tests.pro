QT += core gui quick quickcontrols2 testlib dbus network
CONFIG += c++17 testcase
INCLUDEPATH += /usr/include
LIBS += -lLayerShellQtInterface
TARGET = arjuy-displays_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += SOURCE_DIR=\\\"$$PWD\\\"

HEADERS += ../src/backend.h ../src/eventwatcher.h ../src/hyprctl.h ../src/layoutgeometry.h ../src/identifier.h ../src/monitorparser.h \
    ../src/theme.h ../src/translator.h ../src/versions.h ../src/workspaces.h
SOURCES += arjuy-displays_tests.cpp ../src/backend.cpp ../src/eventwatcher.cpp ../src/hyprctl.cpp ../src/layoutgeometry.cpp ../src/identifier.cpp \
    ../src/monitorparser.cpp ../src/theme.cpp ../src/translator.cpp ../src/versions.cpp ../src/workspaces.cpp
RESOURCES += ../src/resources.qrc
