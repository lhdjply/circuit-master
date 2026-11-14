cmake_minimum_required(VERSION 3.21)

project(CircuitMaster VERSION 0.1.0 LANGUAGES CXX
    DESCRIPTION "电路仿真大师 - 基于 Qt6 的跨平台电路原理图与仿真软件")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(GNUInstallDirs)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Charts Svg)
find_package(Qt6 6.5 QUIET COMPONENTS Test)

qt_standard_project_setup()

# ---------------------------------------------------------------- 核心引擎
add_library(csm_core STATIC
    src/core/Units.cpp
    src/core/Component.cpp
    src/core/Netlist.cpp
    src/core/Circuit.cpp
    src/sim/Simulator.cpp
)
target_include_directories(csm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(csm_core PUBLIC Qt6::Core)
target_compile_options(csm_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---------------------------------------------------------------- 图形界面
set(CSM_UI_SOURCES
    src/ui/Symbols.cpp
    src/ui/ComponentItem.cpp
    src/ui/WireItem.cpp
    src/ui/SchematicScene.cpp
    src/ui/SchematicView.cpp
    src/ui/PropertyPanel.cpp
    src/ui/WaveformWindow.cpp
    src/ui/SimulationSettingsDialog.cpp
    src/ui/MainWindow.cpp
)
set(CSM_UI_HEADERS
    src/ui/Symbols.h
    src/ui/ComponentItem.h
    src/ui/WireItem.h
    src/ui/SchematicScene.h
    src/ui/SchematicView.h
    src/ui/PropertyPanel.h
    src/ui/WaveformWindow.h
    src/ui/SimulationSettingsDialog.h
    src/ui/MainWindow.h
)

add_library(csm_ui STATIC ${CSM_UI_SOURCES} ${CSM_UI_HEADERS})
target_link_libraries(csm_ui PUBLIC csm_core Qt6::Widgets Qt6::Charts Qt6::Svg)
target_compile_options(csm_ui PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---------------------------------------------------------------- 资源
# 图标与应用图标统一放在 resources/assets/icons，由 qrc 以 /icons 前缀暴露
set(CSM_RESOURCES
    resources/assets/circuit-master.qrc
    resources/examples/examples.qrc
)

# ---------------------------------------------------------------- 可执行程序
qt_add_executable(CircuitMaster
    src/main.cpp
    ${CSM_RESOURCES}
)
target_link_libraries(CircuitMaster PRIVATE csm_ui)
set_target_properties(CircuitMaster PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)

# ---------------------------------------------------------------- 测试
if (TARGET Qt6::Test)
    enable_testing()

    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE csm_core Qt6::Test)
    add_test(NAME core COMMAND test_core)

    add_executable(test_ui tests/test_ui.cpp ${CSM_RESOURCES})
    target_link_libraries(test_ui PRIVATE csm_ui Qt6::Test)
    add_test(NAME ui COMMAND test_ui)
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# ---------------------------------------------------------------- 安装
# 桌面入口由模板生成，Exec 指向实际安装位置
set(CSM_DESKTOP_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/CircuitMaster")
configure_file(resources/linux/circuit-master.desktop.in
               "${CMAKE_CURRENT_BINARY_DIR}/circuit-master.desktop" @ONLY)

install(TARGETS CircuitMaster RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/circuit-master.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/assets/icons/icon.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME circuit-master.svg)
install(DIRECTORY resources/examples/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/circuit-master/examples
        FILES_MATCHING PATTERN "*.csm")
