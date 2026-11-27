#include "ui/MainWindow.h"
#include "core/Language.h"

#include "ui/MultimeterWindow.h"

#include "csm_version.h"


#include "core/Circuit.h"
#include "core/Autosave.h"
#include "core/Netlist.h"
#include "core/Shortcuts.h"
#include "core/Units.h"
#include "ui/ComponentItem.h"
#include "ui/PropertyPanel.h"
#include "ui/SchematicScene.h"
#include "ui/SchematicView.h"
#include "ui/WireItem.h"
#include "ui/SettingsDialog.h"
#include "ui/Symbols.h"
#include "ui/Theme.h"
#include "ui/TitleBar.h"
#include "ui/DialogFrame.h"
#include "ui/SafeWrite.h"
#include "ui/UpdateDialog.h"
#include "ui/WaveformWindow.h"
#include "ui/Dialogs.h"

#include <QAbstractItemView>
#include <QUuid>
#include <QDateTime>
#include <QResizeEvent>
#include <QWindow>
#include <QImage>
#include <cmath>
#include <QAction>
#include <QRegularExpression>
#include <QComboBox>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGuiApplication>
#include <QSet>

#include <limits>
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
#include <QScrollArea>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
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
                      QStringLiteral("%1:%2:%3")
                          .arg(items.first()->data(Qt::UserRole).toInt())
                          .arg(items.first()->data(Qt::UserRole + 1).toInt())
                          .arg(items.first()->data(Qt::UserRole + 2).toInt())
                          .toUtf8());
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
                      QStringLiteral("%1:%2:%3")
                          .arg(item->data(Qt::UserRole).toInt())
                          .arg(item->data(Qt::UserRole + 1).toInt())
                          .arg(item->data(Qt::UserRole + 2).toInt())
                          .toUtf8());

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
/// 某类接地的节点号（没有该类地时返回 0，即主地）
int referenceNodeFor(const Circuit& circuit, const Netlist& net, GroundKind kind)
{
    for (const Component& comp : circuit.components) {
        if (comp.type == ComponentType::Ground && comp.groundKind == kind)
            return net.nodeOf(comp.id, 0);
    }
    return 0;
}

QVector<QPair<QString, QString>> probeSpecs(const Circuit& circuit, const Netlist& net)
{
    QVector<QPair<QString, QString>> probes;
    QSet<int> seenNodes;
    for (const Component& comp : circuit.components) {
        if (isVoltageTap(comp.type)) {
            const int node = net.nodeOf(comp.id, 0);
            if (node <= 0 || seenNodes.contains(node))
                continue;
            seenNodes.insert(node);
            // 测试点可以选参考地：非 GND 时取 V(a,b) 的差分信号
            QString signal = QStringLiteral("V(%1)").arg(node);
            QString label = QStringLiteral("%1 (V(%2))").arg(comp.refdes).arg(node);
            if (comp.groundKind != GroundKind::Gnd) {
                const int ref = referenceNodeFor(circuit, net, comp.groundKind);
                signal = QStringLiteral("V(%1,%2)").arg(node).arg(ref);
                label = QStringLiteral("%1 (V(%2,%3))").arg(comp.refdes).arg(node).arg(ref);
            }
            probes.append({label, signal});
        } else if (comp.type == ComponentType::CurrentProbe) {
            probes.append({QStringLiteral("%1 (I)").arg(comp.refdes),
                           QStringLiteral("I(%1)").arg(comp.refdes)});
        }
        // 万用表不进波形监控：读数由它自己的七段码窗口负责，波形页只画 TP / IP
        if (comp.type == ComponentType::Multimeter)
            continue;
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
        if (isVoltageTap(comp.type)) {
            const int node = net.nodeOf(comp.id, 0);
            if (node <= 0 || seenNodes.contains(node))
                continue;
            seenNodes.insert(node);
            if (comp.groundKind != GroundKind::Gnd) {
                const int ref = referenceNodeFor(circuit, net, comp.groundKind);
                probes.append({QStringLiteral("V(%1,%2)").arg(node).arg(ref),
                               QStringLiteral("%1 (V(%2,%3))").arg(comp.refdes).arg(node).arg(ref)});
            } else {
                probes.append({QStringLiteral("V(%1)").arg(node),
                               QStringLiteral("%1 (V(%2))").arg(comp.refdes).arg(node)});
            }
        } else if (comp.type == ComponentType::CurrentProbe) {
            probes.append({QStringLiteral("I(%1)").arg(comp.refdes),
                           QStringLiteral("%1 (I)").arg(comp.refdes)});
        }
        // 万用表不列进波形页（读数走它自己的窗口），只保留 TP / IP
    }
    if (probes.isEmpty()) {
        result.waves.clear();
        result.probeWaves.clear();
        return false;
    }
    // 有测试点但没有时间序列（直流工作点就是这种情况）：波形为空，但测试点是有的。
    // 这里必须返回 true，否则调用方会以为「电路没有测试点」，连连续仿真都不启动。
    if (result.waves.isEmpty()) {
        result.probeWaves.clear();
        return true;
    }

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
/// 图标规格：切主题时要按同样的参数重新生成
struct IconSpec {
    QString name;
    QString themeName;
    QStyle::StandardPixmap fallback = QStyle::SP_CustomBase;
};

/// 用 QIcon::cacheKey() 当键，就不用去改二十多个创建动作的地方
QHash<qint64, IconSpec>& iconSpecs()
{
    static QHash<qint64, IconSpec> map;
    return map;
}

QIcon buildActionIcon(const IconSpec& spec)
{
    // 工具栏图标平时保持仓库里那套配色（蓝色描边、绿色播放…），
    // 只有在深色主题下才统一刷成浅色 —— 深底上那套深色图标看不清楚。
    const bool dark = Theme::isDark();

    QIcon icon = svgIcon(QStringLiteral(":/icons/%1.svg").arg(spec.name));
    if (!icon.isNull()) {
        if (dark)
            icon = iconForDarkSurface(icon);
        iconSpecs().insert(icon.cacheKey(), spec);
        return icon;
    }
    icon = QIcon::fromTheme(spec.themeName);
    if (!icon.isNull()) {
        if (dark)
            icon = iconForDarkSurface(icon);
        iconSpecs().insert(icon.cacheKey(), spec);
        return icon;
    }
    icon = qApp->style()->standardIcon(spec.fallback);
    if (dark)
        icon = iconForDarkSurface(icon);
    iconSpecs().insert(icon.cacheKey(), spec);
    return icon;
}

/// 示例的显示名：英文界面下优先用清单里的 name_en，没有就退回 name
QString exampleDisplayName(const QJsonObject& root, const QString& fallback)
{
    if (csm::language() == csm::Language::English) {
        const QString english = root.value(QStringLiteral("name_en")).toString();
        if (!english.isEmpty())
            return english;
    }
    const QString name = root.value(QStringLiteral("name")).toString();
    return name.isEmpty() ? fallback : name;
}

QIcon actionIcon(const QString& name, const QString& themeName, QStyle::StandardPixmap fallback)
{
    return buildActionIcon(IconSpec{name, themeName, fallback});
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // 跟随系统模式下，系统深浅色一变就整体重刷（图标、元件库、图表都要跟着）
    Theme::setChangeHandler([this] { applyTheme(); });
    setWindowTitle(CSM_TR("Circuit Master"));
    const QRect available = QGuiApplication::primaryScreen()
        ? QGuiApplication::primaryScreen()->availableGeometry()
        : QRect(0, 0, 1400, 880);
    resize(qMin(1400, available.width() - 80), qMin(880, available.height() - 80));
    QIcon appIcon = svgIcon(QStringLiteral(":/icons/icon.svg"));
    if (appIcon.isNull())
        appIcon = componentIcon(ComponentType::VoltageSource, QSize(64, 64));
    setWindowIcon(appIcon);

    // 中央是标签页：一个标签一个文档。先建第一个空白文档，后面 addDocument() 也走它
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    // 标签栏不要拿焦点：QTabBar 会把空格当作「激活当前标签」吃掉，
    // 于是「空格旋转元件」的快捷键失效
    m_tabs->tabBar()->setFocusPolicy(Qt::NoFocus);
    // 关闭按钮换成自绘的灰色 ✖（悬停变蓝）—— 主题默认给的是红色，跟界面不搭
    m_tabs->tabBar()->setStyleSheet(QStringLiteral(R"(
QTabBar::close-button {
    image: url(:/icons/tab_close.svg);
    subcontrol-position: right;
}
QTabBar::close-button:hover {
    image: url(:/icons/tab_close_active.svg);
}
)"));
    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::switchToDocument);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        closeDocument(index);
    });

    // 先把第一个空白文档建出来：后面 createStatusBar() 之类的会读 doc()->options
    addDocument();

    createActions();
    scanExamples();
    // 无边框：系统那条标题栏由自绘的一行代替（菜单 + 标题 + 三个按钮）
    setWindowFlag(Qt::FramelessWindowHint);

    createMenus();
    createTitleBar();
    m_resizeGrips = new ResizeGrips(this);
    createToolBar();
    createDocks();
    createMonitorWindow();
    // 波形窗口有两个实例（静态 / 动态），图表配色各刷一次
    if (m_wavePlot)
        m_wavePlot->applyTheme();
    for (Document* document : m_documents) {
        if (document && document->liveScope)
            document->liveScope->applyTheme();
    }
    createStatusBar();

    appendLog(CSM_TR("Circuit Master %1  —  Qt6 / MNA solver")
                  .arg(QStringLiteral(CSM_VERSION_STRING)));
    appendLog(CSM_TR("Tip: left-drag to select and move, double-click to edit, %1 to rotate; "
                     "%2 runs the simulation.")
                  .arg(keyLabel(m_actRotate), keyLabel(m_actRun)));
    appendLog(CSM_TR("New blank circuit: drop parts from the library on the left, "
                     "or load an example from Simulate → Example Circuits."));
    refreshKeyHints();   // 状态栏提示里的按键要与真实快捷键一致

    if (m_actMonitor) {
        m_actMonitor->setEnabled(false);
        m_actMonitor->setChecked(false);
    }
    if (m_waveLive)
        m_waveLive->stopLiveScope();
    m_liveDoc = nullptr;

    // 启动后自动检查一次更新：先让窗口画出来再查，只有真发现新版本才弹窗。
    // 测试里用 CSM_NO_UPDATE_CHECK=1 关掉，避免用例依赖网络。
    if (!qEnvironmentVariableIsSet("CSM_NO_UPDATE_CHECK")) {
        QTimer::singleShot(3000, this, [this] { checkForUpdates(false); });
    }
}

// ---------------------------------------------------------------- 多文档

Document* MainWindow::doc() const
{
    if (!m_tabs || m_documents.isEmpty())
        return nullptr;
    const int index = m_tabs->currentIndex();
    return (index >= 0 && index < m_documents.size()) ? m_documents.at(index) : nullptr;
}

int MainWindow::indexOfPath(const QString& path) const
{
    if (path.isEmpty())
        return -1;
    const QString target = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_documents.size(); ++i) {
        const QString open = m_documents.at(i)->filePath;
        if (!open.isEmpty() && QFileInfo(open).absoluteFilePath() == target)
            return i;
    }
    return -1;
}

void MainWindow::updateTabTitle(Document* document)
{
    if (!document || !m_tabs)
        return;
    const int index = m_documents.indexOf(document);
    if (index < 0)
        return;
    m_tabs->setTabText(index, document->tabTitle());
    m_tabs->setTabToolTip(index, document->filePath.isEmpty()
                                     ? CSM_TR("Not saved to a file yet")
                                     : document->filePath);
    if (document == doc()) {
        setWindowTitle(CSM_TR("%1%2 — Circuit Master")
                           .arg(document->name,
                                document->dirty ? QStringLiteral(" *") : QString()));
    }
}

Document* MainWindow::addDocument(const QString& name, const QString& path)
{
    auto* document = new Document;
    document->name = name;
    document->filePath = path;
    document->options.kind = AnalysisKind::Transient;
    document->options.tStop = 5e-3;
    document->options.tStep = 1e-5;
    document->options.fStart = 10.0;
    document->options.fStop = 1e6;
    document->options.acPoints = 160;

    document->recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    document->scene = new SchematicScene(this);
    document->view = new SchematicView(document->scene, this);
    wireDocument(document);
    // 新文档的视图也要挂上编辑快捷键（空格旋转、Delete…）。
    // 构造期间第一个文档走这条路径时这些动作还没建出来，函数里做了空指针兜底，
    // 之后构造函数末尾会再挂一次。
    attachCanvasActions(document->view);

    m_documents.append(document);
    m_tabs->addTab(document->view, document->tabTitle());
    m_tabs->setTabToolTip(m_documents.size() - 1,
                          path.isEmpty() ? CSM_TR("Not saved to a file yet") : path);

    // currentChanged 会触发 switchToDocument；这里再显式切一次，
    // 保证即使旧标签就是当前标签（第一次创建时 index 不变）也完成绑定
    m_tabs->setCurrentWidget(document->view);
    switchToDocument(m_documents.indexOf(document));
    resetHistory(); // 空白电路本身就是第一个历史点
    return document;
}

