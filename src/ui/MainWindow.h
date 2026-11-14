#pragma once

#include "core/UpdateChecker.h"
#include "sim/Simulator.h"

#include <QJsonObject>
#include <QMainWindow>

class QAction;
class QDockWidget;
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
    void openSettings();
    void scanExamples();
    void loadExample(int index);
    void checkForUpdates(bool manual);
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
    WaveformWindow* m_wave = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QListWidget* m_library = nullptr;
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
    QAction* m_actDelete = nullptr;
    QAction* m_actRotate = nullptr;
    QAction* m_actSelectAll = nullptr;

    QAction* m_actZoomIn = nullptr;
    QAction* m_actZoomOut = nullptr;
    QAction* m_actZoomFit = nullptr;
    QAction* m_actGrid = nullptr;

    QAction* m_actRun = nullptr;
    QAction* m_actSettings = nullptr;
    QAction* m_actScope = nullptr;
    QAction* m_actCheckUpdates = nullptr;
    QAction* m_actGuide = nullptr;
    QAction* m_actAbout = nullptr;

    QAction* m_actSelectTool = nullptr;
    QAction* m_actWireTool = nullptr;
};

} // namespace csm
