cmake_minimum_required(VERSION 3.16)

set(FISHUI_VERSION 1.0)
project(fishui VERSION ${FISHUI_VERSION})

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#DEFAULT COMPONENTS DEFINITIONS
option(COMPONENT_EDITOR "Build editor component" ON)
option(COMPONENT_FM "Build filemanager component" ON)
option(COMPONENT_ACCOUNTS "Build accounts component" ON)
option(COMPONENT_TERMINAL "Build terminal component" ON)
option(COMPONENT_STORE "Build store component" OFF)
option(COMPONENT_TAGGING "Build tagging component" ON)
option(COMPONENT_SYNCING "Build syncing component" ON)

find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR})

set(QT Core Gui Widgets Quick QuickControls2 DBus Qml)
find_package(Qt6 REQUIRED ${QT})
# KWindowShadow owns the Wayland shadow protocol and its native surface
# lifecycle.
find_package(KF6WindowSystem REQUIRED)
# org_kde_kwin_blur, for the region behind a translucent window.
find_package(KWayland REQUIRED)

# Location of the ported libcutefish (cutefish-framework) checkout that
# supplies the Appearance library. Overridable so the side-by-side forks can
# live anywhere.
set(CUTEFISH_FRAMEWORK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cutefish-framework"
    CACHE PATH "Path to the cutefish-framework (libcutefish port) source directory")

# The QML module links the appearance library, so a private-prefix install has
# to ship it as well.
set(CUTEFISH_INSTALL_APPEARANCE ON)
add_subdirectory(${CUTEFISH_FRAMEWORK_SOURCE_DIR}/appearance
                 ${CMAKE_CURRENT_BINARY_DIR}/cutefish-framework-appearance-build)

# The Appearance subdirectory creates the Cutefish::Appearance alias, but CMake
# aliases are only visible below the directory they are created in. Recreate it
# here so the sibling src/ directory can link it.
add_library(Cutefish::Appearance ALIAS cutefish-framework-appearance)

include(GenerateExportHeader)
include(GNUInstallDirs)
include(ECMGenerateHeaders)

# Get the installation directory from Qt 6, overridable as a cache variable so
# the port can install into a private prefix without touching system paths.
# qtpaths ships in Qt's private bin directory on several distributions
# (Arch/CachyOS: /usr/lib/qt6/bin), which is not necessarily on PATH - so look
# there instead of failing the configure step.
find_program(QT_PATHS_EXECUTABLE
    NAMES qtpaths6 qtpaths
    HINTS ENV QT6_BIN_DIR
          /usr/lib/qt6/bin
          /usr/lib64/qt6/bin
          /usr/local/lib/qt6/bin
          ${CMAKE_INSTALL_PREFIX}/bin)
if(NOT QT_PATHS_EXECUTABLE)
    message(FATAL_ERROR "qtpaths not found. Pass -DQT_PATHS_EXECUTABLE=/path/to/qtpaths6")
endif()
if(NOT INSTALL_QMLDIR)
    execute_process(COMMAND ${QT_PATHS_EXECUTABLE} -query QT_INSTALL_QML
            OUTPUT_VARIABLE INSTALL_QMLDIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            )
endif()
set(INSTALL_QMLDIR "${INSTALL_QMLDIR}" CACHE PATH "QML import installation directory")
if (INSTALL_QMLDIR)
    message(STATUS "qml directory:" "${INSTALL_QMLDIR}")
else ()
    message(FATAL_ERROR "qml directory cannot be detected.")
endif ()

# Install

# Qt Widgets Style
#if (UNIX)
#    find_package(ECM REQUIRED NO_MODULE)
#    set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
#    include(ECMQueryQmake)
#    query_qmake(CMAKE_INSTALL_QTPLUGINDIR QT_INSTALL_PLUGINS)
#    add_subdirectory(qtwidgets-style)
#endif ()

add_subdirectory(src)