void MainWindow::wireDocument(Document* document)
{
    SchematicScene* scene = document->scene;
    SchematicView* view = document->view;

    connect(scene, &SchematicScene::circuitChanged, this, [this, document] {
        // 后台标签（不是当前显示的那个）被改动时不碰界面状态
        if (document != doc())
            return;
        setDirty(true);
        // 上一次静态仿真的读数过时了：万用表先显示横杠，
        // 免得改动之后还盯着一个「看着像实时」的旧数字
        if (!m_loading) {
            if (doc()->hasResult)
                doc()->resultStale = true;
            refreshMeterWindows(); // 顺带把「档位」同步到读数窗口
        }
        // 仿真运行中改了电路（例如开关闭合/断开）：稍后重建引擎，
        // 否则实时仿真还跑在旧拓扑上，LED 不会跟着变
        if (m_waveLive && m_waveLive->isLive() && m_liveRestartTimer)
            m_liveRestartTimer->start();
    });
    connect(scene, &SchematicScene::editCommitted, this, [this, document] {
        if (document == doc())
            pushHistory();
    });
    connect(scene, &QGraphicsScene::selectionChanged, this, [this, document] {
        if (document == doc())
            onSelectionChanged();
    });
    connect(scene, &SchematicScene::editRequested, this, [this, document](ComponentItem* item) {
        if (document == doc())
            onEditRequested(item);
    });
    connect(view, &SchematicView::componentEditRequested, this,
            [this, document](ComponentItem* item) {
                if (document == doc())
                    onEditRequested(item);
            });
    // 仿真运行中点击开关：拨动它，实时引擎会在 200 ms 内换上新拓扑
    connect(view, &SchematicView::switchToggled, this, [this, document](ComponentItem* item) {
        if (document == doc())
            toggleSwitch(item);
    });
    // 按钮开关：按住才动作
    connect(view, &SchematicView::pushButtonPressed, this,
            [this, document](ComponentItem* item) {
                if (document == doc())
                    pressPushButton(item);
            });
    connect(view, &SchematicView::pushButtonReleased, this,
            [this, document](ComponentItem* item) {
                if (document == doc())
                    releasePushButton(item);
            });
    connect(view, &SchematicView::scenePositionChanged, this, [this](const QPointF& p) {
        if (m_coordLabel)
            m_coordLabel->setText(
                QStringLiteral("x: %1   y: %2").arg(int(p.x())).arg(int(p.y())));
    });
    connect(view, &SchematicView::hintChanged, this, [this](const QString& hint) {
        if (m_hintLabel)
            m_hintLabel->setText(hint);
    });
    connect(view, &SchematicView::toolChanged, this, [this, view] {
        if (view != m_view)
            return;
        const bool wire = view->tool() == SchematicView::Tool::Wire;
        if (m_actWireTool)
            m_actWireTool->setChecked(wire);
        if (m_actSelectTool)
            m_actSelectTool->setChecked(!wire);
    });
    connect(view, &SchematicView::componentPlaced, this,
            [this, document](ComponentItem* item) {
                if (document != doc())
                    return;
                if (m_props)
                    m_props->setComponent(item);
                if (m_actSelectTool)
                    m_actSelectTool->setChecked(true);
            });
}

void MainWindow::switchToDocument(int index)
{
    if (index < 0 || index >= m_documents.size())
        return;
    Document* document = m_documents.at(index);

    // 万用表窗口属于原来那个文档，切走时收掉；但**实时仿真不停** ——
    // 它属于发起仿真的那个文档（m_liveDoc），切回来看应当还在跑
    if (m_scene != document->scene)
        closeMeterWindows();

    m_scene = document->scene;
    bindLiveScope(document);   // 换成这个图纸自己的动态示波器
    m_view = document->view;
    if (m_view)
        m_view->setLiveRunning(m_waveLive && m_waveLive->isLive());
    if (m_view)
        m_view->setFocus();

    // 构造期间这些控件还不存在，用空指针判断兜住
    if (!m_props)
        return;

    m_props->setComponent(nullptr);
    if (m_actMonitor)
        m_actMonitor->setEnabled(document->hasResult);
    updateTitle();
    updateHistoryActions();
    updateAnalysisLabel();
    if (m_actCut)
        m_actCut->setEnabled(false);
    if (m_actCopy)
        m_actCopy->setEnabled(false);
    if (m_actDelete)
        m_actDelete->setEnabled(false);
    if (m_actRotate)
        m_actRotate->setEnabled(false);
    if (m_actPaste)
        m_actPaste->setEnabled(m_scene && m_scene->hasClipboard());
    updateRunAction();

    // 静态波形页跟着换成本文档上次的结果
    if (m_wavePlot && document->hasResult)
        m_wavePlot->setResult(document->lastResult);
}

bool MainWindow::closeDocument(int index)
{
    if (index < 0 || index >= m_documents.size())
        return false;
    Document* document = m_documents.at(index);

    // 脏的先问一句：临时切到那个标签再问，用户才知道说的是哪份
    if (document->dirty) {
        const int previous = m_tabs->currentIndex();
        m_tabs->setCurrentIndex(index);
        const bool ok = maybeSave();
        if (!ok) {
            m_tabs->setCurrentIndex(previous);
            return false;
        }
    }

    // 关掉的文档：停掉并销毁它自己的动态示波器，否则引擎会跑在清空后的电路上
    if (document->liveScope) {
        document->liveScope->stopLiveScope();
        if (m_waveTabs)
            m_waveTabs->removeTab(m_waveTabs->indexOf(document->liveScope));
        document->liveScope->deleteLater();
        if (m_waveLive == document->liveScope)
            m_waveLive = nullptr;
        if (m_liveDoc == document)
            m_liveDoc = nullptr;
        document->liveScope = nullptr;
    }

    // 最后一个标签不真删：清成空白文档，省得窗口里什么都没有
    if (m_documents.size() == 1) {
        m_loading = true;
        closeMeterWindows();
        document->scene->clearAll();
        m_loading = false;
        document->name = CSM_TR("Untitled");
        document->filePath.clear();
        document->dirty = false;
        document->hasResult = false;
        document->resultStale = false;
        document->lastResult = SimulationResult();
        clearLedStates();
        clearBlast();
        resetHistory();
        updateTabTitle(document);
        updateRunAction();
        if (m_actMonitor) {
            m_actMonitor->setEnabled(false);
            m_actMonitor->setChecked(false);
        }
        return true;
    }

    const bool wasCurrent = (document == doc());
    if (wasCurrent)
        closeMeterWindows();

    m_tabs->removeTab(index);
    m_documents.removeAt(index);
    document->scene->deleteLater();
    document->view->deleteLater();
    delete document;

    // removeTab 会自动触发 currentChanged；但如果删的是后台标签，
    // 当前索引可能整体前移，这里再对齐一次
    if (m_tabs->count() > 0)
        switchToDocument(m_tabs->currentIndex());
    return true;
}

void MainWindow::closeCurrentDocument()
{
    const int index = m_tabs ? m_tabs->currentIndex() : -1;
    if (index >= 0)
        closeDocument(index);
}

void MainWindow::createActions()
{
    m_actNew = new QAction(
        actionIcon(QStringLiteral("new"), QStringLiteral("document-new"), QStyle::SP_FileIcon),
        CSM_TR("&New"), this);
    m_actNew->setShortcut(QKeySequence::New);

    m_actOpen = new QAction(
        actionIcon(QStringLiteral("open"), QStringLiteral("document-open"),
                   QStyle::SP_DialogOpenButton),
        CSM_TR("&Open…"), this);
    m_actOpen->setShortcut(QKeySequence::Open);

    m_actSave = new QAction(
        actionIcon(QStringLiteral("save"), QStringLiteral("document-save"),
                   QStyle::SP_DialogSaveButton),
        CSM_TR("&Save"), this);
    m_actSave->setShortcut(QKeySequence::Save);

    m_actSaveAs = new QAction(
        actionIcon(QStringLiteral("save_as"), QStringLiteral("document-save-as"), QStyle::SP_DialogSaveButton),
        CSM_TR("Save &As…"), this);
    m_actSaveAs->setShortcut(QKeySequence::SaveAs);


    m_actCloseTab = new QAction(
        actionIcon(QStringLiteral("close_tab"), QStringLiteral("window-close"), QStyle::SP_DialogCloseButton),
        CSM_TR("Close &Tab"), this);
    m_actCloseTab->setShortcut(QKeySequence::Close); // Ctrl+W
    m_actCloseTab->setStatusTip(CSM_TR("Close the current tab; unsaved changes are confirmed first"));

    m_actNextTab = new QAction(
        actionIcon(QStringLiteral("next_tab"), QStringLiteral("go-next"), QStyle::SP_ArrowForward),
        CSM_TR("Next Tab"), this);
    m_actNextTab->setShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")));
    m_actNextTab->setShortcutContext(Qt::WindowShortcut);

    m_actPrevTab = new QAction(
        actionIcon(QStringLiteral("prev_tab"), QStringLiteral("go-previous"), QStyle::SP_ArrowBack),
        CSM_TR("Previous Tab"), this);
    m_actPrevTab->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")));
    m_actPrevTab->setShortcutContext(Qt::WindowShortcut);

    m_actQuit = new QAction(
        actionIcon(QStringLiteral("quit"), QStringLiteral("application-exit"), QStyle::SP_DialogCloseButton),
        CSM_TR("&Quit"), this);
    m_actQuit->setShortcut(QKeySequence::Quit);

    m_actUndo = new QAction(
        actionIcon(QStringLiteral("undo"), QStringLiteral("edit-undo"), QStyle::SP_ArrowBack),
        CSM_TR("Undo"), this);
    m_actUndo->setShortcut(QKeySequence::Undo);
    m_actUndo->setEnabled(false);

    m_actRedo = new QAction(
        actionIcon(QStringLiteral("redo"), QStringLiteral("edit-redo"), QStyle::SP_ArrowForward),
        CSM_TR("Redo"), this);
    m_actRedo->setShortcut(QKeySequence::Redo);
    m_actRedo->setEnabled(false);

    m_actCut = new QAction(
        actionIcon(QStringLiteral("cut"), QStringLiteral("edit-cut"), QStyle::SP_CustomBase), CSM_TR("Cut"), this);
    m_actCut->setShortcut(QKeySequence::Cut);
    m_actCut->setEnabled(false);
    m_actCopy = new QAction(
        actionIcon(QStringLiteral("copy"), QStringLiteral("edit-copy"), QStyle::SP_CustomBase), CSM_TR("Copy"), this);
    m_actCopy->setShortcut(QKeySequence::Copy);
    m_actCopy->setEnabled(false);
    m_actPaste = new QAction(
        actionIcon(QStringLiteral("paste"), QStringLiteral("edit-paste"), QStyle::SP_CustomBase), CSM_TR("Paste"), this);
    m_actPaste->setShortcut(QKeySequence::Paste);
    m_actPaste->setEnabled(false);

    m_actDelete = new QAction(
        actionIcon(QStringLiteral("delete"), QStringLiteral("edit-delete"), QStyle::SP_TrashIcon),
        CSM_TR("Delete"), this);
    m_actDelete->setShortcut(QKeySequence::Delete);

    m_actRotate = new QAction(
        actionIcon(QStringLiteral("rotate"), QStringLiteral("object-rotate-right"), QStyle::SP_CustomBase),
        CSM_TR("Rotate 90°"), this);
    m_actRotate->setObjectName(QStringLiteral("actRotate"));
    // 空格旋转：和 KiCad 的习惯一致，选中元件后连按空格就能连续转
    m_actRotate->setShortcut(QKeySequence(Qt::Key_Space));
    m_actRotate->setShortcutContext(Qt::WindowShortcut);

    // 对齐与分布：按符号本体的外框算，位移仍会被网格吸附
    const QVector<QPair<QString, SchematicScene::AlignMode>> alignItems = {
        {CSM_TR("Align left"), SchematicScene::AlignMode::Left},
        {CSM_TR("Center horizontally"), SchematicScene::AlignMode::CenterH},
        {CSM_TR("Right align"), SchematicScene::AlignMode::Right},
        {CSM_TR("Align top"), SchematicScene::AlignMode::Top},
        {CSM_TR("Center vertically"), SchematicScene::AlignMode::CenterV},
        {CSM_TR("Align bottom"), SchematicScene::AlignMode::Bottom},
    };
    // 每种对齐有自己的图标，一眼能分出来
    const auto alignIcon = [](SchematicScene::AlignMode mode) {
        switch (mode) {
        case SchematicScene::AlignMode::Left:
            return actionIcon(QStringLiteral("align_left"), QStringLiteral("format-align-left"),
                              QStyle::SP_CustomBase);
        case SchematicScene::AlignMode::CenterH:
            return actionIcon(QStringLiteral("align_center_h"),
                              QStringLiteral("format-align-horizontal-center"),
                              QStyle::SP_CustomBase);
        case SchematicScene::AlignMode::Right:
            return actionIcon(QStringLiteral("align_right"), QStringLiteral("format-align-right"),
                              QStyle::SP_CustomBase);
        case SchematicScene::AlignMode::Top:
            return actionIcon(QStringLiteral("align_top"), QStringLiteral("format-align-top"),
                              QStyle::SP_CustomBase);
        case SchematicScene::AlignMode::CenterV:
            return actionIcon(QStringLiteral("align_center_v"),
                              QStringLiteral("format-align-vertical-center"),
                              QStyle::SP_CustomBase);
        case SchematicScene::AlignMode::Bottom:
            return actionIcon(QStringLiteral("align_bottom"),
                              QStringLiteral("format-align-bottom"), QStyle::SP_CustomBase);
        }
        return QIcon();
    };
    for (const auto& entry : alignItems) {
        auto* action = new QAction(alignIcon(entry.second), entry.first, this);
        action->setEnabled(false);
        const SchematicScene::AlignMode mode = entry.second;
        connect(action, &QAction::triggered, this, [this, mode] {
            if (m_scene)
                m_scene->alignSelection(mode);
        });
        m_alignActions.append(action);
    }

    const QVector<QPair<QString, Qt::Orientation>> distributeItems = {
        {CSM_TR("Distribute horizontally"), Qt::Horizontal},
        {CSM_TR("Distribute vertically"), Qt::Vertical},
    };
    for (const auto& entry : distributeItems) {
        const Qt::Orientation orientation = entry.second;
        auto* action = new QAction(
            orientation == Qt::Horizontal
                ? actionIcon(QStringLiteral("distribute_h"), QStringLiteral("format-justify-fill"),
                             QStyle::SP_CustomBase)
                : actionIcon(QStringLiteral("distribute_v"), QStringLiteral("format-justify-fill"),
                             QStyle::SP_CustomBase),
            entry.first, this);
        action->setEnabled(false);
        connect(action, &QAction::triggered, this, [this, orientation] {
            if (m_scene)
                m_scene->distributeSelection(orientation);
        });
        m_distributeActions.append(action);
    }

    m_actSelectAll = new QAction(
        actionIcon(QStringLiteral("select_all"), QStringLiteral("edit-select-all"), QStyle::SP_FileDialogDetailedView),
        CSM_TR("Select All"), this);
    m_actSelectAll->setShortcut(QKeySequence::SelectAll);

    m_actZoomIn = new QAction(
        actionIcon(QStringLiteral("zoom_in"), QStringLiteral("zoom-in"), QStyle::SP_ArrowUp),
        CSM_TR("Zoom In"), this);
    m_actZoomIn->setShortcut(QKeySequence::ZoomIn);

    m_actZoomOut = new QAction(
        actionIcon(QStringLiteral("zoom_out"), QStringLiteral("zoom-out"), QStyle::SP_ArrowDown),
        CSM_TR("Zoom Out"), this);
    m_actZoomOut->setShortcut(QKeySequence::ZoomOut);

    m_actZoomFit = new QAction(
        actionIcon(QStringLiteral("zoom_fit"), QStringLiteral("zoom-fit-best"),
                   QStyle::SP_FileDialogContentsView),
        CSM_TR("Fit to Window"), this);
    m_actZoomFit->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));

    m_actGrid = new QAction(
        actionIcon(QStringLiteral("grid"), QStringLiteral("view-grid"), QStyle::SP_FileDialogListView),
        CSM_TR("Show Grid"), this);
    m_actGrid->setCheckable(true);
    m_actGrid->setChecked(true);

    m_actRun = new QAction(
        actionIcon(QStringLiteral("run"), QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay),
        CSM_TR("Run Simulation"), this);
    m_actRun->setShortcut(QKeySequence(Qt::Key_F5));
    m_actRun->setObjectName(QStringLiteral("actRun"));

    m_actMonitor = new QAction(
        actionIcon(QStringLiteral("monitor"), QStringLiteral("utilities-system-monitor"),
                   QStyle::SP_ComputerIcon),
        CSM_TR("Waveform Monitor"), this);
    m_actMonitor->setObjectName(QStringLiteral("actMonitor"));
    // F6 打开 / 收起波形监控（F5 运行是它的前一步）
    m_actMonitor->setShortcut(QKeySequence(Qt::Key_F6));
    m_actMonitor->setCheckable(true);
    m_actMonitor->setEnabled(false); // 只有仿真运行时才允许打开
    m_actMonitor->setToolTip(
        CSM_TR("Waveform monitor (%1): Static Waveforms / Live Scope tabs (available while simulating)")
            .arg(keyLabel(m_actMonitor)));

    m_actSelectTool = new QAction(
        actionIcon(QStringLiteral("select"), QStringLiteral("edit-select"), QStyle::SP_ArrowUp),
        CSM_TR("Select"), this);
    m_actSelectTool->setCheckable(true);
    m_actSelectTool->setChecked(true);
    m_actSelectTool->setShortcut(QKeySequence(Qt::Key_V));
    m_actSelectTool->setToolTip(CSM_TR("Select/move parts (V)"));

    m_actWireTool = new QAction(
        actionIcon(QStringLiteral("wire"), QStringLiteral("draw-line"), QStyle::SP_ArrowRight),
        CSM_TR("Wire"), this);
    m_actWireTool->setCheckable(true);
    m_actWireTool->setShortcut(QKeySequence(Qt::Key_W));
    m_actWireTool->setToolTip(CSM_TR("Draw wire (W)"));

    auto* toolGroup = new QActionGroup(this);
    toolGroup->addAction(m_actSelectTool);
    toolGroup->addAction(m_actWireTool);

    m_actCheckUpdates = new QAction(
        actionIcon(QStringLiteral("update"), QStringLiteral("system-software-update"), QStyle::SP_BrowserReload),
        CSM_TR("Check for Updates…"), this);

    m_actGuide = new QAction(
        actionIcon(QStringLiteral("guide"), QStringLiteral("help-contents"), QStyle::SP_MessageBoxQuestion),
        CSM_TR("Quick Start"), this);

    m_actAbout = new QAction(
        actionIcon(QStringLiteral("about"), QStringLiteral("help-about"), QStyle::SP_MessageBoxInformation),
        CSM_TR("About Circuit Master"), this);

    connect(m_actNew, &QAction::triggered, this, &MainWindow::newCircuit);
    connect(m_actOpen, &QAction::triggered, this, &MainWindow::openCircuit);
    connect(m_actSave, &QAction::triggered, this, [this] { saveCircuit(); });
    connect(m_actSaveAs, &QAction::triggered, this, [this] { saveCircuitAs(); });
    connect(m_actCloseTab, &QAction::triggered, this, &MainWindow::closeCurrentDocument);
    connect(m_actNextTab, &QAction::triggered, this, [this] {
        if (!m_tabs || m_tabs->count() < 2)
            return;
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    connect(m_actPrevTab, &QAction::triggered, this, [this] {
        if (!m_tabs || m_tabs->count() < 2)
            return;
        const int count = m_tabs->count();
        m_tabs->setCurrentIndex((m_tabs->currentIndex() - 1 + count) % count);
    });
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);

    connect(m_actUndo, &QAction::triggered, this, &MainWindow::undo);
    connect(m_actRedo, &QAction::triggered, this, &MainWindow::redo);
    connect(m_actDelete, &QAction::triggered, this, [this] {
        m_view->deleteSelection();
        refreshMeterWindows(); // 删掉万用表时顺手关掉它的读数窗口
    });
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
            m_liveDoc = nullptr;
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


    connect(m_actCut, &QAction::triggered, this, [this] {
        m_scene->cutSelection();
        m_actPaste->setEnabled(m_scene->hasClipboard());
        onSelectionChanged();
    });
    connect(m_actCopy, &QAction::triggered, this, [this] {
        m_scene->copySelection();
        m_actPaste->setEnabled(m_scene->hasClipboard());
    });
    connect(m_actPaste, &QAction::triggered, this, [this] {
        m_scene->pasteClipboard();
        onSelectionChanged();
    });

    connect(m_actSelectTool, &QAction::triggered, this,
            [this] { m_view->setTool(SchematicView::Tool::Select); });
    connect(m_actWireTool, &QAction::triggered, this,
            [this] { m_view->setTool(SchematicView::Tool::Wire); });

    connect(m_actCheckUpdates, &QAction::triggered, this, [this] { checkForUpdates(true); });
    connect(m_actGuide, &QAction::triggered, this, &MainWindow::showQuickGuide);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::showAbout);

    m_actSnapWires = new QAction(
        actionIcon(QStringLiteral("snap_wires"), QStringLiteral("snap-grid"), QStyle::SP_CustomBase),
        CSM_TR("Snap Wires to Grid"), this);
    m_actSnapWires->setToolTip(CSM_TR("Snap wire corners to the grid, fixing offsets left by older paste"));
    connect(m_actSnapWires, &QAction::triggered, this, [this] { m_scene->snapWiresToGrid(); });

    // Esc：连线中取消连线并回到选择工具，否则取消选择。
    // 做成动作（而不是画布里写死按键）是为了让它在「设置 → 快捷键」里也能改。
    m_actCancel = new QAction(CSM_TR("Cancel / Clear Selection"), this);
    m_actCancel->setObjectName(QStringLiteral("actCancel"));
    m_actCancel->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(m_actCancel, &QAction::triggered, this, [this] {
        if (!m_view)
            return;
        if (m_view->tool() == SchematicView::Tool::Wire) {
            m_view->cancelWire();
            m_view->setTool(SchematicView::Tool::Select);
            return;
        }
        m_scene->clearSelection();
    });

    m_actDelete->setEnabled(false);
    m_actRotate->setEnabled(false);

    // 这些快捷键只在画布获得焦点时生效，避免与属性面板输入冲突
    attachCanvasActions(m_view);

    registerShortcutBindings();   // 把用户在设置里改过的快捷键套上去
}

