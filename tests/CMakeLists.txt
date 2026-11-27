include(Catch)

add_executable(tst_core core/main.cpp core/tst_store.cpp core/tst_blobstore.cpp
                        core/tst_update_checker.cpp)
target_link_libraries(tst_core PRIVATE clipman_core Catch2::Catch2 clipman_warnings)
catch_discover_tests(tst_core)

foreach(name capture_service history_window autostart)
    qt_add_executable(tst_${name} ui/tst_${name}.cpp)
    target_include_directories(tst_${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME tst_${name} COMMAND tst_${name})
    set_tests_properties(tst_${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# 被测的 UI / 桌面集成源文件直接编进对应测试目标，产品结构不变。
target_sources(tst_history_window PRIVATE ${CMAKE_SOURCE_DIR}/src/ui/HistoryWindow.cpp
                                        ${CMAKE_SOURCE_DIR}/src/ui/FavoriteItemDelegate.cpp)
target_link_libraries(tst_history_window PRIVATE clipman_core Qt6::Widgets Qt6::Test)

target_sources(tst_autostart PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/Autostart.cpp)
target_link_libraries(tst_autostart PRIVATE Qt6::Test)

# HotkeySpec 是纯逻辑（只依赖 QtGui），单独注册，不需要 offscreen 平台。
qt_add_executable(tst_hotkey_spec platform/tst_hotkey_spec.cpp)
target_include_directories(tst_hotkey_spec PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_sources(tst_hotkey_spec PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/HotkeySpec.cpp)
target_link_libraries(tst_hotkey_spec PRIVATE Qt6::Gui Qt6::Test)
add_test(NAME tst_hotkey_spec COMMAND tst_hotkey_spec)

target_link_libraries(tst_capture_service PRIVATE clipman_core Qt6::Concurrent Qt6::Test)
