cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# 版本号只有一个来源：仓库根目录的 VERSION 文件
#
#   file(STRINGS ... LIMIT_COUNT 1) 只取第一行；string(STRIP) 顺带吃掉行尾的
#   \r —— Windows 上按 CRLF 检出时，版本号会带进一个回车，project(VERSION)
#   会直接解析失败。
# ---------------------------------------------------------------------------
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CSM_VERSION_STRING LIMIT_COUNT 1)
string(STRIP "${CSM_VERSION_STRING}" CSM_VERSION_STRING)
if(CSM_VERSION_STRING STREQUAL "")
    message(FATAL_ERROR
            "无法从 ${CMAKE_CURRENT_SOURCE_DIR}/VERSION 读取版本号（文件缺失或为空）")
endif()

# VERSION 里按 tag 的写法带 v 前缀（v0.1.0），但 project(VERSION) 只接受纯数字；
# 这里剥掉前缀给 CMake 用，原样保留一份给界面与 --version 展示。
string(REGEX REPLACE "^[vV]" "" CSM_VERSION_NUMBER "${CSM_VERSION_STRING}")
if(NOT CSM_VERSION_NUMBER MATCHES "^[0-9]+(\\.[0-9]+)*$")
    message(FATAL_ERROR
            "VERSION 里的版本号不合法：'${CSM_VERSION_STRING}'（期望形如 v0.1.0 或 0.1.0）")
endif()

# file() 只在配置期读一次，不登记成重配置依赖的话，改完 VERSION 直接构建会继续
# 用旧版本号（必须手工再 configure 一次）。
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

project(CircuitMaster VERSION ${CSM_VERSION_NUMBER} LANGUAGES CXX
    DESCRIPTION "电路仿真大师 - 基于 Qt6 的跨平台电路原理图与仿真软件")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(GNUInstallDirs)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Charts Svg)
find_package(Qt6 6.5 QUIET COMPONENTS Test)

# Debian 打包的版本号只能来自 debian/changelog（dpkg 的硬约束，不能动态生成）。
# 两份版本号不一致时给个提示：通常意味着改了 VERSION 却忘了 dch。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/debian/changelog")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/debian/changelog" CSM_DEB_LINE LIMIT_COUNT 1)
    if(CSM_DEB_LINE MATCHES "^[^(]+\\(([^)]+)\\)")
        set(CSM_DEB_VERSION "${CMAKE_MATCH_1}")
        # 允许 Debian 修订号（0.1.0-1）：只比较上游版本部分
        string(REGEX REPLACE "-[0-9]+$" "" CSM_DEB_UPSTREAM "${CSM_DEB_VERSION}")
        if(NOT CSM_DEB_UPSTREAM STREQUAL CSM_VERSION_NUMBER)
            message(WARNING
                    "VERSION（${CSM_VERSION_STRING}）与 debian/changelog"
                    "（${CSM_DEB_VERSION}）不一致：安装包版本取 changelog，"
                    "发布前请用 `dch -v ${CSM_VERSION_NUMBER}` 同步")
        endif()
    endif()
endif()

qt_standard_project_setup()

# ---------------------------------------------------------------- 版本头文件
# 源码里不再硬编码版本号，统一使用 csm_version.h 中的宏
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/csm_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/csm_version.h"
    @ONLY
)
include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated")

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

# Windows 的 exe 图标与版本信息来自 PE 资源段：qrc 里的图标只作用于运行期窗口，
# 资源管理器里看到的图标要靠 .rc 编译进可执行文件。
if(WIN32)
    enable_language(RC)
    target_sources(CircuitMaster PRIVATE resources/win/circuit-master.rc)
endif()

# ---------------------------------------------------------------- 测试
# 打包时用 -DCSM_BUILD_TESTS=OFF 跳过（见 debian/rules）
option(CSM_BUILD_TESTS "构建单元测试（需要 Qt6::Test）" ON)

if (CSM_BUILD_TESTS AND TARGET Qt6::Test)
    enable_testing()

    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE csm_core Qt6::Test)
    add_test(NAME core COMMAND test_core)

    add_executable(test_ui tests/test_ui.cpp ${CSM_RESOURCES})
    target_compile_definitions(test_ui PRIVATE
        CSM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
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
