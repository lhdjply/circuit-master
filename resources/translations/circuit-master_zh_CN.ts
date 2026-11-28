<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<!--
  中文翻译表。源码里的文案是英文（CSM_TR("Open File")），这里是中文。

  改完不用手动编译：构建时 lrelease 会把它编成 .qm 嵌进资源。
  源码里新增文案后跑一次：
      python3 scripts/update-translations.py
  （或构建 update_translations 目标）
  新条目会以空译文加入，界面在这些地方退回英文。
-->
<TS version="2.1" language="zh_CN" sourcelanguage="en">
<context>
    <name>csm</name>
    <message>
        <source>
Waveform exceeds the range (scale locked), click Auto Scale to restore</source>
        <translation>
波形超出量程（手动量程已锁定），点「自动缩放」恢复自动</translation>
    </message>
    <message>
        <source> (This release has no changelog; open the release page.)</source>
        <translation>（该版本没有更新日志，请打开发布页查看。）</translation>
    </message>
    <message>
        <source> (local)</source>
        <translation>（本地）</translation>
    </message>
    <message>
        <source> comes before </source>
        <translation> 先于 </translation>
    </message>
    <message>
        <source> is formatted as </source>
        <translation> 被格式化成 </translation>
    </message>
    <message>
        <source> minutes</source>
        <translation> 分钟</translation>
    </message>
    <message>
        <source> mm</source>
        <translation> mm</translation>
    </message>
    <message>
        <source>"%1" has unsaved changes. Save them?</source>
        <translation>「%1」有未保存的更改，是否保存？</translation>
    </message>
    <message>
        <source>"%1" is not a finite number, using %2.</source>
        <translation>「%1」不是有限数，改用 %2。</translation>
    </message>
    <message>
        <source>%1 %2 (%3 × %4 mm)</source>
        <translation>%1 %2（%3 × %4 mm）</translation>
    </message>
    <message>
        <source>%1 is already used by %2; pick another shortcut.</source>
        <translation>%1 已经被「%2」占用，请换一个组合。</translation>
    </message>
    <message>
        <source>%1 s left</source>
        <translation>剩余 %1 s</translation>
    </message>
    <message>
        <source>%1 signals, %2 selected
%3 data points</source>
        <translation>%1 个信号，已选 %2 个
共 %3 个数据点</translation>
    </message>
    <message>
        <source>%1 × %2 mm · %3 × %4 units on the canvas</source>
        <translation>%1 × %2 mm · 画布上 %3 × %4 个单位</translation>
    </message>
    <message>
        <source>%1 — Multimeter</source>
        <translation>%1 — 万用表</translation>
    </message>
    <message>
        <source>%1%2 — Circuit Master</source>
        <translation>%1%2 — 电路仿真大师</translation>
    </message>
    <message>
        <source>%1/s</source>
        <translation>速度 %1/s</translation>
    </message>
    <message>
        <source>&amp;Edit</source>
        <translation>编辑(&amp;E)</translation>
    </message>
    <message>
        <source>&amp;File</source>
        <translation>文件(&amp;F)</translation>
    </message>
    <message>
        <source>&amp;Help</source>
        <translation>帮助(&amp;H)</translation>
    </message>
    <message>
        <source>&amp;New</source>
        <translation>新建(&amp;N)</translation>
    </message>
    <message>
        <source>&amp;Open…</source>
        <translation>打开(&amp;O)…</translation>
    </message>
    <message>
        <source>&amp;Print…</source>
        <translation>打印(&amp;P)…</translation>
    </message>
    <message>
        <source>&amp;Quit</source>
        <translation>退出(&amp;Q)</translation>
    </message>
    <message>
        <source>&amp;Save</source>
        <translation>保存(&amp;S)</translation>
    </message>
    <message>
        <source>&amp;Simulate</source>
        <translation>仿真(&amp;S)</translation>
    </message>
    <message>
        <source>&amp;View</source>
        <translation>视图(&amp;V)</translation>
    </message>
    <message>
        <source>(unbound)</source>
        <translation>（未绑定）</translation>
    </message>
    <message>
        <source>**Peak** amplitude, measured from the DC offset</source>
        <translation>**峰值**幅值，从直流偏置算起</translation>
    </message>
    <message>
        <source>**RMS** value of the sine source; peak = RMS x 1.414</source>
        <translation>正弦源的**有效值**（RMS）；峰值 = 有效值 × 1.414</translation>
    </message>
    <message>
        <source>. Check for shorted voltage sources or parallel ideal sources, or for floating nodes that are not connected.</source>
        <translation>。请检查是否存在电压源短路回路、理想电压源并联，或未连接的悬空节点。</translation>
    </message>
    <message>
        <source>&lt;b&gt;Three steps to a simulation&lt;/b&gt;&lt;ol&gt;&lt;li&gt;Drag parts from the &lt;b&gt;library&lt;/b&gt; on the left onto the canvas; select one and press &lt;b&gt;%1&lt;/b&gt; to rotate.&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Wire&lt;/b&gt; (%2) in the toolbar, click on a pin and drag out the polyline, double-click to finish.&lt;br&gt;Wire ends snap to pins and a junction dot appears automatically.&lt;/li&gt;&lt;li&gt;Double-click a part to change its value (you can type 10kΩ, 4.7µF or 1mH; the default unit applies if omitted), then press &lt;b&gt;%3&lt;/b&gt; to run and see the waveforms.&lt;/li&gt;&lt;/ol&gt;&lt;b&gt;Note&lt;/b&gt;: the circuit must contain a GND symbol, otherwise the reference potential is undefined.&lt;br&gt;&lt;br&gt;&lt;span style='color:%4'&gt;Tip: Simulate → Example Circuits has several runnable examples.&lt;/span&gt;</source>
        <translation>&lt;b&gt;三步完成一次仿真&lt;/b&gt;&lt;ol&gt;&lt;li&gt;从左侧&lt;b&gt;元件库&lt;/b&gt;把元件拖到画布上；选中后按 &lt;b&gt;%1&lt;/b&gt; 旋转。&lt;/li&gt;&lt;li&gt;点击工具栏&lt;b&gt;连线&lt;/b&gt;（%2），在引脚处单击并拖出折线，双击结束。&lt;br&gt;导线端点吸附到引脚后会自动出现连接点。&lt;/li&gt;&lt;li&gt;双击元件修改数值（可直接写 10kΩ、4.7µF、1mH，不写单位按默认单位），按 &lt;b&gt;%3&lt;/b&gt; 运行仿真，波形显示在查看器中。&lt;/li&gt;&lt;/ol&gt;&lt;b&gt;注意&lt;/b&gt;：电路必须包含接地符号（GND），否则无法确定参考电位。&lt;br&gt;&lt;br&gt;&lt;span style='color:%4'&gt;提示：菜单「仿真 → 示例电路」内置多个可直接运行的例子。&lt;/span&gt;</translation>
    </message>
    <message>
        <source>&lt;p style='color:%1'&gt;This build has no bundled changelog.&lt;/p&gt;</source>
        <translation>&lt;p style='color:%1'&gt;这个构建里没有内置更新日志。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>A DC operating point has no time series
See the report on this page; switch to Live Scope for waveforms</source>
        <translation>直流工作点没有时间序列
报告显示在本页；要看连续波形请切到「动态示波器」</translation>
    </message>
    <message>
        <source>AC Sweep</source>
        <translation>交流扫频</translation>
    </message>
    <message>
        <source>AC amps</source>
        <translation>交流电流档</translation>
    </message>
    <message>
        <source>AC analysis done: %1 points (%2 ~ %3)</source>
        <translation>交流分析完成：%1 个频点（%2 ~ %3）</translation>
    </message>
    <message>
        <source>AC analysis failed at %1: %2</source>
        <translation>交流分析在 %1 处失败：%2</translation>
    </message>
    <message>
        <source>AC frequency</source>
        <translation>交流频率</translation>
    </message>
    <message>
        <source>AC frequency must be between %1 and %2 Hz</source>
        <translation>交流频率要在 %1 ~ %2 Hz 之间</translation>
    </message>
    <message>
        <source>AC mode averages one full period: window = 1 / this frequency (%1-%2 Hz).
Set it to the measured signal's frequency, otherwise the window does not line up with the waveform and the reading drifts.</source>
        <translation>交流档按一个完整周期取平均：窗口 = 1 / 这个频率（%1~%2 Hz）。
