cmake_minimum_required(VERSION 3.16)

set(PROJECT_NAME cutefish-dock)
project(${PROJECT_NAME})

include(GNUInstallDirs)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 CONFIG REQUIRED Widgets DBus Concurrent LinguistTools QuickControls2)

# org_kde_plasma_window_management protocol, for window tracking.
find_package(KWayland REQUIRED)
# wlr-layer-shell, for the floating panel and its exclusive zone.
find_package(LayerShellQt REQUIRED)

set(SRCS
    src/applicationitem.h
    src/applicationmodel.cpp
    src/docksettings.cpp
    src/main.cpp
    src/mainwindow.cpp
    src/systemappmonitor.cpp
    src/systemappitem.cpp
    src/processprovider.cpp
    src/trashmanager.cpp
    src/utils.cpp
    src/xwindowinterface.cpp
    src/activity.cpp
)

set(RESOURCES
    resources.qrc
)

qt6_add_dbus_adaptor(DBUS_SOURCES
                     src/com.cutefish.Dock.xml
                     src/mainwindow.h MainWindow)
set_source_files_properties(${DBUS_SOURCES} PROPERTIES SKIP_AUTOGEN ON)

add_executable(${PROJECT_NAME} ${SRCS} ${DBUS_SOURCES} ${RESOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Concurrent
        Qt6::DBus
        Plasma::KWaylandClient
        LayerShellQt::Interface
)

file(GLOB TS_FILES translations/*.ts)
qt6_add_translation(QM_FILES ${TS_FILES})
add_custom_target(translations DEPENDS ${QM_FILES} SOURCES ${TS_FILES})
add_dependencies(${PROJECT_NAME} translations)

# The desktop file grants the Wayland interfaces (org_kde_plasma_window_management)
# that KWin requires before announcing window management to the dock. KWin matches
# the client by the canonical Exec, so it has to point at the installed binary -
# configured here instead of hardcoded, which also keeps a ~/.local install valid.
configure_file(cutefish-dock.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/cutefish-dock.desktop @ONLY)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(QM_FILES)
    install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/translations)
endif()
install(FILES cutefish-dock-list.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cutefish-dock.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
