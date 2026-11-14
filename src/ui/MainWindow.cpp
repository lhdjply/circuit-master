#include "ui/MainWindow.h"

#include "core/Circuit.h"
#include "core/Netlist.h"
#include "core/Units.h"
#include "ui/ComponentItem.h"
#include "ui/PropertyPanel.h"
#include "ui/SchematicScene.h"
#include "ui/SchematicView.h"
#include "ui/SimulationSettingsDialog.h"
#include "ui/Symbols.h"
#include "ui/WaveformWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGuiApplication>
#include <QScreen>
#include <QSet>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QStyle>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace csm {

namespace {

AnalysisKind kindFromTag(const QString& tag)
{
    if (tag == QLatin1String("operating-point"))
        return AnalysisKind::OperatingPoint;
    if (tag == QLatin1String("ac"))
        return AnalysisKind::AC;
    return AnalysisKind::Transient;
}

QString kindSuffix(AnalysisKind kind)
{
    switch (kind) {
    case AnalysisKind::OperatingPoint: return QStringLiteral("（工作点）");
    case AnalysisKind::AC:             return QStringLiteral("（交流扫频）");
    case AnalysisKind::Transient:      return QStringLiteral("（瞬态）");
    }
    return QString();
}

SimulationOptions optionsFromJson(const QJsonObject& analysis)
{
    SimulationOptions options;
    options.kind = kindFromTag(analysis.value(QStringLiteral("kind")).toString());
    options.tStop = analysis.value(QStringLiteral("tStop")).toDouble(options.tStop);
    options.tStep = analysis.value(QStringLiteral("tStep")).toDouble(options.tStep);
    options.fStart = analysis.value(QStringLiteral("fStart")).toDouble(options.fStart);
    options.fStop = analysis.value(QStringLiteral("fStop")).toDouble(options.fStop);
    options.acPoints = analysis.value(QStringLiteral("acPoints")).toInt(options.acPoints);
    return options;
}

/// 只保留测试点波形：电压测试点取节点电压，电流测试点取支路电流。
/// 电路中没有测试点时清空波形，返回 false。
bool applyProbeSignals(const Circuit& circuit, const Netlist& net, SimulationResult& result)
{
    struct ProbeRef {
        QString signalName;
        QString displayName;
    };

    QVector<ProbeRef> probes;
    QSet<int> seenNodes;
    for (const Component& comp : circuit.components) {
        if (comp.type == ComponentType::Probe) {
            const int node = net.nodeOf(comp.id, 0);
            if (node <= 0 || seenNodes.contains(node))
                continue;
            seenNodes.insert(node);
            probes.append({QStringLiteral("V(%1)").arg(node),
                           QStringLiteral("%1 (V(%2))").arg(comp.refdes).arg(node)});
        } else if (comp.type == ComponentType::CurrentProbe) {
            probes.append({QStringLiteral("I(%1)").arg(comp.refdes),
                           QStringLiteral("%1 (I)").arg(comp.refdes)});
        }
    }
    if (probes.isEmpty()) {
        result.waves.clear();
        result.probeWaves.clear();
        return false;
    }
    if (result.waves.isEmpty())
        return false;

    QVector<Signal> ordered;
    for (const ProbeRef& probe : probes) {
        const Signal* wave = result.signal(probe.signalName);
        if (!wave)
            continue;
        Signal copy = *wave;
        copy.name = probe.displayName;
        ordered.append(copy);
    }
    if (ordered.isEmpty())
        return false;

    result.waves = ordered;
    result.probeWaves.clear();
    for (int i = 0; i < result.waves.size(); ++i)
        result.probeWaves.append(i);
    return true;
}

/// 优先使用内置 SVG 图标，缺失时回退到系统主题图标。
QIcon actionIcon(const QString& name, const QString& themeName, QStyle::StandardPixmap fallback)
{
    QIcon icon(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!icon.isNull())
        return icon;
    icon = QIcon::fromTheme(themeName);
    if (icon.isNull())
        icon = qApp->style()->standardIcon(fallback);
    return icon;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("电路仿真大师"));
    const QRect available = QGuiApplication::primaryScreen()
        ? QGuiApplication::primaryScreen()->availableGeometry()
        : QRect(0, 0, 1400, 880);
    resize(qMin(1400, available.width() - 80), qMin(880, available.height() - 80));
    QIcon appIcon(QStringLiteral(":/icons/icon.svg"));
    if (appIcon.isNull())
        appIcon = componentIcon(ComponentType::VoltageSource, QSize(64, 64));
    setWindowIcon(appIcon);