填成被测信号的频率，否则窗口和波形对不齐，读数会飘。</translation>
    </message>
    <message>
        <source>AC volts</source>
        <translation>交流电压档</translation>
    </message>
    <message>
        <source>AGND (analog)</source>
        <translation>AGND（模拟地）</translation>
    </message>
    <message>
        <source>AND gate</source>
        <translation>与门</translation>
    </message>
    <message>
        <source>About Circuit Master</source>
        <translation>关于电路仿真大师</translation>
    </message>
    <message>
        <source>Actual size (1:1)</source>
        <translation>实际大小 (1:1)</translation>
    </message>
    <message>
        <source>Add junction here</source>
        <translation>在此处打交点</translation>
    </message>
    <message>
        <source>Align and Distribute</source>
        <translation>对齐与分布</translation>
    </message>
    <message>
        <source>Align bottom</source>
        <translation>底对齐</translation>
    </message>
    <message>
        <source>Align left</source>
        <translation>左对齐</translation>
    </message>
    <message>
        <source>Align top</source>
        <translation>顶对齐</translation>
    </message>
    <message>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <source>All download mirrors failed</source>
        <translation>所有下载地址都失败了</translation>
    </message>
    <message>
        <source>Already up to date (v%1).</source>
        <translation>已是最新版本（v%1）。</translation>
    </message>
    <message>
        <source>Amp/div</source>
        <translation>电流/格</translation>
    </message>
    <message>
        <source>Amplitude (peak)</source>
        <translation>幅值（峰值）</translation>
    </message>
    <message>
        <source>Analog ICs</source>
        <translation>模拟 IC</translation>
    </message>
    <message>
        <source>Analysis</source>
        <translation>分析</translation>
    </message>
    <message>
        <source>Analysis: AC %1 ~ %2</source>
        <translation>分析：交流 %1 ~ %2</translation>
    </message>
    <message>
        <source>Analysis: DC operating point</source>
        <translation>分析：直流工作点</translation>
    </message>
    <message>
        <source>Analysis: transient %1 / step %2</source>
        <translation>分析：瞬态 %1 / 步长 %2</translation>
    </message>
    <message>
        <source>Appearance</source>
        <translation>外观</translation>
    </message>
    <message>
        <source>Auto</source>
        <translation>自动</translation>
    </message>
    <message>
        <source>Auto Scale</source>
        <translation>自动缩放</translation>
    </message>
    <message>
        <source>Auto save failed: %1</source>
        <translation>自动保存失败：%1</translation>
    </message>
    <message>
        <source>Auto saved %1 circuit(s).</source>
        <translation>已自动保存 %1 份电路。</translation>
    </message>
    <message>
        <source>Autosave</source>
        <translation>自动保存</translation>
    </message>
    <message>
        <source>Basic</source>
        <translation>基础</translation>
    </message>
    <message>
        <source>Bias current flowing into each input terminal.</source>
        <translation>两个输入端各自流进器件的电流。</translation>
    </message>
    <message>
        <source>Black and white</source>
        <translation>黑白</translation>
    </message>
    <message>
        <source>Both pins of %1 are shorted together.</source>
        <translation>%1 的两个引脚被短接在一起。</translation>
    </message>
    <message>
        <source>Breakdown Vz</source>
        <translation>击穿电压 Vz</translation>
    </message>
    <message>
        <source>Built with Qt6 / C++17 / CMake　·　GPL-3.0 free software, a replacement for Multisim on Linux</source>
        <translation>基于 Qt6 / C++17 / CMake　·　GPL-3.0 自由软件，可作为 Multisim 在 Linux 上的替代方案</translation>
    </message>
    <message>
        <source>Button %1 pressed (%2)</source>
        <translation>按钮 %1 按下（%2）</translation>
    </message>
    <message>
        <source>CSV files (*.csv)</source>
        <translation>CSV 文件 (*.csv)</translation>
    </message>
    <message>
        <source>CTR</source>
        <translation>电流传输比 CTR</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Cancel / Clear Selection</source>
        <translation>取消 / 取消选择</translation>
    </message>
    <message>
        <source>Cannot check for updates:
%1</source>
        <translation>无法检查更新：
