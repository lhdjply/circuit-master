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

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Charts Svg Network)
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
# ------------------------------------------------------------ 平台与架构头文件
# 编译期就把「当前是哪个平台、哪个架构」定下来，运行期只读常量，
# 不必再依赖 QSysInfo 的字符串（不同 Qt 版本拼写并不完全一致）。
if(WIN32)
    set(CSM_HOST_PLATFORM "Windows")
elseif(APPLE)
    set(CSM_HOST_PLATFORM "MacOS")
elseif(ANDROID)
    # Android 也算 UNIX，必须放在它前面判断，否则会被判成 Linux
    set(CSM_HOST_PLATFORM "Android")
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
    src/core/Units.cpp
    src/core/Component.cpp
    src/core/Netlist.cpp
    src/core/Circuit.cpp
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
    src/ui/SingleInstance.cpp
    src/ui/Symbols.cpp
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

# ---------------------------------------------------------------- 可执行程序
qt_add_executable(CircuitMaster
    src/main.cpp
    ${CSM_RESOURCES}
)
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

# Android：qt_add_executable 会顺带产出 APK/AAB 的构建目标。
# versionCode 必须是单调递增的整数，这里从 VERSION 推出来
# （0.3.3 → 303），发布新版本时自然递增。
if(ANDROID)
    string(REPLACE "." ";" CSM_VERSION_PARTS "${CSM_VERSION_NUMBER}")
    list(GET CSM_VERSION_PARTS 0 CSM_VER_MAJOR)
    list(GET CSM_VERSION_PARTS 1 CSM_VER_MINOR)
    list(GET CSM_VERSION_PARTS 2 CSM_VER_PATCH)
    math(EXPR CSM_ANDROID_VERSION_CODE
         "${CSM_VER_MAJOR} * 10000 + ${CSM_VER_MINOR} * 100 + ${CSM_VER_PATCH}")

    # 自定义清单 + 应用图标。不这么做装出来是 Qt 默认的绿机器人图标，
    # 清单里也没有侧载更新要用的权限。
    # 清单由 CMake 用 @变量@ 直接填，不用 androiddeployqt 的 INSERT 占位符
    # —— 那几个占位符在不同 Qt 版本里替换规则不完全一样。
    set(CSM_ANDROID_PACKAGE "io.github.lhdjply.circuitmaster")
    set(CSM_ANDROID_PACKAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/android-package")
    configure_file(resources/android/AndroidManifest.xml.in
                   "${CSM_ANDROID_PACKAGE_DIR}/AndroidManifest.xml" @ONLY)
    file(COPY resources/android/res DESTINATION "${CSM_ANDROID_PACKAGE_DIR}")

    set_target_properties(CircuitMaster PROPERTIES
        QT_ANDROID_PACKAGE_NAME "${CSM_ANDROID_PACKAGE}"
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CSM_ANDROID_PACKAGE_DIR}"
        QT_ANDROID_VERSION_NAME "${CSM_VERSION_NUMBER}"
        QT_ANDROID_VERSION_CODE ${CSM_ANDROID_VERSION_CODE}
        # minSdk / targetSdk 写在清单的 uses-sdk 里（23 / 34）：这两个值要和
        # Qt 那一侧 Gradle/AGP 对得上，跟着 Qt 6.7 的默认值走最稳
    )
    message(STATUS "Android 包名 ${CSM_ANDROID_PACKAGE}，"
                   "版本 ${CSM_VERSION_NUMBER}（versionCode ${CSM_ANDROID_VERSION_CODE}）")
endif()

# ---------------------------------------------------------------- 测试
# 打包时用 -DCSM_BUILD_TESTS=OFF 跳过（见 debian/rules）；
# Android 上没有能跑 ctest 的环境，也默认关掉
if(ANDROID)
    # Android 上 ctest 跑不起来（没有桌面事件循环，也没有 adb 拉日志的钩子），
    # 默认不建测试二进制，免得白编一遍
    set(CSM_BUILD_TESTS_DEFAULT OFF)
else()
    set(CSM_BUILD_TESTS_DEFAULT ON)
endif()
option(CSM_BUILD_TESTS "构建单元测试（需要 Qt6::Test）" ${CSM_BUILD_TESTS_DEFAULT})

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
# Android 的一切（清单、图标、库）都由 Qt 的 androiddeployqt 负责，
# 这里不要往 prefix 里装任何东西
if(ANDROID)
    # 空分支：交给 Qt 打包
elseif(APPLE)
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