QString MainWindow::keyLabel(const QAction* action) const
{
    if (!action)
        return QStringLiteral("—");
    const QKeySequence sequence = action->shortcut();
    if (sequence.isEmpty())
        return CSM_TR("(unbound)");
    return sequence.toString(QKeySequence::NativeText);
}

void MainWindow::refreshKeyHints()
{
    for (Document* document : m_documents) {
        if (document && document->view)
            document->view->setKeyHints(keyLabel(m_actRotate), keyLabel(m_actRun));
    }
    if (m_actRun)
        m_actRun->setToolTip(CSM_TR("Run Simulation (%1)").arg(keyLabel(m_actRun)));
    if (m_actMonitor)
        m_actMonitor->setToolTip(
            CSM_TR("Waveform monitor (%1): Static Waveforms / Live Scope tabs (available while simulating)")
                .arg(keyLabel(m_actMonitor)));
}

void MainWindow::applyAutosaveSettings()
{
    if (m_autosaveTimer) {
        if (Autosave::enabled())
            m_autosaveTimer->start(Autosave::intervalMinutes() * 60 * 1000);
        else
            m_autosaveTimer->stop();
    }
    // 崩溃恢复有自己的间隔：自动保存可以设得很疏，崩溃保护仍保持勤快
    if (m_recoveryTimer) {
        if (Autosave::recoveryEnabled())
            m_recoveryTimer->start(Autosave::recoveryIntervalMinutes() * 60 * 1000);
        else
            m_recoveryTimer->stop();
    }
}

void MainWindow::autosaveNow()
{
    if (!Autosave::enabled())
        return;

    int saved = 0;
    for (Document* document : m_documents) {
        if (!document || !document->scene || !document->dirty)
            continue;
        if (document->filePath.isEmpty())
            continue;   // 未命名的图纸没有文件可写，交给崩溃恢复的副本

        QString error;
        if (writeDocumentToDisk(document, &error))
            ++saved;
        else
            appendLog(CSM_TR("Auto save failed: %1").arg(error));
    }
    if (saved > 0)
        appendLog(CSM_TR("Auto saved %1 circuit(s).").arg(saved));
}

void MainWindow::writeRecoveryCopies()
{
    if (!Autosave::recoveryEnabled())
        return;
    for (Document* document : m_documents) {
        if (!document || !document->scene || !document->dirty)
            continue;
        Autosave::Record record;
        record.id = document->recoveryId;
        record.originalPath = document->filePath;
        record.savedAt = QDateTime::currentDateTime();
        QJsonObject root = document->scene->toCircuit().toJson();
        root.insert(QStringLiteral("analysis"), analysisToJson(document->options));
        record.circuit = root;
        Autosave::write(record);
    }
}

bool MainWindow::hasRecoverableWork() const
{
    return !Autosave::pending().isEmpty();
}

void MainWindow::offerRecovery()
{
    const QVector<Autosave::Record> records = Autosave::pending();
    if (records.isEmpty())
        return;
    if (!Autosave::recoveryEnabled()) {
        // 用户关掉了「崩溃恢复」：不问，直接清掉残留副本
        Autosave::clearAll();
        return;
    }

    QStringList lines;
    for (const Autosave::Record& record : records) {
        const QString name = record.originalPath.isEmpty()
            ? CSM_TR("Untitled")
            : QFileInfo(record.originalPath).fileName();
        lines << QStringLiteral("%1  ·  %2")
                     .arg(name, record.savedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    }
    const auto answer = Dialogs::confirm(
        this, CSM_TR("Recover unsaved work?"),
        CSM_TR("The application did not exit cleanly last time; %1 unsaved circuit(s) were "
               "recovered.\n\n%2\n\nRestore them?")
            .arg(QString::number(records.size()), lines.join(QLatin1Char('\n'))));
    if (!answer) {
        Autosave::clearAll();
        appendLog(CSM_TR("Recovered files discarded."));
        return;
    }

    for (const Autosave::Record& record : records) {
        const QString title = record.originalPath.isEmpty()
            ? CSM_TR("Recovered circuit")
            : QFileInfo(record.originalPath).completeBaseName();
        // 原文件还在就用原路径打开（用户接着存回去），否则当成未命名的新标签
        Document* target = addDocument(title, record.originalPath);
        m_loading = true;
        target->scene->load(Circuit::fromJson(record.circuit));
        m_loading = false;
        target->dirty = true;
        updateTabTitle(target);
        // 恢复出来的内容已经在新标签里了，别再让它触发一次恢复提示
        Autosave::remove(record.id);
    }
    appendLog(CSM_TR("Recovered %1 circuit(s).").arg(QString::number(records.size())));
    resetHistory();
}

void MainWindow::registerShortcutBindings()
{
    m_shortcutBindings.clear();
    // id 是稳定标识（存进配置），顺序就是设置对话框里的顺序
    const QVector<QPair<QString, QAction*>> entries = {
        {QStringLiteral("file.new"), m_actNew},
        {QStringLiteral("file.open"), m_actOpen},
        {QStringLiteral("file.save"), m_actSave},
        {QStringLiteral("file.saveAs"), m_actSaveAs},
        {QStringLiteral("file.closeTab"), m_actCloseTab},
        {QStringLiteral("file.nextTab"), m_actNextTab},
        {QStringLiteral("file.prevTab"), m_actPrevTab},
        {QStringLiteral("edit.undo"), m_actUndo},
        {QStringLiteral("edit.redo"), m_actRedo},
        {QStringLiteral("edit.cut"), m_actCut},
        {QStringLiteral("edit.copy"), m_actCopy},
        {QStringLiteral("edit.paste"), m_actPaste},
        {QStringLiteral("edit.delete"), m_actDelete},
        {QStringLiteral("edit.rotate"), m_actRotate},
        {QStringLiteral("edit.selectAll"), m_actSelectAll},
        {QStringLiteral("view.zoomIn"), m_actZoomIn},
        {QStringLiteral("view.zoomOut"), m_actZoomOut},
        {QStringLiteral("view.zoomFit"), m_actZoomFit},
        {QStringLiteral("tool.select"), m_actSelectTool},
        {QStringLiteral("tool.wire"), m_actWireTool},
        {QStringLiteral("tool.cancel"), m_actCancel},
        {QStringLiteral("sim.run"), m_actRun},
        {QStringLiteral("sim.monitor"), m_actMonitor},
    };
    for (const auto& entry : entries) {
        if (!entry.second)
            continue;
        m_shortcutBindings.append({entry.first, entry.second, entry.second->shortcut()});
    }
    // 套用用户改过的值；空字符串表示「不绑定」
    for (const ShortcutBinding& binding : m_shortcutBindings) {
        const QString fallback = binding.defaultKey.toString(QKeySequence::PortableText);
        const QString saved = Shortcuts::load(binding.id, fallback);
        binding.action->setShortcut(saved.isEmpty()
                                        ? QKeySequence()
                                        : QKeySequence(saved, QKeySequence::PortableText));
    }
    refreshKeyHints();
}

void MainWindow::applyShortcutEdits(const QHash<QString, QKeySequence>& edited)
{
    for (const ShortcutBinding& binding : m_shortcutBindings) {
        const QString fallback = binding.defaultKey.toString(QKeySequence::PortableText);
        const QKeySequence sequence = edited.value(binding.id, binding.action->shortcut());
        binding.action->setShortcut(sequence);
        Shortcuts::save(binding.id, sequence.toString(QKeySequence::PortableText), fallback);
    }
    refreshKeyHints();
}

void MainWindow::attachCanvasActions(SchematicView* view)
{
    if (!view)
        return;
    for (QAction* action : {m_actUndo, m_actRedo, m_actCut, m_actCopy, m_actPaste, m_actDelete,
                            m_actRotate, m_actSelectAll, m_actSelectTool, m_actWireTool,
                            m_actCancel}) {
        if (!action)
            continue;   // 构造期间可能还没建出来
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        view->addAction(action);
    }
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(CSM_TR("&File"));
    fileMenu->addAction(m_actNew);
    fileMenu->addAction(m_actOpen);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actSave);
    fileMenu->addAction(m_actSaveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actCloseTab);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actQuit);

    QMenu* editMenu = menuBar()->addMenu(CSM_TR("&Edit"));
    editMenu->addAction(m_actUndo);
    editMenu->addAction(m_actRedo);
    editMenu->addSeparator();
    editMenu->addAction(m_actCut);
    editMenu->addAction(m_actCopy);
    editMenu->addAction(m_actPaste);
    editMenu->addSeparator();
    editMenu->addAction(m_actDelete);
    editMenu->addAction(m_actRotate);
    editMenu->addSeparator();

    QMenu* arrangeMenu = editMenu->addMenu(CSM_TR("Align and Distribute"));
    arrangeMenu->setObjectName(QStringLiteral("menuArrange"));
    arrangeMenu->setIcon(actionIcon(QStringLiteral("arrange"), QStringLiteral("format-align-justify"),
                                    QStyle::SP_FileDialogDetailedView));
    for (QAction* action : m_alignActions)
        arrangeMenu->addAction(action);
    arrangeMenu->addSeparator();
    for (QAction* action : m_distributeActions)
        arrangeMenu->addAction(action);

    editMenu->addSeparator();
    editMenu->addAction(m_actSelectAll);
    editMenu->addSeparator();
    editMenu->addAction(m_actSnapWires);

    editMenu->addSeparator();
    QAction* settingsAction = editMenu->addAction(CSM_TR("Settings…"));
    settingsAction->setIcon(actionIcon(QStringLiteral("settings"), QStringLiteral("preferences-system"),
                                       QStyle::SP_FileDialogListView));
    settingsAction->setShortcut(QKeySequence::Preferences);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);

    QMenu* viewMenu = menuBar()->addMenu(CSM_TR("&View"));
    viewMenu->addAction(m_actNextTab);
    viewMenu->addAction(m_actPrevTab);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actZoomIn);
    viewMenu->addAction(m_actZoomOut);
    viewMenu->addAction(m_actZoomFit);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actGrid);
    viewMenu->addSeparator();
    // 面板开关。三块面板的标题栏上都有 ✕，关掉之后原本没有任何入口能再打开
    viewMenu->setObjectName(QStringLiteral("menuView"));

    QMenu* simMenu = menuBar()->addMenu(CSM_TR("&Simulate"));
    simMenu->addAction(m_actRun);
    simMenu->addAction(m_actMonitor);
    simMenu->addSeparator();

    QMenu* examples = simMenu->addMenu(CSM_TR("Example Circuits"));
    examples->setIcon(actionIcon(QStringLiteral("examples"), QStringLiteral("document-open-recent"),
                                 QStyle::SP_DirOpenIcon));
    for (int i = 0; i < m_exampleTitles.size(); ++i) {
        QAction* action = examples->addAction(
            actionIcon(QStringLiteral("examples"), QStringLiteral("document-open-recent"),
                       QStyle::SP_FileIcon),
            m_exampleTitles.at(i));
        connect(action, &QAction::triggered, this, [this, i] { loadExample(i); });
    }

    QMenu* helpMenu = menuBar()->addMenu(CSM_TR("&Help"));
    helpMenu->addAction(m_actCheckUpdates);
    helpMenu->addSeparator();
    helpMenu->addAction(m_actGuide);
    helpMenu->addAction(m_actAbout);
}

