# 电路仿真大师（Circuit Master）

一款面向 Linux 的跨平台电路原理图编辑与仿真软件，使用 **Qt 6 + C++17 + CMake** 开发，
目标是提供 Multisim 的核心工作流替代方案：**画原理图 → 点运行 → 看波形**。

![主界面](docs/screenshots/main.png)

## 功能特性

**原理图编辑器**

- 网格画布、元件拖动吸附、旋转（空格键）、框选、删除
- 撤销 / 重做（Ctrl+Z / Ctrl+Shift+Z），覆盖放置、移动、删除、粘贴与参数修改
- 框选后可整体拖动（元件与导线一起移动，连接关系保持不变）
- 元件右键菜单：旋转 90°、水平翻转、垂直翻转、复制/粘贴（Ctrl+C / Ctrl+V）、编辑属性、删除
- 元件库：顶部下拉框按「基础 / 半导体 / 电源 / 开关与接地 / 测试点」分类筛选，
  拖到画布上放置
- 折线导线绘制：端点自动吸附引脚，T 型连接与交叉点自动生成连接圆点
- 双击元件或选中后在右侧属性面板编辑参数，数值可直接带单位：`10kΩ`、`4.7µF`、`10mH`、`5V`、`1mA`；
  不写单位时按元件默认单位（Ω/F/H/V/A）
- 内置 6 个可直接运行的示例电路（位于 `resources/examples`，可直接编辑）

**仿真引擎（改进节点法 MNA）**

| 分析 | 说明 |
| --- | --- |
| 直流工作点 | 含二极管牛顿-拉夫逊迭代与 pnjlim 电压限幅 |
| 瞬态分析 | 后向欧拉积分，电容/电感伴随模型，结果含所有节点电压与元件电流 |
| 交流扫频 | 复数 MNA，十倍频扫频，输出幅频特性（dB），对数刻度波特图 |

**实时示波器**

- 与静态波形共用同一白色界面，连续仿真、滚动显示，不需要“停止时间”
- 步长自动推导：取「每窗约 500 点」与「每个电源周期 20 点」的较小值，避免大时基下采样混叠
- 启动时按峰值自动选一次「电压/格」「电流/格」，之后仅在被削顶时自动放大，缩小需手动
- 6×10 网格，时间轴固定为 0–时间窗，波形在其中滚动；左下角显示当前时间与“时间/格”
- 时间/格 5 ns/div–100 s/div（总窗口 = 10 格），支持暂停 / 继续 / 停止
- 关闭窗口或点「停止」即结束实时仿真
- 电压与电流分别使用左右 Y 轴
- 「波形监控」是独立窗口，两个标签页：静态波形（曲线与导出）和动态示波器（连续滚动）；
  运行仿真不会自动弹窗，点「波形监控」才打开，关掉窗口仿真仍在后台继续；
  分析参数（分析类型、停止时间、最大步长、扫频范围）就在静态波形页里，改完点「重新仿真」

**检查更新**

- 菜单「帮助 → 检查更新…」手动检查，或启动 3 秒后自动检查一次（只在新版本出现时弹窗）
- 数据来源是随 Release 上传的 `latest.json`，依次尝试 gh-proxy.org / ghfast.top / ghproxy.net 三个镜像，
  首条请求 700 ms 未返回就并行补发一条，避免单条连接卡住
- 只查询与展示，不下载、不自动替换自身；清单里的发布页地址只认本仓库，防止被篡改的清单带偏
- 设置环境变量 `CSM_NO_UPDATE_CHECK=1` 可关闭启动检查（测试即用此方式）

**测试点与波形**

- 电压测试点 TP：单端探针，测量所在节点电压
- 电流测试点 IP：二端探针，串联 0 V 源测量支路电流
- 波形只显示测试点信号；未放置测试点时波形为空
- 电压/电流同时显示时自动启用左右双 Y 轴，并按 mV/mA/µA 自动缩放
- 框选缩放、自动缩放、时间轴自动切换 ns/µs/ms、工程记数法刻度
- 导出 CSV 数据、导出 PNG 图片

**文件与互操作**

- `.csm` 工程文件（JSON，可读、易扩展；可携带分析类型与参数）
- 一键导出 SPICE 网表（`.cir`），可交给 ngspice 等外部工具复核
- 桌面集成：`.desktop` 文件与矢量图标，`cmake --install` 即可出现在应用菜单

### 界面截图

| 属性编辑 | 瞬态分析（RC 充电） |
| --- | --- |
| ![属性](docs/screenshots/properties.png) | ![瞬态](docs/screenshots/transient_rc.png) |