%1</translation>
    </message>
    <message>
        <source>Cannot open the example</source>
        <translation>无法打开示例</translation>
    </message>
    <message>
        <source>Cannot parse the update manifest: %1</source>
        <translation>无法解析更新清单：%1</translation>
    </message>
    <message>
        <source>Cannot read that value, e.g. 10kΩ, 4.7µF, 1meg</source>
        <translation>无法识别该数值，示例：10kΩ、4.7µF、1meg</translation>
    </message>
    <message>
        <source>Cannot simulate</source>
        <translation>无法仿真</translation>
    </message>
    <message>
        <source>Cannot start the installer: %1</source>
        <translation>无法启动安装程序：%1</translation>
    </message>
    <message>
        <source>Cannot start the live scope</source>
        <translation>无法运行示波器</translation>
    </message>
    <message>
        <source>Cannot start the live simulation: %1</source>
        <translation>无法启动实时仿真：%1</translation>
    </message>
    <message>
        <source>Cannot write the PDF file.</source>
        <translation>无法写入 PDF 文件。</translation>
    </message>
    <message>
        <source>Cannot write the file.</source>
        <translation>无法写入文件。</translation>
    </message>
    <message>
        <source>Cannot write the image file.</source>
        <translation>无法写入图片文件。</translation>
    </message>
    <message>
        <source>Capacitor</source>
        <translation>电容</translation>
    </message>
    <message>
        <source>Center horizontally</source>
        <translation>水平居中</translation>
    </message>
    <message>
        <source>Center vertically</source>
        <translation>垂直居中</translation>
    </message>
    <message>
        <source>Changelog</source>
        <translation>更新日志</translation>
    </message>
    <message>
        <source>Check for Updates</source>
        <translation>检查更新</translation>
    </message>
    <message>
        <source>Check for Updates…</source>
        <translation>检查更新…</translation>
    </message>
    <message>
        <source>Checking for updates…</source>
        <translation>正在检查更新…</translation>
    </message>
    <message>
        <source>Checksum mismatch: expected %1, got %2</source>
        <translation>校验和对不上：清单是 %1，实际是 %2</translation>
    </message>
    <message>
        <source>Checksum verified. Installing, confirm in the authorization window…</source>
        <translation>校验通过，正在安装，请在授权窗口确认…</translation>
    </message>
    <message>
        <source>Circuit Master</source>
        <translation>电路仿真大师</translation>
    </message>
    <message>
        <source>Circuit Master %1  —  Qt6 / MNA solver</source>
        <translation>电路仿真大师 %1  —  Qt6 / MNA 求解器</translation>
    </message>
    <message>
        <source>Circuit Master circuit (*.csm)</source>
        <translation>电路仿真大师电路 (*.csm)</translation>
    </message>
    <message>
        <source>Circuit Master circuit (*.csm);;All files (*)</source>
        <translation>电路仿真大师电路 (*.csm);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Circuit check failed</source>
        <translation>电路检查未通过</translation>
    </message>
    <message>
        <source>Circuit update failed: %1</source>
        <translation>电路更新失败：%1</translation>
    </message>
    <message>
        <source>Circuit updated, simulation continues</source>
        <translation>电路已更新，仿真继续</translation>
    </message>
    <message>
        <source>Clear</source>
        <translation>清除</translation>
    </message>
    <message>
        <source>Clear this shortcut, leaving the command unbound</source>
        <translation>清空这一行的快捷键（改为不绑定）</translation>
    </message>
    <message>
        <source>Click a shortcut and press the new keys. The × at the end of a row clears it, leaving that command without a shortcut.</source>
        <translation>点一下快捷键再按下新的按键。点行尾的「×」可清空这一行，该命令就不再绑定按键。</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source>Close &amp;Tab</source>
        <translation>关闭标签页(&amp;W)</translation>
    </message>
    <message>
        <source>Close the current tab; unsaved changes are confirmed first</source>
        <translation>关闭当前标签页；有未保存的更改会先问一句</translation>
    </message>
    <message>
        <source>Closed</source>
        <translation>闭合</translation>
    </message>
    <message>
        <source>Color</source>
        <translation>彩色</translation>
    </message>
    <message>
        <source>Color:</source>
        <translation>颜色：</translation>
    </message>
    <message>
        <source>Command</source>
        <translation>命令</translation>
    </message>
    <message>
        <source>Copy</source>
        <translation>复制</translation>
    </message>
    <message>
        <source>Crash recovery</source>
        <translation>崩溃恢复</translation>
    </message>
    <message>
        <source>Crash recovery copy could not be written: %1</source>
        <translation>崩溃恢复副本没能写成：%1</translation>
    </message>
    <message>
        <source>Cross-platform schematic capture and circuit simulation.&lt;br&gt;&lt;br&gt;&lt;b&gt;Solver&lt;/b&gt;　modified nodal analysis (MNA) with Newton-Raphson iteration,&lt;br&gt;　　　　　supporting DC operating point, transient and AC small-signal sweep.&lt;br&gt;&lt;b&gt;Parts&lt;/b&gt;　resistors, capacitors, inductors, transformers, diodes, transistors,&lt;br&gt;　　　　　MOSFETs, op-amps, logic gates, switches, sources, multimeters, probes, grounds.</source>
        <translation>跨平台的电路原理图与仿真软件。&lt;br&gt;&lt;br&gt;&lt;b&gt;求解器&lt;/b&gt;　改进节点法（MNA）+ 牛顿-拉夫逊迭代，&lt;br&gt;　　　　　　支持直流工作点、瞬态与交流小信号扫频。&lt;br&gt;&lt;b&gt;元件&lt;/b&gt;　　电阻、电容、电感、变压器、二极管、三极管、&lt;br&gt;　　　　　　MOS、运放、逻辑门、开关、电源、万用表、测试点、接地。</translation>
    </message>
    <message>
        <source>Current (%1A)</source>
        <translation>电流 (%1A)</translation>
    </message>
    <message>
        <source>Current (A)</source>
        <translation>电流 (A)</translation>
    </message>
    <message>
        <source>Current gain β</source>
        <translation>电流放大 β</translation>
    </message>
    <message>
        <source>Current probe</source>
        <translation>电流测试点</translation>
    </message>
    <message>
        <source>Current source</source>
        <translation>电流源</translation>
    </message>
    <message>
        <source>Current version v%1. The download is your call; the application never replaces itself.</source>
        <translation>当前版本 v%1。下载由你决定，本程序不会自行替换自身。</translation>
    </message>
    <message>
        <source>Custom</source>
        <translation>自定义</translation>
    </message>
    <message>
        <source>Custom size</source>
        <translation>自定义尺寸</translation>
    </message>
    <message>
        <source>Cut</source>
        <translation>剪切</translation>
    </message>
    <message>
        <source>DC</source>
        <translation>直流 (DC)</translation>
    </message>
    <message>
        <source>DC amps</source>
        <translation>直流电流档</translation>
    </message>
    <message>
        <source>DC operating point computed; the report is in Waveform Monitor → Static Waveforms; switch to Live Scope to see continuous waveforms.</source>
        <translation>直流工作点已算出：报告显示在「波形监控 → 静态波形」；要看连续波形请切到「动态示波器」。</translation>
    </message>
    <message>
        <source>DC operating point failed: </source>
        <translation>直流工作点求解失败：</translation>
    </message>
    <message>
        <source>DC operating point result</source>
        <translation>直流工作点分析结果</translation>
    </message>
    <message>
        <source>DC value</source>
        <translation>直流值</translation>
    </message>
    <message>
        <source>DC volts</source>
        <translation>直流电压档</translation>
    </message>
    <message>
        <source>DGND (digital)</source>
        <translation>DGND（数字地）</translation>
    </message>
    <message>
        <source>Dark</source>
        <translation>深色</translation>
    </message>
    <message>
        <source>Delay</source>
        <translation>延迟</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation>删除</translation>
    </message>
    <message>
        <source>Designator</source>
        <translation>编号</translation>
    </message>
    <message>
        <source>Detected a %1 build; will download: %2%3</source>
        <translation>检测到当前是%1运行，将下载：%2%3</translation>
    </message>
    <message>
        <source>Differential resistance between the + and − inputs.</source>
        <translation>接在 +/− 两个输入之间的差模电阻。</translation>
    </message>
    <message>
        <source>Digital logic</source>
        <translation>数字逻辑</translation>
    </message>
    <message>
        <source>Digits</source>
        <translation>位数</translation>
    </message>
    <message>
        <source>Diode</source>
        <translation>二极管</translation>
    </message>
    <message>
        <source>Discard</source>
        <translation>丢弃</translation>
    </message>
    <message>
        <source>Distribute horizontally</source>
        <translation>水平等距</translation>
    </message>
    <message>
        <source>Distribute vertically</source>
        <translation>垂直等距</translation>
    </message>
    <message>
        <source>Dot polarity</source>
        <translation>同名端</translation>
    </message>
    <message>
        <source>Dots on top (in phase)</source>
        <translation>同名端都在上（同相）</translation>
    </message>
    <message>
        <source>Download and Install</source>
        <translation>下载并安装</translation>
    </message>
    <message>
        <source>Download and Install Again</source>
        <translation>重新下载并安装</translation>
    </message>
    <message>
        <source>Download failed: %1</source>
        <translation>下载失败：%1</translation>
    </message>
    <message>
        <source>Downloaded %1</source>
        <translation>已下载 %1</translation>
    </message>
    <message>
        <source>Downloaded %1 / %2</source>
        <translation>已下载 %1 / %2</translation>
    </message>
    <message>
        <source>Downloaded, starting the installer…</source>
        <translation>已下载，正在启动安装程序…</translation>
    </message>
    <message>
        <source>Downloading %1; it installs automatically when done…</source>
        <translation>正在下载 %1，完成后会自动安装…</translation>
    </message>
    <message>
        <source>Downloading %2 from %1 …</source>
        <translation>正在从 %1 下载 %2 …</translation>
    </message>
    <message>
        <source>Downloading, please wait…</source>
        <translation>正在下载，请稍候…</translation>
    </message>
    <message>
        <source>Drag on the plot to zoom, right-click to reset</source>
        <translation>在图上拖拽可框选放大，右键复位</translation>
    </message>
    <message>
        <source>Drag parts onto the canvas</source>
        <translation>把元件拖到画布上放置</translation>
    </message>
    <message>
        <source>Draw wire (W)</source>
        <translation>绘制导线 (W)</translation>
    </message>
    <message>
        <source>Duplicate component id %1, renumbered to %2.</source>
        <translation>元件编号 %1 重复，已改成 %2。</translation>
    </message>
    <message>
        <source>Duty %</source>
        <translation>占空比 %</translation>
    </message>
    <message>
        <source>Duty cycle of the square wave, 0-100 %</source>
        <translation>方波的占空比，0-100 %</translation>
    </message>
    <message>
        <source>EARTH (protective)</source>
        <translation>EARTH（保护地）</translation>
    </message>
    <message>
        <source>Edit parameters…</source>
        <translation>编辑参数…</translation>
    </message>
    <message>
        <source>Element currents:</source>
        <translation>元件电流：</translation>
    </message>
    <message>
        <source>Enter N1:N2 or N1:N2:N3</source>
        <translation>请按 N1:N2 或 N1:N2:N3 填写</translation>
    </message>
    <message>
        <source>Enter a value</source>
        <translation>请输入数值</translation>
    </message>
    <message>
        <source>Every</source>
        <translation>每隔</translation>
    </message>
    <message>
        <source>Example "%1" loaded. Press %2 to run the simulation.</source>
        <translation>已载入示例「%1」。按 %2 运行仿真。</translation>
    </message>
    <message>
        <source>Example Circuits</source>
        <translation>示例电路</translation>
    </message>
    <message>
        <source>Example is damaged</source>
        <translation>示例损坏</translation>
    </message>
    <message>
        <source>Export CSV</source>
        <translation>导出 CSV</translation>
    </message>
    <message>
        <source>Export Image</source>
        <translation>导出图片</translation>
    </message>
    <message>
        <source>Export failed</source>
        <translation>导出失败</translation>
    </message>
    <message>
        <source>Export waveform data</source>
        <translation>导出波形数据</translation>
    </message>
    <message>
        <source>Exported: %1</source>
        <translation>已导出：%1</translation>
    </message>
    <message>
        <source>Fall time</source>
        <translation>下降时间</translation>
    </message>
    <message>
        <source>Filter parts by category</source>
        <translation>按类别筛选元件</translation>
    </message>
    <message>
        <source>Finished: HTTP %1, %2 bytes received, error %3 (%4)</source>
        <translation>结束：HTTP %1，已收 %2 字节，错误 %3（%4）</translation>
    </message>
    <message>
        <source>Fit to Sheet</source>
        <translation>适应图纸</translation>
    </message>
    <message>
        <source>Fit to Window</source>
        <translation>适应窗口</translation>
    </message>
    <message>
        <source>Fit to sheet</source>
        <translation>适应图纸</translation>
    </message>
    <message>
        <source>Flip horizontally</source>
        <translation>水平翻转</translation>
    </message>
    <message>
        <source>Flip vertically</source>
        <translation>垂直翻转</translation>
    </message>
    <message>
        <source>Follow System</source>
        <translation>跟随系统</translation>
    </message>
    <message>
        <source>Forward drop Vf</source>
        <translation>正向压降 Vf</translation>
    </message>
    <message>
        <source>Frequency</source>
        <translation>频率</translation>
    </message>
    <message>
        <source>Frequency (Hz)</source>
        <translation>频率 (Hz)</translation>
    </message>
    <message>
        <source>GND (main)</source>
        <translation>GND（主地）</translation>
    </message>
    <message>
        <source>Gain-bandwidth (Hz)</source>
        <translation>增益带宽积 (Hz)</translation>
    </message>
    <message>
        <source>Gain-bandwidth product. The dominant pole sits at GBW / A0;