    m_scene = new SchematicScene(this);
    m_view = new SchematicView(m_scene, this);
    setCentralWidget(m_view);

    m_options.kind = AnalysisKind::Transient;
    m_options.tStop = 5e-3;
    m_options.tStep = 1e-5;
    m_options.fStart = 10.0;
    m_options.fStop = 1e6;
    m_options.acPoints = 160;

    createActions();
    scanExamples();
    createMenus();
    createToolBar();
    createDocks();
    createStatusBar();

    connect(m_scene, &SchematicScene::circuitChanged, this, [this] { setDirty(true); });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_scene, &SchematicScene::editRequested, this, &MainWindow::onEditRequested);
    connect(m_view, &SchematicView::scenePositionChanged, this, [this](const QPointF& p) {
        m_coordLabel->setText(
            QStringLiteral("x: %1   y: %2").arg(int(p.x())).arg(int(p.y())));
    });
    connect(m_view, &SchematicView::hintChanged, this,
            [this](const QString& hint) { m_hintLabel->setText(hint); });
    connect(m_view, &SchematicView::toolChanged, this, [this] {
        const bool wire = m_view->tool() == SchematicView::Tool::Wire;
        m_actWireTool->setChecked(wire);
        m_actSelectTool->setChecked(!wire);
    });
    connect(m_view, &SchematicView::componentPlaced, this, [this](ComponentItem* item) {
        m_props->setComponent(item);
        m_actSelectTool->setChecked(true);
    });

    appendLog(QStringLiteral("电路仿真大师 0.1.0  —  Qt6 / MNA 求解器"));
    appendLog(QStringLiteral("提示：左键选择拖动，双击编辑参数，R 键旋转；按 F5 运行仿真。"));
    appendLog(QStringLiteral("已新建空白电路：可从左侧元件库放置元件，"
                             "或通过「仿真 → 示例电路」载入示例。"));

    m_dirty = false;
    updateTitle();
}

