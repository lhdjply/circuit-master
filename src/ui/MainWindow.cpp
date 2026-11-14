#include "ui/MainWindow.h"

#include "csm_version.h"


#include "core/Circuit.h"
#include "core/Netlist.h"
#include "core/Units.h"
#include "ui/ComponentItem.h"
#include "ui/PropertyPanel.h"
#include "ui/SchematicScene.h"
#include "ui/SchematicView.h"
#include "ui/Symbols.h"
#include "ui/UpdateDialog.h"
#include "ui/WaveformWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
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
#include <QDesktopServices>
#include <QScreen>
#include <QSet>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QDrag>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
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

/// 电路内容签名：元件按 id 排序、导线按顶点排序，保证与图元遍历顺序无关。
QByteArray circuitSignature(const Circuit& circuit)
{
    QStringList parts;
    for (const Component& comp : circuit.components) {
        parts << QStringLiteral("C|%1|%2|%3|%4|%5|%6|%7|%8")
                     .arg(comp.id)
                     .arg(int(comp.type))
                     .arg(comp.pos.x())
                     .arg(comp.pos.y())
                     .arg(comp.rotation)
                     .arg(comp.refdes)
                     .arg(QString::number(comp.value, 'g', 12))
                     .arg(int(comp.mirrorX) | (int(comp.mirrorY) << 1));
    }
    QStringList wires;
    for (const Wire& wire : circuit.wires) {
        QStringList points;
        for (const QPointF& p : wire.points)
            points << QStringLiteral("%1,%2").arg(p.x()).arg(p.y());
        wires << points.join(QLatin1Char(';'));
    }
    wires.sort();
    parts << wires;
    return parts.join(QLatin1Char('#')).toUtf8();
}

constexpr char kComponentMimeType[] = "application/x-csm-component";

class ComponentLibrary : public QListWidget {
public:
    explicit ComponentLibrary(QWidget* parent = nullptr)
        : QListWidget(parent)
    {
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
        setDefaultDropAction(Qt::CopyAction);
    }

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override
    {
        if (items.isEmpty())
            return nullptr;
        auto* mime = new QMimeData;
        mime->setData(QLatin1String(kComponentMimeType),
                      QByteArray::number(items.first()->data(Qt::UserRole).toInt()));
        return mime;
    }

    /// 只把元件图标作为拖影，避免把列表项的选中背景一起拖出来。
    void startDrag(Qt::DropActions supportedActions) override
    {
        Q_UNUSED(supportedActions);

        QListWidgetItem* item = currentItem();
        if (!item)
            return;

        auto* mime = new QMimeData;
        mime->setData(QLatin1String(kComponentMimeType),
                      QByteArray::number(item->data(Qt::UserRole).toInt()));

        const qreal ratio = devicePixelRatioF();
        const QSize logical(72, 48);
        QPixmap pixmap(QSize(int(logical.width() * ratio), int(logical.height() * ratio)));
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        item->icon().paint(&painter, QRect(QPoint(0, 0), logical), Qt::AlignCenter);
        painter.end();

        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(logical.width() / 2, logical.height() / 2));
        drag->exec(Qt::CopyAction);

        clearSelection();
        setCurrentItem(nullptr);
    }
};


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

QJsonObject analysisToJson(const SimulationOptions& options)
{
    QJsonObject object;
    switch (options.kind) {
    case AnalysisKind::OperatingPoint:
        object.insert(QStringLiteral("kind"), QStringLiteral("operating-point"));
        break;
    case AnalysisKind::Transient:
        object.insert(QStringLiteral("kind"), QStringLiteral("transient"));
        break;
    case AnalysisKind::AC:
        object.insert(QStringLiteral("kind"), QStringLiteral("ac"));
        break;
    }
    object.insert(QStringLiteral("tStop"), options.tStop);
    object.insert(QStringLiteral("tStep"), options.tStep);
    object.insert(QStringLiteral("fStart"), options.fStart);
    object.insert(QStringLiteral("fStop"), options.fStop);
    object.insert(QStringLiteral("acPoints"), options.acPoints);
    object.insert(QStringLiteral("scopeTimeDiv"), options.scopeTimeDiv);
    object.insert(QStringLiteral("scopeVoltDiv"), options.scopeVoltDiv);
    object.insert(QStringLiteral("scopeAmpDiv"), options.scopeAmpDiv);
    return object;
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
    options.scopeTimeDiv = analysis.value(QStringLiteral("scopeTimeDiv")).toDouble(options.scopeTimeDiv);
    options.scopeVoltDiv = analysis.value(QStringLiteral("scopeVoltDiv")).toDouble(options.scopeVoltDiv);
    options.scopeAmpDiv = analysis.value(QStringLiteral("scopeAmpDiv")).toDouble(options.scopeAmpDiv);
    return options;
}