0 means no bandwidth limit (ideal).</source>
        <translation>增益带宽积。主极点位于 GBW / A0；填 0 表示不限带宽（理想）。</translation>
    </message>
    <message>
        <source>Giving up on %1: received %2 bytes in %3 ms</source>
        <translation>放弃当前地址（%1）：已收 %2 字节，用时 %3 ms</translation>
    </message>
    <message>
        <source>Got it</source>
        <translation>知道了</translation>
    </message>
    <message>
        <source>Grid size</source>
        <translation>网格大小</translation>
    </message>
    <message>
        <source>Ground</source>
        <translation>接地</translation>
    </message>
    <message>
        <source>Ground type</source>
        <translation>接地种类</translation>
    </message>
    <message>
        <source>High level (V)</source>
        <translation>高电平 (V)</translation>
    </message>
    <message>
        <source>High level must be greater than 0</source>
        <translation>高电平必须大于 0</translation>
    </message>
    <message>
        <source>IGBT</source>
        <translation>IGBT</translation>
    </message>
    <message>
        <source>In degrees; any angle works, e.g. 15, 45, 60</source>
        <translation>单位是度，可以填任意角度，例如 15、45、60</translation>
    </message>
    <message>
        <source>Inductor</source>
        <translation>电感</translation>
    </message>
    <message>
        <source>Initial value</source>
        <translation>初始值</translation>
    </message>
    <message>
        <source>Input bias current (A)</source>
        <translation>输入偏置电流 (A)</translation>
    </message>
    <message>
        <source>Input offset (V)</source>
        <translation>输入失调 (V)</translation>
    </message>
    <message>
        <source>Input offset voltage: shifts the +/− input difference.</source>
        <translation>输入失调电压：等效成 +/− 输入之间的固定偏差。</translation>
    </message>
    <message>
        <source>Input resistance (Ω)</source>
        <translation>输入电阻 (Ω)</translation>
    </message>
    <message>
        <source>Install did not finish (exit code %1). Installer kept at: %2%3</source>
        <translation>安装未完成（退出码 %1）。安装包保留在：%2%3</translation>
    </message>
    <message>
        <source>Install finished and the installer was removed. Restart the application to use the new version.</source>
        <translation>安装完成，安装包已删除。重新启动程序即可用新版本。</translation>
    </message>
    <message>
        <source>Installed</source>
        <translation>安装版</translation>
    </message>
    <message>
        <source>Installer downloaded: %1</source>
        <translation>安装包已下载：%1</translation>
    </message>
    <message>
        <source>Installing, confirm in the authorization window…</source>
        <translation>正在安装，请在授权窗口里确认…</translation>
    </message>
    <message>
        <source>JSON parse error: %1</source>
        <translation>JSON 解析错误：%1</translation>
    </message>
    <message>
        <source>LED</source>
        <translation>发光二极管</translation>
    </message>
    <message>
        <source>Landscape</source>
        <translation>横向</translation>
    </message>
    <message>
        <source>Language</source>
        <translation>语言</translation>
    </message>
    <message>
        <source>Later</source>
        <translation>稍后</translation>
    </message>
    <message>
        <source>Library</source>
        <translation>元件库</translation>
    </message>
    <message>
        <source>Light</source>
        <translation>浅色</translation>
    </message>
    <message>
        <source>Live Scope</source>
        <translation>动态示波器</translation>
    </message>
    <message>
        <source>Live Scope — Circuit Master</source>
        <translation>实时示波器 — 电路仿真大师</translation>
    </message>
    <message>
        <source>Live scope started: it keeps simulating; switch back to Static Waveforms or close the panel to stop.</source>
        <translation>动态示波器已启动：连续仿真，切回静态波形或关闭面板即停止。</translation>
    </message>
    <message>
        <source>Live simulation starting…</source>
        <translation>实时仿真启动…</translation>
    </message>
    <message>
        <source>Live simulation stopped: %1</source>
        <translation>实时仿真中止：%1</translation>
    </message>
    <message>
        <source>Location:</source>
        <translation>位置：</translation>
    </message>
    <message>
        <source>Logic gate</source>
        <translation>逻辑门</translation>
    </message>
    <message>
        <source>Magnitude (dB)</source>
        <translation>幅值 (dB)</translation>
    </message>
    <message>
        <source>Main toolbar</source>
        <translation>主工具栏</translation>
    </message>
    <message>
        <source>Marked with '+': positive terminal on pin 1</source>
        <translation>符号带「+」，正极在引脚 1 一侧</translation>
    </message>
    <message>
        <source>Max Step</source>
        <translation>最大步长</translation>
    </message>
    <message>
        <source>Maximize</source>
        <translation>最大化</translation>
    </message>
    <message>
        <source>Measurement</source>
        <translation>测量</translation>
    </message>
    <message>
        <source>Measurement probe</source>
        <translation>测量探针</translation>
    </message>
    <message>
        <source>Messages</source>
        <translation>消息</translation>
    </message>
    <message>
        <source>Minimize</source>
        <translation>最小化</translation>
    </message>
    <message>
        <source>Mirror %1</source>
        <translation>地址 %1</translation>
    </message>
    <message>
        <source>Multimeter</source>
        <translation>万用表</translation>
    </message>
    <message>
        <source>Multimeter readings:</source>
        <translation>万用表读数：</translation>
    </message>
    <message>
        <source>N-MOSFET</source>
        <translation>N 沟道 MOS</translation>
    </message>
    <message>
        <source>N-channel MOSFET</source>
        <translation>N 沟道 MOS</translation>
    </message>
    <message>
        <source>NOT gate</source>
        <translation>非门</translation>
    </message>
    <message>
        <source>NPN transistor</source>
        <translation>NPN 三极管</translation>
    </message>
    <message>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <source>Net label</source>
        <translation>网络标签</translation>
    </message>
    <message>
        <source>Net labels with the same name are merged into one net, so no wire is needed.</source>
        <translation>同名的网络标签在网表里并成一个网络 —— 不用拉线也能把两处连起来。</translation>
    </message>
    <message>
        <source>Net name</source>
        <translation>网络名</translation>
    </message>
    <message>
        <source>New blank circuit (new tab).</source>
        <translation>已新建空白电路（新标签页）。</translation>
    </message>
    <message>
        <source>New blank circuit: drop parts from the library on the left, or load an example from Simulate → Example Circuits.</source>
        <translation>已新建空白电路：可从左侧元件库放置元件，或通过「仿真 → 示例电路」载入示例。</translation>
    </message>
    <message>
        <source>New version available</source>
        <translation>发现新版本</translation>
    </message>
    <message>
        <source>Newton iteration did not converge in 150 steps; check the loops with diodes or switches.</source>
        <translation>牛顿迭代 150 次仍未收敛，请检查二极管/开关所在回路。</translation>
    </message>
    <message>
        <source>Next Tab</source>
        <translation>下一个标签页</translation>
    </message>
    <message>
        <source>Next page</source>
        <translation>下一页</translation>
    </message>
    <message>
        <source>No GND in the circuit; %1 is used as the reference node (0 V).</source>
        <translation>电路里没有 GND，已把 %1 当作参考节点（0 V）。</translation>
    </message>
    <message>
        <source>No GND in the circuit; the negative terminal of multimeter %1 is used as the reference (0 V).</source>
        <translation>电路里没有 GND，已把万用表 %1 的负端当作参考节点（0 V）。</translation>
    </message>
    <message>
        <source>No package matches this system; download it manually from the release page: %1</source>
        <translation>没有找到适合当前系统的安装包，可到发布页手动下载：%1</translation>
    </message>
    <message>
        <source>No test points in the circuit, the waveform is empty.
Add a voltage (TP) or current (IP) test point and run again.</source>
        <translation>电路中没有测试点，波形为空。