void MainWindow::createActions()
{
    m_actNew = new QAction(
        actionIcon(QStringLiteral("new"), QStringLiteral("document-new"), QStyle::SP_FileIcon),
        QStringLiteral("新建(&N)"), this);
    m_actNew->setShortcut(QKeySequence::New);

    m_actOpen = new QAction(
        actionIcon(QStringLiteral("open"), QStringLiteral("document-open"),
                   QStyle::SP_DialogOpenButton),
        QStringLiteral("打开(&O)…"), this);
    m_actOpen->setShortcut(QKeySequence::Open);

    m_actSave = new QAction(
        actionIcon(QStringLiteral("save"), QStringLiteral("document-save"),
                   QStyle::SP_DialogSaveButton),
        QStringLiteral("保存(&S)"), this);
    m_actSave->setShortcut(QKeySequence::Save);

    m_actSaveAs = new QAction(QStringLiteral("另存为(&A)…"), this);
    m_actSaveAs->setShortcut(QKeySequence::SaveAs);

    m_actExport = new QAction(QStringLiteral("导出 SPICE 网表…"), this);
    m_actExport->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));

    m_actQuit = new QAction(QStringLiteral("退出(&Q)"), this);
    m_actQuit->setShortcut(QKeySequence::Quit);

    m_actDelete = new QAction(
        actionIcon(QStringLiteral("delete"), QStringLiteral("edit-delete"), QStyle::SP_TrashIcon),
        QStringLiteral("删除"), this);
    m_actDelete->setShortcut(QKeySequence::Delete);

    m_actRotate = new QAction(QStringLiteral("旋转 90°"), this);
    m_actRotate->setShortcut(QKeySequence(Qt::Key_R));
    m_actRotate->setShortcutContext(Qt::WindowShortcut);

    m_actSelectAll = new QAction(QStringLiteral("全选"), this);
    m_actSelectAll->setShortcut(QKeySequence::SelectAll);

    m_actZoomIn = new QAction(
        actionIcon(QStringLiteral("zoom_in"), QStringLiteral("zoom-in"), QStyle::SP_ArrowUp),
        QStringLiteral("放大"), this);
    m_actZoomIn->setShortcut(QKeySequence::ZoomIn);

    m_actZoomOut = new QAction(
        actionIcon(QStringLiteral("zoom_out"), QStringLiteral("zoom-out"), QStyle::SP_ArrowDown),
        QStringLiteral("缩小"), this);
    m_actZoomOut->setShortcut(QKeySequence::ZoomOut);

    m_actZoomFit = new QAction(
        actionIcon(QStringLiteral("zoom_fit"), QStringLiteral("zoom-fit-best"),
                   QStyle::SP_FileDialogContentsView),
        QStringLiteral("适应窗口"), this);
    m_actZoomFit->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));

    m_actGrid = new QAction(
        actionIcon(QStringLiteral("grid"), QStringLiteral("view-grid"), QStyle::SP_FileDialogListView),
        QStringLiteral("显示网格"), this);
    m_actGrid->setCheckable(true);
    m_actGrid->setChecked(true);

    m_actRun = new QAction(
        actionIcon(QStringLiteral("run"), QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay),
        QStringLiteral("运行仿真"), this);
    m_actRun->setShortcut(QKeySequence(Qt::Key_F5));

    m_actSettings = new QAction(
        actionIcon(QStringLiteral("settings"), QStringLiteral("preferences-system"),
                   QStyle::SP_FileDialogDetailedView),
        QStringLiteral("分析设置…"), this);

    m_actSelectTool = new QAction(
        actionIcon(QStringLiteral("select"), QStringLiteral("edit-select"), QStyle::SP_ArrowUp),
        QStringLiteral("选择"), this);
    m_actSelectTool->setCheckable(true);
    m_actSelectTool->setChecked(true);
    m_actSelectTool->setShortcut(QKeySequence(Qt::Key_V));
    m_actSelectTool->setToolTip(QStringLiteral("选择/移动元件 (V)"));

    m_actWireTool = new QAction(
        actionIcon(QStringLiteral("wire"), QStringLiteral("draw-line"), QStyle::SP_ArrowRight),
        QStringLiteral("连线"), this);
    m_actWireTool->setCheckable(true);
    m_actWireTool->setShortcut(QKeySequence(Qt::Key_W));
    m_actWireTool->setToolTip(QStringLiteral("绘制导线 (W)"));

    auto* toolGroup = new QActionGroup(this);
    toolGroup->addAction(m_actSelectTool);
    toolGroup->addAction(m_actWireTool);

    m_actGuide = new QAction(QStringLiteral("快速上手"), this);
    m_actGuide->setShortcut(QKeySequence::HelpContents);

    m_actAbout = new QAction(QStringLiteral("关于电路仿真大师"), this);

    connect(m_actNew, &QAction::triggered, this, &MainWindow::newCircuit);
    connect(m_actOpen, &QAction::triggered, this, &MainWindow::openCircuit);
    connect(m_actSave, &QAction::triggered, this, [this] { saveCircuit(); });
    connect(m_actSaveAs, &QAction::triggered, this, [this] { saveCircuitAs(); });
    connect(m_actExport, &QAction::triggered, this, &MainWindow::exportNetlist);
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);

    connect(m_actDelete, &QAction::triggered, this, [this] { m_view->deleteSelection(); });
    connect(m_actRotate, &QAction::triggered, this, [this] { m_view->rotateSelection(); });
    connect(m_actSelectAll, &QAction::triggered, this, [this] {
        for (QGraphicsItem* item : m_scene->items())
            item->setSelected(true);
    });

    connect(m_actZoomIn, &QAction::triggered, this, [this] { m_view->zoomBy(1.2); });
    connect(m_actZoomOut, &QAction::triggered, this, [this] { m_view->zoomBy(1.0 / 1.2); });
    connect(m_actZoomFit, &QAction::triggered, this, [this] { m_view->zoomFit(); });
    connect(m_actGrid, &QAction::toggled, this, [this](bool on) { m_scene->setShowGrid(on); });

    connect(m_actRun, &QAction::triggered, this, &MainWindow::runSimulation);
    connect(m_actSettings, &QAction::triggered, this, &MainWindow::openSettings);

    connect(m_actSelectTool, &QAction::triggered, this,
            [this] { m_view->setTool(SchematicView::Tool::Select); });
    connect(m_actWireTool, &QAction::triggered, this,
            [this] { m_view->setTool(SchematicView::Tool::Wire); });

    connect(m_actGuide, &QAction::triggered, this, &MainWindow::showQuickGuide);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::showAbout);

    m_actDelete->setEnabled(false);
    m_actRotate->setEnabled(false);

    // 这些快捷键只在画布获得焦点时生效，避免与属性面板输入冲突
    for (QAction* action : {m_actDelete, m_actRotate, m_actSelectAll, m_actSelectTool,
                            m_actWireTool}) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m_view->addAction(action);
    }
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    fileMenu->addAction(m_actNew);
    fileMenu->addAction(m_actOpen);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actSave);
    fileMenu->addAction(m_actSaveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actExport);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actQuit);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    editMenu->addAction(m_actDelete);
    editMenu->addAction(m_actRotate);
    editMenu->addSeparator();
    editMenu->addAction(m_actSelectAll);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    viewMenu->addAction(m_actZoomIn);
    viewMenu->addAction(m_actZoomOut);
    viewMenu->addAction(m_actZoomFit);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actGrid);

    QMenu* simMenu = menuBar()->addMenu(QStringLiteral("仿真(&S)"));
    simMenu->addAction(m_actRun);
    simMenu->addAction(m_actSettings);
    simMenu->addSeparator();

    QMenu* examples = simMenu->addMenu(QStringLiteral("示例电路"));
    for (int i = 0; i < m_exampleTitles.size(); ++i) {
        QAction* action = examples->addAction(m_exampleTitles.at(i));
        connect(action, &QAction::triggered, this, [this, i] { loadExample(i); });
    }

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    helpMenu->addAction(m_actGuide);
    helpMenu->addAction(m_actAbout);
}

