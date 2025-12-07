QT += core
QT += sql
LIBS += -luser32
QT = core sql network

CONFIG += c++17 cmdline precompile_header
PRECOMPILED_HEADER = pch.h

INCLUDEPATH += $$PWD/../../inc/network_headers
INCLUDEPATH += $$PWD
INCLUDEPATH += C:/Qt/6.9.1/msvc2022_64/include
INCLUDEPATH += C:/Qt/6.9.1/msvc2022_64/include/QtCore
INCLUDEPATH += $$PWD/../../inc/sqlite
INCLUDEPATH += $$PWD/../../inc/browser


DESTDIR = $$PWD/../../bin/
OBJECTS_DIR = $$PWD/../../obj/$${TARGET}/
MOC_DIR = $$PWD/../../moc/$${TARGET}/

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        abstractworker.cpp \
        clientnetworkmanager.cpp \
        clientprivilegemanager.cpp \
        clientservicemanager.cpp \
        clientutils.cpp \
        deleteddatacollector.cpp \
        externalstorage.cpp \
        filemanager.cpp \
        lnkcollector.cpp \
        main.cpp \
        messengercollector.cpp \
        NetworkManager.cpp \
        pch.cpp \
        prefetch.cpp \
        simplebrowsercollector.cpp \
        $$PWD/../../inc/sqlite/sqlite3.c    # sqlite3.c 추가

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
        abstractworker.h \
        clientnetworkmanager.h \
        clientprivilegemanager.h \
        clientservicemanager.h \
        clientutils.h \
        deleteddatacollector.h \
        externalstorage.h \
        filemanager.h \
        ip_helper.h \
        lnkcollector.h \
        messengercollector.h \
        NetworkManager.h \
        pch.h \
        prefetch.h \
        simplebrowsercollector.h \
