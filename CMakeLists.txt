cmake_minimum_required(VERSION 3.21)

# 版本号单一真源；打 tag 时 CI 用 -DCLIPORY_VERSION=<tag 去掉 v> 覆盖。
set(CLIPORY_VERSION "0.6.3" CACHE STRING "Clipory 应用版本号")

project(Clipory VERSION ${CLIPORY_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 打包/安装后的二进制不携带指向本机 Qt 安装目录的 RUNPATH。
set(CMAKE_SKIP_INSTALL_RPATH ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Gui Sql Concurrent DBus Network)

find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xfixes)

qt_standard_project_setup()

add_library(clipory_warnings INTERFACE)
target_compile_options(clipory_warnings INTERFACE -Wall -Wextra -Wpedantic)

# 纯核心逻辑（存储、blob、捕获处理），不依赖 Widgets，供主程序与测试共用。
qt_add_library(clipory_core STATIC
    src/core/ClipboardItem.h
    src/core/ClipboardItem.cpp
    src/core/BlobStore.h
    src/core/BlobStore.cpp
    src/core/CaptureService.h
    src/core/CaptureService.cpp
    src/core/Entry.h
    src/core/Store.h
    src/core/Store.cpp
    src/core/UpdateChecker.h
    src/core/UpdateChecker.cpp
)
target_include_directories(clipory_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(clipory_core PUBLIC Qt6::Gui Qt6::Sql Qt6::Concurrent Qt6::Network)
target_link_libraries(clipory_core PRIVATE clipory_warnings)

qt_add_executable(clipory
    src/main.cpp
    src/platform/ClipboardSource.h
    src/platform/ClipboardWatcher.h
    src/platform/ClipboardWatcher.cpp
    src/platform/Autostart.h
    src/platform/Autostart.cpp
    src/platform/HotkeySpec.h
    src/platform/HotkeySpec.cpp
    src/platform/SingleInstance.h
    src/platform/SingleInstance.cpp
    src/platform/X11Hotkey.h
    src/platform/X11Hotkey.cpp
    src/platform/XcbClipboardWatcher.h
    src/platform/XcbClipboardWatcher.cpp
    src/ui/FavoriteItemDelegate.h
    src/ui/FavoriteItemDelegate.cpp
    src/ui/HistoryWindow.h
    src/ui/HistoryWindow.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/TrayController.h
    src/ui/TrayController.cpp
)

target_include_directories(clipory PRIVATE src)
target_compile_definitions(clipory PRIVATE CLIPORY_VERSION="${PROJECT_VERSION}")

qt_add_resources(clipory "clipory_icons"
    PREFIX "/icons"
    BASE resources/icons
    FILES resources/icons/clipory.svg)

target_link_libraries(clipory PRIVATE
    clipory_core
    clipory_warnings
    Qt6::Widgets
    Qt6::DBus
    PkgConfig::XCB
)

include(GNUInstallDirs)

# 系统级自启项必须落在 /etc（XDG_CONFIG_DIRS），不能跟着 /usr 前缀走。
set(CLIPORY_AUTOSTART_DIR "/etc/xdg/autostart" CACHE PATH "系统级开机自启目录")

install(TARGETS clipory RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/clipory.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/icons/clipory.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES packaging/clipory-autostart.desktop
        DESTINATION ${CLIPORY_AUTOSTART_DIR})

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "clipory")
set(CPACK_PACKAGE_VENDOR "clipory")
set(CPACK_PACKAGE_CONTACT "clipory@localhost")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "X11 剪贴板历史工具")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6core6t64 (>= 6.4), libqt6gui6t64, libqt6widgets6t64, libqt6sql6t64, \
libqt6sql6-sqlite, libqt6dbus6t64, libqt6concurrent6t64, libqt6svg6, libqt6network6t64, \
libxcb1, libxcb-xfixes0, libxfixes3")

execute_process(COMMAND dpkg --print-architecture
                OUTPUT_VARIABLE CLIPORY_DEB_ARCH
                OUTPUT_STRIP_TRAILING_WHITESPACE)
if(CLIPORY_DEB_ARCH)
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CLIPORY_DEB_ARCH}")
endif()

include(CPack)

include(CTest)
if(BUILD_TESTING)
    find_package(Catch2 3 REQUIRED)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