void MainWindow::createTitleBar()
{
    m_titleBar = new TitleBar(this);

    // 标题与三个按钮挂在菜单栏右上角：这样「菜单 + 标题 + 窗口按钮」本来就是
    // 同一行，也不用把 QMenuBar 从 QMainWindow 里搬走（搬走会让 menuBar()
    // 返回一个新建的空菜单栏，所有菜单都找不到）。
    // 左上角放应用图标（和多数桌面软件一样，一眼认出是哪个程序）。
    // 设成透明接收鼠标：拖它也算「拖空白处」，能搬窗口。
    auto* iconBox = new QWidget(this);
    // 竖向撑满菜单栏：否则它只包住图标那么高，底下那条边框会画在图标正下方
    // （看起来像图标下面多了一道短线），而不是和菜单栏的分界线对齐
    iconBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* iconLayout = new QHBoxLayout(iconBox);
    iconLayout->setContentsMargins(8, 0, 8, 0);
    auto* iconLabel = new QLabel(iconBox);
    iconLabel->setObjectName(QStringLiteral("titleBarIcon"));
    QIcon mark = svgIcon(QStringLiteral(":/icons/icon.svg"));
    if (!mark.isNull())
        iconLabel->setPixmap(mark.pixmap(QSize(18, 18)));
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    iconLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);
    // 不要给它画背景和边框：这一格保持透明，菜单栏自己的底色与那条分界线
    // 就会从下面透上来，和两边的线严丝合缝（自己画的话高度对不齐，
    // 图标下面会多出一道短线）
    iconBox->setAttribute(Qt::WA_TransparentForMouseEvents);
    menuBar()->setCornerWidget(iconBox, Qt::TopLeftCorner);

    menuBar()->setCornerWidget(m_titleBar, Qt::TopRightCorner);

    // 顶栏比原生菜单栏高（要和标题、窗口按钮对齐），默认的条目排版会贴着上边。
    // 用内边距把文字压到竖直中线 —— 34 px 减去文字高度，上下各 8 px 正好。
    Theme::styleWidget(menuBar(), [] {
        return QStringLiteral(R"(
QMenuBar {
    background: %1;
    border: none;
    border-bottom: 1px solid %3;
    padding: 0px;
    spacing: 0px;
}
QMenuBar::item {
    background: transparent;
    padding: 8px 8px;
    margin: 0px;
    border: none;
}
QMenuBar::item:selected {
    background: %2;
    border-radius: 5px;
}
)").arg(Theme::surface().name(), Theme::surfaceAlt().name(),
                  Theme::border().name());
    });
    // 菜单栏右侧的空白也算「标题区」：拖它搬窗口，双击最大化
    menuBar()->installEventFilter(this);

    connect(m_titleBar, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(m_titleBar, &TitleBar::maximizeRequested, this, &MainWindow::toggleMaximized);
    connect(m_titleBar, &TitleBar::closeRequested, this, &QWidget::close);
    m_titleBar->setMaximized(isMaximized());
}

void MainWindow::toggleMaximized()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        if (m_titleBar)
            m_titleBar->setMaximized(isMaximized());
    }
}

void MainWindow::createToolBar()
{
    QToolBar* bar = addToolBar(CSM_TR("Main toolbar"));
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
        GroundKind groundKind = GroundKind::Gnd; // 接地符号的种类
        GateKind gateKind = GateKind::And;       // 逻辑门的种类
    };
    const QVector<Entry> entries = {
        // 按分类排好，同类的挨在一起，「全部」列表也有顺序
        {ComponentType::Resistor, CSM_TR("Resistor"), CSM_TR("Basic")},
        {ComponentType::Capacitor, CSM_TR("Capacitor"), CSM_TR("Basic")},
        {ComponentType::Inductor, CSM_TR("Inductor"), CSM_TR("Basic")},
        {ComponentType::Transformer, CSM_TR("Transformer"), CSM_TR("Basic")},

        {ComponentType::Diode, CSM_TR("Diode"), CSM_TR("Semiconductors")},
        {ComponentType::Zener, CSM_TR("Zener diode"), CSM_TR("Semiconductors")},
        {ComponentType::Led, CSM_TR("LED"), CSM_TR("Semiconductors")},
        {ComponentType::Npn, CSM_TR("NPN transistor"), CSM_TR("Semiconductors")},
        {ComponentType::Pnp, CSM_TR("PNP transistor"), CSM_TR("Semiconductors")},
        {ComponentType::Nmos, CSM_TR("N-MOSFET"), CSM_TR("Semiconductors")},
        {ComponentType::Pmos, CSM_TR("P-MOSFET"), CSM_TR("Semiconductors")},
        {ComponentType::Igbt, CSM_TR("IGBT"), CSM_TR("Semiconductors")},

        {ComponentType::OpAmp, CSM_TR("Op-amp"), CSM_TR("Analog ICs")},
        {ComponentType::Tl431, CSM_TR("TL431 reference"), CSM_TR("Analog ICs")},
        {ComponentType::Optocoupler, CSM_TR("Optocoupler"), CSM_TR("Analog ICs")},
        {ComponentType::Ob2362, CSM_TR("OB2362 controller"), CSM_TR("Analog ICs")},

        {ComponentType::LogicGate, CSM_TR("AND gate"), CSM_TR("Digital logic"),
         GroundKind::Gnd, GateKind::And},
        {ComponentType::LogicGate, CSM_TR("OR gate"), CSM_TR("Digital logic"),
         GroundKind::Gnd, GateKind::Or},
        {ComponentType::LogicGate, CSM_TR("NOT gate"), CSM_TR("Digital logic"),
         GroundKind::Gnd, GateKind::Not},
        {ComponentType::LogicGate, CSM_TR("XOR gate"), CSM_TR("Digital logic"),
         GroundKind::Gnd, GateKind::Xor},
        {ComponentType::LogicGate, CSM_TR("XNOR gate"), CSM_TR("Digital logic"),
         GroundKind::Gnd, GateKind::Xnor},

        {ComponentType::VoltageSource, CSM_TR("Voltage source"), CSM_TR("Sources")},
        {ComponentType::CurrentSource, CSM_TR("Current source"), CSM_TR("Sources")},
        {ComponentType::SignalGenerator, CSM_TR("Signal generator"), CSM_TR("Sources")},

        {ComponentType::Switch, CSM_TR("Switch"), CSM_TR("Switches and Grounds")},
        {ComponentType::PushButton, CSM_TR("Push button"), CSM_TR("Switches and Grounds")},
        {ComponentType::Ground, CSM_TR("Ground"), CSM_TR("Switches and Grounds"),
         GroundKind::Gnd},
        {ComponentType::NetLabel, CSM_TR("Net label"), CSM_TR("Switches and Grounds")},

        {ComponentType::MeasureProbe, CSM_TR("Measurement probe"), CSM_TR("Measurement")},
        {ComponentType::Probe, CSM_TR("Voltage probe"), CSM_TR("Measurement")},
        {ComponentType::CurrentProbe, CSM_TR("Current probe"), CSM_TR("Measurement")},
        {ComponentType::Multimeter, CSM_TR("Multimeter"), CSM_TR("Measurement")},
    };

    const QString category = m_libraryCategory ? m_libraryCategory->currentText() : QString();
    // 注意用 QStringLiteral：QLatin1String 会把 UTF-8 字节按 Latin-1 解释，
    // 与下拉框里的 QString 永远不相等，列表会变成空的
    const bool showAll = category.isEmpty() || category == CSM_TR("All");

    for (const Entry& entry : entries) {
        if (!showAll && entry.category != category)
            continue;
        // 图标按「具体这一个元件」画：五种逻辑门、五种接地符号长得都不一样，
        // 只给 ComponentType 的话五条会全画成同一个样子
        Component proto;
        proto.type = entry.type;
        proto.gateKind = entry.gateKind;
        proto.groundKind = entry.groundKind;
        auto* item = new QListWidgetItem(componentIcon(proto), entry.text, m_library);
        item->setData(Qt::UserRole, int(entry.type));
        item->setData(Qt::UserRole + 1, int(entry.groundKind));
        item->setData(Qt::UserRole + 2, int(entry.gateKind));
        item->setSizeHint(QSize(0, 46));
    }
}

