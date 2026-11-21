#pragma once

#include "core/UpdateChecker.h"
#include "core/Language.h"
#include "ui/Document.h"
#include "sim/Simulator.h"

#include <QJsonObject>
#include <QMainWindow>

class QAction;
class QComboBox;
class QDockWidget;
class QTabWidget;
class QTimer;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;

namespace csm {

class ComponentItem;
class PropertyPanel;
class SchematicScene;
class SchematicView;
class WaveformWindow;
class MultimeterWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    /// 直接打开指定路径（命令行参数、文件关联用），失败时返回 false 并已提示
    bool openPath(const QString& path);
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void runSimulation();
    void openScope();


protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createDocks();
    void createStatusBar();
    /// 切换主题后重装调色板并让所有窗口重画
    void applyTheme();

    void newCircuit();
    void openCircuit();
    bool saveCircuit();
    bool saveCircuitAs();
    void scanExamples();
    void loadExample(int index);
    void checkForUpdates(bool manual);
    void createMonitorWindow();
    /// 把当前分析参数同步到静态波形页的输入框
    void syncMonitorOptions();
    /// 运行仿真按钮在「运行 / 停止」之间切换外观
    void updateRunAction();
    /// 显示「波形监控」面板；tab 0 = 静态波形，1 = 动态示波器
    void showMonitor(int tab);
    void onWaveTabChanged(int index);
    /// 用当前电路启动实时示波器，成功返回 true
    bool startLiveScope();
    /// 取（必要时创建）某个文档自己的动态示波器
    WaveformWindow* liveScopeFor(Document* document);
    /// 把某个文档的动态示波器挂到「动态示波器」页，并让 m_waveLive 指向它
    void bindLiveScope(Document* document);
    /// 按某个文档的电路刷新 LED 亮度
    void refreshDocumentLeds(Document* document);
    /// m_waveLive 属于哪个文档（现在恒等于当前文档，留着当断言用）
    Document* m_liveDoc = nullptr;
    /// 按仿真结果点亮/熄灭电路里的发光二极管
    void applyLedStates(const SimulationResult& result);
    /// 更新测量探针上的读数面板
    void applyProbeReadouts(const SimulationResult& result);
    /// 实时仿真中刷新测量探针：按当前窗口累积最小/最大/均值/有效值
    void refreshLiveProbes();
    /// 清掉实时统计（开始 / 停止仿真、探针移动时）
    void resetLiveProbeStats();
    /// 熄灭所有 LED（停止仿真或换电路时调用）
    void clearLedStates();
    /// 极性电容反接击穿：标记元件并播放爆炸动画
    void applyBlownCapacitors(const QHash<int, double>& blown);
    void clearBlast();
    void showQuickGuide();
    void showSettings();
    void showAbout();

    void pushHistory();
    void undo();
    void redo();
    void resetHistory();
    void restoreSnapshot(const QJsonObject& snapshot);
    void updateHistoryActions();

    void onSelectionChanged();
    void onEditRequested(ComponentItem* item);
    /// 点击万用表时打开（或置前）它的读数窗口
    void toggleSwitch(ComponentItem* item);
    /// 按住 / 松开按钮开关；仿真停下时把所有按钮还原成静止状态
    void pressPushButton(ComponentItem* item);
    void releasePushButton(ComponentItem* item);
    void restorePushButtons();
    void openMeterWindow(ComponentItem* item);
    /// 电路里有没有主接地符号。没有 GND 时（参考点由万用表负端顶替）
    /// 不给开波形监控 —— 节点电压是相对表笔算的，波形没有统一的参考点
    bool monitorAvailable() const;
    /// 万用表读数窗口只在实时仿真运行时才有意义（其余时候读数全是横杠）
    bool meterReadoutAvailable() const;
    /// 按当前仿真状态刷新所有打开的万用表读数窗口
    void refreshMeterWindows();
    /// 关闭所有万用表读数窗口（新建 / 打开电路时调用）
    void closeMeterWindows();
    void setDirty(bool dirty);
    bool maybeSave();
    void updateTitle();
    void updateAnalysisLabel();
    void appendLog(const QString& text);
    void buildLibrary();

    // ---- 多文档标签页 ----
    /// 当前文档（永远非空：至少保留一个空白标签）
    Document* doc() const;
    /// 新建一个空白标签并切过去
    Document* addDocument(const QString& name = CSM_TR("Untitled"),
                          const QString& path = QString());
    /// 把 scene / view 的信号接到主窗口（每个文档接一次）
    void wireDocument(Document* document);
    /// 切到第 index 个标签：重绑缓存指针、刷新界面
    void switchToDocument(int index);
    /// 关闭第 index 个标签（脏的先问保存）
    bool closeDocument(int index);
    /// 关闭当前标签
    void closeCurrentDocument();
    /// 刷新某个标签的标题（名字 + 星号）
    void updateTabTitle(Document* document);
    /// 某个文档是否已经打开（按绝对路径判断）
    int indexOfPath(const QString& path) const;
    /// 把电路装进标签页：当前是「干净的空文档」就沿用它，否则新开一个
    Document* loadCircuitData(const Circuit& circuit, const QString& title, const QString& path,
                              const SimulationOptions& options);