/// 收集测试点与对应信号名（显示名, 信号名）。
QVector<QPair<QString, QString>> probeSpecs(const Circuit& circuit, const Netlist& net)
{
    QVector<QPair<QString, QString>> probes;
    QSet<int> seenNodes;
    for (const Component& comp : circuit.components) {
        if (comp.type == ComponentType::Probe) {
            const int node = net.nodeOf(comp.id, 0);
            if (node <= 0 || seenNodes.contains(node))
                continue;
            seenNodes.insert(node);
            probes.append({QStringLiteral("%1 (V(%2))").arg(comp.refdes).arg(node),
                           QStringLiteral("V(%1)").arg(node)});
        } else if (comp.type == ComponentType::CurrentProbe) {
            probes.append({QStringLiteral("%1 (I)").arg(comp.refdes),
                           QStringLiteral("I(%1)").arg(comp.refdes)});
        }
    }
    return probes;
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
    QIcon icon = svgIcon(QStringLiteral(":/icons/%1.svg").arg(name));
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
    QIcon appIcon = svgIcon(QStringLiteral(":/icons/icon.svg"));
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
    createMonitorWindow();
    createStatusBar();

    connect(m_scene, &SchematicScene::circuitChanged, this, [this] { setDirty(true); });
    connect(m_scene, &SchematicScene::editCommitted, this, &MainWindow::pushHistory);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_scene, &SchematicScene::editRequested, this, &MainWindow::onEditRequested);
    connect(m_view, &SchematicView::componentEditRequested, this, &MainWindow::onEditRequested);
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

    appendLog(QStringLiteral("电路仿真大师 %1  —  Qt6 / MNA 求解器")
                  .arg(QStringLiteral(CSM_VERSION_STRING)));
    appendLog(QStringLiteral("提示：左键选择拖动，双击编辑参数，空格键旋转；按 F5 运行仿真。"));
    appendLog(QStringLiteral("已新建空白电路：可从左侧元件库放置元件，"
                             "或通过「仿真 → 示例电路」载入示例。"));

    m_dirty = false;
    m_hasResult = false;
    if (m_actMonitor) {
        m_actMonitor->setEnabled(false);
        m_actMonitor->setChecked(false);
    }
    if (m_waveLive)
        m_waveLive->stopLiveScope();
    updateTitle();
    resetHistory();

    // 启动后自动检查一次更新：先让窗口画出来再查，只有真发现新版本才弹窗。
    // 测试里用 CSM_NO_UPDATE_CHECK=1 关掉，避免用例依赖网络。
    if (!qEnvironmentVariableIsSet("CSM_NO_UPDATE_CHECK")) {
        QTimer::singleShot(3000, this, [this] { checkForUpdates(false); });
    }
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

    m_actUndo = new QAction(
        actionIcon(QStringLiteral("undo"), QStringLiteral("edit-undo"), QStyle::SP_ArrowBack),
        QStringLiteral("撤销"), this);
    m_actUndo->setShortcut(QKeySequence::Undo);
    m_actUndo->setEnabled(false);

    m_actRedo = new QAction(
        actionIcon(QStringLiteral("redo"), QStringLiteral("edit-redo"), QStyle::SP_ArrowForward),
        QStringLiteral("重做"), this);
    m_actRedo->setShortcut(QKeySequence::Redo);
    m_actRedo->setEnabled(false);

    m_actDelete = new QAction(
        actionIcon(QStringLiteral("delete"), QStringLiteral("edit-delete"), QStyle::SP_TrashIcon),
        QStringLiteral("删除"), this);
    m_actDelete->setShortcut(QKeySequence::Delete);

    m_actRotate = new QAction(QStringLiteral("旋转 90°"), this);
    // 空格旋转：和 KiCad 的习惯一致，选中元件后连按空格就能连续转
    m_actRotate->setShortcut(QKeySequence(Qt::Key_Space));
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
    m_actRun->setObjectName(QStringLiteral("actRun"));

    m_actMonitor = new QAction(
        actionIcon(QStringLiteral("monitor"), QStringLiteral("utilities-system-monitor"),
                   QStyle::SP_ComputerIcon),
        QStringLiteral("波形监控"), this);
    m_actMonitor->setObjectName(QStringLiteral("actMonitor"));
    m_actMonitor->setCheckable(true);
    m_actMonitor->setEnabled(false); // 只有仿真运行时才允许打开
    m_actMonitor->setToolTip(
        QStringLiteral("波形监控面板：静态波形 / 动态示波器两个标签页（仿真运行时可用）"));

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

    m_actCheckUpdates = new QAction(QStringLiteral("检查更新…"), this);

    m_actGuide = new QAction(QStringLiteral("快速上手"), this);
    m_actGuide->setShortcut(QKeySequence::HelpContents);

    m_actAbout = new QAction(QStringLiteral("关于电路仿真大师"), this);

    connect(m_actNew, &QAction::triggered, this, &MainWindow::newCircuit);
    connect(m_actOpen, &QAction::triggered, this, &MainWindow::openCircuit);
    connect(m_actSave, &QAction::triggered, this, [this] { saveCircuit(); });
    connect(m_actSaveAs, &QAction::triggered, this, [this] { saveCircuitAs(); });
    connect(m_actExport, &QAction::triggered, this, &MainWindow::exportNetlist);
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);

    connect(m_actUndo, &QAction::triggered, this, &MainWindow::undo);
    connect(m_actRedo, &QAction::triggered, this, &MainWindow::redo);
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

    connect(m_actRun, &QAction::triggered, this, [this] {
        // 动态示波器在跑时，这个按钮就是「停止」
        if (m_waveLive && m_waveLive->isLive()) {
            m_waveLive->stopLiveScope();
            return;
        }
        runSimulation();
    });
    connect(m_actMonitor, &QAction::toggled, this, [this](bool on) {
        if (!m_waveWindow)
            return;
        if (on)
            showMonitor(m_waveTabs ? m_waveTabs->currentIndex() : 0);
        else
            m_waveWindow->hide();
    });


    connect(m_actSelectTool, &QAction::triggered, this,
            [this] { m_view->setTool(SchematicView::Tool::Select); });
    connect(m_actWireTool, &QAction::triggered, this,
            [this] { m_view->setTool(SchematicView::Tool::Wire); });

    connect(m_actCheckUpdates, &QAction::triggered, this, [this] { checkForUpdates(true); });
    connect(m_actGuide, &QAction::triggered, this, &MainWindow::showQuickGuide);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::showAbout);

    m_actDelete->setEnabled(false);
    m_actRotate->setEnabled(false);

    // 这些快捷键只在画布获得焦点时生效，避免与属性面板输入冲突
    for (QAction* action : {m_actUndo, m_actRedo, m_actDelete, m_actRotate, m_actSelectAll,
                            m_actSelectTool, m_actWireTool}) {
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
    editMenu->addAction(m_actUndo);
    editMenu->addAction(m_actRedo);
    editMenu->addSeparator();
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
    simMenu->addAction(m_actMonitor);
    simMenu->addSeparator();

    QMenu* examples = simMenu->addMenu(QStringLiteral("示例电路"));
    for (int i = 0; i < m_exampleTitles.size(); ++i) {
        QAction* action = examples->addAction(m_exampleTitles.at(i));
        connect(action, &QAction::triggered, this, [this, i] { loadExample(i); });
    }

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    helpMenu->addAction(m_actCheckUpdates);
    helpMenu->addSeparator();
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
    bar->addAction(m_actUndo);
    bar->addAction(m_actRedo);
    bar->addSeparator();
    bar->addAction(m_actSelectTool);
    bar->addAction(m_actWireTool);
    bar->addSeparator();
    bar->addAction(m_actRun);
    bar->addAction(m_actMonitor);
    bar->addSeparator();
    bar->addAction(m_actZoomFit);
}

