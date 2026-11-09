# Unit tests for StreamingPreferences host overrides and the stream command line.
# Build and run: qmake6 tests/streamingpreferences && make check

QT += testlib qml
QT -= gui
CONFIG += testcase console c++17
CONFIG -= app_bundle

TARGET = tst_streamingpreferences

APP_DIR = $$PWD/../../app
INCLUDEPATH += $$APP_DIR $$APP_DIR/settings

SOURCES += \
    tst_streamingpreferences.cpp \
    wmstubs.cpp \
    $$APP_DIR/settings/streamingpreferences.cpp \
    $$APP_DIR/cli/commandlineparser.cpp

HEADERS += \
    $$APP_DIR/settings/streamingpreferences.h \
    $$APP_DIR/cli/commandlineparser.h