    QTabWidget* m_tabs = nullptr;          // 中央标签页：一个标签一个文档
    QVector<Document*> m_documents;
    // 当前文档的缓存指针：切标签时重绑，界面操作照旧用这两个
    SchematicScene* m_scene = nullptr;
    SchematicView* m_view = nullptr;
    PropertyPanel* m_props = nullptr;
    QDockWidget* m_logDock = nullptr;
    QWidget* m_waveWindow = nullptr;   // 独立的「波形监控」窗口
    QTabWidget* m_waveTabs = nullptr;
    WaveformWindow* m_wavePlot = nullptr;   // 静态波形
    WaveformWindow* m_waveLive = nullptr;   // 动态示波器
    QHash<QString, MultimeterWindow*> m_meterWindows; // 万用表读数窗口（按参考编号）

    /// 实时仿真中测量探针的统计（键为元件 id）。
    /// 峰峰值 / 有效值用最近 kLiveProbeWindow 个采样算（滑动窗口），
    /// 直流用最后一个瞬时值 —— 累计平均会让「拨一下开关」看起来像慢慢漂移。
    struct LiveProbeStats {
        QVector<double> volts;      // 最近的电压采样
        QVector<double> amps;       // 最近的电流采样
        double lastVolts = 0.0;     // 瞬时值
        double lastAmps = 0.0;
        bool hasVoltage = false;
        bool hasCurrent = false;
    };
    QHash<int, LiveProbeStats> m_probeStats;
    /// 「只在仿真时能点开」这个提示只提示一次，免得每次选中都刷一条日志
    bool m_meterHintShown = false;
    QPlainTextEdit* m_log = nullptr;
    QListWidget* m_library = nullptr;
    QComboBox* m_libraryCategory = nullptr;
    QLabel* m_coordLabel = nullptr;
    QLabel* m_hintLabel = nullptr;
    QLabel* m_analysisLabel = nullptr;

    QStringList m_examplePaths;
    QStringList m_exampleTitles;
    bool m_loading = false;
    bool m_updateCheckScheduled = false;
    QTimer* m_liveRestartTimer = nullptr; // 电路改动后重建实时引擎（防抖）
    QTimer* m_blastTimer = nullptr;       // 爆炸动画驱动
    double m_blastProgress = 0.0;
    QSet<int> m_blownIds;
    /// 正在销毁：子控件在 ~QWidget() 阶段被析构时仍会发信号（例如 QLineEdit
    /// 失焦触发 editingFinished），那时本窗口的成员已经析构，回调必须直接返回。
    bool m_destroying = false;

    QAction* m_actNew = nullptr;
    QAction* m_actOpen = nullptr;
    QAction* m_actSave = nullptr;
    QAction* m_actSaveAs = nullptr;
    QAction* m_actCloseTab = nullptr;
    QAction* m_actNextTab = nullptr;
    QAction* m_actPrevTab = nullptr;
    QAction* m_actQuit = nullptr;

    QAction* m_actUndo = nullptr;
    QAction* m_actRedo = nullptr;
    QAction* m_actCut = nullptr;
    QAction* m_actCopy = nullptr;
    QAction* m_actPaste = nullptr;
    QAction* m_actDelete = nullptr;
    QAction* m_actRotate = nullptr;
    /// 「对齐与分布」子菜单里的动作：对齐要 ≥2 个元件，分布要 ≥3 个
    QVector<QAction*> m_alignActions;
    QVector<QAction*> m_distributeActions;
    QAction* m_actSelectAll = nullptr;
    QAction* m_actSnapWires = nullptr;

    QAction* m_actZoomIn = nullptr;
    QAction* m_actZoomOut = nullptr;
    QAction* m_actZoomFit = nullptr;
    QAction* m_actGrid = nullptr;

    QAction* m_actRun = nullptr;
    QAction* m_actMonitor = nullptr;   // 波形监控（静态波形 / 动态示波器 二合一）
    QAction* m_actCheckUpdates = nullptr;
    QAction* m_actGuide = nullptr;
    QAction* m_actAbout = nullptr;

    QAction* m_actSelectTool = nullptr;
    QAction* m_actWireTool = nullptr;
};

} // namespace csm