放置电压测试点（TP）或电流测试点（IP）后再运行。</translation>
    </message>
    <message>
        <source>No test points, the waveform is empty.</source>
        <translation>没有测试点，波形为空。</translation>
    </message>
    <message>
        <source>No update manifest URL available</source>
        <translation>没有可用的更新清单地址</translation>
    </message>
    <message>
        <source>Node %1</source>
        <translation>节点 %1</translation>
    </message>
    <message>
        <source>Node %1 (%2)</source>
        <translation>节点 %1 (%2)</translation>
    </message>
    <message>
        <source>Node voltages exceed a sane range (&gt;1 GV); check the circuit wiring.</source>
        <translation>节点电压超出合理范围（&gt;1 GV），请检查电路连接。</translation>
    </message>
    <message>
        <source>Node voltages:</source>
        <translation>节点电压：</translation>
    </message>
    <message>
        <source>Nodes %1, branch unknowns %2, components %3</source>
        <translation>节点数 %1，支路未知量 %2，元件 %3</translation>
    </message>
    <message>
        <source>Normally closed</source>
        <translation>常闭</translation>
    </message>
    <message>
        <source>Normally closed (conducts while released)</source>
        <translation>常闭（松开时接通）</translation>
    </message>
    <message>
        <source>Normally open</source>
        <translation>常开</translation>
    </message>
    <message>
        <source>Not a Circuit Master circuit file.</source>
        <translation>不是电路仿真大师的电路文件。</translation>
    </message>
    <message>
        <source>Not saved to a file yet</source>
        <translation>尚未保存到文件</translation>
    </message>
    <message>
        <source>Note: limited by the point cap, the step is still coarser than the circuit dynamics, so switching waveforms will be distorted. Consider a stop time below %1.</source>
        <translation>注意：受点数上限限制，当前步长仍比电路动态粗，开关波形会失真。建议把停止时间缩短到 %1 以内。</translation>
    </message>
    <message>
        <source>Nothing to plot
Place a voltage (TP) or current (IP) test point and run again</source>
        <translation>没有可显示的波形
请在原理图中放置电压测试点（TP）或电流测试点（IP）后重新运行</translation>
    </message>
    <message>
        <source>OB2362 controller</source>
        <translation>OB2362 控制器</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <source>OR gate</source>
        <translation>或门</translation>
    </message>
    <message>
        <source>Offset</source>
        <translation>偏置</translation>
    </message>
    <message>
        <source>On-state drop Vf</source>
        <translation>导通压降 Vf</translation>
    </message>
    <message>
        <source>Op-amp</source>
        <translation>运算放大器</translation>
    </message>
    <message>
        <source>Open</source>
        <translation>断开</translation>
    </message>
    <message>
        <source>Open Circuit</source>
        <translation>打开电路</translation>
    </message>
    <message>
        <source>Open Containing Folder</source>
        <translation>打开所在文件夹</translation>
    </message>
    <message>
        <source>Open File</source>
        <translation>打开文件</translation>
    </message>
    <message>
        <source>Open failed</source>
        <translation>打开失败</translation>
    </message>
    <message>
        <source>Open-loop gain</source>
        <translation>开环增益</translation>
    </message>
    <message>
        <source>Opened: %1 (%2 components)</source>
        <translation>已打开：%1（%2 个元件）</translation>
    </message>
    <message>
        <source>Operating point</source>
        <translation>工作点</translation>
    </message>
    <message>
        <source>Operating point analysis: steady-state values.</source>
        <translation>工作点分析：结果为稳态值。</translation>
    </message>
    <message>
        <source>Optocoupler</source>
        <translation>光耦</translation>
    </message>
    <message>
        <source>Orientation</source>
        <translation>方向</translation>
    </message>
    <message>
        <source>Orientation:</source>
        <translation>方向：</translation>
    </message>
    <message>
        <source>Output file:</source>
        <translation>输出文件：</translation>
    </message>
    <message>
        <source>Output high level. Low level is fixed at 0 V; input threshold is half of it (5% hysteresis).</source>
        <translation>输出的高电平。低电平固定 0 V，输入阈值取它的一半（带 5% 回差）。</translation>
    </message>
    <message>
        <source>Output resistance (Ω)</source>
        <translation>输出电阻 (Ω)</translation>
    </message>
    <message>
        <source>Output swing high (V)</source>
        <translation>输出上限 (V)</translation>
    </message>
    <message>
        <source>Output swing low (V)</source>
        <translation>输出下限 (V)</translation>
    </message>
    <message>
        <source>P-MOSFET</source>
        <translation>P 沟道 MOS</translation>
    </message>
    <message>
        <source>P-channel MOSFET</source>
        <translation>P 沟道 MOS</translation>
    </message>
    <message>
        <source>PDF document</source>
        <translation>PDF 文件</translation>
    </message>
    <message>
        <source>PDF document (*.pdf)</source>
        <translation>PDF 文档 (*.pdf)</translation>
    </message>
    <message>
        <source>PGND (power)</source>
        <translation>PGND（功率地）</translation>
    </message>
    <message>
        <source>PNG image (*.png)</source>
        <translation>PNG 图片 (*.png)</translation>
    </message>
    <message>
        <source>PNG images (*.png)</source>
        <translation>PNG 图片 (*.png)</translation>
    </message>
    <message>
        <source>PNP transistor</source>
        <translation>PNP 三极管</translation>
    </message>
    <message>
        <source>Page %1 / %2</source>
        <translation>第 %1 / %2 页</translation>
    </message>
    <message>
        <source>Page Set&amp;up…</source>
        <translation>页面设置(&amp;U)…</translation>
    </message>
    <message>
        <source>Page Setup</source>
        <translation>页面设置</translation>
    </message>
    <message>
        <source>Page Setup…</source>
        <translation>页面设置…</translation>
    </message>
    <message>
        <source>Page: %1</source>
        <translation>图纸：%1</translation>
    </message>
    <message>
        <source>Paper size</source>
        <translation>纸张大小</translation>
    </message>
    <message>
        <source>Paper size and orientation for this sheet</source>
        <translation>设置这张图纸的纸张大小与方向</translation>
    </message>
    <message>
        <source>Paper:</source>
        <translation>纸张：</translation>
    </message>
    <message>
        <source>Paste</source>
        <translation>粘贴</translation>
    </message>
    <message>
        <source>Pause</source>
        <translation>暂停</translation>
    </message>
    <message>
        <source>Period</source>
        <translation>周期</translation>
    </message>
    <message>
        <source>Phase °</source>
        <translation>相位 °</translation>
    </message>
    <message>
        <source>Pin %2 of %1 is floating, not connected to anything.</source>
        <translation>%1 的引脚 %2 悬空，未与任何元件相连。</translation>
    </message>
    <message>
        <source>Place some parts and wire them up first.</source>
        <translation>请先放置元件并连接电路。</translation>
    </message>
    <message>
        <source>Points</source>
        <translation>点数</translation>
    </message>
    <message>
        <source>Polarity</source>
        <translation>极性</translation>
    </message>
    <message>
        <source>Polarized (electrolytic)</source>
        <translation>有极性（电解电容）</translation>
    </message>
    <message>
        <source>Portable</source>
        <translation>便携版</translation>
    </message>
    <message>
        <source>Portable package downloaded to %1; unzip it over the original folder</source>
        <translation>便携包已下载到 %1，解压后覆盖原目录即可</translation>
    </message>
    <message>
        <source>Portrait</source>
        <translation>纵向</translation>
    </message>
    <message>
        <source>Previous Tab</source>
        <translation>上一个标签页</translation>
    </message>
    <message>
        <source>Previous page</source>
        <translation>上一页</translation>
    </message>
    <message>
        <source>Primary + auxiliary + secondary</source>
        <translation>初级 + 辅助 + 次级</translation>
    </message>
    <message>
        <source>Primary inductance L1</source>
        <translation>初级电感 L1</translation>
    </message>
    <message>
        <source>Print</source>
        <translation>打印</translation>
    </message>
    <message>
        <source>Print this sheet</source>
        <translation>打印这张图纸</translation>
    </message>
    <message>
        <source>Print to file</source>
        <translation>打印到文件</translation>
    </message>
    <message>
        <source>Print to file (PDF)</source>
        <translation>打印到文件（PDF）</translation>
    </message>
    <message>
        <source>Printed to: %1</source>
        <translation>已打印到：%1</translation>
    </message>
    <message>
        <source>Printer</source>
        <translation>打印机</translation>
    </message>
    <message>
        <source>Probe I(dc): %1</source>
        <translation>I(直流): %1</translation>
    </message>
    <message>
        <source>Probe I(pp): %1</source>
        <translation>I(峰-峰): %1</translation>
    </message>
    <message>
        <source>Probe I(rms): %1</source>
        <translation>I(有效值): %1</translation>
    </message>
    <message>
        <source>Probe V(dc): %1</source>
        <translation>V(直流): %1</translation>
    </message>
    <message>
        <source>Probe V(pp): %1</source>
        <translation>V(峰-峰): %1</translation>
    </message>
    <message>
        <source>Probe V(rms): %1</source>
        <translation>V(有效值): %1</translation>
    </message>
    <message>
        <source>Probe f: %1</source>
        <translation>频率: %1</translation>
    </message>
    <message>
        <source>Properties</source>
        <translation>属性</translation>
    </message>
    <message>
        <source>Published %1. %2</source>
        <translation>发布于 %1。%2</translation>
    </message>
    <message>
        <source>Pulse (PULSE)</source>
        <translation>脉冲 (PULSE)</translation>
    </message>
    <message>
        <source>Pulse value</source>
        <translation>脉冲值</translation>
    </message>
    <message>
        <source>Pulse width</source>
        <translation>脉宽</translation>
    </message>
    <message>
        <source>Push button</source>
        <translation>按钮开关</translation>
    </message>
    <message>
        <source>Quick Start</source>
        <translation>快速上手</translation>
    </message>
    <message>
        <source>RMS</source>
        <translation>有效值 (RMS)</translation>
    </message>
    <message>
        <source>Range</source>
        <translation>档位</translation>
    </message>
    <message>
        <source>Re-pick the scale from the current waveform (use after setting Volt/div manually)</source>
        <translation>按当前波形重新自动选量程（手动改过「电压/格」之后用它恢复自动）</translation>
    </message>
    <message>
        <source>Recover unsaved work?</source>
        <translation>恢复上次未保存的电路？</translation>
    </message>
    <message>
        <source>Recovered %1 circuit(s).</source>
        <translation>已恢复 %1 份电路。</translation>
    </message>
    <message>
        <source>Recovered circuit</source>
        <translation>恢复的电路</translation>
    </message>
    <message>
        <source>Recovered files discarded.</source>
        <translation>已丢弃恢复的文件。</translation>
    </message>
    <message>
        <source>Redo</source>
        <translation>重做</translation>
    </message>
    <message>
        <source>Redone (%1/%2)</source>
        <translation>已重做（%1/%2）</translation>
    </message>
    <message>
        <source>Reference Vref</source>
        <translation>基准电压 Vref</translation>
    </message>
    <message>
        <source>Request timed out</source>
        <translation>请求超时</translation>
    </message>
    <message>
        <source>Reset label positions</source>
        <translation>还原标签位置</translation>
    </message>
    <message>
        <source>Resistance</source>
        <translation>电阻档</translation>
    </message>
    <message>
        <source>Resistor</source>
        <translation>电阻</translation>
    </message>
    <message>
        <source>Restore</source>
        <translation>还原</translation>
    </message>
    <message>
        <source>Restore Defaults</source>
        <translation>恢复默认</translation>
    </message>
    <message>
        <source>Resume</source>
        <translation>继续</translation>
    </message>
    <message>
        <source>Retrying with another mirror (%1/%2)…</source>
        <translation>换个地址重试（%1/%2）…</translation>
    </message>
    <message>
        <source>Right align</source>
        <translation>右对齐</translation>
    </message>
    <message>
        <source>Rise time</source>
        <translation>上升时间</translation>
    </message>
    <message>
        <source>Rotate 90°</source>
        <translation>旋转 90°</translation>
    </message>
    <message>
        <source>Rotation (°)</source>
        <translation>旋转角度 (°)</translation>
    </message>
    <message>
        <source>Run Again</source>
        <translation>重新仿真</translation>
    </message>
    <message>
        <source>Run Simulation</source>
        <translation>运行仿真</translation>
    </message>
    <message>
        <source>Run Simulation (%1)</source>
        <translation>运行仿真（%1）</translation>
    </message>
    <message>
        <source>Run Simulation (F5)</source>
        <translation>运行仿真（F5）</translation>
    </message>
    <message>
        <source>Saturation Vce(sat)</source>
        <translation>饱和压降 Vce(sat)</translation>
    </message>
    <message>
        <source>Save</source>
        <translation>保存</translation>
    </message>
    <message>
        <source>Save &amp;As…</source>
        <translation>另存为(&amp;A)…</translation>
    </message>
    <message>
        <source>Save Changes</source>
        <translation>保存更改</translation>
    </message>
    <message>
        <source>Save Circuit</source>
        <translation>保存电路</translation>
    </message>
    <message>
        <source>Save failed</source>
        <translation>保存失败</translation>
    </message>
    <message>
        <source>Saved to %1</source>
        <translation>已保存到 %1</translation>
    </message>
    <message>
        <source>Saved: %1</source>
        <translation>已保存：%1</translation>
    </message>
    <message>
        <source>Saving</source>
        <translation>保存</translation>
    </message>
    <message>
        <source>Sawtooth</source>
        <translation>锯齿波</translation>
    </message>
    <message>
        <source>Scale:</source>
        <translation>比例：</translation>
    </message>
    <message>
        <source>Secondary dot at bottom (inverted)</source>
        <translation>次级同名端在下（反相）</translation>
    </message>
    <message>
        <source>Select</source>
        <translation>选择</translation>
    </message>
    <message>
        <source>Select All</source>
        <translation>全选</translation>
    </message>
    <message>
        <source>Select a part on the schematic