void MainWindow::buildLibrary()
{
    // 元件按用途分类；下拉框切换分类，列表只显示该类的元件
    struct Entry {
        ComponentType type;
        QString text;
        QString category;
    };
    const QVector<Entry> entries = {
        {ComponentType::Resistor, QStringLiteral("电阻  R"), QStringLiteral("基础")},
        {ComponentType::Capacitor, QStringLiteral("电容  C"), QStringLiteral("基础")},
        {ComponentType::Inductor, QStringLiteral("电感  L"), QStringLiteral("基础")},
        {ComponentType::Diode, QStringLiteral("二极管  D"), QStringLiteral("半导体")},
        {ComponentType::Led, QStringLiteral("发光二极管  LED"), QStringLiteral("半导体")},
        {ComponentType::Npn, QStringLiteral("NPN 三极管  Q"), QStringLiteral("半导体")},
        {ComponentType::Pnp, QStringLiteral("PNP 三极管  Q"), QStringLiteral("半导体")},
        {ComponentType::Nmos, QStringLiteral("N 沟道 MOS  M"), QStringLiteral("半导体")},
        {ComponentType::Pmos, QStringLiteral("P 沟道 MOS  M"), QStringLiteral("半导体")},
        {ComponentType::Igbt, QStringLiteral("IGBT  Z"), QStringLiteral("半导体")},
        {ComponentType::OpAmp, QStringLiteral("运算放大器  U"), QStringLiteral("半导体")},
        {ComponentType::VoltageSource, QStringLiteral("电压源  V"), QStringLiteral("电源")},
        {ComponentType::CurrentSource, QStringLiteral("电流源  I"), QStringLiteral("电源")},
        {ComponentType::Switch, QStringLiteral("开关  S"), QStringLiteral("开关与接地")},
        {ComponentType::Ground, QStringLiteral("接地  GND"), QStringLiteral("开关与接地")},
        {ComponentType::Probe, QStringLiteral("电压测试点  TP"), QStringLiteral("测试点")},
        {ComponentType::CurrentProbe, QStringLiteral("电流测试点  IP"), QStringLiteral("测试点")},
    };

    const QString category = m_libraryCategory ? m_libraryCategory->currentText() : QString();
    // 注意用 QStringLiteral：QLatin1String 会把 UTF-8 字节按 Latin-1 解释，
    // 与下拉框里的 QString 永远不相等，列表会变成空的
    const bool showAll = category.isEmpty() || category == QStringLiteral("全部");

    for (const Entry& entry : entries) {
        if (!showAll && entry.category != category)
            continue;
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

    m_libraryCategory = new QComboBox(libraryContainer);
    m_libraryCategory->addItem(QStringLiteral("全部"));
    for (const QString& name : {QStringLiteral("基础"), QStringLiteral("半导体"),
                                QStringLiteral("电源"), QStringLiteral("开关与接地"),
                                QStringLiteral("测试点")}) {
        m_libraryCategory->addItem(name);
    }
    m_libraryCategory->setToolTip(QStringLiteral("按类别筛选元件"));
    libraryLayout->addWidget(m_libraryCategory);

    auto* hint = new QLabel(QStringLiteral("把元件拖到画布上放置"), libraryContainer);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#64748b; font-size:11px;"));
    libraryLayout->addWidget(hint);

    m_library = new ComponentLibrary(libraryContainer);
    m_library->setIconSize(QSize(46, 34));
    m_library->setSpacing(2);
    m_library->setUniformItemSizes(true);
    libraryLayout->addWidget(m_library, 1);
    libraryDock->setWidget(libraryContainer);
    addDockWidget(Qt::LeftDockWidgetArea, libraryDock);
    buildLibrary();

    connect(m_libraryCategory, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] {
                m_library->clear();
                buildLibrary();
            });

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
    connect(m_props, &PropertyPanel::changed, this, [this] {
        setDirty(true);
        pushHistory();
    });

    auto* logDock = new QDockWidget(QStringLiteral("消息"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    m_logDock = logDock;
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
        QStringLiteral("选择模式：拖动元件移动，双击元件编辑参数，空格键旋转"), this);
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

MainWindow::~MainWindow()
{
    m_destroying = true;

    // 子控件在 ~QWidget() 里被销毁时会失去焦点，QLineEdit 因此发出
    // editingFinished → PropertyPanel::changed → 本窗口的槽；此时 MainWindow
    // 的成员（m_history、m_scene…）已经析构，回调里访问它们就是 use-after-free。
    // 销毁前先把这些连接断掉。
    if (m_props)
        m_props->disconnect(this);
    if (m_scene)
        m_scene->disconnect(this);
    if (m_view)
        m_view->disconnect(this);
    if (m_wavePlot)
        m_wavePlot->disconnect(this);
    if (m_waveLive)
        m_waveLive->disconnect(this);
}

void MainWindow::pushHistory()
{
    if (m_destroying || m_loading || m_restoring)
        return;

    const Circuit circuit = m_scene->toCircuit();
    const QByteArray signature = circuitSignature(circuit);
    if (m_historyIndex >= 0 && m_historyIndex < m_historySignatures.size()
        && m_historySignatures.at(m_historyIndex) == signature)
        return; // 内容没变，不产生新记录

    // 丢弃重做分支
    while (m_history.size() > m_historyIndex + 1) {
        m_history.removeLast();
        m_historySignatures.removeLast();
    }

    m_history.append(circuit.toJson());
    m_historySignatures.append(signature);

    constexpr int kMaxHistory = 200;
    while (m_history.size() > kMaxHistory) {
        m_history.removeFirst();
        m_historySignatures.removeFirst();
    }
    m_historyIndex = m_history.size() - 1;
    updateHistoryActions();
}

void MainWindow::resetHistory()
{
    m_history.clear();
    m_historySignatures.clear();
    m_historyIndex = -1;
    pushHistory();
    updateHistoryActions();
}

void MainWindow::restoreSnapshot(const QJsonObject& snapshot)
{
    QString error;
    const Circuit circuit = Circuit::fromJson(snapshot, &error);
    if (!error.isEmpty()) {
        appendLog(QStringLiteral("⚠ 无法恢复历史状态：%1").arg(error));
        return;
    }

    m_restoring = true;
    m_scene->load(circuit);
    m_restoring = false;

    m_props->setComponent(nullptr);
    setDirty(true);
}

void MainWindow::undo()
{
    if (m_historyIndex <= 0)
        return;
    --m_historyIndex;
    restoreSnapshot(m_history.at(m_historyIndex));
    updateHistoryActions();
    appendLog(QStringLiteral("已撤销（%1/%2）")
                  .arg(m_historyIndex + 1)
                  .arg(m_history.size()));
}

void MainWindow::redo()
{
    if (m_historyIndex < 0 || m_historyIndex >= m_history.size() - 1)
        return;
    ++m_historyIndex;
    restoreSnapshot(m_history.at(m_historyIndex));
    updateHistoryActions();
    appendLog(QStringLiteral("已重做（%1/%2）")
                  .arg(m_historyIndex + 1)
                  .arg(m_history.size()));
}

void MainWindow::updateHistoryActions()
{
    if (!m_actUndo || !m_actRedo)
        return;
    m_actUndo->setEnabled(m_historyIndex > 0);
    m_actRedo->setEnabled(m_historyIndex >= 0 && m_historyIndex < m_history.size() - 1);
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

    resetHistory();

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
    m_hasResult = false;
    if (m_actMonitor) {
        m_actMonitor->setEnabled(false);
        m_actMonitor->setChecked(false);
    }
    if (m_waveLive)
        m_waveLive->stopLiveScope();
    clearLedStates();
    updateTitle();
    resetHistory();
    updateRunAction();
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
    QJsonObject root = circuit.toJson();
    root.insert(QStringLiteral("analysis"), analysisToJson(m_options));
    const QJsonDocument doc(root);
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
    const QJsonObject root = doc.object();
    const Circuit circuit = Circuit::fromJson(root, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), error);
        return;
    }
    if (circuit.components.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("打开失败"),
                             QStringLiteral("文件中没有元件。"));
        return;
    }

    const QJsonObject analysis = root.value(QStringLiteral("analysis")).toObject();
    if (!analysis.isEmpty()) {
        m_options = optionsFromJson(analysis);
        updateAnalysisLabel();
        syncMonitorOptions();
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

    // 运行仿真只做计算，不自动弹任何窗口：波形由「静态波形」/「动态示波器」显式打开
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

    // 先点亮 LED：工作点分支会直接弹对话框返回，放在后面就轮不到它了
    applyLedStates(result);

    const bool hasProbes = applyProbeSignals(circuit, net, result);
    if (!hasProbes && result.kind != AnalysisKind::OperatingPoint)
        appendLog(QStringLiteral("⚠ 电路中没有测试点，波形为空。请放置电压测试点（TP）或电流测试点（IP）。"));

    // 结果留着，由用户打开「波形监控」再看；面板开着就顺手刷新
    m_lastResult = result;
    m_hasResult = true;
    if (m_wavePlot && m_waveWindow && m_waveWindow->isVisible() && m_waveTabs
        && m_waveTabs->currentIndex() == 0)
        m_wavePlot->setResult(m_lastResult);

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

    // 开始连续仿真，但不自动弹窗口：要看就点「波形监控」。
    // 有没有测试点都照跑，只是没测试点时波形是空的。
    if (m_waveLive && !m_waveLive->isLive())
        startLiveScope();
    if (hasProbes) {
        appendLog(QStringLiteral("仿真运行中：点「波形监控」查看波形，点「停止仿真」结束。"));
    } else {
        appendLog(QStringLiteral("仿真运行中，但电路没有测试点，波形为空。"
                                 "放置 TP / IP 后可看到曲线。"));
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // 关掉波形监控窗口只是收起界面，仿真继续跑，随时可以再打开
    if (watched == m_waveWindow && event->type() == QEvent::Close) {
        if (m_actMonitor)
            m_actMonitor->setChecked(false);
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::syncMonitorOptions()
{
    if (m_wavePlot)
        m_wavePlot->setOptions(m_options);
}

void MainWindow::updateRunAction()
{
    if (!m_actRun)
        return;
    const bool running = m_waveLive && m_waveLive->isLive();

    // 波形监控只在仿真运行时可用；停止后连同面板一起收起
    if (m_actMonitor) {
        m_actMonitor->setEnabled(running);
        if (!running && m_actMonitor->isChecked())
            m_actMonitor->setChecked(false);
    }
    if (!running) {
        if (m_waveWindow && m_waveWindow->isVisible())
            m_waveWindow->hide();
        clearLedStates();
    }

    if (running) {
        m_actRun->setText(QStringLiteral("停止仿真"));
        m_actRun->setIcon(actionIcon(QStringLiteral("stop"),
                                     QStringLiteral("media-playback-stop"),
                                     QStyle::SP_MediaStop));
        m_actRun->setToolTip(QStringLiteral("停止正在运行的动态示波器"));
    } else {
        m_actRun->setText(QStringLiteral("运行仿真"));
        m_actRun->setIcon(actionIcon(QStringLiteral("run"),
                                     QStringLiteral("media-playback-start"),
                                     QStyle::SP_MediaPlay));
        m_actRun->setToolTip(QStringLiteral("运行仿真（F5）"));
    }
}

void MainWindow::createMonitorWindow()
{
    // 独立窗口：不占用主界面，多屏时可以拖到另一块屏上
    m_waveWindow = new QWidget(this, Qt::Window);
    m_waveWindow->setObjectName(QStringLiteral("waveMonitorWindow"));
    m_waveWindow->setWindowTitle(QStringLiteral("波形监控 — 电路仿真大师"));
    m_waveWindow->resize(1040, 620);
    auto* windowLayout = new QVBoxLayout(m_waveWindow);
    windowLayout->setContentsMargins(0, 0, 0, 0);

    m_waveTabs = new QTabWidget(m_waveWindow);
    m_wavePlot = new WaveformWindow(m_waveTabs);
    m_waveLive = new WaveformWindow(m_waveTabs);
    m_waveTabs->addTab(m_wavePlot, QStringLiteral("静态波形"));
    m_waveTabs->addTab(m_waveLive, QStringLiteral("动态示波器"));
    m_waveTabs->setCurrentIndex(0);
    windowLayout->addWidget(m_waveTabs);

    m_waveWindow->installEventFilter(this);
    m_waveWindow->hide();

    connect(m_waveTabs, &QTabWidget::currentChanged, this, &MainWindow::onWaveTabChanged);

    syncMonitorOptions();
    connect(m_wavePlot, &WaveformWindow::analysisRequested, this,
            [this](const SimulationOptions& options) {
                m_options = options;
                updateAnalysisLabel();
                runSimulation();
            });

    // 静态波形里的「示波器」按钮直接切到动态标签页
    connect(m_wavePlot, &WaveformWindow::scopeRequested, this,
            [this] { m_waveTabs->setCurrentIndex(1); });

    connect(m_waveLive, &WaveformWindow::liveStateChanged, this, [this](bool) {
        updateRunAction();
    });
    // 刻度改动记进 m_options，保存工程时一并写入
    connect(m_waveLive, &WaveformWindow::liveScalesChanged, this,
            [this](double timeDiv, double voltDiv, double ampDiv) {
                m_options.scopeTimeDiv = timeDiv;
                m_options.scopeVoltDiv = voltDiv;
                m_options.scopeAmpDiv = ampDiv;
            });

    // 实时仿真推进时同步刷新 LED 亮度
    connect(m_waveLive, &WaveformWindow::liveUpdated, this, [this] {
        for (ComponentItem* item : m_scene->componentItems()) {
            if (item->component().type != ComponentType::Led)
                continue;
            const double amps =
                m_waveLive->liveValue(QStringLiteral("I(%1)").arg(item->component().refdes));
            const double i = std::abs(amps);
            item->setLedBrightness(i < 1e-4 ? 0.0
                                            : std::min(1.0, std::log10(i / 1e-4) / 2.0));
        }
    });
}

void MainWindow::showMonitor(int tab)
{
    if (!m_waveWindow)
        return;
    m_waveWindow->show();
    m_waveWindow->raise();
    m_waveWindow->activateWindow();
    if (m_actMonitor && !m_actMonitor->isChecked()) {
        const QSignalBlocker blocker(m_actMonitor);
        m_actMonitor->setChecked(true);
    }
    if (tab == 0 && m_hasResult && m_wavePlot)
        m_wavePlot->setResult(m_lastResult);
    if (m_waveTabs)
        m_waveTabs->setCurrentIndex(tab);
}

void MainWindow::onWaveTabChanged(int index)
{
    if (!m_waveWindow || !m_waveWindow->isVisible())
        return;

    if (index == 1) {
        // 切到示波器时如果还没在跑，就启动；已经在跑就不打断
        if (m_waveLive && !m_waveLive->isLive()) {
            if (!startLiveScope() && m_waveTabs)
                m_waveTabs->setCurrentIndex(0);
        }
        return;
    }

    // 切回静态波形只是换显示，不停止正在跑的实时仿真
    if (m_wavePlot && m_hasResult)
        m_wavePlot->setResult(m_lastResult);
}

bool MainWindow::startLiveScope()
{
    const Circuit circuit = m_scene->toCircuit();
    const Netlist net = circuit.buildNetlist();
    if (!net.valid) {
        QMessageBox::warning(this, QStringLiteral("无法运行示波器"), net.error);
        return false;
    }

    // 没有测试点也照样启动，只是波形为空（面板里会提示放 TP / IP）
    const QVector<QPair<QString, QString>> probes = probeSpecs(circuit, net);

    m_waveLive->setLiveScalePreferences(m_options.scopeTimeDiv, m_options.scopeVoltDiv,
                                        m_options.scopeAmpDiv);
    m_waveLive->startLive(circuit, probes);
    appendLog(QStringLiteral("动态示波器已启动：连续仿真，切回静态波形或关闭面板即停止。"));
    return true;
}

void MainWindow::openScope()
{
    showMonitor(1);
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
    syncMonitorOptions();
    loadCircuitData(circuit, name, QString());
    appendLog(QStringLiteral("已载入示例「%1」。按 F5 运行仿真。").arg(name));
}

void MainWindow::applyLedStates(const SimulationResult& result)
{
    // 正向电流越大越亮：0.1 mA 起亮，10 mA 到最亮
    const auto brightnessFor = [](double amps) {
        const double i = std::abs(amps);
        if (i < 1e-4)
            return 0.0;
        return std::min(1.0, std::log10(i / 1e-4) / 2.0);
    };

    for (ComponentItem* item : m_scene->componentItems()) {
        if (item->component().type != ComponentType::Led)
            continue;
        const QString name = QStringLiteral("I(%1)").arg(item->component().refdes);
        double current = 0.0;
        if (const Signal* wave = result.signal(name)) {
            if (!wave->y.isEmpty())
                current = wave->y.last();
        } else {
            current = result.elementCurrents.value(name, 0.0);
        }
        item->setLedBrightness(brightnessFor(current));
    }
}

void MainWindow::clearLedStates()
{
    if (!m_scene)
        return;
    for (ComponentItem* item : m_scene->componentItems()) {
        if (item->component().type == ComponentType::Led && item->ledBrightness() > 0.0)
            item->setLedBrightness(0.0);
    }
}

void MainWindow::checkForUpdates(bool manual)
{
    // 同一进程只安排一次启动检查：关掉再开新窗口不必再弹一遍
    if (!manual) {
        if (m_updateCheckScheduled)
            return;
        m_updateCheckScheduled = true;

        // 最小化状态没有窗口可弹，等窗口真的出来再说
        if (!isVisible())
            return;
    }

    auto* checker = new UpdateChecker(this);
    connect(checker, &UpdateChecker::updateAvailable, this,
            [this, checker, manual](const ReleaseInfo& release) {
                checker->deleteLater();
                if (!manual && !isVisible())
                    return;
                appendLog(QStringLiteral("发现新版本 %1").arg(release.tagName));
                UpdateDialog dialog(release, this);
                dialog.exec();
            });
    connect(checker, &UpdateChecker::failed, this,
            [this, checker, manual](const QString& reason) {
                checker->deleteLater();
                appendLog(QStringLiteral("检查更新失败：%1").arg(reason));
                // 自动检查失败是后台的一次尝试，不打扰；手动检查必须给出可读原因
                if (manual) {
                    QMessageBox::warning(this, QStringLiteral("检查更新"),
                                         QStringLiteral("无法检查更新：\n%1").arg(reason));
                }
            });
    connect(checker, &UpdateChecker::upToDate, this,
            [this, checker, manual](const QString& current) {
                checker->deleteLater();
                if (manual) {
                    QMessageBox::information(
                        this, QStringLiteral("检查更新"),
                        QStringLiteral("已是最新版本（v%1）。").arg(current));
                }
            });

    if (manual)
        appendLog(QStringLiteral("正在检查更新…"));
    checker->check();
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
        "<li>从左侧<b>元件库</b>把元件拖到画布上；选中后按 <b>空格</b> 旋转。</li>"
        "<li>点击工具栏<b>连线</b>（W），在引脚处单击并拖出折线，双击结束。<br>"
        "导线端点吸附到引脚后会自动出现连接点。</li>"
        "<li>双击元件修改数值（可直接写 10kΩ、4.7µF、1mH，不写单位按默认单位），"
        "按 <b>F5</b> 运行仿真，"
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
            "<h3>电路仿真大师 %1</h3>"
            "<p>基于 Qt6 / C++17 / CMake 的跨平台电路原理图与仿真软件。</p>"
            "<p>求解器：改进节点法（MNA）+ 牛顿-拉夫逊迭代；<br>"
            "支持直流工作点、后向欧拉瞬态分析与交流小信号扫频。</p>"
            "<p>元件：电阻、电容、电感、二极管、运放、开关、电压/电流源、测试点、接地。</p>"
            "<p style='color:#64748b'>本程序为自由软件，可作为 Multisim 在 Linux 上的替代方案。</p>")
            .arg(QStringLiteral(CSM_VERSION_STRING)));
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
