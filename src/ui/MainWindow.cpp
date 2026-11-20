#include "ui/MainWindow.h"

#include "ui/MultimeterWindow.h"

#include "csm_version.h"


#include "core/Circuit.h"
#include "core/Netlist.h"
#include "core/Units.h"
#include "ui/ComponentItem.h"
#include "ui/PropertyPanel.h"
#include "ui/SchematicScene.h"
#include "ui/SchematicView.h"
#include "ui/Symbols.h"
#include "ui/Theme.h"
#include "ui/UpdateDialog.h"
#include "ui/WaveformWindow.h"
#include "ui/Dialogs.h"

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
        if (comp.type == ComponentType::Probe) {
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
        if (comp.type == ComponentType::Probe) {
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
    setWindowTitle(QStringLiteral("电路仿真大师"));
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
    createMenus();
    createToolBar();
    createDocks();
    createMonitorWindow();
    // 波形窗口有两个实例（静态 / 动态），图表配色各刷一次
    if (m_wavePlot)
        m_wavePlot->applyTheme();
    if (m_waveLive)
        m_waveLive->applyTheme();
    createStatusBar();

    appendLog(QStringLiteral("电路仿真大师 %1  —  Qt6 / MNA 求解器")
                  .arg(QStringLiteral(CSM_VERSION_STRING)));
    appendLog(QStringLiteral("提示：左键选择拖动，双击编辑参数，空格键旋转；按 F5 运行仿真。"));
    appendLog(QStringLiteral("已新建空白电路：可从左侧元件库放置元件，"
                             "或通过「仿真 → 示例电路」载入示例。"));

    if (m_actMonitor) {
        m_actMonitor->setEnabled(false);
        m_actMonitor->setChecked(false);
    }
    if (m_waveLive)
        m_waveLive->stopLiveScope();

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
                                     ? QStringLiteral("尚未保存到文件")
                                     : document->filePath);
    if (document == doc()) {
        setWindowTitle(QStringLiteral("%1%2 — 电路仿真大师")
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

    document->scene = new SchematicScene(this);
    document->view = new SchematicView(document->scene, this);
    wireDocument(document);

    m_documents.append(document);
    m_tabs->addTab(document->view, document->tabTitle());
    m_tabs->setTabToolTip(m_documents.size() - 1,
                          path.isEmpty() ? QStringLiteral("尚未保存到文件") : path);

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

    // 切走之前先把「跑到一半」的东西收干净：实时仿真与万用表窗口都只属于
    // 原来那个文档，留着会显示错的数据
    if (m_scene != document->scene) {
        if (m_waveLive && m_waveLive->isLive())
            m_waveLive->stopLiveScope();
        closeMeterWindows();
    }

    m_scene = document->scene;
    // 新标签页的视图继承「仿真是否在跑」的状态（实时引擎是全局的）
    if (m_view && m_waveLive)
        m_view->setLiveRunning(m_waveLive->isLive());
    m_view = document->view;
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

    // 最后一个标签不真删：清成空白文档，省得窗口里什么都没有
    if (m_documents.size() == 1) {
        m_loading = true;
        closeMeterWindows();
        document->scene->clearAll();
        m_loading = false;
        document->name = QStringLiteral("未命名");
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


    m_actCloseTab = new QAction(QStringLiteral("关闭标签页(&W)"), this);
    m_actCloseTab->setShortcut(QKeySequence::Close); // Ctrl+W
    m_actCloseTab->setStatusTip(QStringLiteral("关闭当前标签页；有未保存的更改会先问一句"));

    m_actNextTab = new QAction(QStringLiteral("下一个标签页"), this);
    m_actNextTab->setShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")));
    m_actNextTab->setShortcutContext(Qt::WindowShortcut);

    m_actPrevTab = new QAction(QStringLiteral("上一个标签页"), this);
    m_actPrevTab->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")));
    m_actPrevTab->setShortcutContext(Qt::WindowShortcut);

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

    m_actCut = new QAction(QStringLiteral("剪切"), this);
    m_actCut->setShortcut(QKeySequence::Cut);
    m_actCut->setEnabled(false);
    m_actCopy = new QAction(QStringLiteral("复制"), this);
    m_actCopy->setShortcut(QKeySequence::Copy);
    m_actCopy->setEnabled(false);
    m_actPaste = new QAction(QStringLiteral("粘贴"), this);
    m_actPaste->setShortcut(QKeySequence::Paste);
    m_actPaste->setEnabled(false);

    m_actDelete = new QAction(
        actionIcon(QStringLiteral("delete"), QStringLiteral("edit-delete"), QStyle::SP_TrashIcon),
        QStringLiteral("删除"), this);
    m_actDelete->setShortcut(QKeySequence::Delete);

    m_actRotate = new QAction(QStringLiteral("旋转 90°"), this);
    m_actRotate->setObjectName(QStringLiteral("actRotate"));
    // 空格旋转：和 KiCad 的习惯一致，选中元件后连按空格就能连续转
    m_actRotate->setShortcut(QKeySequence(Qt::Key_Space));
    m_actRotate->setShortcutContext(Qt::WindowShortcut);

    // 对齐与分布：按符号本体的外框算，位移仍会被网格吸附
    const QVector<QPair<QString, SchematicScene::AlignMode>> alignItems = {
        {QStringLiteral("左对齐"), SchematicScene::AlignMode::Left},
        {QStringLiteral("水平居中"), SchematicScene::AlignMode::CenterH},
        {QStringLiteral("右对齐"), SchematicScene::AlignMode::Right},
        {QStringLiteral("顶对齐"), SchematicScene::AlignMode::Top},
        {QStringLiteral("垂直居中"), SchematicScene::AlignMode::CenterV},
        {QStringLiteral("底对齐"), SchematicScene::AlignMode::Bottom},
    };
    for (const auto& entry : alignItems) {
        auto* action = new QAction(entry.first, this);
        action->setEnabled(false);
        const SchematicScene::AlignMode mode = entry.second;
        connect(action, &QAction::triggered, this, [this, mode] {
            if (m_scene)
                m_scene->alignSelection(mode);
        });
        m_alignActions.append(action);
    }

    const QVector<QPair<QString, Qt::Orientation>> distributeItems = {
        {QStringLiteral("水平等距"), Qt::Horizontal},
        {QStringLiteral("垂直等距"), Qt::Vertical},
    };
    for (const auto& entry : distributeItems) {
        auto* action = new QAction(entry.first, this);
        action->setEnabled(false);
        const Qt::Orientation orientation = entry.second;
        connect(action, &QAction::triggered, this, [this, orientation] {
            if (m_scene)
                m_scene->distributeSelection(orientation);
        });
        m_distributeActions.append(action);
    }

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

    m_actSnapWires = new QAction(QStringLiteral("导线对齐网格"), this);
    m_actSnapWires->setToolTip(QStringLiteral("把导线的拐点吸到网格上，修老版本粘贴留下的歪线"));
    connect(m_actSnapWires, &QAction::triggered, this, [this] { m_scene->snapWiresToGrid(); });

    m_actDelete->setEnabled(false);
    m_actRotate->setEnabled(false);

    // 这些快捷键只在画布获得焦点时生效，避免与属性面板输入冲突
    for (QAction* action : {m_actUndo, m_actRedo, m_actCut, m_actCopy, m_actPaste, m_actDelete,
                            m_actRotate, m_actSelectAll, m_actSelectTool, m_actWireTool}) {
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
    fileMenu->addAction(m_actCloseTab);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actQuit);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
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

    QMenu* arrangeMenu = editMenu->addMenu(QStringLiteral("对齐与分布"));
    arrangeMenu->setObjectName(QStringLiteral("menuArrange"));
    for (QAction* action : m_alignActions)
        arrangeMenu->addAction(action);
    arrangeMenu->addSeparator();
    for (QAction* action : m_distributeActions)
        arrangeMenu->addAction(action);

    editMenu->addSeparator();
    editMenu->addAction(m_actSelectAll);
    editMenu->addSeparator();
    editMenu->addAction(m_actSnapWires);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
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
    viewMenu->addSeparator();

    // 主题：跟随系统 / 浅色 / 深色，选完立刻生效并存盘
    QMenu* themeMenu = viewMenu->addMenu(QStringLiteral("主题"));
    themeMenu->setObjectName(QStringLiteral("menuTheme"));
    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    const QVector<QPair<QString, Theme::Mode>> themeItems = {
        {QStringLiteral("跟随系统"), Theme::Mode::System},
        {QStringLiteral("浅色"), Theme::Mode::Light},
        {QStringLiteral("深色"), Theme::Mode::Dark},
    };
    for (const auto& item : themeItems) {
        QAction* action = themeMenu->addAction(item.first);
        action->setCheckable(true);
        action->setChecked(Theme::mode() == item.second);
        themeGroup->addAction(action);
        const Theme::Mode mode = item.second;
        connect(action, &QAction::triggered, this, [this, mode] {
            Theme::save(mode);
            applyTheme();
        });
    }
    viewMenu->addSeparator();

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
        GroundKind groundKind = GroundKind::Gnd; // 接地符号的种类
        GateKind gateKind = GateKind::And;       // 逻辑门的种类
    };
    const QVector<Entry> entries = {
        // 按分类排好，同类的挨在一起，「全部」列表也有顺序
        {ComponentType::Resistor, QStringLiteral("电阻  R"), QStringLiteral("基础")},
        {ComponentType::Capacitor, QStringLiteral("电容  C"), QStringLiteral("基础")},
        {ComponentType::Inductor, QStringLiteral("电感  L"), QStringLiteral("基础")},
        {ComponentType::Transformer, QStringLiteral("变压器  T"), QStringLiteral("基础")},

        {ComponentType::Diode, QStringLiteral("二极管  D"), QStringLiteral("半导体")},
        {ComponentType::Led, QStringLiteral("发光二极管  LED"), QStringLiteral("半导体")},
        {ComponentType::Npn, QStringLiteral("NPN 三极管  Q"), QStringLiteral("半导体")},
        {ComponentType::Pnp, QStringLiteral("PNP 三极管  Q"), QStringLiteral("半导体")},
        {ComponentType::Nmos, QStringLiteral("N 沟道 MOS  M"), QStringLiteral("半导体")},
        {ComponentType::Pmos, QStringLiteral("P 沟道 MOS  M"), QStringLiteral("半导体")},
        {ComponentType::Igbt, QStringLiteral("IGBT  Z"), QStringLiteral("半导体")},

        {ComponentType::OpAmp, QStringLiteral("运算放大器  U"), QStringLiteral("模拟 IC")},
        {ComponentType::Tl431, QStringLiteral("TL431 基准  VR"), QStringLiteral("模拟 IC")},
        {ComponentType::Optocoupler, QStringLiteral("光耦  OC"), QStringLiteral("模拟 IC")},
        {ComponentType::Ob2362, QStringLiteral("OB2362 控制器  U"), QStringLiteral("模拟 IC")},

        {ComponentType::LogicGate, QStringLiteral("与门  AND"), QStringLiteral("数字逻辑"),
         GroundKind::Gnd, GateKind::And},
        {ComponentType::LogicGate, QStringLiteral("或门  OR"), QStringLiteral("数字逻辑"),
         GroundKind::Gnd, GateKind::Or},
        {ComponentType::LogicGate, QStringLiteral("非门  NOT"), QStringLiteral("数字逻辑"),
         GroundKind::Gnd, GateKind::Not},
        {ComponentType::LogicGate, QStringLiteral("异或门  XOR"), QStringLiteral("数字逻辑"),
         GroundKind::Gnd, GateKind::Xor},
        {ComponentType::LogicGate, QStringLiteral("同或门  XNOR"), QStringLiteral("数字逻辑"),
         GroundKind::Gnd, GateKind::Xnor},

        {ComponentType::VoltageSource, QStringLiteral("电压源  V"), QStringLiteral("电源")},
        {ComponentType::CurrentSource, QStringLiteral("电流源  I"), QStringLiteral("电源")},

        {ComponentType::Switch, QStringLiteral("开关  S"), QStringLiteral("开关与接地")},
        {ComponentType::PushButton, QStringLiteral("按钮开关  SB"), QStringLiteral("开关与接地")},
        {ComponentType::Ground, QStringLiteral("接地  GND"), QStringLiteral("开关与接地"),
         GroundKind::Gnd},
        {ComponentType::NetLabel, QStringLiteral("网络标签  NET"), QStringLiteral("开关与接地")},

        {ComponentType::Probe, QStringLiteral("电压测试点  TP"), QStringLiteral("测量")},
        {ComponentType::CurrentProbe, QStringLiteral("电流测试点  IP"), QStringLiteral("测量")},
        {ComponentType::Multimeter, QStringLiteral("万用表  XMM"), QStringLiteral("测量")},
    };

    const QString category = m_libraryCategory ? m_libraryCategory->currentText() : QString();
    // 注意用 QStringLiteral：QLatin1String 会把 UTF-8 字节按 Latin-1 解释，
    // 与下拉框里的 QString 永远不相等，列表会变成空的
    const bool showAll = category.isEmpty() || category == QStringLiteral("全部");

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
    auto* libraryDock = new QDockWidget(QStringLiteral("元件库"), this);
    libraryDock->setObjectName(QStringLiteral("libraryDock"));
    libraryDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* libraryContainer = new QWidget(libraryDock);
    auto* libraryLayout = new QVBoxLayout(libraryContainer);
    libraryLayout->setContentsMargins(6, 6, 6, 6);
    libraryLayout->setSpacing(6);

    m_libraryCategory = new QComboBox(libraryContainer);
    m_libraryCategory->addItem(QStringLiteral("全部"));
    // 按「器件性质」分，别把 11 个东西都堆在「半导体」里
    for (const QString& name : {QStringLiteral("基础"), QStringLiteral("半导体"),
                                QStringLiteral("模拟 IC"), QStringLiteral("数字逻辑"),
                                QStringLiteral("电源"), QStringLiteral("开关与接地"),
                                QStringLiteral("测量")}) {
        m_libraryCategory->addItem(name);
    }
    m_libraryCategory->setToolTip(QStringLiteral("按类别筛选元件"));
    libraryLayout->addWidget(m_libraryCategory);

    auto* hint = new QLabel(QStringLiteral("把元件拖到画布上放置"), libraryContainer);
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
            viewMenu->addAction(dock->toggleViewAction());
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
    if (m_waveLive)
        m_waveLive->applyTheme();

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
        QStringLiteral("选择模式：拖动元件移动，双击元件编辑参数，空格键旋转"), this);
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
        text = QStringLiteral("分析：直流工作点");
        break;
    case AnalysisKind::Transient:
        text = QStringLiteral("分析：瞬态 %1 / 步长 %2")
                   .arg(formatEng(doc()->options.tStop, QStringLiteral("s"), 3),
                        formatEng(doc()->options.tStep, QStringLiteral("s"), 3));
        break;
    case AnalysisKind::AC:
        text = QStringLiteral("分析：交流 %1 ~ %2")
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
    if (m_waveLive)
        m_waveLive->disconnect(this);
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
        appendLog(QStringLiteral("⚠ 无法恢复历史状态：%1").arg(error));
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
    appendLog(QStringLiteral("已撤销（%1/%2）")
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
    appendLog(QStringLiteral("已重做（%1/%2）")
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
            appendLog(QStringLiteral(
                "提示：万用表读数窗口只在实时仿真运行时打开。按 F5 运行仿真后再点它。"));
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
    appendLog(QStringLiteral("按钮 %1 按下（%2）")
                  .arg(comp.refdes, comp.normallyClosed ? QStringLiteral("断开")
                                                        : QStringLiteral("接通")));
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
    appendLog(QStringLiteral("开关 %1 %2")
                  .arg(comp.refdes, comp.closed ? QStringLiteral("闭合")
                                                : QStringLiteral("断开")));
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
    setWindowTitle(QStringLiteral("%1%2 — 电路仿真大师")
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
    const bool reuse = target && target->name == QStringLiteral("未命名")
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
    appendLog(QStringLiteral("已新建空白电路（新标签页）。"));
}

bool MainWindow::saveCircuit()
{
    if (doc()->filePath.isEmpty())
        return saveCircuitAs();

    const Circuit circuit = m_scene->toCircuit();
    QFile file(doc()->filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Dialogs::warn(this, QStringLiteral("保存失败"), file.errorString());
        return false;
    }
    QJsonObject root = circuit.toJson();
    root.insert(QStringLiteral("analysis"), analysisToJson(doc()->options));
    const QJsonDocument json(root);
    file.write(json.toJson(QJsonDocument::Indented));
    file.close();

    doc()->dirty = false;
    updateTitle();
    appendLog(QStringLiteral("已保存：%1").arg(doc()->filePath));
    return true;
}

bool MainWindow::saveCircuitAs()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存电路"), doc()->name + QStringLiteral(".csm"),
        QStringLiteral("电路仿真大师电路 (*.csm);;所有文件 (*)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QLatin1String(".csm"), Qt::CaseInsensitive))
        path += QStringLiteral(".csm");

    doc()->filePath = path;
    doc()->name = QFileInfo(path).fileName();
    return saveCircuit();
}

void MainWindow::openCircuit()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开电路"), QString(),
        QStringLiteral("电路仿真大师电路 (*.csm);;所有文件 (*)"));
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
        appendLog(QStringLiteral("该文件已经在标签页里：%1").arg(path));
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Dialogs::warn(this, QStringLiteral("打开失败"), file.errorString());
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if (json.isNull()) {
        Dialogs::warn(this, QStringLiteral("打开失败"),
                      QStringLiteral("JSON 解析错误：%1").arg(parseError.errorString()));
        return false;
    }

    QString error;
    const QJsonObject root = json.object();
    const Circuit circuit = Circuit::fromJson(root, &error);
    if (!error.isEmpty()) {
        Dialogs::warn(this, QStringLiteral("打开失败"), error);
        return false;
    }
    if (circuit.components.isEmpty()) {
        Dialogs::warn(this, QStringLiteral("打开失败"), QStringLiteral("文件中没有元件。"));
        return false;
    }

    SimulationOptions options = doc() ? doc()->options : SimulationOptions();
    const QJsonObject analysis = root.value(QStringLiteral("analysis")).toObject();
    if (!analysis.isEmpty())
        options = optionsFromJson(analysis);

    loadCircuitData(circuit, QFileInfo(path).fileName(), path, options);
    appendLog(QStringLiteral("已打开：%1（%2 个元件）")
                  .arg(path)
                  .arg(circuit.components.size()));
    return true;
}

void MainWindow::runSimulation()
{
    const Circuit circuit = m_scene->toCircuit();
    if (circuit.components.isEmpty()) {
        Dialogs::info(this, QStringLiteral("无法仿真"),
                                 QStringLiteral("请先放置元件并连接电路。"));
        return;
    }

    const Netlist net = circuit.buildNetlist();
    if (!net.valid) {
        Dialogs::warn(this, QStringLiteral("电路检查未通过"), net.error);
        return;
    }
    for (const QString& warning : net.warnings)
        appendLog(QStringLiteral("⚠ ") + warning);

    // 运行仿真只做计算，不自动弹任何窗口：波形由「静态波形」/「动态示波器」显式打开
    QApplication::setOverrideCursor(Qt::WaitCursor);
    SimulationResult result = Simulator::run(circuit, doc()->options);
    QApplication::restoreOverrideCursor();

    appendLog(QStringLiteral("──────── 仿真 ────────"));
    for (const QString& line : result.log)
        appendLog(line);

    if (!result.ok) {
        appendLog(QStringLiteral("✘ 仿真失败：") + result.error);
        Dialogs::error(this, QStringLiteral("仿真失败"), result.error);
        return;
    }

    appendLog(QStringLiteral("✔ 完成，用时 %1 ms").arg(QString::number(result.elapsedMs, 'f', 1)));

    // 先点亮 LED / 播放爆炸：工作点分支会直接弹对话框返回，放在后面就轮不到它了
    applyLedStates(result);
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
        appendLog(QStringLiteral("⚠ 电路中没有测试点，波形为空。请放置电压测试点（TP）或电流测试点（IP）。"));

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
        appendLog(QStringLiteral("直流工作点已算出：报告显示在「波形监控 → 静态波形」；"
                                 "要看连续波形请切到「动态示波器」。"));
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
                doc()->options = options;
                updateAnalysisLabel();
                runSimulation();
            });

    connect(m_waveLive, &WaveformWindow::liveStateChanged, this, [this](bool running) {
        updateRunAction();
        // 仿真运行时点击开关 = 拨动，停下来就恢复成普通的选中 / 拖动
        if (m_view)
            m_view->setLiveRunning(running);
        // 读数窗口只在仿真运行时可用，停了就一起收掉，
        // 免得留在桌面上显示一堆不再更新的数字
        if (!running) {
            closeMeterWindows();
            restorePushButtons(); // 松手事件可能没送到（仿真中途停止）
        }

        // 没有 GND 的电路不开波形监控：参考点由万用表负端顶替，
        // 节点电压之间没有统一基准，画出来也没意义。
        if (m_actMonitor && running && !monitorAvailable()) {
            m_actMonitor->setEnabled(false);
            m_actMonitor->setToolTip(QStringLiteral(
                "波形监控需要有接地符号（GND）。当前电路没有 GND，只能看万用表读数。"));
            if (m_actMonitor->isChecked()) {
                const QSignalBlocker blocker(m_actMonitor);
                m_actMonitor->setChecked(false);
            }
            if (m_waveWindow)
                m_waveWindow->hide();
        }
    });
    // 刻度改动记进 doc()->options，保存工程时一并写入
    connect(m_waveLive, &WaveformWindow::liveScalesChanged, this,
            [this](double timeDiv, double voltDiv, double ampDiv) {
                doc()->options.scopeTimeDiv = timeDiv;
                doc()->options.scopeVoltDiv = voltDiv;
                doc()->options.scopeAmpDiv = ampDiv;
            });

    // 电路改动（比如拨动开关）后重建实时引擎，让仿真跟着新拓扑走
    m_liveRestartTimer = new QTimer(this);
    m_liveRestartTimer->setSingleShot(true);
    m_liveRestartTimer->setInterval(200);
    connect(m_liveRestartTimer, &QTimer::timeout, this, [this] {
        if (!m_waveLive || !m_waveLive->isLive())
            return;

        // 优先原地更新：时间轴与已有波形都保留，只把新拓扑/数值喂给引擎
        const Circuit circuit = m_scene->toCircuit();
        const Netlist net = circuit.buildNetlist();
        if (!net.valid) {
            appendLog(QStringLiteral("⚠ 电路无法求解：%1").arg(net.error));
            return;
        }
        const QVector<QPair<QString, QString>> probes = probeSpecs(circuit, net);
        if (!m_waveLive->updateLiveCircuit(circuit, probes)) {
            // 测试点或节点编号变了，只能重启引擎
            startLiveScope();
        }
    });

    // 实时仿真推进时同步刷新打开的万用表读数窗口
    connect(m_waveLive, &WaveformWindow::liveUpdated, this, [this] {
        refreshMeterWindows();
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
        appendLog(QStringLiteral(
            "波形监控需要有接地符号（GND）。当前电路没有 GND，只能看万用表读数。"));
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

bool MainWindow::startLiveScope()
{
    const Circuit circuit = m_scene->toCircuit();
    const Netlist net = circuit.buildNetlist();
    if (!net.valid) {
        Dialogs::warn(this, QStringLiteral("无法运行示波器"), net.error);
        return false;
    }

    // 没有测试点也照样启动，只是波形为空（面板里会提示放 TP / IP）
    const QVector<QPair<QString, QString>> probes = probeSpecs(circuit, net);

    m_waveLive->setLiveScalePreferences(doc()->options.scopeTimeDiv, doc()->options.scopeVoltDiv,
                                        doc()->options.scopeAmpDiv);
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
        Dialogs::warn(this, QStringLiteral("无法打开示例"), file.errorString());
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if (json.isNull()) {
        Dialogs::warn(this, QStringLiteral("示例损坏"),
                             QStringLiteral("JSON 解析错误：%1").arg(parseError.errorString()));
        return;
    }

    QString circuitError;
    const QJsonObject root = json.object();
    const Circuit circuit = Circuit::fromJson(root, &circuitError);
    if (!circuitError.isEmpty() || circuit.components.isEmpty()) {
        Dialogs::warn(this, QStringLiteral("示例损坏"),
                             circuitError.isEmpty() ? QStringLiteral("示例中没有元件。")
                                                    : circuitError);
        return;
    }

    QString name = root.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = QFileInfo(m_examplePaths.at(index)).baseName();

    // 示例不带路径（另存时才问），分析参数跟着示例走
    const SimulationOptions options =
        optionsFromJson(root.value(QStringLiteral("analysis")).toObject());
    loadCircuitData(circuit, name, QString(), options);
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
        appendLog(QStringLiteral("💥 %1 电解电容反接击穿：反向电压 %2")
                      .arg(refdes, formatEng(reverse, QStringLiteral("V"), 3)));
    }
    appendLog(QStringLiteral("提示：电解电容的正极在引脚 1 一侧，反接会击穿。选中电容可勾选/取消极性。"));

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
                appendLog(QStringLiteral("发现新版本 %1（%2 个安装包）")
                              .arg(release.tagName)
                              .arg(release.assets.size()));
                UpdateDialog dialog(release, this);
                dialog.exec();
                if (!dialog.savedPath().isEmpty())
                    appendLog(QStringLiteral("安装包已下载：%1").arg(dialog.savedPath()));
            });
    connect(checker, &UpdateChecker::failed, this,
            [this, checker, manual](const QString& reason) {
                checker->deleteLater();
                appendLog(QStringLiteral("检查更新失败：%1").arg(reason));
                // 自动检查失败是后台的一次尝试，不打扰；手动检查必须给出可读原因
                if (manual) {
                    Dialogs::warn(this, QStringLiteral("检查更新"),
                                         QStringLiteral("无法检查更新：\n%1").arg(reason));
                }
            });
    connect(checker, &UpdateChecker::upToDate, this,
            [this, checker, manual](const QString& current) {
                checker->deleteLater();
                if (manual) {
                    Dialogs::info(this, QStringLiteral("检查更新"),
                        QStringLiteral("已是最新版本（v%1）。").arg(current));
                }
            });

    if (manual)
        appendLog(QStringLiteral("正在检查更新…"));
    checker->check();
}

void MainWindow::showQuickGuide()
{
    Dialogs::guide(this, QStringLiteral("快速上手"), QStringLiteral(
        "<b>三步完成一次仿真</b>"
        "<ol>"
        "<li>从左侧<b>元件库</b>把元件拖到画布上；选中后按 <b>空格</b> 旋转。</li>"
        "<li>点击工具栏<b>连线</b>（W），在引脚处单击并拖出折线，双击结束。<br>"
        "导线端点吸附到引脚后会自动出现连接点。</li>"
        "<li>双击元件修改数值（可直接写 10kΩ、4.7µF、1mH，不写单位按默认单位），"
        "按 <b>F5</b> 运行仿真，波形显示在查看器中。</li>"
        "</ol>"
        "<b>注意</b>：电路必须包含接地符号（GND），否则无法确定参考电位。<br><br>"
        "<span style='color:%1'>提示：菜单「仿真 → 示例电路」内置多个可直接运行的例子。</span>")
                      .arg(Theme::textMuted().name()));
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
        this, QStringLiteral("保存更改"),
        QStringLiteral("「%1」有未保存的更改，是否保存？").arg(doc()->name));

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
    event->accept();
}

} // namespace csm