to edit its parameters</source>
        <translation>在原理图中选择一个元件
以编辑其参数</translation>
    </message>
    <message>
        <source>Select mode: drag to move, double-click to edit, %1 to rotate</source>
        <translation>选择模式：拖动元件移动，双击元件编辑参数，%1 旋转</translation>
    </message>
    <message>
        <source>Select mode: drag to move, double-click to edit, Space to rotate</source>
        <translation>选择模式：拖动元件移动，双击元件编辑参数，空格键旋转</translation>
    </message>
    <message>
        <source>Select/move parts (V)</source>
        <translation>选择/移动元件 (V)</translation>
    </message>
    <message>
        <source>Semiconductors</source>
        <translation>半导体</translation>
    </message>
    <message>
        <source>Sent to printer: %1</source>
        <translation>已发送到打印机：%1</translation>
    </message>
    <message>
        <source>Series resistance at the output; the output sags under load.</source>
        <translation>输出端的等效串联电阻，带载时输出电压会掉。</translation>
    </message>
    <message>
        <source>Settings</source>
        <translation>设置</translation>
    </message>
    <message>
        <source>Settings…</source>
        <translation>设置…</translation>
    </message>
    <message>
        <source>Shortcut</source>
        <translation>快捷键</translation>
    </message>
    <message>
        <source>Shortcuts</source>
        <translation>快捷键</translation>
    </message>
    <message>
        <source>Show Grid</source>
        <translation>显示网格</translation>
    </message>
    <message>
        <source>Signal generator</source>
        <translation>信号发生器</translation>
    </message>
    <message>
        <source>Signals</source>
        <translation>信号</translation>
    </message>
    <message>
        <source>Simulation failed</source>
        <translation>仿真失败</translation>
    </message>
    <message>
        <source>Simulation is running, but the circuit has no test points so the waveform is empty. Place a TP / IP to see curves.</source>
        <translation>仿真运行中，但电路没有测试点，波形为空。放置 TP / IP 后可看到曲线。</translation>
    </message>
    <message>
        <source>Simulation running: click Waveform Monitor to see the traces, Stop Simulation to end.</source>
        <translation>仿真运行中：点「波形监控」查看波形，点「停止仿真」结束。</translation>
    </message>
    <message>
        <source>Sine</source>
        <translation>正弦</translation>
    </message>
    <message>
        <source>Sine (SIN)</source>
        <translation>正弦 (SIN)</translation>
    </message>
    <message>
        <source>Single winding</source>
        <translation>单绕组</translation>
    </message>
    <message>
        <source>Singular linear system (tiny pivot at row %1)</source>
        <translation>线性方程组奇异（第 %1 行主元过小）</translation>
    </message>
    <message>
        <source>Slew rate (V/µs)</source>
        <translation>压摆率 (V/µs)</translation>
    </message>
    <message>
        <source>Snap Wires to Grid</source>
        <translation>导线对齐网格</translation>
    </message>
    <message>
        <source>Snap wire corners to the grid, fixing offsets left by older paste</source>
        <translation>把导线的拐点吸到网格上，修老版本粘贴留下的歪线</translation>
    </message>
    <message>
        <source>Sources</source>
        <translation>电源</translation>
    </message>
    <message>
        <source>Space</source>
        <translation>空格</translation>
    </message>
    <message>
        <source>Square</source>
        <translation>方波</translation>
    </message>
    <message>
        <source>Start</source>
        <translation>起始</translation>
    </message>
    <message>
        <source>Static Waveforms</source>
        <translation>静态波形</translation>
    </message>
    <message>
        <source>Stop</source>
        <translation>终止</translation>
    </message>
    <message>
        <source>Stop Simulation</source>
        <translation>停止仿真</translation>
    </message>
    <message>
        <source>Stop Time</source>
        <translation>停止时间</translation>
    </message>
    <message>
        <source>Stop the running live scope</source>
        <translation>停止正在运行的动态示波器</translation>
    </message>
    <message>
        <source>Switch</source>
        <translation>开关</translation>
    </message>
    <message>
        <source>Switch %1 %2</source>
        <translation>开关 %1 %2</translation>
    </message>
    <message>
        <source>Switch closed</source>
        <translation>开关闭合</translation>
    </message>
    <message>
        <source>Switches and Grounds</source>
        <translation>开关与接地</translation>
    </message>
    <message>
        <source>TL431 reference</source>
        <translation>TL431 基准</translation>
    </message>
    <message>
        <source>That file is already open in a tab: %1</source>
        <translation>该文件已经在标签页里：%1</translation>
    </message>
    <message>
        <source>The application did not exit cleanly last time; %1 unsaved circuit(s) were recovered.

