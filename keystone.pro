QT += core gui qml quick quickcontrols2 quick3d svg dbus concurrent

CONFIG += c++17 release
TARGET = keystone
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/elementlist.h \
    src/filepicker.h \
    src/ifcinfo.h \
    src/loader.h \
    src/mesh.h \
    src/meshgeometry.h \
    src/portalfilepicker.h \
    src/step.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/elementlist.cpp \
    src/ifcinfo.cpp \
    src/loader.cpp \
    src/mesh.cpp \
    src/meshgeometry.cpp \
    src/portalfilepicker.cpp \
    src/step.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