void MainWindow::createToolBar()
{
    QToolBar* bar = addToolBar(QStringLiteral("主工具栏"));
    bar->setMovable(false);
    bar->setIconSize(QSize(22, 22));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    bar->addAction(m_actNew);
    bar->addAction(m_actOpen);
    bar->addAction(m_actSave);
    bar->addSeparator();
    bar->addAction(m_actSelectTool);
    bar->addAction(m_actWireTool);
    bar->addSeparator();
    bar->addAction(m_actRun);
    bar->addAction(m_actSettings);
    bar->addSeparator();
    bar->addAction(m_actZoomFit);
}

void MainWindow::buildLibrary()
{
    struct Entry {
        ComponentType type;
        QString text;
    };
    const QVector<Entry> entries = {
        {ComponentType::Resistor, QStringLiteral("电阻  R")},
        {ComponentType::Capacitor, QStringLiteral("电容  C")},
        {ComponentType::Inductor, QStringLiteral("电感  L")},
        {ComponentType::Diode, QStringLiteral("二极管  D")},
        {ComponentType::OpAmp, QStringLiteral("运算放大器  U")},
        {ComponentType::Switch, QStringLiteral("开关  S")},
        {ComponentType::VoltageSource, QStringLiteral("电压源  V")},
        {ComponentType::CurrentSource, QStringLiteral("电流源  I")},
        {ComponentType::Ground, QStringLiteral("接地  GND")},
        {ComponentType::Probe, QStringLiteral("电压测试点  TP")},
        {ComponentType::CurrentProbe, QStringLiteral("电流测试点  IP")},
    };

    for (const Entry& entry : entries) {
        auto* item = new QListWidgetItem(componentIcon(entry.type), entry.text, m_library);
        item->setData(Qt::UserRole, int(entry.type));
        item->setSizeHint(QSize(0, 46));
    }
}