%2

Restore them?</source>
        <translation>上次程序没有正常退出，恢复了 %1 份未保存的电路。

%2

要恢复吗？</translation>
    </message>
    <message>
        <source>The circuit has no main ground symbol (GND). Place a GND and connect it, otherwise the reference node is undefined (AGND/DGND/PGND/EARTH do not count; a multimeter's negative terminal works as a reference too).</source>
        <translation>电路中没有主接地符号（GND）。请放置 GND 并连接到电路，否则无法确定参考节点（AGND/DGND/PGND/EARTH 不算参考地；有万用表时也可以拿它的负端当参考）。</translation>
    </message>
    <message>
        <source>The circuit has no solvable nodes.</source>
        <translation>电路中没有可求解的节点。</translation>
    </message>
    <message>
        <source>The circuit has no solvable nodes. Place parts and wire them together.</source>
        <translation>电路中没有可求解的节点。请放置元件并用导线连接。</translation>
    </message>
    <message>
        <source>The downloaded file did not match its checksum, trying another mirror (%1/%2)…</source>
        <translation>下载的文件校验对不上，换一个镜像重试（%1/%2）…</translation>
    </message>
    <message>
        <source>The example has no components.</source>
        <translation>示例中没有元件。</translation>
    </message>
    <message>
        <source>The file has no components.</source>
        <translation>文件中没有元件。</translation>
    </message>
    <message>
        <source>The installer has started; the package is cleaned up automatically once you finish</source>
        <translation>安装程序已启动，按提示完成后会自动清理安装包</translation>
    </message>
    <message>
        <source>The interface language applies after a restart.</source>
        <translation>界面语言在重启后生效。</translation>
    </message>
    <message>
        <source>The interface language has been changed. Restart the application to apply it.</source>
        <translation>界面语言已修改，重启程序后生效。</translation>
    </message>
    <message>
        <source>The manifest has no download URL</source>
        <translation>清单里没有下载地址</translation>
    </message>
    <message>
        <source>The manifest has no usable version field</source>
        <translation>更新清单里没有可用的 version 字段</translation>
    </message>
    <message>
        <source>The net name cannot be empty</source>
        <translation>网络名不能为空</translation>
    </message>
    <message>
        <source>The recovery record has no id.</source>
        <translation>恢复记录缺少 id。</translation>
    </message>
    <message>
        <source>The solution contains non-finite values; the circuit may be diverging.</source>
        <translation>求解结果出现非有限数值，电路可能发散。</translation>
    </message>
    <message>
        <source>The turns ratio must be positive</source>
        <translation>匝数比必须是正数</translation>
    </message>
    <message>
        <source>The waveform monitor needs a GND symbol. This circuit has none, so only multimeter readings are available.</source>
        <translation>波形监控需要有接地符号（GND）。当前电路没有 GND，只能看万用表读数。</translation>
    </message>
    <message>
        <source>Theme</source>
        <translation>主题</translation>
    </message>
    <message>
        <source>There is nothing on the schematic to export.</source>
        <translation>原理图里没有可导出的内容。</translation>
    </message>
    <message>
        <source>This mirror is too slow, switching</source>
        <translation>这个地址太慢，换一个</translation>
    </message>
    <message>
        <source>This mirror sent no data, switching</source>
        <translation>这个地址一直没有数据，换一个</translation>
    </message>
    <message>
        <source>This parameter is unitless; enter a plain number</source>
        <translation>该参数没有单位，请直接输入数字</translation>
    </message>
    <message>
        <source>This release has no manifest</source>
        <translation>该 Release 没有更新清单</translation>
    </message>
    <message>
        <source>Threshold Vth</source>
        <translation>开启电压 Vth</translation>
    </message>
    <message>
        <source>Time (%1)</source>
        <translation>时间 (%1)</translation>
    </message>
    <message>
        <source>Time/div</source>
        <translation>时间/格</translation>
    </message>
    <message>
        <source>Tip: %1 is an AC range and needs one full period (%2 Hz → %3) before it reads; the current stop time %4 is too short, readings will show dashes.</source>
        <translation>提示：%1 是交流档，需要一个完整周期（%2 Hz → %3）才有读数；当前停止时间 %4 不够，读数会显示横杠。</translation>
    </message>
    <message>
        <source>Tip: left-drag to select and move, double-click to edit, %1 to rotate; %2 runs the simulation.</source>
        <translation>提示：左键选择拖动，双击编辑参数，%1 旋转；按 %2 运行仿真。</translation>
    </message>
    <message>
        <source>Tip: the max step %1 is coarser than the switching dynamics (suggest ≤ %2), the waveforms will be distorted and the output may not resolve.</source>
        <translation>提示：最大步长 %1 比电路的开关动态粗（建议 ≤ %2），开关波形会失真，输出可能算不出来。</translation>
    </message>
    <message>
        <source>Tip: the multimeter window opens only while the live simulation runs. Press %1, then click it.</source>
        <translation>提示：万用表读数窗口只在实时仿真运行时打开。按 %1 运行仿真后再点它。</translation>
    </message>
    <message>
        <source>Tip: the positive terminal of an electrolytic capacitor is on pin 1; reversing it will break it down. Select the capacitor to toggle polarity.</source>
        <translation>提示：电解电容的正极在引脚 1 一侧，反接会击穿。选中电容可勾选/取消极性。</translation>
    </message>
    <message>
        <source>Tip: values may carry units (10kΩ, 4.7µF, 10mH, 5V, 1mA); without a unit the component default applies.</source>
        <translation>提示：数值可直接带单位（10kΩ、4.7µF、10mH、5V、1mA）；不写单位时按元件默认单位。</translation>
    </message>
    <message>
        <source>Too many steps; adjusted to %1 steps (step %2).</source>
        <translation>步数过多，已自动调整为 %1 步（步长 %2）。</translation>
    </message>
    <message>
        <source>Transformer</source>
        <translation>变压器</translation>
    </message>
    <message>
        <source>Transient</source>
        <translation>瞬态</translation>
    </message>
    <message>
        <source>Transient analysis</source>
        <translation>瞬态分析</translation>
    </message>
    <message>
        <source>Transient analysis done: %1 steps, step %2, ended at %3</source>
        <translation>瞬态分析完成：%1 步，步长 %2，终止于 %3</translation>
    </message>
    <message>
        <source>Transient failed at t = %1: %2</source>
        <translation>瞬态求解在 t = %1 时失败：%2</translation>
    </message>
    <message>
        <source>Triangle</source>
        <translation>三角波</translation>
    </message>
    <message>
        <source>Try %1/%2  %3</source>
        <translation>尝试 %1/%2  %3</translation>
    </message>
    <message>
        <source>Turns ratio N1:N2</source>
        <translation>匝数比 N1:N2</translation>
    </message>
    <message>
        <source>Turns ratio N1:N2:N3</source>
        <translation>匝数比 N1:N2:N3</translation>
    </message>
    <message>
        <source>Type:</source>
        <translation>类型：</translation>
    </message>
    <message>
        <source>Unchecked: normally open, conducts while held during simulation.
Checked: normally closed, opens while held.</source>
        <translation>不勾：常开，仿真时按住才接通。
勾上：常闭，仿真时按住才断开。</translation>
    </message>
    <message>
        <source>Undo</source>
        <translation>撤销</translation>
    </message>
    <message>
        <source>Undone (%1/%2)</source>
        <translation>已撤销（%1/%2）</translation>
    </message>
    <message>
        <source>Unit mismatch, expected %1</source>
        <translation>单位不匹配，应为 %1</translation>
    </message>
    <message>
        <source>Unknown component type "%1" (skipped).</source>
        <translation>认不出的元件型号「%1」，已跳过。</translation>
    </message>
    <message>
        <source>Unknown error</source>
        <translation>未知错误</translation>
    </message>
    <message>
        <source>Unknown unit: %1</source>
        <translation>无法识别的单位：%1</translation>
    </message>
    <message>
        <source>Unknown value: %1</source>
        <translation>无法识别的数值：%1</translation>
    </message>
    <message>
        <source>Untitled</source>
        <translation>未命名</translation>
    </message>
    <message>
        <source>Update check failed: %1</source>
        <translation>检查更新失败：%1</translation>
    </message>
    <message>
        <source>Value</source>
        <translation>数值</translation>
    </message>
    <message>
        <source>Value (F)</source>
        <translation>数值 (F)</translation>
    </message>
    <message>
        <source>Value (H)</source>
        <translation>数值 (H)</translation>
    </message>
    <message>
        <source>Value (Ω)</source>
        <translation>数值 (Ω)</translation>
    </message>
    <message>
        <source>Version %1 available (%2 packages)</source>
        <translation>发现新版本 %1（%2 个安装包）</translation>
    </message>
    <message>
        <source>Version %1 has been released</source>
        <translation>新版本 %1 已发布</translation>
    </message>
    <message>
        <source>Volt/div</source>
        <translation>电压/格</translation>
    </message>
    <message>
        <source>Voltage (%1V)</source>
        <translation>电压 (%1V)</translation>
    </message>
    <message>
        <source>Voltage (V)</source>
        <translation>电压 (V)</translation>
    </message>
    <message>
        <source>Voltage probe</source>
        <translation>电压测试点</translation>
    </message>
    <message>
        <source>Voltage source</source>
        <translation>电压源</translation>
    </message>
    <message>
        <source>Volts per microsecond. 0 means unlimited.</source>
        <translation>单位 V/µs；0 表示不限速。</translation>
    </message>
    <message>
        <source>Warning: %1</source>
        <translation>提示：%1</translation>
    </message>
    <message>
        <source>Waveform</source>
        <translation>波形</translation>
    </message>
    <message>
        <source>Waveform Monitor</source>
        <translation>波形监控</translation>
    </message>
    <message>
        <source>Waveform Monitor — Circuit Master</source>
        <translation>波形监控 — 电路仿真大师</translation>
    </message>
    <message>
        <source>Waveform Viewer — %1 — Circuit Master</source>
        <translation>波形查看器 — %1 — 电路仿真大师</translation>
    </message>
    <message>
        <source>Waveform monitor (%1): Static Waveforms / Live Scope tabs (available while simulating)</source>
        <translation>波形监控（%1）：静态波形 / 动态示波器两页（仿真运行时可用）</translation>
    </message>
    <message>
        <source>Windings</source>
        <translation>绕组形式</translation>
    </message>
    <message>
        <source>Wire</source>
        <translation>连线</translation>
    </message>
    <message>
        <source>Wire mode: click to add a corner, double-click or right-click to finish, Esc to cancel</source>
        <translation>连线模式：单击放置拐点，双击或右键结束，Esc 取消</translation>
    </message>
    <message>
        <source>XNOR gate</source>
        <translation>同或门</translation>
    </message>
    <message>
        <source>XOR gate</source>
        <translation>异或门</translation>
    </message>
    <message>
        <source>Zener diode</source>
        <translation>稳压二极管</translation>
    </message>
    <message>
        <source>Zoom In</source>
        <translation>放大</translation>
    </message>
    <message>
        <source>Zoom Out</source>
        <translation>缩小</translation>
    </message>
    <message>
        <source>Zoom so the whole sheet fills the view</source>
        <translation>缩放到整张图纸铺满视图</translation>
    </message>
    <message>
        <source>e.g. -15</source>
        <translation>例如 -15</translation>
    </message>
    <message>
        <source>e.g. 0%1</source>
        <translation>例如 0%1</translation>
    </message>
    <message>
        <source>e.g. 0.2</source>
        <translation>例如 0.2</translation>
    </message>
    <message>
        <source>e.g. 0.5</source>
        <translation>例如 0.5</translation>
    </message>
    <message>
        <source>e.g. 0.7</source>
        <translation>例如 0.7</translation>
    </message>
    <message>
        <source>e.g. 1</source>
        <translation>例如 1</translation>
    </message>
    <message>
        <source>e.g. 1.2</source>
        <translation>例如 1.2</translation>
    </message>
    <message>
        <source>e.g. 100</source>
        <translation>例如 100</translation>
    </message>
    <message>
        <source>e.g. 100k</source>
        <translation>例如 100k</translation>
    </message>
    <message>
        <source>e.g. 100n</source>
        <translation>例如 100n</translation>
    </message>
    <message>
        <source>e.g. 100nF / 1µF</source>
        <translation>例如 100nF / 1µF</translation>
    </message>
    <message>
        <source>e.g. 10k / 4.7u</source>
        <translation>例如 10k / 4.7u</translation>
    </message>
    <message>
        <source>e.g. 10kΩ / 4.7k</source>
        <translation>例如 10kΩ / 4.7k</translation>
    </message>
    <message>
        <source>e.g. 10mH</source>
        <translation>例如 10mH</translation>
    </message>
    <message>
        <source>e.g. 15</source>
        <translation>例如 15</translation>
    </message>
    <message>
        <source>e.g. 1M</source>
        <translation>例如 1M</translation>
    </message>
    <message>
        <source>e.g. 1kHz</source>
        <translation>例如 1kHz</translation>
    </message>
    <message>
        <source>e.g. 1ms</source>
        <translation>例如 1ms</translation>
    </message>
    <message>
        <source>e.g. 2</source>
        <translation>例如 2</translation>
    </message>
    <message>
        <source>e.g. 2.5</source>
        <translation>例如 2.5</translation>
    </message>
    <message>
        <source>e.g. 220%1</source>
        <translation>例如 220%1</translation>
    </message>
    <message>
        <source>e.g. 2M</source>
        <translation>例如 2M</translation>
    </message>
    <message>
        <source>e.g. 2m</source>
        <translation>例如 2m</translation>
    </message>
    <message>
        <source>e.g. 3</source>
        <translation>例如 3</translation>
    </message>
    <message>
        <source>e.g. 3.3</source>
        <translation>例如 3.3</translation>
    </message>
    <message>
        <source>e.g. 45</source>
        <translation>例如 45</translation>
    </message>
    <message>
        <source>e.g. 5%1</source>
        <translation>例如 5%1</translation>
    </message>
    <message>
        <source>e.g. 5.1</source>
        <translation>例如 5.1</translation>
    </message>
    <message>
        <source>e.g. 50</source>
        <translation>例如 50</translation>
    </message>
    <message>
        <source>e.g. 50Hz</source>
        <translation>例如 50Hz</translation>
    </message>
    <message>
        <source>e.g. VCC / GND_D / OUT</source>
        <translation>例如 VCC / GND_D / OUT</translation>
    </message>
    <message>
        <source>landscape</source>
        <translation>横向</translation>
    </message>
    <message>
        <source>pkexec not found, cannot install automatically: %1</source>
        <translation>未找到 pkexec，无法自动安装：%1</translation>
    </message>
    <message>
        <source>portrait</source>
        <translation>纵向</translation>
    </message>
    <message>
        <source>t = %1  ·  %2/div  ·  step %3</source>
        <translation>t = %1  ·  %2/div  ·  步长 %3</translation>
    </message>
    <message>
        <source>unknown error</source>
        <translation>未知错误</translation>
    </message>
    <message>
        <source>──────── Simulation ────────</source>
        <translation>──────── 仿真 ────────</translation>
    </message>
    <message>
        <source>⚠ Could not restore the previous state: %1</source>
        <translation>⚠ 无法恢复历史状态：%1</translation>
    </message>
    <message>
        <source>⚠ No test points in the circuit, the waveform is empty. Place a voltage (TP) or current (IP) test point.</source>
        <translation>⚠ 电路中没有测试点，波形为空。请放置电压测试点（TP）或电流测试点（IP）。</translation>
    </message>
    <message>
        <source>⚠ The circuit cannot be solved: %1</source>
        <translation>⚠ 电路无法求解：%1</translation>
    </message>
    <message>
        <source>⚠ The circuit changed a lot; the simulation restarts when you switch back</source>
        <translation>⚠ 电路结构变化较大，切回该图纸后仿真会重新开始</translation>
    </message>
    <message>
        <source>✔ Done in %1 ms</source>
        <translation>✔ 完成，用时 %1 ms</translation>
    </message>
    <message>
        <source>✘ Simulation failed: </source>
        <translation>✘ 仿真失败：</translation>
    </message>
    <message>
        <source>💥 %1 electrolytic capacitor broke down: reverse voltage %2</source>
        <translation>💥 %1 电解电容反接击穿：反向电压 %2</translation>
    </message>
</context>
</TS>
