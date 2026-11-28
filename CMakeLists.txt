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
# 不要开 CMAKE_INCLUDE_CURRENT_DIR：它把源码根目录加进头文件搜索路径。
# Windows 的文件系统大小写不敏感，Qt 头文件链里的 #include <version> 会命中
# 根目录的 VERSION 文件（内容 v0.1.0），被当成 C++ 编译直接报错。
# 各目标需要的路径由 target_include_directories 显式给出。
set(CMAKE_INCLUDE_CURRENT_DIR OFF)

include(GNUInstallDirs)

# 下限取 6.4：Debian 12 / Ubuntu 24.04 就是这一版。打包在 Debian 12 里做，
# 产物才能装到大多数 Debian 系发行版上。
#
# 因此**只能用 6.4 就有的接口**。6.5+ 的（QStyleHints::colorScheme、
# QList::assign(6.6) 等）要么换成等价写法，要么用 QT_VERSION_CHECK 包起来 ——
# 否则本地 Qt 6.8 编得过，Debian 12 上却报 “has no member named ...”。
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Charts Svg Network)
find_package(Qt6 6.4 QUIET COMPONENTS Test)

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
# ------------------------------------------------------------ 平台与架构头文件
# 编译期就把「当前是哪个平台、哪个架构」定下来，运行期只读常量，
# 不必再依赖 QSysInfo 的字符串（不同 Qt 版本拼写并不完全一致）。
if(WIN32)
    set(CSM_HOST_PLATFORM "Windows")
elseif(APPLE)
    set(CSM_HOST_PLATFORM "MacOS")
elseif(UNIX)
    set(CSM_HOST_PLATFORM "Linux")
else()
    set(CSM_HOST_PLATFORM "Other")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" CSM_HOST_CPU)
if(CSM_HOST_CPU MATCHES "^(x86_64|amd64|x64)$")
    set(CSM_HOST_ARCH "Amd64")
elseif(CSM_HOST_CPU MATCHES "^(aarch64|arm64)$")
    set(CSM_HOST_ARCH "Arm64")
elseif(CSM_HOST_CPU MATCHES "^loongarch64$|^loong64$")
    set(CSM_HOST_ARCH "Loong64")
elseif(CSM_HOST_CPU MATCHES "^riscv64$")
    set(CSM_HOST_ARCH "Riscv64")
else()
    set(CSM_HOST_ARCH "Other")
endif()
message(STATUS "Target platform: ${CSM_HOST_PLATFORM}/${CSM_HOST_ARCH}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/csm_platform.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/csm_platform.h"
    @ONLY
)
include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated")