void MainWindow::createDocks()
{
    auto* libraryDock = new QDockWidget(QStringLiteral("元件库"), this);
    libraryDock->setObjectName(QStringLiteral("libraryDock"));
    libraryDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* libraryContainer = new QWidget(libraryDock);
    auto* libraryLayout = new QVBoxLayout(libraryContainer);
    libraryLayout->setContentsMargins(6, 6, 6, 6);
    libraryLayout->setSpacing(6);

    auto* hint = new QLabel(QStringLiteral("点击元件，再在画布上单击放置"), libraryContainer);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#64748b; font-size:11px;"));
    libraryLayout->addWidget(hint);

    m_library = new QListWidget(libraryContainer);
    m_library->setIconSize(QSize(46, 34));
    m_library->setSpacing(2);
    m_library->setUniformItemSizes(true);
    libraryLayout->addWidget(m_library, 1);
    libraryDock->setWidget(libraryContainer);
    addDockWidget(Qt::LeftDockWidgetArea, libraryDock);
    buildLibrary();

    connect(m_library, &QListWidget::itemClicked, this, &MainWindow::onLibraryClicked);
    connect(m_library, &QListWidget::itemActivated, this, &MainWindow::onLibraryClicked);

    auto* propsDock = new QDockWidget(QStringLiteral("属性"), this);
    propsDock->setObjectName(QStringLiteral("propsDock"));
    m_props = new PropertyPanel(propsDock);
    auto* propsScroll = new QScrollArea(propsDock);
    propsScroll->setWidgetResizable(true);
    propsScroll->setFrameShape(QFrame::NoFrame);
    propsScroll->setWidget(m_props);
    propsDock->setWidget(propsScroll);
    propsDock->setMinimumWidth(290);
    addDockWidget(Qt::RightDockWidgetArea, propsDock);
    connect(m_props, &PropertyPanel::changed, this, [this] { setDirty(true); });

    auto* logDock = new QDockWidget(QStringLiteral("消息"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    m_log = new QPlainTextEdit(logDock);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setStyleSheet(
        QStringLiteral("background:#f8fafc; color:#334155; font-family:monospace;"));
    logDock->setWidget(m_log);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    logDock->setMaximumHeight(170);
}

void MainWindow::createStatusBar()
{
    m_hintLabel = new QLabel(
        QStringLiteral("选择模式：拖动元件移动，双击元件编辑参数，R 键旋转"), this);
    m_coordLabel = new QLabel(QStringLiteral("x: 0   y: 0"), this);
    m_coordLabel->setMinimumWidth(120);
    m_analysisLabel = new QLabel(this);
    m_analysisLabel->setStyleSheet(QStringLiteral("color:#475569;"));

    statusBar()->addWidget(m_hintLabel, 1);
    statusBar()->addPermanentWidget(m_analysisLabel);
    statusBar()->addPermanentWidget(m_coordLabel);
    updateAnalysisLabel();
}

void MainWindow::updateAnalysisLabel()
{
    QString text;
    switch (m_options.kind) {
    case AnalysisKind::OperatingPoint:
        text = QStringLiteral("分析：直流工作点");
        break;
    case AnalysisKind::Transient:
        text = QStringLiteral("分析：瞬态 %1 / 步长 %2")
                   .arg(formatEng(m_options.tStop, QStringLiteral("s"), 3),
                        formatEng(m_options.tStep, QStringLiteral("s"), 3));
        break;
    case AnalysisKind::AC:
        text = QStringLiteral("分析：交流 %1 ~ %2")
                   .arg(formatEng(m_options.fStart, QStringLiteral("Hz"), 3),
                        formatEng(m_options.fStop, QStringLiteral("Hz"), 3));
        break;
    }
    m_analysisLabel->setText(text);
}

void MainWindow::onLibraryClicked(QListWidgetItem* item)
{
    if (!item)
        return;
    const auto type = ComponentType(item->data(Qt::UserRole).toInt());
    m_view->setPendingType(type);
    m_view->setFocus();
}

void MainWindow::onSelectionChanged()
{
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    ComponentItem* item = nullptr;
    for (QGraphicsItem* g : selected) {
        if (g->type() == ComponentItem::Type) {
            item = static_cast<ComponentItem*>(g);
            break;
        }
    }
    m_props->setComponent(item);
    m_actDelete->setEnabled(!selected.isEmpty());
    m_actRotate->setEnabled(item != nullptr && supportsRotation(item->component().type));
}

void MainWindow::onEditRequested(ComponentItem* item)
{
    m_props->setComponent(item);
    m_props->setFocus();
}

void MainWindow::setDirty(bool dirty)
{
    if (m_loading)
        return;
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    updateTitle();
}

void MainWindow::updateTitle()
{
    setWindowTitle(QStringLiteral("%1%2 — 电路仿真大师")
                       .arg(m_documentName, m_dirty ? QStringLiteral(" *") : QString()));
}

void MainWindow::appendLog(const QString& text)
{
    if (m_log)
        m_log->appendPlainText(text);
}

void MainWindow::loadCircuitData(const Circuit& circuit, const QString& title, const QString& path)
{
    m_loading = true;
    m_scene->load(circuit);
    m_loading = false;

    m_documentName = title;
    m_filePath = path;
    m_props->setComponent(nullptr);
    m_dirty = false;
    updateTitle();

    QTimer::singleShot(0, this, [this] { m_view->zoomFit(); });
}

void MainWindow::newCircuit()
{
    if (!maybeSave())
        return;

    m_loading = true;
    m_scene->clearAll();
    m_loading = false;

    m_documentName = QStringLiteral("未命名");
    m_filePath.clear();
    m_props->setComponent(nullptr);
    m_dirty = false;
    updateTitle();
    appendLog(QStringLiteral("已新建空白电路。"));
}

bool MainWindow::saveCircuit()
{
    if (m_filePath.isEmpty())
        return saveCircuitAs();

    const Circuit circuit = m_scene->toCircuit();
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), file.errorString());
        return false;
    }
    const QJsonDocument doc(circuit.toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    m_dirty = false;
    updateTitle();
    appendLog(QStringLiteral("已保存：%1").arg(m_filePath));
    return true;
}