| 交流扫频（RC 低通，-3 dB @ 1.59 kHz） | 二极管半波整流 |
| --- | --- |
| ![交流](docs/screenshots/ac_lowpass.png) | ![整流](docs/screenshots/transient_diode.png) |

| 运放反相放大器（R2/R1 = 10） | 输出波形（反相 10 倍） |
| --- | --- |
| ![运放](docs/screenshots/opamp.png) | ![运放波形](docs/screenshots/transient_opamp.png) |

![动态示波器](docs/screenshots/scope.png)

![检查更新](docs/screenshots/update_dialog.png)

五种三端器件（NPN / PNP / N 沟道 MOS / P 沟道 MOS / IGBT）：

![三端器件](docs/screenshots/transistors.png)

波形监控窗口（静态波形 / 动态示波器，分析参数在静态页里）：

![波形监控](docs/screenshots/monitor.png)

发光二极管会按正向电流点亮（3 mA 时）：

![LED 点亮](docs/screenshots/led_on.png)

## 快速开始

### 依赖

- CMake ≥ 3.21
- C++17 编译器（GCC 11+ / Clang 14+）
- Qt 6.5+：`Core`、`Gui`、`Widgets`、`Charts`、`Svg`，测试还需要 `Test`

Deepin / Debian / Ubuntu：

```bash
sudo apt install build-essential cmake qt6-base-dev qt6-charts-dev libqt6svg6-dev
```