void MainWindow::createDocks()
{
    auto* libraryDock = new QDockWidget(CSM_TR("Library"), this);
    libraryDock->setObjectName(QStringLiteral("libraryDock"));
    libraryDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* libraryContainer = new QWidget(libraryDock);
    auto* libraryLayout = new QVBoxLayout(libraryContainer);
    libraryLayout->setContentsMargins(6, 6, 6, 6);
    libraryLayout->setSpacing(6);

    m_libraryCategory = new QComboBox(libraryContainer);
    m_libraryCategory->addItem(CSM_TR("All"));
    // 按「器件性质」分，别把 11 个东西都堆在「半导体」里
    for (const QString& name : {CSM_TR("Basic"), CSM_TR("Semiconductors"),
                                CSM_TR("Analog ICs"), CSM_TR("Digital logic"),
                                CSM_TR("Sources"), CSM_TR("Switches and Grounds"),
                                CSM_TR("Measurement")}) {
        m_libraryCategory->addItem(name);
    }
    m_libraryCategory->setToolTip(CSM_TR("Filter parts by category"));
    libraryLayout->addWidget(m_libraryCategory);

    auto* hint = new QLabel(CSM_TR("Drag parts onto the canvas"), libraryContainer);
    hint->setWordWrap(true);
    Theme::styleWidget(hint, [] { return QStringLiteral("color:%1; font-size:11px;").arg(Theme::textMuted().name()); });
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

    auto* propsDock = new QDockWidget(CSM_TR("Properties"), this);
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

    auto* logDock = new QDockWidget(CSM_TR("Messages"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    m_logDock = logDock;
    m_log = new QPlainTextEdit(logDock);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    Theme::styleWidget(m_log, [] {
        return QStringLiteral("background:%1; color:%2; font-family:monospace;")
            .arg(Theme::surfaceAlt().name(), Theme::textSecondary().name());
    });
    logDock->setWidget(m_log);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    logDock->setMaximumHeight(170);

    // 菜单建好之后才能往里挂（createMenus 在 createDocks 之前跑）
    if (QMenu* viewMenu = menuBar()->findChild<QMenu*>(QStringLiteral("menuView"))) {
        for (QDockWidget* dock : findChildren<QDockWidget*>()) {
            if (dock->objectName() == QStringLiteral("drawerDock"))
                continue;
            QAction* toggle = dock->toggleViewAction();
            // 三块面板各有各的图标，菜单里一眼能认出是哪块
            if (dock->objectName() == QStringLiteral("libraryDock"))
                toggle->setIcon(actionIcon(QStringLiteral("library"),
                                           QStringLiteral("view-list-details"),
                                           QStyle::SP_FileDialogListView));
            else if (dock->objectName() == QStringLiteral("propsDock"))
                toggle->setIcon(actionIcon(QStringLiteral("properties"),
                                           QStringLiteral("document-properties"),
                                           QStyle::SP_FileDialogInfoView));
            else if (dock->objectName() == QStringLiteral("logDock"))
                toggle->setIcon(actionIcon(QStringLiteral("messages"),
                                           QStringLiteral("dialog-information"),
                                           QStyle::SP_MessageBoxInformation));
            viewMenu->addAction(toggle);
        }
    }
}

void MainWindow::applyTheme()
{
    Theme::apply(*qApp);
    // 画布颜色是每次绘制时向 Theme 取的，这里只需要让它重画一次
    for (Document* document : m_documents) {
        if (document->scene)
            document->scene->invalidate(document->scene->sceneRect(), QGraphicsScene::BackgroundLayer);
        if (document->scene)
            document->scene->update();
    }
    if (m_wavePlot)
        m_wavePlot->applyTheme();
    for (Document* document : m_documents) {
        if (document && document->liveScope)
            document->liveScope->applyTheme();
    }

    // 工具栏 / 菜单图标是按主题着色渲染的，切主题要按名字重新生成
    for (QAction* action : findChildren<QAction*>()) {
        const auto spec = iconSpecs().value(action->icon().cacheKey());
        if (spec.name.isEmpty())
            continue;
        action->setIcon(buildActionIcon(spec));
    }
    // 元件库的图标同样是渲染出来的，重建一遍列表
    if (m_library) {
        m_library->clear();
        buildLibrary();
    }

    for (QWidget* widget : QApplication::topLevelWidgets())
        widget->update();
}

void MainWindow::createStatusBar()
{
    m_hintLabel = new QLabel(
        CSM_TR("Select mode: drag to move, double-click to edit, Space to rotate"), this);
    m_coordLabel = new QLabel(QStringLiteral("x: 0   y: 0"), this);
    m_coordLabel->setMinimumWidth(120);
    m_analysisLabel = new QLabel(this);
    Theme::styleWidget(m_analysisLabel, [] { return QStringLiteral("color:%1;").arg(Theme::textSecondary().name()); });

    statusBar()->addWidget(m_hintLabel, 1);
    statusBar()->addPermanentWidget(m_analysisLabel);
    statusBar()->addPermanentWidget(m_coordLabel);
    updateAnalysisLabel();
}

void MainWindow::updateAnalysisLabel()
{
    QString text;
    switch (doc()->options.kind) {
    case AnalysisKind::OperatingPoint:
        text = CSM_TR("Analysis: DC operating point");
        break;
    case AnalysisKind::Transient:
        text = CSM_TR("Analysis: transient %1 / step %2")
                   .arg(formatEng(doc()->options.tStop, QStringLiteral("s"), 3),
                        formatEng(doc()->options.tStep, QStringLiteral("s"), 3));
        break;
    case AnalysisKind::AC:
        text = CSM_TR("Analysis: AC %1 ~ %2")
                   .arg(formatEng(doc()->options.fStart, QStringLiteral("Hz"), 3),
                        formatEng(doc()->options.fStop, QStringLiteral("Hz"), 3));
        break;
    }
    m_analysisLabel->setText(text);
}

MainWindow::~MainWindow()
{
    m_destroying = true;

    // 子控件在 ~QWidget() 里被销毁时会失去焦点，QLineEdit 因此发出
    // editingFinished → PropertyPanel::changed → 本窗口的槽；此时 MainWindow
    // 的成员（doc()->history、m_scene…）已经析构，回调里访问它们就是 use-after-free。
    // 销毁前先把这些连接断掉。
    if (m_props)
        m_props->disconnect(this);
    if (m_scene)
        m_scene->disconnect(this);
    if (m_view)
        m_view->disconnect(this);
    if (m_wavePlot)
        m_wavePlot->disconnect(this);
    for (Document* document : m_documents) {
        if (document && document->liveScope)
            document->liveScope->disconnect(this);
    }
}

void MainWindow::pushHistory()
{
    if (m_destroying || m_loading || doc()->restoring)
        return;

    const Circuit circuit = m_scene->toCircuit();
    const QByteArray signature = circuitSignature(circuit);
    if (doc()->historyIndex >= 0 && doc()->historyIndex < doc()->historySignatures.size()
        && doc()->historySignatures.at(doc()->historyIndex) == signature)
        return; // 内容没变，不产生新记录

    // 丢弃重做分支
    while (doc()->history.size() > doc()->historyIndex + 1) {
        doc()->history.removeLast();
        doc()->historySignatures.removeLast();
    }

    doc()->history.append(circuit.toJson());
    doc()->historySignatures.append(signature);

    constexpr int kMaxHistory = 200;
    while (doc()->history.size() > kMaxHistory) {
        doc()->history.removeFirst();
        doc()->historySignatures.removeFirst();
    }
    doc()->historyIndex = doc()->history.size() - 1;
    updateHistoryActions();
}

void MainWindow::resetHistory()
{
    doc()->history.clear();
    doc()->historySignatures.clear();
    doc()->historyIndex = -1;
    pushHistory();
    updateHistoryActions();
}

void MainWindow::restoreSnapshot(const QJsonObject& snapshot)
{
    QString error;
    const Circuit circuit = Circuit::fromJson(snapshot, &error);
    if (!error.isEmpty()) {
        appendLog(CSM_TR("⚠ Could not restore the previous state: %1").arg(error));
        return;
    }

    doc()->restoring = true;
    closeMeterWindows();
    m_scene->load(circuit);
    doc()->restoring = false;

    m_props->setComponent(nullptr);
    setDirty(true);
}

void MainWindow::undo()
{
    if (doc()->historyIndex <= 0)
        return;
    --doc()->historyIndex;
    restoreSnapshot(doc()->history.at(doc()->historyIndex));
    updateHistoryActions();
    appendLog(CSM_TR("Undone (%1/%2)")
                  .arg(doc()->historyIndex + 1)
                  .arg(doc()->history.size()));
}

void MainWindow::redo()
{
    if (doc()->historyIndex < 0 || doc()->historyIndex >= doc()->history.size() - 1)
        return;
    ++doc()->historyIndex;
    restoreSnapshot(doc()->history.at(doc()->historyIndex));
    updateHistoryActions();
    appendLog(CSM_TR("Redone (%1/%2)")
                  .arg(doc()->historyIndex + 1)
                  .arg(doc()->history.size()));
}

void MainWindow::updateHistoryActions()
{
    if (!m_actUndo || !m_actRedo)
        return;
    m_actUndo->setEnabled(doc()->historyIndex > 0);
    m_actRedo->setEnabled(doc()->historyIndex >= 0 && doc()->historyIndex < doc()->history.size() - 1);
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
    if (item && item->component().type == ComponentType::Multimeter) {
        if (meterReadoutAvailable()) {
            openMeterWindow(item);
        } else if (!m_meterHintShown) {
            // 只说一次：读数窗口在没跑仿真时全是横杠，开了也没用
            m_meterHintShown = true;
            appendLog(CSM_TR("Tip: the multimeter window opens only while the live simulation runs. "
                             "Press %1, then click it.")
                          .arg(keyLabel(m_actRun)));
        }
    }
    const int selectedCount = m_scene->selectedComponents().size();
    for (QAction* action : m_alignActions)
        action->setEnabled(selectedCount >= 2);
    for (QAction* action : m_distributeActions)
        action->setEnabled(selectedCount >= 3);

    m_actDelete->setEnabled(!selected.isEmpty());
    m_actCut->setEnabled(!selected.isEmpty());
    m_actCopy->setEnabled(!selected.isEmpty());
    m_actPaste->setEnabled(m_scene->hasClipboard());
    m_actRotate->setEnabled(item != nullptr && supportsRotation(item->component().type));
}

bool MainWindow::monitorAvailable() const
{
    if (!m_scene)
        return false;
    for (const Component& comp : m_scene->toCircuit().components) {
        if (comp.type == ComponentType::Ground)
            return true;
    }
    return false;
}

bool MainWindow::meterReadoutAvailable() const
{
    // 只有实时仿真在跑的时候读数才有意义（静态结果那份留在波形页里看）
    return m_waveLive && m_waveLive->isLive();
}

void MainWindow::pressPushButton(ComponentItem* item)
{
    if (!item || item->component().type != ComponentType::PushButton)
        return;
    // 按下 = 与静止状态相反：常开的接通，常闭的断开
    const Component comp = item->component();
    item->setClosed(!comp.normallyClosed);
    appendLog(CSM_TR("Button %1 pressed (%2)")
                  .arg(comp.refdes, comp.normallyClosed ? CSM_TR("Open")
                                                        : CSM_TR("Closed")));
}

void MainWindow::releasePushButton(ComponentItem* item)
{
    if (!item || item->component().type != ComponentType::PushButton)
        return;
    item->setClosed(item->component().normallyClosed);
}

void MainWindow::restorePushButtons()
{
    if (!m_scene)
        return;
    for (ComponentItem* item : m_scene->componentItems()) {
        if (item->component().type == ComponentType::PushButton)
            item->setClosed(item->component().normallyClosed);
    }
}

void MainWindow::toggleSwitch(ComponentItem* item)
{
    if (!item || item->component().type != ComponentType::Switch)
        return;

    Component comp = item->component();
    comp.closed = !comp.closed;
    item->setClosed(comp.closed); // 符号与模型一起改，画布立刻看到通/断
    m_scene->commitEdit();        // 记进撤销历史；circuitChanged 会带着它推给实时引擎
    appendLog(CSM_TR("Switch %1 %2")
                  .arg(comp.refdes, comp.closed ? CSM_TR("Closed")
                                                : CSM_TR("Open")));
}

void MainWindow::openMeterWindow(ComponentItem* item)
{
    if (!item || item->component().type != ComponentType::Multimeter)
        return;
    const QString refdes = item->component().refdes;
    if (auto* existing = m_meterWindows.value(refdes)) {
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto* window = new MultimeterWindow(refdes, item->component().multimeterMode, this);
    connect(window, &MultimeterWindow::closed, this, [this](MultimeterWindow* w) {
        m_meterWindows.remove(w->refdes());
    });
    m_meterWindows.insert(refdes, window);
    window->show();
    refreshMeterWindows();
}

void MainWindow::refreshMeterWindows()
{
    if (m_meterWindows.isEmpty())
        return;

    // 元件被删掉时顺手关掉它的窗口
    QSet<QString> present;
    for (const Component& comp : m_scene->toCircuit().components) {
        if (comp.type == ComponentType::Multimeter)
            present.insert(comp.refdes);
    }
    for (auto it = m_meterWindows.begin(); it != m_meterWindows.end();) {
        if (!present.contains(it.key())) {
            (*it)->deleteLater();
            it = m_meterWindows.erase(it);
        } else {
            ++it;
        }
    }

    // 实时仿真进行中：每个时钟从实时引擎取当前值；
    // 否则用静态仿真的末次结果（没有结果时窗口保持横杠）
    // 档位在属性面板里可能改过，读数窗口要跟着变（表头文字、单位、符号位）
    for (auto it = m_meterWindows.constBegin(); it != m_meterWindows.constEnd(); ++it) {
        for (const Component& comp : m_scene->toCircuit().components) {
            if (comp.type == ComponentType::Multimeter && comp.refdes == it.key()) {
                it.value()->setMode(comp.multimeterMode);
                break;
            }
        }
    }

    for (auto it = m_meterWindows.constBegin(); it != m_meterWindows.constEnd(); ++it) {
        const QString refdes = it.key();
        if (m_waveLive && m_waveLive->isLive()) {
            it.value()->setValue(m_waveLive->liveValue(refdes));
            continue;
        }
        if (doc()->hasResult && !doc()->resultStale && doc()->lastResult.waves.size() > 0) {
            const Signal* sig = doc()->lastResult.signal(refdes);
            if (sig && !sig->y.isEmpty()) {
                it.value()->setValue(sig->y.last());
                continue;
            }
        }
        it.value()->setValue(std::numeric_limits<double>::quiet_NaN());
    }
}

void MainWindow::closeMeterWindows()
{
    for (auto it = m_meterWindows.begin(); it != m_meterWindows.end(); ++it)
        (*it)->deleteLater();
    m_meterWindows.clear();
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
    if (doc()->dirty == dirty)
        return;
    doc()->dirty = dirty;
    updateTitle();
}

void MainWindow::updateTitle()
{
    Document* document = doc();
    if (!document)
        return;
    setWindowTitle(CSM_TR("%1%2 — Circuit Master")
                       .arg(document->name,
                            document->dirty ? QStringLiteral(" *") : QString()));
    // 标签页上的文字也要跟着变（名字改了、或者多出 / 去掉星号）
    if (m_tabs && document->view) {
        const int index = m_tabs->indexOf(document->view);
        if (index >= 0)
            m_tabs->setTabText(index, document->tabTitle());
    }
}

void MainWindow::appendLog(const QString& text)
{
    if (m_log)
        m_log->appendPlainText(text);
}

Document* MainWindow::loadCircuitData(const Circuit& circuit, const QString& title,
                                      const QString& path, const SimulationOptions& options)
{
    // 当前标签如果还是「干净的空文档」，就直接用它 —— 不要在它旁边再开一个空标签，
    // 那样用户每次打开文件都会多出一个什么都不含的页
    Document* target = doc();
    const bool reuse = target && target->name == CSM_TR("Untitled")
        && target->filePath.isEmpty() && !target->dirty
        && target->scene->toCircuit().components.isEmpty();
    if (!reuse)
        target = addDocument(title, path);
    else
        target->name = title, target->filePath = path;

    target->options = options;

    m_loading = true;
    closeMeterWindows();
    target->scene->load(circuit);
    m_loading = false;

    // 关闭标签页时那一份动态示波器是被销毁的（见 closeDocument）。这里复用的
    // 文档如果正是它，m_waveLive 还指着空 —— 之后 startLiveScope() 会被跳过，
    // 表现出来就是「点了运行仿真，按钮不变、波形也出不来」。重新绑一次。
    bindLiveScope(target);

    target->dirty = false;
    target->hasResult = false;
    target->resultStale = false;
    target->lastResult = SimulationResult();
    if (m_props)
        m_props->setComponent(nullptr);
    clearLedStates();
    clearBlast();
    if (m_waveLive)
        m_waveLive->stopLiveScope();
    m_liveDoc = nullptr;
    if (m_actMonitor) {
        m_actMonitor->setEnabled(false);
        m_actMonitor->setChecked(false);
    }
    updateTabTitle(target);
    updateAnalysisLabel();
    syncMonitorOptions();
    updateRunAction();
    resetHistory();

    QTimer::singleShot(0, this, [this, target] {
        if (target == doc() && m_view)
            m_view->zoomFit();
    });
    return target;
}

void MainWindow::newCircuit()
{
    // 新建就是新开一个标签，不碰当前文档，所以不用问「是否保存」
    addDocument();
    appendLog(CSM_TR("New blank circuit (new tab)."));
}

bool MainWindow::writeDocumentToDisk(Document* document, QString* error)
{
    if (!document || document->filePath.isEmpty())
        return false;

    QJsonObject root = document->scene->toCircuit().toJson();
    root.insert(QStringLiteral("analysis"), analysisToJson(document->options));
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);

    // 原子写：先写同目录的临时文件，成功才替换目标。直接用 QFile 的话会先把
    // .csm 截成 0 字节，写到一半崩溃、断电或磁盘满就只剩半截。
    QString writeError;
    if (!SafeWrite::bytes(document->filePath, data, &writeError)) {
        if (error)
            *error = writeError.isEmpty() ? CSM_TR("Unknown error") : writeError;
        return false;
    }

    document->dirty = false;
    Autosave::remove(document->recoveryId);   // 已经存回文件，恢复副本可以删了
    if (document == doc())
        updateTitle();
    else
        updateTabTitle(document);
    return true;
}

bool MainWindow::saveCircuit()
{
    if (doc()->filePath.isEmpty())
        return saveCircuitAs();

    QString error;
    if (!writeDocumentToDisk(doc(), &error)) {
        Dialogs::warn(this, CSM_TR("Save failed"), error);
        return false;
    }
    appendLog(CSM_TR("Saved: %1").arg(doc()->filePath));
    return true;
}

bool MainWindow::saveCircuitAs()
{
    // 用 QFileDialog 实例而不是静态函数：需要知道用户选的是哪个过滤器，
    // 否则「选了 PNG、却没输扩展名」会被当成另存 .csm
    QFileDialog dialog(this, CSM_TR("Save Circuit"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters({CSM_TR("Circuit Master circuit (*.csm)"),
                           CSM_TR("PNG image (*.png)")});
    dialog.selectFile(doc()->name + QStringLiteral(".csm"));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return false;

    QString path = dialog.selectedFiles().constFirst();
    const bool wantsPng = dialog.selectedNameFilter().contains(QLatin1String(".png"))
                          || path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive);
    if (wantsPng) {
        if (!path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)) {
            // 默认名是 xxx.csm，换到 PNG 过滤器后别变成 xxx.csm.png
            if (path.endsWith(QLatin1String(".csm"), Qt::CaseInsensitive))
                path.chop(4);
            path += QStringLiteral(".png");
        }
        return exportSchematicImage(path);
    }

    if (!path.endsWith(QLatin1String(".csm"), Qt::CaseInsensitive))
        path += QStringLiteral(".csm");

    doc()->filePath = path;
    doc()->name = QFileInfo(path).fileName();
    return saveCircuit();
}

bool MainWindow::exportSchematicImage(const QString& path)
{
    // 按所有元件的包围盒算，不是当前视口 —— 缩放过 / 滚动过也不影响导出内容
    QRectF bounds = m_scene->itemsBoundingRect();
    if (bounds.isEmpty()) {
        Dialogs::warn(this, CSM_TR("Export failed"),
                      CSM_TR("There is nothing on the schematic to export."));
        return false;
    }
    const qreal margin = 20.0;
    bounds.adjust(-margin, -margin, margin, margin);

    // 2 倍像素：默认 DPI 下文字和 1 px 的网格会糊
    constexpr qreal kScale = 2.0;
    QImage image(int(std::ceil(bounds.width() * kScale)),
                 int(std::ceil(bounds.height() * kScale)),
                 QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        Dialogs::warn(this, CSM_TR("Export failed"), CSM_TR("Cannot write the image file."));
        return false;
    }
    image.setDevicePixelRatio(kScale);
    image.fill(Theme::canvas());

    // 选中态带高亮框，导出时不该出现在图里
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    m_scene->clearSelection();

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    m_scene->render(&painter, QRectF(QPointF(0, 0), bounds.size()), bounds);
    painter.end();

    for (QGraphicsItem* item : selected)
        item->setSelected(true);

    QString writeError;
    if (!SafeWrite::image(image, path, &writeError)) {
        Dialogs::warn(this, CSM_TR("Export failed"),
                      writeError.isEmpty() ? CSM_TR("Cannot write the image file.") : writeError);
        return false;
    }
    appendLog(CSM_TR("Exported: %1").arg(path));
    return true;
}

void MainWindow::openCircuit()
{
    const QString path = QFileDialog::getOpenFileName(
        this, CSM_TR("Open Circuit"), QString(),
        CSM_TR("Circuit Master circuit (*.csm);;All files (*)"));
    if (path.isEmpty())
        return;

    openPath(path);
}

bool MainWindow::openPath(const QString& path)
{
    // 已经开着就切过去，别开两份
    const int existing = indexOfPath(path);
    if (existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        appendLog(CSM_TR("That file is already open in a tab: %1").arg(path));
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Dialogs::warn(this, CSM_TR("Open failed"), file.errorString());
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if (json.isNull()) {
        Dialogs::warn(this, CSM_TR("Open failed"),
                      CSM_TR("JSON parse error: %1").arg(parseError.errorString()));
        return false;
    }

    QString error;
    const QJsonObject root = json.object();
    const Circuit circuit = Circuit::fromJson(root, &error);
    if (!error.isEmpty()) {
        Dialogs::warn(this, CSM_TR("Open failed"), error);
        return false;
    }
    if (circuit.components.isEmpty()) {
        Dialogs::warn(this, CSM_TR("Open failed"), CSM_TR("The file has no components."));
        return false;
    }

    SimulationOptions options = doc() ? doc()->options : SimulationOptions();
    const QJsonObject analysis = root.value(QStringLiteral("analysis")).toObject();
    if (!analysis.isEmpty())
        options = optionsFromJson(analysis);

    loadCircuitData(circuit, QFileInfo(path).fileName(), path, options);
    appendLog(CSM_TR("Opened: %1 (%2 components)")
                  .arg(path)
                  .arg(circuit.components.size()));
    return true;
}

void MainWindow::runSimulation()
{
    const Circuit circuit = m_scene->toCircuit();
    if (circuit.components.isEmpty()) {
        Dialogs::info(this, CSM_TR("Cannot simulate"),
                                 CSM_TR("Place some parts and wire them up first."));
        return;
    }

    const Netlist net = circuit.buildNetlist();
    if (!net.valid) {
        Dialogs::warn(this, CSM_TR("Circuit check failed"), net.error);
        return;
    }
    for (const QString& warning : net.warnings)
        appendLog(QStringLiteral("⚠ ") + warning);

    // 运行仿真只做计算，不自动弹任何窗口：波形由「静态波形」/「动态示波器」显式打开
    QApplication::setOverrideCursor(Qt::WaitCursor);
    SimulationResult result = Simulator::run(circuit, doc()->options);
    QApplication::restoreOverrideCursor();

    appendLog(CSM_TR("──────── Simulation ────────"));
    for (const QString& line : result.log)
        appendLog(line);

    if (!result.ok) {
        appendLog(CSM_TR("✘ Simulation failed: ") + result.error);
        Dialogs::error(this, CSM_TR("Simulation failed"), result.error);
        return;
    }

    appendLog(CSM_TR("✔ Done in %1 ms").arg(QString::number(result.elapsedMs, 'f', 1)));

    // 先点亮 LED / 播放爆炸：工作点分支会直接弹对话框返回，放在后面就轮不到它了
    applyLedStates(result);
    applyProbeReadouts(result);
    applyBlownCapacitors(result.blownCapacitors);

    // 波形页只画测试点；万用表不进波形页，但它在实时仿真里要出读数，
    // 所以「要不要启动实时引擎」得单独看有没有万用表
    const bool hasProbes = applyProbeSignals(circuit, net, result);
    bool hasMeter = false;
    for (const Component& comp : circuit.components) {
        if (comp.type == ComponentType::Multimeter)
            hasMeter = true;
    }
    if (!hasProbes && !hasMeter && result.kind != AnalysisKind::OperatingPoint)
        appendLog(CSM_TR("⚠ No test points in the circuit, the waveform is empty. Place a voltage (TP) or current (IP) test point."));

    // 结果留着，由用户打开「波形监控」再看；面板开着就顺手刷新
    doc()->lastResult = result;
    doc()->hasResult = true;
    doc()->resultStale = false;
    if (m_wavePlot && m_waveWindow && m_waveWindow->isVisible() && m_waveTabs
        && m_waveTabs->currentIndex() == 0)
        m_wavePlot->setResult(doc()->lastResult);
    refreshMeterWindows();

    // 有测试点就进入「运行中」状态（连续仿真），与「运行仿真 / 停止仿真」按钮语义一致。
    // 放在工作点分支之前：否则算完直流工作点就 return，按钮会停在「运行仿真」上，
    // 看起来像「点了就停」。
    if ((hasProbes || hasMeter) && m_waveLive && !m_waveLive->isLive())
        startLiveScope();

    if (result.kind == AnalysisKind::OperatingPoint) {
        // 不再自动切标签页：用户在静态页点「重新仿真」时就该留在静态页看报告
        // 不自动弹报告：结果写进日志，需要时从「仿真 → 直流工作点报告…」打开
        for (const QString& line : result.report.split(QLatin1Char('\n'))) {
            if (!line.trimmed().isEmpty())
                appendLog(line);
        }
        appendLog(CSM_TR("DC operating point computed; the report is in Waveform Monitor → Static Waveforms; "
                         "switch to Live Scope to see continuous waveforms."));
        return;
    }

    // 开始连续仿真，但不自动弹窗口：要看就点「波形监控」。
    // 有没有测试点都照跑，只是没测试点时波形是空的。
    if (m_waveLive && !m_waveLive->isLive())
        startLiveScope();
    if (hasProbes) {
        appendLog(CSM_TR("Simulation running: click Waveform Monitor to see the traces, Stop Simulation to end."));
    } else {
        appendLog(CSM_TR("Simulation is running, but the circuit has no test points so the waveform is empty. "
                         "Place a TP / IP to see curves."));
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // 关掉波形监控窗口只是收起界面，仿真继续跑，随时可以再打开
    if (watched == m_waveWindow && event->type() == QEvent::Close) {
        if (m_actMonitor)
            m_actMonitor->setChecked(false);
    }

    // 菜单栏的空白处当标题栏用：拖动搬窗口、双击最大化。
    // 落在菜单项上就放过，那是要点菜单的。
    if (watched == menuBar()) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const bool onBlank = (event->type() == QEvent::MouseButtonPress
                              || event->type() == QEvent::MouseMove
                              || event->type() == QEvent::MouseButtonRelease
                              || event->type() == QEvent::MouseButtonDblClick)
                             && !menuBar()->actionAt(mouse->position().toPoint());
        if (onBlank) {
            switch (event->type()) {
            case QEvent::MouseButtonPress:
                if (mouse->button() == Qt::LeftButton) {
                    // 优先交给窗口管理器；不支持时自己按偏移量搬
                    QWindow* handle = windowHandle();
                    if (!handle || !handle->startSystemMove()) {
                        m_menuDragging = true;
                        m_menuDragOffset = mouse->globalPosition().toPoint()
                                           - frameGeometry().topLeft();
                    }
                    return true;
                }
                break;
            case QEvent::MouseMove:
                if (m_menuDragging && (mouse->buttons() & Qt::LeftButton)) {
                    move(mouse->globalPosition().toPoint() - m_menuDragOffset);
                    return true;
                }
                break;
            case QEvent::MouseButtonRelease:
                m_menuDragging = false;
                break;
            case QEvent::MouseButtonDblClick:
                if (mouse->button() == Qt::LeftButton) {
                    toggleMaximized();
                    return true;
                }
                break;
            default:
                break;
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::syncMonitorOptions()
{
    if (m_wavePlot)
        m_wavePlot->setOptions(doc()->options);
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
        clearBlast();
    }

    if (running) {
        m_actRun->setText(CSM_TR("Stop Simulation"));
        m_actRun->setIcon(actionIcon(QStringLiteral("stop"),
                                     QStringLiteral("media-playback-stop"),
                                     QStyle::SP_MediaStop));
        m_actRun->setToolTip(CSM_TR("Stop the running live scope"));
    } else {
        m_actRun->setText(CSM_TR("Run Simulation"));
        m_actRun->setIcon(actionIcon(QStringLiteral("run"),
                                     QStringLiteral("media-playback-start"),
                                     QStyle::SP_MediaPlay));
        m_actRun->setToolTip(CSM_TR("Run Simulation (F5)"));
    }
}

void MainWindow::createMonitorWindow()
{
    // 独立窗口：不占用主界面，多屏时可以拖到另一块屏上
    m_waveWindow = new QWidget(this, Qt::Window);
    m_waveWindow->setObjectName(QStringLiteral("waveMonitorWindow"));
    m_waveWindow->setWindowTitle(CSM_TR("Waveform Monitor — Circuit Master"));
    m_waveWindow->resize(1040, 620);
    // 和主窗口一样：无边框 + 自绘顶栏，标题跟着窗口标题走
    // （切到「动态示波器」页时它会变）
    auto* windowLayout = makeFramelessShell(*m_waveWindow, CSM_TR("Waveform Monitor"),
                                            QMargins(0, 0, 0, 0), 0, true);
    new ResizeGrips(m_waveWindow);
    // 和万用表、各个对话框一样不在任务栏里单列 —— 它由「波形监控 / F6」开关
    m_waveWindow->setWindowFlag(Qt::Tool, true);

    m_waveTabs = new QTabWidget(m_waveWindow);
    m_wavePlot = new WaveformWindow(m_waveTabs);
    m_waveTabs->addTab(m_wavePlot, CSM_TR("Static Waveforms"));
    bindLiveScope(doc());   // 「动态示波器」页用当前图纸自己的那一份
    m_waveTabs->setCurrentIndex(0);
    windowLayout->addWidget(m_waveTabs);

    m_waveWindow->installEventFilter(this);
    m_waveWindow->hide();

    connect(m_waveTabs, &QTabWidget::currentChanged, this, &MainWindow::onWaveTabChanged);

    syncMonitorOptions();
    connect(m_wavePlot, &WaveformWindow::analysisRequested, this,
            [this](const SimulationOptions& options) {
                doc()->options = options;
                updateAnalysisLabel();
                runSimulation();
            });

    // 电路改动（比如拨动开关）后重建实时引擎，让仿真跟着新拓扑走
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setObjectName(QStringLiteral("autosaveTimer"));
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autosaveNow);

    m_recoveryTimer = new QTimer(this);
    m_recoveryTimer->setObjectName(QStringLiteral("recoveryTimer"));
    connect(m_recoveryTimer, &QTimer::timeout, this, &MainWindow::writeRecoveryCopies);

    applyAutosaveSettings();

    m_liveRestartTimer = new QTimer(this);
    m_liveRestartTimer->setSingleShot(true);
    m_liveRestartTimer->setInterval(200);
    connect(m_liveRestartTimer, &QTimer::timeout, this, [this] {
        if (!m_waveLive || !m_waveLive->isLive())
            return;

        // 优先原地更新：时间轴与已有波形都保留，只把新拓扑/数值喂给引擎。
        // 取发起仿真的那个文档的电路 —— 用户可能已经切到别的标签页了
        SchematicScene* liveScene = m_liveDoc ? m_liveDoc->scene : m_scene;
        const Circuit circuit = liveScene->toCircuit();
        const Netlist net = circuit.buildNetlist();
        if (!net.valid) {
            appendLog(CSM_TR("⚠ The circuit cannot be solved: %1").arg(net.error));
            return;
        }
        const QVector<QPair<QString, QString>> probes = probeSpecs(circuit, net);
        if (!m_waveLive->updateLiveCircuit(circuit, probes)) {
            // 测试点或节点编号变了，只能重启引擎。但只在「发起仿真的那个文档」
            // 正显示着的时候重启 —— 否则会把仿真悄悄换到别的图纸上
            if (m_liveDoc && m_liveDoc == doc()) {
                startLiveScope();
            } else {
                appendLog(CSM_TR("⚠ The circuit changed a lot; the simulation restarts when you switch back"));
            }
        }
    });

    // 实时仿真推进时同步刷新打开的万用表读数窗口
    connect(m_waveLive, &WaveformWindow::liveUpdated, this, [this] {
        refreshMeterWindows();
    });

    // 实时仿真推进时同步刷新 LED 亮度
    connect(m_waveLive, &WaveformWindow::liveUpdated, this, [this] {
        // 仿真可能跑在后台文档上（用户切到了别的标签页），LED 要刷那个文档的
        SchematicScene* ledScene = m_liveDoc ? m_liveDoc->scene : m_scene;
        for (ComponentItem* item : ledScene->componentItems()) {
            if (item->component().type != ComponentType::Led)
                continue;
            const double amps =
                m_waveLive->liveValue(QStringLiteral("I(%1)").arg(item->component().refdes));
            const double i = std::abs(amps);
            item->setLedBrightness(i < 1e-4 ? 0.0
                                            : std::min(1.0, std::log10(i / 1e-4) / 2.0));
        }
        // 实时仿真里也可能把电解电容接反
        const QHash<int, double> blown = m_waveLive->liveBlownCapacitors();
        for (auto it = blown.constBegin(); it != blown.constEnd(); ++it) {
            if (m_blownIds.contains(it.key()))
                continue;
            QHash<int, double> fresh;
            fresh.insert(it.key(), it.value());
            applyBlownCapacitors(fresh);
        }
    });
}

void MainWindow::showMonitor(int tab)
{
    if (!m_waveWindow)
        return;
    if (!monitorAvailable()) {
        // 没有 GND：参考点由万用表负端顶替，节点电压之间没有统一基准，
        // 波形画出来也没有意义。万用表读数窗口照常可用。
        appendLog(CSM_TR("The waveform monitor needs a GND symbol. This circuit has none, so only multimeter readings are available."));
        if (m_actMonitor) {
            const QSignalBlocker blocker(m_actMonitor);
            m_actMonitor->setChecked(false);
        }
        return;
    }
    m_waveWindow->show();
    m_waveWindow->raise();
    m_waveWindow->activateWindow();
    if (m_actMonitor && !m_actMonitor->isChecked()) {
        const QSignalBlocker blocker(m_actMonitor);
        m_actMonitor->setChecked(true);
    }
    if (tab == 0 && doc()->hasResult && m_wavePlot)
        m_wavePlot->setResult(doc()->lastResult);
    if (m_waveTabs)
        m_waveTabs->setCurrentIndex(tab);
}

void MainWindow::onWaveTabChanged(int index)
{
    if (!m_waveWindow)
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
    if (m_wavePlot && doc()->hasResult)
        m_wavePlot->setResult(doc()->lastResult);
}

WaveformWindow* MainWindow::liveScopeFor(Document* document)
{
    if (!document)
        return nullptr;
    if (document->liveScope)
        return document->liveScope;

    // 每个图纸一份：自己的引擎、自己的定时器，切走之后照样推进
    auto* scope = new WaveformWindow(m_waveTabs);
    document->liveScope = scope;

    connect(scope, &WaveformWindow::liveStateChanged, this, [this, document](bool running) {
        updateRunAction();
        if (document == doc() && m_view)
            m_view->setLiveRunning(running);
        if (running && document == doc())
            resetLiveProbeStats();   // 新的一轮实时仿真，统计从头算
        if (!running) {
            if (document == doc()) {
                closeMeterWindows();
                resetLiveProbeStats();
            }
            restorePushButtons();
        }
        if (document == doc() && running && m_actMonitor && !monitorAvailable()) {
            m_actMonitor->setEnabled(false);
            m_actMonitor->setToolTip(CSM_TR("The waveform monitor needs a GND symbol. This circuit has none, so only multimeter readings are available."));
            if (m_actMonitor->isChecked()) {
                const QSignalBlocker blocker(m_actMonitor);
                m_actMonitor->setChecked(false);
            }
            if (m_waveWindow)
                m_waveWindow->hide();
        }
    });

    // 刻度改动记进该文档的 options，保存工程时一并写入
    connect(scope, &WaveformWindow::liveScalesChanged, this,
            [document](double timeDiv, double voltDiv, double ampDiv) {
                document->options.scopeTimeDiv = timeDiv;
                document->options.scopeVoltDiv = voltDiv;
                document->options.scopeAmpDiv = ampDiv;
            });

    // 电路里有 LED / 电解电容时，实时仿真要不断刷新它们的显示
    connect(scope, &WaveformWindow::liveUpdated, this, [this, document] {
        if (document == doc()) {
            refreshMeterWindows();
            refreshLiveProbes();   // 测量探针的读数跟着实时刷新
        }
        refreshDocumentLeds(document);
    });
    return scope;
}

void MainWindow::bindLiveScope(Document* document)
{
    if (!m_waveTabs || !document)
        return;
    WaveformWindow* scope = liveScopeFor(document);

    // 「动态示波器」固定在第 1 页：先把别人的摘下来（不销毁，它自己的定时器
    // 继续跑），再插自己的
    for (int i = m_waveTabs->count() - 1; i >= 1; --i) {
        if (m_waveTabs->widget(i) != scope)
            m_waveTabs->removeTab(i);
    }
    if (m_waveTabs->indexOf(scope) < 0)
        m_waveTabs->insertTab(1, scope, CSM_TR("Live Scope"));

    m_waveLive = scope;
    m_liveDoc = document;
}

void MainWindow::refreshDocumentLeds(Document* document)
{
    if (!document || !document->liveScope || !document->scene)
        return;
    WaveformWindow* scope = document->liveScope;
    if (!scope->isLive())
        return;

    for (ComponentItem* item : document->scene->componentItems()) {
        if (item->component().type != ComponentType::Led)
            continue;
        const QString name = QStringLiteral("I(%1)").arg(item->component().refdes);
        const double amps = std::abs(scope->liveValue(name));
        item->setLedBrightness(amps < 1e-4 ? 0.0
                                          : std::min(1.0, std::log10(amps / 1e-4) / 2.0));
    }

    // 实时仿真里也可能把电解电容接反
    const QHash<int, double> blown = scope->liveBlownCapacitors();
    QHash<int, double> fresh;
    for (auto it = blown.constBegin(); it != blown.constEnd(); ++it) {
        if (!m_blownIds.contains(it.key()))
            fresh.insert(it.key(), it.value());
    }
    if (!fresh.isEmpty())
        applyBlownCapacitors(fresh);
}

bool MainWindow::startLiveScope()
{
    const Circuit circuit = m_scene->toCircuit();
    const Netlist net = circuit.buildNetlist();
    if (!net.valid) {
        Dialogs::warn(this, CSM_TR("Cannot start the live scope"), net.error);
        return false;
    }

    // 没有测试点也照样启动，只是波形为空（面板里会提示放 TP / IP）
    const QVector<QPair<QString, QString>> probes = probeSpecs(circuit, net);

    m_waveLive->setLiveScalePreferences(doc()->options.scopeTimeDiv, doc()->options.scopeVoltDiv,
                                        doc()->options.scopeAmpDiv);
    m_waveLive->startLive(circuit, probes);
    m_liveDoc = doc();   // 这条仿真属于当前文档
    appendLog(CSM_TR("Live scope started: it keeps simulating; switch back to Static Waveforms or close the panel to stop."));
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

        const QString name = exampleDisplayName(root, file);
        m_examplePaths.append(dir.filePath(file));
        // 不拼分析类型：菜单里只列电路名，类型在打开后的分析标签上能看到
        m_exampleTitles.append(name);
    }
}

void MainWindow::loadExample(int index)
{
    if (index < 0 || index >= m_examplePaths.size())
        return;

    QFile file(m_examplePaths.at(index));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Dialogs::warn(this, CSM_TR("Cannot open the example"), file.errorString());
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if (json.isNull()) {
        Dialogs::warn(this, CSM_TR("Example is damaged"),
                             CSM_TR("JSON parse error: %1").arg(parseError.errorString()));
        return;
    }

    QString circuitError;
    const QJsonObject root = json.object();
    const Circuit circuit = Circuit::fromJson(root, &circuitError);
    if (!circuitError.isEmpty() || circuit.components.isEmpty()) {
        Dialogs::warn(this, CSM_TR("Example is damaged"),
                             circuitError.isEmpty() ? CSM_TR("The example has no components.")
                                                    : circuitError);
        return;
    }

    QString name = exampleDisplayName(root, QFileInfo(m_examplePaths.at(index)).baseName());

    // 示例不带路径（另存时才问），分析参数跟着示例走
    const SimulationOptions options =
        optionsFromJson(root.value(QStringLiteral("analysis")).toObject());
    loadCircuitData(circuit, name, QString(), options);
    appendLog(CSM_TR("Example \"%1\" loaded. Press %2 to run the simulation.")
                  .arg(name, keyLabel(m_actRun)));
}

void MainWindow::applyProbeReadouts(const SimulationResult& result)
{
    if (!m_scene)
        return;

    // 只有瞬态分析的波形是「电压随时间」，才能算峰峰值 / 有效值 / 频率。
    // 交流扫频的纵轴是 dB 增益，工作点分析没有时间序列 —— 这两种情况不显示面板，
    // 否则算出来是些看着像电压的假数字。
    if (result.kind != AnalysisKind::Transient) {
        // 交流扫频的纵轴是 dB、工作点没有时间序列：显示横杠，但框要留着
        for (ComponentItem* item : m_scene->componentItems()) {
            if (item->component().type == ComponentType::MeasureProbe)
                item->setProbeReadout(ComponentItem::placeholderProbeReadout());
        }
        return;
    }

    // 探针显示的是「这个节点上的电压」的统计量；节点号要从网表里查
    const Circuit circuit = m_scene->toCircuit();
    const Netlist net = circuit.buildNetlist();

    // 探针贴在哪根线、读哪个元件的电流，由场景算好（见 updateProbeAttachments）
    for (ComponentItem* item : m_scene->componentItems()) {
        if (item->component().type != ComponentType::MeasureProbe)
            continue;
        const int node = net.nodeOf(item->component().id, 0);
        const Signal* voltage = node > 0
                                    ? result.signal(QStringLiteral("V(%1)").arg(node))
                                    : nullptr;
        if (!voltage || voltage->y.isEmpty()) {
            item->setProbeReadout(ComponentItem::placeholderProbeReadout());
            continue;
        }


        const QVector<double>& y = voltage->y;
        double lo = y.first();
        double hi = y.first();
        double sum = 0.0;
        double sumSquares = 0.0;
        for (double v : y) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            sum += v;
            sumSquares += v * v;
        }
        const double dc = sum / y.size();
        const double rms = std::sqrt(sumSquares / y.size());

        // 频率：用自相关找周期，而不是数过零点 —— 整流后的波形、占空比不为 50%
        // 的方波，过零间隔是交替不等的，数出来会是错的。自相关在周期性波形上
        // 会在一个整周期处出现明显峰值；RC 充放电这类非周期波形没有峰，就不显示。
        double frequency = 0.0;
        if (y.size() >= 32 && voltage->x.size() == y.size()) {
            QVector<double> centered(y.size());
            for (int i = 0; i < y.size(); ++i)
                centered[i] = y.at(i) - dc;

            const int maxLag = y.size() / 2;
            QVector<double> correlation(maxLag, 0.0);
            double energy = 0.0;
            for (double v : centered)
                energy += v * v;
            if (energy > 1e-18) {
                for (int lag = 2; lag < maxLag; ++lag) {
                    double sum = 0.0;
                    for (int i = 0; i + lag < centered.size(); ++i)
                        sum += centered.at(i) * centered.at(i + lag);
                    correlation[lag] = sum / energy;
                }
                // 先找一个「明显的谷」：自相关必须先掉到 0.2 以下，再回升到 0.5 以上，
                // 才算真的在振荡。RC 充电这类波形自相关一直很高、不掉下去，
                // 若只看「第一个极大值」会挑到一个很短的滞后，算出几十 kHz 的假频率。
                int trough = -1;
                for (int lag = 2; lag < maxLag; ++lag) {
                    if (correlation.at(lag) < 0.2) {
                        trough = lag;
                        break;
                    }
                }
                if (trough > 0) {
                    for (int lag = trough + 1; lag + 1 < maxLag; ++lag) {
                        if (correlation.at(lag) > 0.5
                            && correlation.at(lag) >= correlation.at(lag - 1)
                            && correlation.at(lag) >= correlation.at(lag + 1)) {
                            const double period = (voltage->x.last() - voltage->x.first())
                                                  * lag / double(y.size() - 1);
                            if (period > 0.0)
                                frequency = 1.0 / period;
                            break;
                        }
                    }
                }
            }
        }

        // formatEng 自己会带工程前缀与单位（"3.42 V"、"600 mV"），不要再拼一个 V
        auto volts = [](double value) {
            return formatEng(value, QStringLiteral("V"), 3);
        };

        QStringList lines;
        lines << CSM_TR("Probe V(pp): %1").arg(volts(hi - lo));
        lines << CSM_TR("Probe V(rms): %1").arg(volts(rms));
        lines << CSM_TR("Probe V(dc): %1").arg(volts(dc));
        // 频率：只在估得出周期时才显示
        if (frequency > 0.0)
            lines << CSM_TR("Probe f: %1").arg(formatEng(frequency, QStringLiteral("Hz"), 3));

        // 电流：把探针处「切开」后这一侧注入的元件电流加起来 ——
        // 一根导线上如果有 T 型分支，分支前后的电流是不同的，单个元件代表不了。
        // 权重由场景算好（probeCurrentWeights），这里只做加权求和。
        QStringList tags;
        QVector<const Signal*> currentWaves;
        QVector<double> currentWeights;
        for (const auto& entry : item->probeCurrentWeights()) {
            QString refdes;
            for (ComponentItem* other : m_scene->componentItems()) {
                if (other->component().id == entry.first) {
                    refdes = other->component().refdes;
                    break;
                }
            }
            if (refdes.isEmpty())
                continue;
            const Signal* wave = result.signal(QStringLiteral("I(%1)").arg(refdes));
            if (!wave || wave->y.isEmpty())
                continue;
            currentWaves.append(wave);
            currentWeights.append(entry.second);
            tags << refdes;
        }

        if (currentWaves.isEmpty()) {
            const QString dash = QStringLiteral("--");
            lines << CSM_TR("Probe I(pp): %1").arg(dash);
            lines << CSM_TR("Probe I(rms): %1").arg(dash);
            lines << CSM_TR("Probe I(dc): %1").arg(dash);
        } else {
            // 逐点加权求和后再统计：峰峰值/有效值对和不能分开算
            const int count = currentWaves.first()->y.size();
            QVector<double> sum(count, 0.0);
            for (int w = 0; w < currentWaves.size(); ++w) {
                const QVector<double>& y = currentWaves.at(w)->y;
                const int n = std::min(count, int(y.size()));
                for (int i = 0; i < n; ++i)
                    sum[i] += currentWeights.at(w) * y.at(i);
            }
            double iLo = sum.first();
            double iHi = sum.first();
            double iSum = 0.0;
            double iSquares = 0.0;
            for (double i : sum) {
                iLo = std::min(iLo, i);
                iHi = std::max(iHi, i);
                iSum += i;
                iSquares += i * i;
            }
            auto amps = [](double value) {
                return formatEng(value, QStringLiteral("A"), 3);
            };
            // 只有一个元件贡献时才标出来源：多个元件合成时标一个反而误导
            const QString tag = (tags.size() == 1) ? QStringLiteral(" [%1]").arg(tags.first())
                                                  : QString();
            lines << CSM_TR("Probe I(pp): %1").arg(amps(iHi - iLo)) + tag;
            lines << CSM_TR("Probe I(rms): %1")
                         .arg(amps(std::sqrt(iSquares / sum.size()))) + tag;
            lines << CSM_TR("Probe I(dc): %1").arg(amps(iSum / sum.size())) + tag;
        }

        item->setProbeReadout(lines);
    }
}

void MainWindow::resetLiveProbeStats()
{
    m_probeStats.clear();
}

void MainWindow::refreshLiveProbes()
{
    if (!m_scene || !m_waveLive || !m_waveLive->isLive())
        return;

    // 滑动窗口的样本数：约 120 拍（33 ms 一拍 ≈ 4 秒）。
    // 不用「从开始累计」——那样拨一下开关之后，平均值要很久才追上，
    // 看起来就像电压在慢慢漂，而纯电阻电路本该是瞬间跳变。
    constexpr int kWindow = 120;

    const Circuit circuit = m_scene->toCircuit();
    const Netlist net = circuit.buildNetlist();
    QSet<int> alive;

    for (ComponentItem* item : m_scene->componentItems()) {
        if (item->component().type != ComponentType::MeasureProbe)
            continue;
        const int id = item->component().id;
        alive.insert(id);

        LiveProbeStats& stats = m_probeStats[id];

        // 电压：探头所在节点
        const int node = net.nodeOf(id, 0);
        if (node > 0) {
            stats.lastVolts = m_waveLive->liveValue(QStringLiteral("V(%1)").arg(node));
            stats.hasVoltage = true;
            stats.volts.append(stats.lastVolts);
            if (stats.volts.size() > kWindow)
                stats.volts.remove(0, stats.volts.size() - kWindow);
        }

        // 电流：按场景给出的权重，把几个元件的电流加权求和
        stats.hasCurrent = false;
        double amps = 0.0;
        int contributors = 0;
        for (const auto& entry : item->probeCurrentWeights()) {
            QString refdes;
            for (ComponentItem* other : m_scene->componentItems()) {
                if (other->component().id == entry.first) {
                    refdes = other->component().refdes;
                    break;
                }
            }
            if (refdes.isEmpty())
                continue;
            amps += entry.second * m_waveLive->liveValue(QStringLiteral("I(%1)").arg(refdes));
            ++contributors;
        }
        if (contributors > 0) {
            stats.hasCurrent = true;
            stats.lastAmps = amps;
            stats.amps.append(amps);
            if (stats.amps.size() > kWindow)
                stats.amps.remove(0, stats.amps.size() - kWindow);
        }
        if (!stats.hasCurrent)
            stats.amps.clear();

        // 窗口里还没攒够样本时不动：避免数字乱跳，也不把静态仿真的值盖掉
        if (stats.volts.size() < 8 && stats.amps.size() < 8)
            continue;

        const QString dash = QStringLiteral("--");
        auto range = [](const QVector<double>& values) {
            double lo = values.first();
            double hi = values.first();
            for (double v : values) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            return std::pair<double, double>(lo, hi);
        };
        auto rms = [](const QVector<double>& values) {
            double squares = 0.0;
            for (double v : values)
                squares += v * v;
            return std::sqrt(squares / values.size());
        };

        QStringList lines;
        if (stats.volts.size() >= 8) {
            const auto [lo, hi] = range(stats.volts);
            lines << CSM_TR("Probe V(pp): %1").arg(formatEng(hi - lo, QStringLiteral("V"), 3));
            lines << CSM_TR("Probe V(rms): %1").arg(formatEng(rms(stats.volts), QStringLiteral("V"), 3));
            // 直流取瞬时值：纯电阻电路拨开关之后应当立刻变
            lines << CSM_TR("Probe V(dc): %1").arg(formatEng(stats.lastVolts, QStringLiteral("V"), 3));
        } else {
            lines << CSM_TR("Probe V(pp): %1").arg(dash);
            lines << CSM_TR("Probe V(rms): %1").arg(dash);
            lines << CSM_TR("Probe V(dc): %1").arg(dash);
        }
        if (stats.amps.size() >= 8) {
            const auto [lo, hi] = range(stats.amps);
            lines << CSM_TR("Probe I(pp): %1").arg(formatEng(hi - lo, QStringLiteral("A"), 3));
            lines << CSM_TR("Probe I(rms): %1").arg(formatEng(rms(stats.amps), QStringLiteral("A"), 3));
            lines << CSM_TR("Probe I(dc): %1")
                         .arg(formatEng(stats.lastAmps, QStringLiteral("A"), 3));
        } else {
            lines << CSM_TR("Probe I(pp): %1").arg(dash);
            lines << CSM_TR("Probe I(rms): %1").arg(dash);
            lines << CSM_TR("Probe I(dc): %1").arg(dash);
        }
        item->setProbeReadout(lines);
    }

    // 已经删掉的探针，把统计也清掉
    for (auto it = m_probeStats.begin(); it != m_probeStats.end();) {
        if (alive.contains(it.key()))
            ++it;
        else
            it = m_probeStats.erase(it);
    }
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

void MainWindow::applyBlownCapacitors(const QHash<int, double>& blown)
{
    if (blown.isEmpty())
        return;

    for (auto it = blown.constBegin(); it != blown.constEnd(); ++it) {
        const int id = it.key();
        m_blownIds.insert(id);
        const double reverse = it.value();
        QString refdes = QStringLiteral("#%1").arg(id);
        for (ComponentItem* item : m_scene->componentItems()) {
            if (item->component().id != id)
                continue;
            refdes = item->component().refdes;
            item->setBlown(true);
            item->setBlastProgress(0.0);
        }
        appendLog(CSM_TR("💥 %1 electrolytic capacitor broke down: reverse voltage %2")
                      .arg(refdes, formatEng(reverse, QStringLiteral("V"), 3)));
    }
    appendLog(CSM_TR("Tip: the positive terminal of an electrolytic capacitor is on pin 1; reversing it will break it down. Select the capacitor to toggle polarity."));

    // 600ms 的爆炸动画
    if (!m_blastTimer) {
        m_blastTimer = new QTimer(this);
        m_blastTimer->setInterval(16);
        connect(m_blastTimer, &QTimer::timeout, this, [this] {
            m_blastProgress += 0.025; // 约 40 帧、640ms：先红温再炸
            for (ComponentItem* item : m_scene->componentItems()) {
                if (m_blownIds.contains(item->component().id))
                    item->setBlastProgress(m_blastProgress);
            }
            if (m_blastProgress >= 1.0)
                m_blastTimer->stop();
        });
    }
    if (!m_blastTimer->isActive()) {
        m_blastProgress = 0.0;
        m_blastTimer->start();
    }
}

void MainWindow::clearBlast()
{
    if (m_blastTimer)
        m_blastTimer->stop();
    m_blastProgress = 0.0;
    m_blownIds.clear();
    if (!m_scene)
        return;
    for (ComponentItem* item : m_scene->componentItems()) {
        if (item->isBlown())
            item->setBlown(false);
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
                appendLog(CSM_TR("Version %1 available (%2 packages)")
                              .arg(release.tagName)
                              .arg(release.assets.size()));
                UpdateDialog dialog(release, this);
                dialog.exec();
                if (!dialog.savedPath().isEmpty())
                    appendLog(CSM_TR("Installer downloaded: %1").arg(dialog.savedPath()));
            });
    connect(checker, &UpdateChecker::failed, this,
            [this, checker, manual](const QString& reason) {
                checker->deleteLater();
                appendLog(CSM_TR("Update check failed: %1").arg(reason));
                // 自动检查失败是后台的一次尝试，不打扰；手动检查必须给出可读原因
                if (manual) {
                    Dialogs::warn(this, CSM_TR("Check for Updates"),
                                         CSM_TR("Cannot check for updates:\n%1").arg(reason));
                }
            });
    connect(checker, &UpdateChecker::upToDate, this,
            [this, checker, manual](const QString& current) {
                checker->deleteLater();
                if (manual) {
                    Dialogs::info(this, CSM_TR("Check for Updates"),
                        CSM_TR("Already up to date (v%1).").arg(current));
                }
            });

    if (manual)
        appendLog(CSM_TR("Checking for updates…"));
    checker->check();
}

void MainWindow::showSettings()
{
    // 菜单文字去掉 & 与省略号，快捷键表里读起来更干净
    QVector<SettingsDialog::ShortcutRow> rows;
    for (const ShortcutBinding& binding : m_shortcutBindings) {
        QString label = binding.action->text();
        if (label.endsWith(QStringLiteral("…")))
            label.chop(1);
        // 菜单文字里的加速键「(&N)」对快捷键表没意义，去掉
        label.remove(QRegularExpression(QStringLiteral("\\(&?.{1,3}\\)$")));
        label.remove(QLatin1Char('&'));
        label = label.trimmed();
        rows.append({binding.id, label, binding.action->shortcut(), binding.defaultKey});
    }

    SettingsDialog dialog(this,
                          [this] { applyTheme(); },
                          [this] {
                              // 网格变了：所有画布重画（吸附步长是运行时取的）
                              for (Document* document : m_documents) {
                                  if (document && document->scene)
                                      document->scene->update();
                              }
                          },
                          rows,
                          [this] { applyAutosaveSettings(); });
    if (dialog.exec() == QDialog::Accepted)
        applyShortcutEdits(dialog.shortcuts());
}

void MainWindow::showQuickGuide()
{
    Dialogs::guide(this, CSM_TR("Quick Start"), CSM_TR(
        "<b>Three steps to a simulation</b>"
        "<ol>"
        "<li>Drag parts from the <b>library</b> on the left onto the canvas; "
        "select one and press <b>%1</b> to rotate.</li>"
        "<li>Click <b>Wire</b> (%2) in the toolbar, click on a pin and drag out the polyline, "
        "double-click to finish.<br>"
        "Wire ends snap to pins and a junction dot appears automatically.</li>"
        "<li>Double-click a part to change its value (you can type 10kΩ, 4.7µF or 1mH; "
        "the default unit applies if omitted), then press <b>%3</b> to run and see the waveforms.</li>"
        "</ol>"
        "<b>Note</b>: the circuit must contain a GND symbol, otherwise the reference potential "
        "is undefined.<br><br>"
        "<span style='color:%4'>Tip: Simulate → Example Circuits has several runnable examples.</span>")
                      .arg(keyLabel(m_actRotate), keyLabel(m_actWireTool), keyLabel(m_actRun),
                           Theme::textMuted().name()));
}

void MainWindow::showAbout()
{
    Dialogs::about(this, QStringLiteral(CSM_VERSION_STRING));
}

bool MainWindow::maybeSave()
{
    if (!doc()->dirty)
        return true;

    const QMessageBox::StandardButton button = Dialogs::askSaveDiscardCancel(
        this, CSM_TR("Save Changes"),
        CSM_TR("\"%1\" has unsaved changes. Save them?").arg(doc()->name));

    if (button == QMessageBox::Save)
        return saveCircuit();
    if (button == QMessageBox::Cancel)
        return false;
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // 关窗口等于一次性关掉所有标签：脏的逐个问一遍
    const int original = m_tabs ? m_tabs->currentIndex() : -1;
    for (int i = 0; i < m_documents.size(); ++i) {
        if (!m_documents.at(i)->dirty)
            continue;
        m_tabs->setCurrentIndex(i); // 让用户看清问的是哪一份
        if (!maybeSave()) {
            if (original >= 0)
                m_tabs->setCurrentIndex(original);
            event->ignore();
            return;
        }
    }
    Autosave::clearAll();   // 正常退出，恢复副本没必要留着
    event->accept();
}

} // namespace csm