bool MainWindow::saveCircuitAs()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存电路"), m_documentName + QStringLiteral(".csm"),
        QStringLiteral("电路仿真大师电路 (*.csm);;所有文件 (*)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QLatin1String(".csm"), Qt::CaseInsensitive))
        path += QStringLiteral(".csm");

    m_filePath = path;
    m_documentName = QFileInfo(path).fileName();
    return saveCircuit();
}

void MainWindow::openCircuit()
{
    if (!maybeSave())
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开电路"), QString(),
        QStringLiteral("电路仿真大师电路 (*.csm);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), file.errorString());
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull()) {
        QMessageBox::warning(this, QStringLiteral("打开失败"),
                             QStringLiteral("JSON 解析错误：%1").arg(parseError.errorString()));
        return;
    }

    QString error;
    const Circuit circuit = Circuit::fromJson(doc.object(), &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), error);
        return;
    }
    if (circuit.components.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("打开失败"),
                             QStringLiteral("文件中没有元件。"));
        return;
    }

    loadCircuitData(circuit, QFileInfo(path).fileName(), path);
    appendLog(QStringLiteral("已打开：%1（%2 个元件）")
                  .arg(path)
                  .arg(circuit.components.size()));
}

void MainWindow::exportNetlist()
{
    const Circuit circuit = m_scene->toCircuit();
    const Netlist net = circuit.buildNetlist();
    if (!net.valid) {
        QMessageBox::warning(this, QStringLiteral("无法导出"), net.error);
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 SPICE 网表"), m_documentName + QStringLiteral(".cir"),
        QStringLiteral("SPICE 网表 (*.cir);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".cir"), Qt::CaseInsensitive))
        path += QStringLiteral(".cir");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }
    QTextStream out(&file);
    out << circuit.toSpiceNetlist();
    file.close();
    appendLog(QStringLiteral("已导出网表：%1").arg(path));
}

void MainWindow::runSimulation()
{
    const Circuit circuit = m_scene->toCircuit();
    if (circuit.components.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("无法仿真"),
                                 QStringLiteral("请先放置元件并连接电路。"));
        return;
    }

    const Netlist net = circuit.buildNetlist();
    if (!net.valid) {
        QMessageBox::warning(this, QStringLiteral("电路检查未通过"), net.error);
        return;
    }
    for (const QString& warning : net.warnings)
        appendLog(QStringLiteral("⚠ ") + warning);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    SimulationResult result = Simulator::run(circuit, m_options);
    QApplication::restoreOverrideCursor();

    appendLog(QStringLiteral("──────── 仿真 ────────"));
    for (const QString& line : result.log)
        appendLog(line);

    if (!result.ok) {
        appendLog(QStringLiteral("✘ 仿真失败：") + result.error);
        QMessageBox::critical(this, QStringLiteral("仿真失败"), result.error);
        return;
    }

    appendLog(QStringLiteral("✔ 完成，用时 %1 ms").arg(QString::number(result.elapsedMs, 'f', 1)));

    const bool hasProbes = applyProbeSignals(circuit, net, result);
    if (!hasProbes && result.kind != AnalysisKind::OperatingPoint)
        appendLog(QStringLiteral("⚠ 电路中没有测试点，波形为空。请放置电压测试点（TP）或电流测试点（IP）。"));

    if (result.kind == AnalysisKind::OperatingPoint) {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("直流工作点"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* text = new QPlainTextEdit(&dialog);
        text->setReadOnly(true);
        text->setPlainText(result.report);
        text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(text);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        layout->addWidget(box);
        dialog.resize(620, 640);
        dialog.exec();
        return;
    }

    if (!m_wave)
        m_wave = new WaveformWindow(this);
    m_wave->setResult(result);
    m_wave->show();
    m_wave->raise();
    m_wave->activateWindow();
}

void MainWindow::openSettings()
{
    SimulationSettingsDialog dialog(m_options, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_options = dialog.options();
    updateAnalysisLabel();
    appendLog(QStringLiteral("分析设置已更新：%1").arg(m_analysisLabel->text()));
}

void MainWindow::scanExamples()
{
    m_examplePaths.clear();
    m_exampleTitles.clear();

    const QDir dir(QStringLiteral(":/examples"));
    const QStringList files = dir.entryList({QStringLiteral("*.csm")}, QDir::Files, QDir::Name);
    for (const QString& file : files) {
        QFile input(dir.filePath(file));
        if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QJsonObject root = QJsonDocument::fromJson(input.readAll()).object();
        input.close();
        if (root.isEmpty())
            continue;

        const QString name = root.value(QStringLiteral("name")).toString(file);
        const AnalysisKind kind =
            kindFromTag(root.value(QStringLiteral("analysis")).toObject()
                            .value(QStringLiteral("kind"))
                            .toString());
        m_examplePaths.append(dir.filePath(file));
        m_exampleTitles.append(name + kindSuffix(kind));
    }
}

void MainWindow::loadExample(int index)
{
    if (index < 0 || index >= m_examplePaths.size())
        return;

    QFile file(m_examplePaths.at(index));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("无法打开示例"), file.errorString());
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull()) {
        QMessageBox::warning(this, QStringLiteral("示例损坏"),
                             QStringLiteral("JSON 解析错误：%1").arg(parseError.errorString()));
        return;
    }

    QString circuitError;
    const QJsonObject root = doc.object();
    const Circuit circuit = Circuit::fromJson(root, &circuitError);
    if (!circuitError.isEmpty() || circuit.components.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("示例损坏"),
                             circuitError.isEmpty() ? QStringLiteral("示例中没有元件。")
                                                    : circuitError);
        return;
    }

    if (m_dirty && !maybeSave())
        return;

    QString name = root.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = QFileInfo(m_examplePaths.at(index)).baseName();

    m_options = optionsFromJson(root.value(QStringLiteral("analysis")).toObject());
    updateAnalysisLabel();
    loadCircuitData(circuit, name, QString());
    appendLog(QStringLiteral("已载入示例「%1」。按 F5 运行仿真。").arg(name));
}

