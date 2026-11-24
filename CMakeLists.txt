cmake_minimum_required(VERSION 3.21)

project(Clipman VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Gui Sql Concurrent)

find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xfixes)

qt_standard_project_setup()

qt_add_executable(clipman
    src/main.cpp
    src/core/Entry.h
    src/core/Store.h
    src/core/Store.cpp
    src/platform/ClipboardSource.h
    src/platform/ClipboardWatcher.h
    src/platform/ClipboardWatcher.cpp
    src/platform/XcbClipboardWatcher.h
    src/platform/XcbClipboardWatcher.cpp
    src/ui/HistoryWindow.h
    src/ui/HistoryWindow.cpp
    src/ui/TrayController.h
    src/ui/TrayController.cpp
)

target_include_directories(clipman PRIVATE src)

target_link_libraries(clipman PRIVATE
    Qt6::Widgets
    Qt6::Gui
    Qt6::Sql
    Qt6::Concurrent
    PkgConfig::XCB
)

install(TARGETS clipman RUNTIME DESTINATION bin)
