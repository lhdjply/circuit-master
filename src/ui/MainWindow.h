#pragma once

#include "core/UpdateChecker.h"
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

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
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

    void newCircuit();
    void openCircuit();
    bool saveCircuit();
    bool saveCircuitAs();
    void exportNetlist();
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
    /// 按仿真结果点亮/熄灭电路里的发光二极管
    void applyLedStates(const SimulationResult& result);
    /// 熄灭所有 LED（停止仿真或换电路时调用）
    void clearLedStates();
    /// 极性电容反接击穿：标记元件并播放爆炸动画
    void applyBlownCapacitors(const QHash<int, double>& blown);
    void clearBlast();
    void showQuickGuide();
    void showAbout();

    void pushHistory();
    void undo();
    void redo();
    void resetHistory();
    void restoreSnapshot(const QJsonObject& snapshot);
    void updateHistoryActions();

    void onSelectionChanged();
    void onEditRequested(ComponentItem* item);
    void setDirty(bool dirty);
    bool maybeSave();
    void updateTitle();
    void updateAnalysisLabel();
    void appendLog(const QString& text);
    void buildLibrary();
    void loadCircuitData(const Circuit& circuit, const QString& title, const QString& path);

    SchematicScene* m_scene = nullptr;
    SchematicView* m_view = nullptr;
    PropertyPanel* m_props = nullptr;
    QDockWidget* m_logDock = nullptr;
    QWidget* m_waveWindow = nullptr;   // 独立的「波形监控」窗口
    QTabWidget* m_waveTabs = nullptr;
    WaveformWindow* m_wavePlot = nullptr;   // 静态波形
    WaveformWindow* m_waveLive = nullptr;   // 动态示波器
    QPlainTextEdit* m_log = nullptr;
    QListWidget* m_library = nullptr;
    QComboBox* m_libraryCategory = nullptr;
    QLabel* m_coordLabel = nullptr;
    QLabel* m_hintLabel = nullptr;
    QLabel* m_analysisLabel = nullptr;

    SimulationOptions m_options;
    QStringList m_examplePaths;
    QStringList m_exampleTitles;
    QString m_documentName = QStringLiteral("未命名");
    QString m_filePath;
    bool m_dirty = false;
    bool m_loading = false;

    QVector<QJsonObject> m_history;
    QVector<QByteArray> m_historySignatures;
    int m_historyIndex = -1;
    bool m_restoring = false;
    bool m_updateCheckScheduled = false;
    SimulationResult m_lastResult;   // 最近一次仿真的结果，供「静态波形」使用
    bool m_hasResult = false;
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
    QAction* m_actExport = nullptr;
    QAction* m_actQuit = nullptr;

    QAction* m_actUndo = nullptr;
    QAction* m_actRedo = nullptr;
    QAction* m_actCut = nullptr;
    QAction* m_actCopy = nullptr;
    QAction* m_actPaste = nullptr;
    QAction* m_actDelete = nullptr;
    QAction* m_actRotate = nullptr;
    QAction* m_actSelectAll = nullptr;

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