void MainWindow::showQuickGuide()
{
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("快速上手"));
    box.setIcon(QMessageBox::Information);
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral(
        "<b>三步完成一次仿真</b>"
        "<ol>"
        "<li>在左侧<b>元件库</b>中点击元件，再在画布上单击放置；按 <b>R</b> 旋转。</li>"
        "<li>点击工具栏<b>连线</b>（W），在引脚处单击并拖出折线，双击结束。<br>"
        "导线端点吸附到引脚后会自动出现连接点。</li>"
        "<li>双击元件修改数值（支持 10k / 4.7u / 1meg），按 <b>F5</b> 运行仿真，"
        "波形会显示在查看器中。</li>"
        "</ol>"
        "<b>注意</b>：电路必须包含接地符号（GND），否则无法确定参考电位。<br><br>"
        "<span style='color:#64748b'>提示：菜单「仿真 → 示例电路」内置 6 个可直接运行的例子。</span>"));
    box.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, QStringLiteral("关于电路仿真大师"),
        QStringLiteral(
            "<h3>电路仿真大师 0.1.0</h3>"
            "<p>基于 Qt6 / C++17 / CMake 的跨平台电路原理图与仿真软件。</p>"
            "<p>求解器：改进节点法（MNA）+ 牛顿-拉夫逊迭代；<br>"
            "支持直流工作点、后向欧拉瞬态分析与交流小信号扫频。</p>"
            "<p>元件：电阻、电容、电感、二极管、开关、电压源、电流源、接地。</p>"
            "<p style='color:#64748b'>本程序为自由软件，可作为 Multisim 在 Linux 上的替代方案。</p>"));
}

bool MainWindow::maybeSave()
{
    if (!m_dirty)
        return true;

    const QMessageBox::StandardButton button = QMessageBox::question(
        this, QStringLiteral("保存更改"),
        QStringLiteral("「%1」有未保存的更改，是否保存？").arg(m_documentName),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

    if (button == QMessageBox::Save)
        return saveCircuit();
    if (button == QMessageBox::Cancel)
        return false;
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

} // namespace csm