### 构建与运行

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/CircuitMaster
```

### 安装到系统

```bash
sudo cmake --install build --prefix /usr/local
```

安装内容包括可执行文件、桌面入口与图标。之后可直接在应用菜单中搜索「电路仿真大师」。

> 桌面入口由 `resources/linux/circuit-master.desktop.in` 在配置阶段生成，`Exec` 取自
> `CMAKE_INSTALL_FULL_BINDIR`。若修改安装前缀，请重新运行一次 `cmake -S . -B build`
> 让路径同步。

### 运行测试

```bash
ctest --test-dir build --output-on-failure
```

- `core`：工程记数法解析、网表提取、分压/RC 充电/二极管整流/AC 低通数值正确性、JSON 往返、SPICE 导出
- `ui`：窗口渲染截图、属性编辑、旋转、鼠标绘制导线、从零搭建 RC 电路并仿真、交流分析截图

## 使用说明

启动后是空白电路。想快速体验可先打开「仿真 → 示例电路」中的任意示例。

1. **放置元件**：从左侧「元件库」把元件拖到画布上放置。
2. **连线**：点击工具栏「连线」（或按 `W`），在引脚处单击，移动鼠标继续单击添加拐点，**双击或右键结束**；`Esc` 取消。
3. **编辑参数**：双击元件，或右键选择「编辑参数…」，在右侧属性面板修改编号与数值，回车生效。
   误操作可用 `Ctrl+Z` 撤销、`Ctrl+Shift+Z` 重做。
   右键还可以旋转、水平/垂直翻转、复制粘贴元件。
4. **放置测试点**：从元件库选择「电压测试点 TP」或「电流测试点 IP」，放在要观察的节点/支路上。
5. **运行仿真**：按 `F5` 或点击「运行仿真」。运行只做计算，不会自动弹窗口；
   要看曲线点「静态波形」，要连续观察点「动态示波器」。
   波形只显示测试点信号，没有测试点时为空；工作点分析会弹出文字报告。
6. **分析参数**：在「波形监控 → 静态波形」页顶部切换工作点 / 瞬态 / 交流扫频，
   设置停止时间、最大步长或扫频范围，点「重新仿真」生效；
   瞬态还可选择「显示方式」为静态波形图或动态示波器；动态模式会自动隐藏
   “停止时间/最大步长”并按时间窗设置步长，关闭窗口或点「停止」结束。
   这些设置会随 `.csm` 一起保存，重新打开时自动恢复。

> 电路必须包含**接地符号（GND）**，否则无法确定参考节点，仿真会给出提示。

### 支持的元件

| 元件 | 参数 |
| --- | --- |
| 电阻 R | 阻值（Ω） |
| 电容 C | 容值（F），瞬态使用后向欧拉伴随模型 |
| 电感 L | 感值（H），以支路电流为未知量 |
| 二极管 D | 1N4148 简化模型（IS=2.52 nA，N=1.752） |
| 发光二极管 LED | 与二极管同方程，由正向压降反推模型；参数：Vf（默认 2 V）。仿真后按正向电流点亮，电流越大越亮 |
| 三极管 Q | NPN / PNP，Ebers-Moll 模型；参数：电流放大系数 β（默认 100） |
| MOS 管 M | N / P 沟道增强型，level-1 平方律模型；参数：开启电压 Vth、跨导参数 K |
| IGBT Z | 栅极按 MOS 模型、输出侧带导通压降 Vf；参数：Vth、K、Vf |
| 运算放大器 U | 三端理想运放（VCVS + 50 Ω 输出电阻），开环增益默认 100 k |
| 电压测试点 TP | 单引脚探针，读取节点电压，不参与矩阵求解 |
| 电流测试点 IP | 二引脚探针，以 0 V 电压源串入支路，读取该支路电流 |
| 开关 S | 闭合 1 mΩ / 断开 1 GΩ |
| 电压源 V | DC / SIN / PULSE |
| 电流源 I | DC / SIN / PULSE |
| 接地 GND | 参考节点 0 |

## 项目结构

```
├── CMakeLists.txt
├── resources/
│   ├── assets/
│   │   ├── circuit-master.qrc      图标资源清单（/icons 前缀）
│   │   └── icons/                  应用图标 + 12 枚工具栏图标
│   ├── examples/                   内置示例（.csm + examples.qrc）
│   ├── linux/circuit-master.desktop.in   桌面入口模板（CMake 生成）
│   └── win/                        Windows 资源
│       ├── circuit-master.ico      exe 图标 / 安装程序图标（由 SVG 生成）
│       ├── circuit-master.rc       PE 资源段（资源管理器里的 exe 图标）
│       ├── circuit-master.iss      Inno Setup 安装脚本
│       └── ChineseSimplified.isl   Inno Setup 简体中文语言包
├── scripts/
│   └── package-linux-tarball.sh    从 .deb 生成便携 tar.gz
├── src/
│   ├── core/                       数据模型与网表
│   │   ├── Component.{h,cpp}       元件定义、引脚几何、源波形
│   │   ├── Circuit.{h,cpp}         电路文档、JSON、SPICE 导出
│   │   ├── Netlist.{h,cpp}         引脚坐标并查集 → 节点编号
│   │   └── Units.{h,cpp}           工程记数法解析/格式化
│   ├── sim/
│   │   ├── Matrix.h                稠密 LU（实数 + 复数）
│   │   └── Simulator.{h,cpp}       MNA 装配、牛顿迭代、瞬态/交流
│   ├── ui/                         原理图编辑器与波形查看器
│   └── main.cpp
├── tests/                          test_core / test_ui
└── docs/architecture.md            求解器与实现细节
```

## 实现要点

- **连接关系**：把所有引脚与导线顶点映射到 10 px 网格并查集合并；引脚落在导线中间时视为 T 型连接。只有“导线端点落在导线上”或“引脚落在导线上”才产生连接点，十字交叉不会被误连。
- **求解器**：未知量为非地节点电压 + 电压源/电感支路电流；每个节点对地加 1 pS 的 gmin 以避免纯浮空节点导致矩阵奇异。
- **非线性**：二极管用牛顿迭代线性化，并采用 SPICE 的 pnjlim 限制每次迭代电压跳变。
- **瞬态**：电容伴随电导 `C/h` 与历史电流源，电感伴随电阻 `L/h` 与历史电压源；以直流工作点作为初始条件。

更详细的公式与矩阵结构见 [docs/architecture.md](docs/architecture.md)。

## 已知限制与路线图

当前版本是 **0.1.0**，聚焦于“可用且结果可信”的最小闭环。已知限制：

- 导线可整体拖动，但不能单独拖动端点（需删除重画）
- 二极管模型未包含串联电阻、结电容；开关为理想电阻模型
- 瞬态为固定步长后向欧拉（一阶精度），暂不支持自适应步长与梯形法
- 稠密矩阵求解，建议元件规模在数百以内
- 交流分析输出幅值，暂未绘制相位曲线

计划：

- [ ] 导线端点拖动、对齐与自动布线
- [ ] 受控源 / 变压器等元件，运放饱和与带宽模型，IGBT 开关特性
- [ ] 自适应步长瞬态、梯形法、SPICE `.model` 导入
- [ ] 交流相位、阻抗、傅里叶与参数扫描
- [ ] 子电路与层次化设计、BOM 与 PDF 出图

## 版本号

版本号只有一个来源：仓库根目录的 `VERSION` 文件（写法与 git tag 一致，如 `v0.1.0`）。

- CMake 在配置阶段读取它，剥掉 `v` 前缀后交给 `project(VERSION ...)`，
  并生成 `csm_version.h`（`CSM_VERSION_STRING` / `CSM_VERSION_NUMBER`）供源码使用
- 界面标题栏、消息区、关于对话框与 `--version` 输出都取自该宏，源码里不再硬编码版本号
- `VERSION` 已登记为 CMake 重配置依赖，改完直接构建即可生效
- 发版时同步更新 `CHANGELOG.md`

```bash
./build/CircuitMaster --version
# 电路仿真大师 v0.1.0
```

## 持续集成

`.github/workflows/` 下有三个工作流（参考 drawio-qt 的结构）：

| 工作流 | 触发 | 内容 |
| --- | --- | --- |
| `build.yaml` | push / PR | Linux（Ubuntu 26.04）与 Windows（MSYS2 UCRT64 / MinGW）编译，校验 Qt ≥ 6.5、QtCharts 可用、产物存在（Windows 还检查 PE 子系统为 GUI） |
| `test.yaml` | push / PR | 两端跑 `ctest`（`QT_QPA_PLATFORM=offscreen`），失败时自动 `--rerun-failed -V` 重跑并上传日志 |
| `release.yaml` | 任意分支 / tag | **Linux 四架构**（amd64 / arm64 / loong64 / riscv64，Deepin crimson 容器 + QEMU）`debuild` 出 `.deb` + 便携 `tar.gz`；**Windows 两架构**（x64 / arm64）`windeployqt` 打便携 zip 与 Inno Setup 安装包；随后生成校验和并发布 Release |

发版流程：

```bash
# 1. 改版本号（VERSION 与 debian/changelog 必须一致，CMake 会校验）
echo "v0.2.0" > VERSION
dch -v 0.2.0    # 或手工编辑 debian/changelog
# 2. 在 CHANGELOG.md 写一节 "## [v0.2.0]"，Release 正文直接取这一节
# 3. 打 tag 触发发布
git tag v0.2.0 && git push origin v0.2.0
```

> 未打 tag 的推送会生成 `continuous_build` 预发布，便于随时下载最新构建。

正式版（打 tag）还会额外发布一个 `latest.json` 更新清单，客户端直接读它判断版本，
不必先连 github.com 的 API：

```
https://github.com/<owner>/<repo>/releases/latest/download/latest.json
```

内容包含 `version`、`tag`、`name`、`page`、`notes`（与 Release 正文同源）与 `published_at`。
预发布不生成该文件（`/releases/latest` 本身就跳过预发布）。

## 打包（Debian / Ubuntu / Deepin）

仓库内已包含 `debian/` 打包文件，可直接用 dpkg 工具链构建 `.deb`：

```bash
# 构建依赖：debhelper、cmake、Qt6 开发包
sudo apt install debhelper cmake qt6-base-dev qt6-charts-dev qt6-svg-dev

