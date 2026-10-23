QT += core gui quick quickcontrols2 quick3d testlib dbus concurrent
CONFIG += c++17 testcase
TARGET = keystone_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    ../src/backend.h \
    ../src/filepicker.h \
    ../src/ifcinfo.h \
    ../src/loader.h \
    ../src/mesh.h \
    ../src/portalfilepicker.h \
    ../src/step.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    keystone_tests.cpp \
    ../src/backend.cpp \
    ../src/ifcinfo.cpp \
    ../src/loader.cpp \
    ../src/mesh.cpp \
    ../src/portalfilepicker.cpp \
    ../src/step.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp

RESOURCES += ../src/resources.qrc