# ---------------------------------------------------------------- 核心引擎
# 用 OBJECT 库而不是 STATIC：源码只编译一次，对象文件直接并进最终可执行文件，
# 构建目录里不再产出 libcsm_core.a / libcsm_ui.a 这类中间产物。
# 测试目标同样链接这两个对象库（每个目标各拿一份对象文件）。
add_library(csm_core OBJECT
    src/core/Grid.cpp
    src/core/PageSetup.cpp
    src/core/Language.cpp
    src/core/Units.cpp
    src/core/Component.cpp
    src/core/Netlist.cpp
    src/core/Circuit.cpp
    src/core/Shortcuts.cpp
    src/core/SafeWrite.cpp
    src/core/Autosave.cpp
    src/core/UpdateChecker.cpp
    src/sim/Simulator.cpp
)
target_include_directories(csm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(csm_core PUBLIC Qt6::Core Qt6::Network)
target_compile_options(csm_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---------------------------------------------------------------- 图形界面
set(CSM_UI_SOURCES
    src/ui/I18n.cpp
    src/ui/Dialogs.cpp
    src/ui/DialogFrame.cpp
    src/ui/SafeWrite.cpp
    src/ui/SingleInstance.cpp
    src/ui/Symbols.cpp
    src/ui/SettingsDialog.cpp
    src/ui/PageSetupDialog.cpp
    src/ui/TitleBar.cpp
    src/ui/Theme.cpp
    src/ui/ComponentItem.cpp
    src/ui/WireItem.cpp
    src/ui/SchematicScene.cpp
    src/ui/SchematicView.cpp
    src/ui/PropertyPanel.cpp
    src/ui/WaveformWindow.cpp
    src/ui/MultimeterWindow.cpp
    src/ui/UpdateDialog.cpp
    src/ui/MainWindow.cpp
)
set(CSM_UI_HEADERS
    src/ui/I18n.h
    src/ui/Dialogs.h
    src/ui/SingleInstance.h
    src/ui/Symbols.h
    src/ui/ComponentItem.h
    src/ui/WireItem.h
    src/ui/SchematicScene.h
    src/ui/SchematicView.h
    src/ui/PropertyPanel.h
    src/ui/WaveformWindow.h
    src/ui/UpdateDialog.h
    src/ui/MainWindow.h
)

add_library(csm_ui OBJECT ${CSM_UI_SOURCES} ${CSM_UI_HEADERS})
target_link_libraries(csm_ui PUBLIC csm_core Qt6::Widgets Qt6::Charts Qt6::Svg)
target_compile_options(csm_ui PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---------------------------------------------------------------- 资源
# 图标与应用图标统一放在 resources/assets/icons，由 qrc 以 /icons 前缀暴露
set(CSM_RESOURCES
    resources/assets/circuit-master.qrc
    resources/examples/examples.qrc
)

# 资源文件里列的那些文件改了，qrc 也要重编 —— 否则改了 .csm 直接构建，程序里
# 还是旧的示例（表现为「示例明明改过，打开却还是老样子」）。AUTORCC 目前只盯
# .qrc 本身，所以这里把清单里的文件手工挂成依赖。
file(GLOB CSM_EXAMPLE_FILES CONFIGURE_DEPENDS resources/examples/*.csm)
set(CSM_ICON_FILES "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/assets/icons")
    file(GLOB CSM_ICON_FILES CONFIGURE_DEPENDS resources/assets/icons/*.svg)
endif()
foreach(qrc IN ITEMS resources/examples/examples.qrc resources/assets/circuit-master.qrc)
    if(qrc STREQUAL "resources/examples/examples.qrc")
        set_property(SOURCE ${qrc} PROPERTY OBJECT_DEPENDS ${CSM_EXAMPLE_FILES})
    else()
        set_property(SOURCE ${qrc} PROPERTY OBJECT_DEPENDS ${CSM_ICON_FILES})
    endif()
endforeach()

# ---------------------------------------------------------------- 翻译
# 源码里文案是英文，中文放在 resources/translations/*.ts。
# lrelease 把 .ts 编成 .qm，再以 :/i18n 前缀嵌进资源 —— 运行时按设置装/卸。
find_package(Qt6 QUIET COMPONENTS LinguistTools)

# ---------------------------------------------------------------- 可执行程序
qt_add_executable(CircuitMaster
    src/main.cpp
    ${CSM_RESOURCES}
)

# 从源码里抽 CSM_TR("...") 更新 .ts。lupdate 认不出这个宏（它只认 tr/translate），
# 所以用 scripts/update-translations.py：
#     cmake --build build --target update_translations
#     cmake --build build --target check_translations   # 只查漏翻
add_custom_target(update_translations
    COMMAND ${CMAKE_COMMAND} -E echo "更新 resources/translations/*.ts ..."
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/update-translations.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Extracting CSM_TR() strings into .ts files"
)
add_custom_target(check_translations
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/update-translations.py --check
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking for untranslated strings"
)

if(Qt6LinguistTools_FOUND)
    # 注意要挂在 csm_core（而非可执行文件）上：测试只链库、不链可执行文件，
    # 挂在可执行文件上的话测试里拿不到 .qm，界面语言就退回英文了。
    # 英文是源语言，不需要 .qm，只编译中文。
    qt_add_lrelease(csm_core
        TS_FILES resources/translations/circuit-master_zh_CN.ts
        QM_FILES_OUTPUT_VARIABLE CSM_QM_FILES
    )
    qt_add_resources(csm_core "translations"
        PREFIX "/i18n"
        BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${CSM_QM_FILES}
    )
else()
    message(WARNING
        "没找到 Qt LinguistTools：中文翻译不会被打包，界面将只有英文。"
        "装好后重新配置即可（Debian/Ubuntu: qt6-l10n-tools，MSYS2: qt6-tools）。")
endif()
# OBJECT 库的对象文件不会自动跟着依赖传递，这里显式把两个都列上
target_link_libraries(CircuitMaster PRIVATE csm_ui csm_core)
# WIN32_EXECUTABLE 在非 Windows 上无副作用，MACOSX_BUNDLE 同理。
# macOS 的 .app 需要显式给出 bundle 版本，否则 Info.plist 里是空的，
# Finder 的「显示简介」与「关于」都看不到版本号。
set_target_properties(CircuitMaster PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "CircuitMaster"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.lhdjply.circuitmaster"
    MACOSX_BUNDLE_BUNDLE_VERSION "${CSM_VERSION_NUMBER}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${CSM_VERSION_NUMBER}"
    MACOSX_BUNDLE_ICON_FILE "circuit-master.icns"
)
# 图标本身要作为资源进 .app，只写 Info.plist 的名字 Finder 找不到文件
if(APPLE)
    set_source_files_properties(resources/mac/circuit-master.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(CircuitMaster PRIVATE resources/mac/circuit-master.icns)
endif()

# Windows 的 exe 图标与版本信息来自 PE 资源段：qrc 里的图标只作用于运行期窗口，
# 资源管理器里看到的图标要靠 .rc 编译进可执行文件。
if(WIN32)
    enable_language(RC)
    target_sources(CircuitMaster PRIVATE resources/win/circuit-master.rc)
endif()

# ---------------------------------------------------------------- 测试
# 默认构建测试；debian/rules 里也开着，打包时会顺带跑一遍 ctest。
# 需要 Qt6::Test（Qt6Test 的 CMake 配置在 qt6-base-dev 里）。
option(CSM_BUILD_TESTS "构建单元测试（需要 Qt6::Test）" ON)

if (CSM_BUILD_TESTS AND TARGET Qt6::Test)
    enable_testing()

    # 每个源文件一个独立的可执行文件（也就一个独立的测试类）：
    # 编译/链接互不影响，ctest 里自然就是 9 条，名字就是文件名。
    set(CSM_CORE_TEST_GROUPS analysis device model update)
    foreach(group ${CSM_CORE_TEST_GROUPS})
        add_executable(test_core_${group}
            tests/test_core_${group}.cpp)
        target_include_directories(test_core_${group} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(test_core_${group} PRIVATE csm_core Qt6::Test)
        # 让用例能直接读仓库里的示例文件（校验内置示例可用）
        target_compile_definitions(test_core_${group} PRIVATE
            CSM_EXAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/examples")
        add_test(NAME test_core_${group} COMMAND test_core_${group})
        # 用例里如果意外弹出模态框，进程会一直等下去。给个上限让它失败而不是挂住 CI
        set_tests_properties(test_core_${group} PROPERTIES TIMEOUT 300)
    endforeach()

    set(CSM_UI_TEST_GROUPS editor meter panel waveform workflow)
    foreach(group ${CSM_UI_TEST_GROUPS})
        add_executable(test_ui_${group}
            tests/test_ui_${group}.cpp
            ${CSM_RESOURCES})
        target_include_directories(test_ui_${group} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_compile_definitions(test_ui_${group} PRIVATE
            CSM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(test_ui_${group} PRIVATE csm_ui csm_core Qt6::Test)
        add_test(NAME test_ui_${group} COMMAND test_ui_${group})
        # 关掉启动时的更新检查：用例不该依赖网络；界面用例跑在 offscreen 上
        set_tests_properties(test_ui_${group} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CSM_NO_UPDATE_CHECK=1"
            TIMEOUT 300)
    endforeach()

endif()

# ---------------------------------------------------------------- 安装
if(APPLE)
    # MACOSX_BUNDLE 的目标必须给 BUNDLE DESTINATION，否则 configure 直接报错。
    # .app 装到 prefix 根下（例如 -DCMAKE_INSTALL_PREFIX=/Applications），
    # 这是 macOS 的惯例；发布用的 .dmg 由 CI 直接对 build/CircuitMaster.app
    # 跑 macdeployqt，不走这里。
    install(TARGETS CircuitMaster BUNDLE DESTINATION .)

    # 示例仍然装到 prefix 下，方便和 .app 一起分发
    install(DIRECTORY resources/examples/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/circuit-master/examples
            FILES_MATCHING PATTERN "*.csm")
else()
    # 桌面入口由模板生成，Exec 指向实际安装位置
    set(CSM_DESKTOP_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/CircuitMaster")
    configure_file(resources/linux/circuit-master.desktop.in
                   "${CMAKE_CURRENT_BINARY_DIR}/circuit-master.desktop" @ONLY)

    install(TARGETS CircuitMaster RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/circuit-master.desktop"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    # MIME 数据库：让桌面环境认识 .csm（debian/postinst 里刷新）
    install(FILES resources/linux/circuit-master-mime.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
            RENAME circuit-master.xml)
    install(FILES resources/assets/icons/icon.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME circuit-master.svg)
    install(DIRECTORY resources/examples/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/circuit-master/examples
            FILES_MATCHING PATTERN "*.csm")

    # 说明与许可证：GPL-3 要求分发时随附许可证全文
    install(FILES README.md CHANGELOG.md LICENSE
            DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/circuit-master)
endif()