# 生成安装包（产物在上一级目录）
dpkg-buildpackage -us -uc -b
```

打包相关约定：

- 版本号：`debian/changelog` 是安装包的版本来源（dpkg 的硬约束），
  与根目录 `VERSION` 不一致时 CMake 会给出警告，发版请用 `dch -v 0.2.0` 同步
- 打包时传入 `-DCSM_BUILD_TESTS=OFF` 跳过单元测试（测试需要 Qt6::Test，与安装内容无关）
- 安装内容全部由 CMake 的 install 规则提供：可执行文件、`.desktop`、SVG 图标、示例电路

Windows 侧产物：

- 便携 zip：解压即用，`windeployqt` 已带齐 Qt 运行库与插件（含 `iconengines`，否则 SVG 图标不显示）
- 安装包 `*-setup.exe`：Inno Setup 6.5+ 编译，默认按用户安装（不弹 UAC），
  中文界面取自 `resources/win/ChineseSimplified.isl`

便携 tar.gz 由 `scripts/package-linux-tarball.sh` 从 `.deb` 生成，
内含启动脚本（设置 `LD_LIBRARY_PATH` / `QT_PLUGIN_PATH`）、Qt 运行库、插件与内置示例：

```bash
scripts/package-linux-tarball.sh --arch amd64 --from-deb ../circuit-master_0.1.0_amd64.deb
```

安装后的文件布局：

```
/usr/bin/CircuitMaster
/usr/share/applications/circuit-master.desktop
/usr/share/icons/hicolor/scalable/apps/circuit-master.svg
/usr/share/circuit-master/examples/*.csm
/usr/share/doc/circuit-master/          README、CHANGELOG、架构说明
```

## 开发约定

提交信息遵循 [Conventional Commits](https://www.conventionalcommits.org/zh-hans/)：
`feat` / `fix` / `docs` / `refactor` / `perf` / `test` / `build` / `chore` 等，
格式为 `<type>(<scope>): <subject>`。详见 [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md)。

仓库提供了提交模板，克隆后执行一次即可启用：

```bash
git config commit.template .gitmessage
```

## 许可

本项目为自由软件，可作为 Multisim 在 Linux 上的替代方案使用与修改。
如需用于商业分发，请自行确认所链接 Qt 模块的许可证（LGPL/GPL）要求。
