#include "core/Circuit.h"
#include "core/Netlist.h"
#include "csm_platform.h"
#include "csm_version.h"
#include "ui/ComponentItem.h"
#include "ui/I18n.h"
#include "ui/MainWindow.h"
#include "ui/MultimeterWindow.h"
#include "ui/PropertyPanel.h"
#include "ui/SchematicScene.h"
#include "ui/SchematicView.h"
#include "ui/Symbols.h"
#include "ui/UpdateDialog.h"
#include "ui/WaveformWindow.h"
#include "ui/WireItem.h"

#include <QAbstractAxis>
#include <QAction>
#include <QApplication>
#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsSceneMouseEvent>
#include <QIcon>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLineSeries>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QValueAxis>
#include <QWidget>
#include <cmath>
#include <cstdio>

using namespace csm;

class UiWaveformTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void yAxisAlwaysCoversTheData();
    void meterIsNotListedInWaveformMonitor();
    void liveScopeKeepsManualScale();
    void autoScaleAlsoPicksTimeBase();
    void changingTimeBaseKeepsVoltageScale();
    void autoScaleLivesInTheLiveScope();
    void liveScopeKeepsSwitchingAtLargeTimeBase();
    void liveScopeWorksWithReferenceGround();
    void constantSignalKeepsUsableAxis();
    void scopeScalesAreRemembered();
    void analysisFieldsFollowFile();
    void liveScopePicksUsableRange();
    void liveScopeFromMenu();
    void captureAcSweep();
    void captureDiodeRectifier();
    void captureOpAmp();

private:

    /// 把窗口截图存到 build/shots/ 下（目录不存在就建出来）。
    /// 拆成独立可执行文件后，不能再指望「别的用例先建好了目录」——
    /// 干净构建时 shots/ 并不存在，grab().save() 会直接失败。
    static bool saveShot(QWidget* widget, const QString& fileName)
    {
        QDir dir(QCoreApplication::applicationDirPath());
        if (!dir.mkpath(QStringLiteral("shots")))
            return false;
        return widget->grab().save(dir.filePath(QStringLiteral("shots/") + fileName));
    }

    // 下面几个是本文件自用的界面操作辅助。为了每个测试文件彻底独立
    // （删掉别的文件也不影响自己），各留一份，不再共用头文件。
    static void settle(int rounds = 3)
    {
        for (int i = 0; i < rounds; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    static void waitShown(MainWindow& window)
    {
        QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 3000);
        settle();
    }

    static QTabWidget* waveTabs(MainWindow& window)
    {
        for (QTabWidget* tabs : window.findChildren<QTabWidget*>()) {
            if (tabs->count() == 2 && tabs->tabText(0) == QStringLiteral("静态波形"))
                return tabs;
        }
        return nullptr;
    }

    static void openMonitor(MainWindow& window, int tab)
    {
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text() == QStringLiteral("波形监控")) {
                // 可勾选动作：setChecked 会触发 toggled，不要再 trigger 一次
                action->setChecked(true);
                settle();
                break;
            }
        }
        QTabWidget* tabs = waveTabs(window);
        if (!tabs)
            QFAIL("找不到波形监控面板");
        tabs->setCurrentIndex(tab);
        settle();
    }

    static void openStaticWave(MainWindow& window) { openMonitor(window, 0); }

    static WaveformWindow* plotPanel(MainWindow& window)
    {
        QTabWidget* tabs = waveTabs(window);
        return tabs ? qobject_cast<WaveformWindow*>(tabs->widget(0)) : nullptr;
    }

    static WaveformWindow* livePanel(MainWindow& window)
    {
        QTabWidget* tabs = waveTabs(window);
        return tabs ? qobject_cast<WaveformWindow*>(tabs->widget(1)) : nullptr;
    }

    static WaveformWindow* waitWaveWindow(MainWindow& window, int timeoutMs = 5000)
    {
        QElapsedTimer timer;
        timer.start();
        WaveformWindow* wave = nullptr;
        while (timer.elapsed() < timeoutMs) {
            wave = plotPanel(window);
            if (wave && wave->isVisible())
                break;
            QTest::qWait(5); // 只睡 5ms 并处理事件，条件一满足立刻返回
        }
        settle();
        return (wave && wave->isVisible()) ? wave : nullptr;
    }
};

void UiWaveformTest::initTestCase()
{
    // 和 main.cpp 一样装上 Qt 自带的中文翻译（输入框右键菜单等）
    // 崩溃时缓冲区里的输出会丢：Windows 上 stdout 重定向到管道是全缓冲的，
    // CI 上只会看到「Failed」却不知道停在哪一步。改成不缓冲，一行一行往外写。
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    csm::installQtTranslations(qApp);
}

void UiWaveformTest::meterIsNotListedInWaveformMonitor()
{
    // 波形页只列电压测试点（TP）与电流测试点（IP）。万用表的读数交给它自己的
    // 七段码窗口，不进波形列表 —— 否则一个 10 MΩ 内阻的支路混在时域曲线上，
    // 既占地方又容易被误读。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
    source->setRotationAngle(90);
    Component src = source->component();
    src.dcValue = 10.0;
    source->setComponent(src);

    scene->placeComponent(ComponentType::Resistor, QPointF(300, 200));
    scene->placeComponent(ComponentType::Probe, QPointF(300, 120));   // TP1
    scene->placeComponent(ComponentType::Multimeter, QPointF(300, 360));
    scene->placeComponent(ComponentType::Ground, QPointF(100, 500));

    scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(270, 200)});
    scene->addWire({QPointF(300, 150), QPointF(300, 200)});           // TP1 引脚接到 R1 左
    scene->addWire({QPointF(270, 200), QPointF(270, 360)});           // 表 +
    scene->addWire({QPointF(330, 200), QPointF(330, 460), QPointF(100, 460), QPointF(100, 500)});
    scene->addWire({QPointF(100, 330), QPointF(100, 500)});
    settle();

    window.runSimulation();
    settle();
    openMonitor(window, 0);
    settle();

    auto* plot = plotPanel(window);
    QVERIFY(plot);
    auto* list = plot->findChild<QListWidget*>();
    QVERIFY(list);
    QVERIFY2(list->count() > 0, "波形页应当有信号");

    bool hasProbe = false;
    for (int i = 0; i < list->count(); ++i) {
        const QString text = list->item(i)->text();
        QVERIFY2(!text.contains(QStringLiteral("XMM")),
                 qPrintable(QStringLiteral("万用表不该出现在波形页：%1").arg(text)));
        if (text.contains(QStringLiteral("TP1")))
            hasProbe = true;
    }
    QVERIFY2(hasProbe, "电压测试点应当出现在波形页");
}

void UiWaveformTest::yAxisAlwaysCoversTheData()
{
    // applyFixedTicks 先按范围/6 取漂亮步长、再定上下限。以前上限写成
    // 「下限 + 步长×6」，不保证装得下数据：-891…587 mV 会得到 -1000…500 mV，
    // 波峰超出 500 的部分被图表裁平，看起来像「幅值不对」。
    // 现在上下限各自向外取整到刻度线，数据必须完整落在轴范围内。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    // 方波源 + 一级 RC（τ 与半周期同量级，波形会有明显的非对称峰谷）
    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
    source->setRotationAngle(90);
    Component src = source->component();
    src.function = SourceFunction::Pulse;
    src.pulseV1 = 0.0;
    src.pulseV2 = 3.3;
    src.pulsePeriod = 0.001;
    src.pulseWidth = 0.0005;
    source->setComponent(src);

    auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(250, 200));
    Component r = resistor->component();
    r.value = 5100.0;
    resistor->setComponent(r);

    auto* cap = scene->placeComponent(ComponentType::Capacitor, QPointF(250, 350));
    cap->setRotationAngle(270);
    Component cc = cap->component();
    cc.value = 10e-6;
    cap->setComponent(cc);

    auto* probe = scene->placeComponent(ComponentType::Probe, QPointF(400, 200));
    QVERIFY(probe);
    scene->placeComponent(ComponentType::Ground, QPointF(100, 420));
    scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(220, 200)});
    scene->addWire({QPointF(280, 200), QPointF(400, 200)});
    scene->addWire({QPointF(250, 230), QPointF(250, 320)});
    scene->addWire({QPointF(100, 330), QPointF(100, 420)});
    scene->addWire({QPointF(250, 380), QPointF(250, 420), QPointF(100, 420)});
    settle();

    window.runSimulation();
    settle();
    QAction* monitor = window.findChild<QAction*>(QStringLiteral("actMonitor"));
    QVERIFY(monitor);
    monitor->setChecked(true);
    settle();
    QTabWidget* tabs = waveTabs(window);
    QVERIFY(tabs);
    tabs->setCurrentIndex(0);
    settle();

    auto* plot = qobject_cast<WaveformWindow*>(tabs->widget(0));
    QVERIFY(plot);
    auto* view = plot->findChild<QChartView*>();
    QChart* chart = view ? view->chart() : nullptr;
    QVERIFY(chart);

    // 数据范围（图内单位已乘工程倍率，与轴一致，可直接比较）
    double lo = std::numeric_limits<double>::max();
    double hi = -std::numeric_limits<double>::max();
    const QList<QAbstractSeries*> seriesList = chart->series();
    QVERIFY2(!seriesList.isEmpty(), "应当画出了波形");
    for (QAbstractSeries* series : seriesList) {
        auto* line = qobject_cast<QLineSeries*>(series);
        if (!line)
            continue;
        for (int i = 0; i < line->count(); ++i) {
            const double v = line->at(i).y();
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    QVERIFY2(hi > lo, "波形应当有起伏");

    for (QAbstractAxis* axis : chart->axes()) {
        auto* value = qobject_cast<QValueAxis*>(axis);
        if (!value)
            continue;
        if (axis->alignment() != Qt::AlignLeft && axis->alignment() != Qt::AlignRight)
            continue;
        QVERIFY2(value->min() <= lo + std::abs(lo) * 1e-9,
                 qPrintable(QStringLiteral("轴下限 %1 高于数据 %2")
                                .arg(value->min())
                                .arg(lo)));
        QVERIFY2(value->max() >= hi - std::abs(hi) * 1e-9,
                 qPrintable(QStringLiteral("轴上限 %1 低于数据 %2（波峰被裁掉）")
                                .arg(value->max())
                                .arg(hi)));
    }
}

void UiWaveformTest::liveScopeKeepsManualScale()
{
    // 手动选「电压/格」之后，自动换挡必须让位：否则选 500 mV/格，
    // 信号一超过 ±1.5 V 就被自动跳到 2 V/格，用户的选择形同虚设。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    // 5 V 源 + 分压，峰值约 2.5 V：用 500 mV/格 必然超出 ±1.5 V
    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
    source->setRotationAngle(90);
    Component src = source->component();
    src.dcValue = 5.0;
    source->setComponent(src);
    auto* r1 = scene->placeComponent(ComponentType::Resistor, QPointF(250, 200));
    Component c1 = r1->component();
    c1.value = 1000.0;
    r1->setComponent(c1);
    auto* r2 = scene->placeComponent(ComponentType::Resistor, QPointF(250, 350));
    r2->setRotationAngle(270);
    Component c2 = r2->component();
    c2.value = 1000.0;
    r2->setComponent(c2);
    auto* probe = scene->placeComponent(ComponentType::Probe, QPointF(400, 200));
    QVERIFY(probe);
    scene->placeComponent(ComponentType::Ground, QPointF(100, 420));
    scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(220, 200)});
    scene->addWire({QPointF(280, 200), QPointF(400, 200)});
    scene->addWire({QPointF(250, 230), QPointF(250, 320)});
    scene->addWire({QPointF(100, 330), QPointF(100, 420)});
    scene->addWire({QPointF(250, 380), QPointF(250, 420), QPointF(100, 420)});
    settle();

    window.runSimulation();
    settle();
    QAction* monitor = window.findChild<QAction*>(QStringLiteral("actMonitor"));
    QVERIFY(monitor);
    monitor->setChecked(true);
    settle();
    QTabWidget* tabs = waveTabs(window);
    QVERIFY(tabs);
    tabs->setCurrentIndex(1); // 动态示波器
    settle();

    auto* live = qobject_cast<WaveformWindow*>(tabs->widget(1));
    QVERIFY(live);

    // 找到「电压/格」下拉框
    QComboBox* voltDiv = nullptr;
    for (QComboBox* box : live->findChildren<QComboBox*>()) {
        for (int i = 0; i < box->count(); ++i) {
            if (box->itemText(i).contains(QStringLiteral("mV"))) {
                voltDiv = box;
                break;
            }
        }
        if (voltDiv)
            break;
    }
    QVERIFY2(voltDiv, "找不到「电压/格」下拉框");

    // 手动选 500 mV/格
    int target = -1;
    for (int i = 0; i < voltDiv->count(); ++i) {
        if (std::abs(voltDiv->itemData(i).toDouble() - 0.5) < 1e-9) {
            target = i;
            break;
        }
    }
    QVERIFY2(target >= 0, "下拉框里应当有 500 mV 档");
    voltDiv->setCurrentIndex(target);

    // 让实时仿真推进几拍（信号峰值 2.5 V，远超 0.5×3 = 1.5 V）。
    // 用 qWait 真正跑事件循环，processEvents 循环里定时器很难被触发。
    QTest::qWait(1500);

    QCOMPARE(live->liveVoltDiv(), 0.5);
    QCOMPARE(voltDiv->currentIndex(), target);
}

void UiWaveformTest::autoScaleAlsoPicksTimeBase()
{
    // 「自动缩放」不能只缩放电压，时基也得跟着走：
    // 1 Hz 的方波用 5 ms/格 只能看到一小段，点一下应当跳到能看几个周期的档位。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
    source->setRotationAngle(90);
    Component src = source->component();
    src.function = SourceFunction::Pulse;
    src.pulseV1 = 0.0;
    src.pulseV2 = 5.0;
    src.pulsePeriod = 1.0;   // 1 Hz
    src.pulseWidth = 0.5;
    source->setComponent(src);
    auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(250, 200));
    Component r = resistor->component();
    r.value = 1000.0;
    resistor->setComponent(r);
    auto* probe = scene->placeComponent(ComponentType::Probe, QPointF(400, 200));
    QVERIFY(probe);
    scene->placeComponent(ComponentType::Ground, QPointF(100, 420));
    scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(220, 200)});
    scene->addWire({QPointF(280, 200), QPointF(400, 200)});
    scene->addWire({QPointF(100, 330), QPointF(100, 420)});
    settle();

    window.runSimulation();
    settle();
    QAction* monitor = window.findChild<QAction*>(QStringLiteral("actMonitor"));
    QVERIFY(monitor);
    monitor->setChecked(true);
    settle();
    QTabWidget* tabs = waveTabs(window);
    QVERIFY(tabs);
    tabs->setCurrentIndex(1);
    QTest::qWait(400);

    auto* live = qobject_cast<WaveformWindow*>(tabs->widget(1));
    QVERIFY(live);
    QVERIFY2(live->isLive(), "动态示波器应当在运行");

    // 手动把时基调到很小
    QComboBox* timeBox = nullptr;
    for (QComboBox* box : live->findChildren<QComboBox*>()) {
        for (int i = 0; i < box->count(); ++i) {
            if (box->itemText(i).contains(QStringLiteral("/div"))
                && !box->itemText(i).contains(QStringLiteral("V"))) {
                timeBox = box;
                break;
            }
        }
        if (timeBox)
            break;
    }
    QVERIFY2(timeBox, "找不到「时间/格」下拉框");

    int tiny = -1;
    for (int i = 0; i < timeBox->count(); ++i) {
        if (std::abs(timeBox->itemData(i).toDouble() - 5e-3) < 1e-12) {
            tiny = i;
            break;
        }
    }
    QVERIFY2(tiny >= 0, "下拉框里应当有 5 ms/div");
    timeBox->setCurrentIndex(tiny);
    QTest::qWait(200);
    QCOMPARE(live->liveTimeDiv(), 5e-3);

    // 点「自动缩放」
    QPushButton* autoBtn = nullptr;
    for (QPushButton* button : live->findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("自动缩放") && button->isVisible())
            autoBtn = button;
    }
    QVERIFY2(autoBtn, "动态示波器里应当有「自动缩放」");
    autoBtn->click();
    QTest::qWait(300);

    // 1 Hz 信号：时基应当跳到能看见若干个周期的档位（≥ 0.1 s/格）
    QVERIFY2(live->liveTimeDiv() >= 0.1,
             qPrintable(QStringLiteral("时基没跟着缩放：%1 s/div").arg(live->liveTimeDiv())));
}

void UiWaveformTest::changingTimeBaseKeepsVoltageScale()
{
    // 点完「自动缩放」再手动换时基，电压档不该被重新自动选一遍。
    // 根因是改时基会 restartLiveEngine()，那里以前把 m_liveAutoScaled
    // 重置成 false，下一拍就当成「初次量程」又跑了一次自动选档。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
    source->setRotationAngle(90);
    Component src = source->component();
    src.function = SourceFunction::Pulse;
    src.pulseV1 = 0.0;
    src.pulseV2 = 5.0;
    src.pulsePeriod = 0.02; // 50 Hz：很快就能定下量程
    src.pulseWidth = 0.01;
    source->setComponent(src);
    auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(250, 200));
    Component r = resistor->component();
    r.value = 1000.0;
    resistor->setComponent(r);
    auto* probe = scene->placeComponent(ComponentType::Probe, QPointF(400, 200));
    QVERIFY(probe);
    scene->placeComponent(ComponentType::Ground, QPointF(100, 420));
    scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(220, 200)});
    scene->addWire({QPointF(280, 200), QPointF(400, 200)});
    scene->addWire({QPointF(100, 330), QPointF(100, 420)});
    settle();

    window.runSimulation();
    settle();
    QAction* monitor = window.findChild<QAction*>(QStringLiteral("actMonitor"));
    QVERIFY(monitor);
    monitor->setChecked(true);
    settle();
    QTabWidget* tabs = waveTabs(window);
    QVERIFY(tabs);
    tabs->setCurrentIndex(1);
    QTest::qWait(700);

    auto* live = qobject_cast<WaveformWindow*>(tabs->widget(1));
    QVERIFY(live && live->isLive());

    // 等自动量程跑过一次
    QTRY_VERIFY_WITH_TIMEOUT(live->liveAutoScaled(), 5000);
    const double volts = live->liveVoltDiv();

    // 手动换时基
    QComboBox* timeBox = nullptr;
    for (QComboBox* box : live->findChildren<QComboBox*>()) {
        for (int i = 0; i < box->count(); ++i) {
            if (box->itemText(i).contains(QStringLiteral("/div"))
                && !box->itemText(i).contains(QStringLiteral("V"))) {
                timeBox = box;
                break;
            }
        }
        if (timeBox)
            break;
    }
    QVERIFY(timeBox);
    const int other = (timeBox->currentIndex() + 3) % timeBox->count();
    timeBox->setCurrentIndex(other);

    // setCurrentIndex 的槽是同步执行的，此刻还没跑任何一拍：
    // 换时基只是换时间窗，「已自动定过量程」的状态不该被清掉，
    // 否则下一拍会当成「初次量程」把电压档重选一遍
    QVERIFY2(live->liveAutoScaled(),
             "换时基后不该退回到「未自动量程」，否则电压档会被重选一遍");
    QCOMPARE(live->liveVoltDiv(), volts);

    // 再跑一会儿也不该变
    QTest::qWait(400);
    QCOMPARE(live->liveVoltDiv(), volts);
}

void UiWaveformTest::autoScaleLivesInTheLiveScope()
{
    // 静态页有框选放大 + 右键复位，不需要「自动缩放」；「示波器」按钮也多余
    //（切标签页就行）。这两个都撤掉，只把「自动缩放」放进动态示波器。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
    source->setRotationAngle(90);
    auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(250, 200));
    auto* probe = scene->placeComponent(ComponentType::Probe, QPointF(400, 200));
    QVERIFY(probe);
    scene->placeComponent(ComponentType::Ground, QPointF(100, 420));
    scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(220, 200)});
    scene->addWire({QPointF(280, 200), QPointF(400, 200)});
    scene->addWire({QPointF(100, 330), QPointF(100, 420)});
    settle();

    window.runSimulation();
    settle();
    QAction* monitor = window.findChild<QAction*>(QStringLiteral("actMonitor"));
    QVERIFY(monitor);
    monitor->setChecked(true);
    settle();

    QTabWidget* tabs = waveTabs(window);
    QVERIFY(tabs);
    auto* staticPage = qobject_cast<WaveformWindow*>(tabs->widget(0));
    auto* livePage = qobject_cast<WaveformWindow*>(tabs->widget(1));
    QVERIFY(staticPage && livePage);

    // 两页共用同一个类，控件都在树里；这里看的是「当前是否露出来」
    auto hasVisibleButton = [](WaveformWindow* page, const QString& text) {
        for (QPushButton* button : page->findChildren<QPushButton*>()) {
            if (button->text() == text && button->isVisible())
                return true;
        }
        return false;
    };

    // 静态页：两个都撤掉
    QVERIFY2(!hasVisibleButton(staticPage, QStringLiteral("自动缩放")),
             "静态波形页不该再有「自动缩放」");
    QVERIFY2(!hasVisibleButton(staticPage, QStringLiteral("示波器")),
             "静态波形页不该再有「示波器」按钮（切标签页即可）");
    // 导出还留着
    QVERIFY(hasVisibleButton(staticPage, QStringLiteral("导出 CSV")));

    // 动态示波器：有「自动缩放」
    tabs->setCurrentIndex(1);
    settle();
    QVERIFY2(hasVisibleButton(livePage, QStringLiteral("自动缩放")),
             "动态示波器里应当有「自动缩放」");
}

void UiWaveformTest::liveScopeKeepsSwitchingAtLargeTimeBase()
{
    // 20 ms/格 时窗口 200 ms，若步长被放大到 10 µs，50 kHz 的开关过程会被整个跨过去，
    // 仿真结果直接是 0 V —— 看起来就是「大时基下没有波形」
    QFile file(QStringLiteral(CSM_SOURCE_DIR "/resources/examples/08_ac_dc_supply.csm"));
    QVERIFY2(file.open(QIODevice::ReadOnly), "示例文件缺失");
    QString error;
    const Circuit circuit = Circuit::fromJson(QJsonDocument::fromJson(file.readAll()).object(),
                                              &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const Netlist net = circuit.buildNetlist();

    int probeId = -1;
    for (const Component& comp : circuit.components)
        if (comp.refdes == QStringLiteral("TP1"))
            probeId = comp.id;
    QVERIFY(probeId > 0);

    WaveformWindow live;
    live.resize(1000, 600);
    live.show();
    live.setLiveScalePreferences(0.02, 10.0, 0.001); // 20 ms/格
    settle();
    live.startLive(circuit, {{QStringLiteral("TP1"),
                              QStringLiteral("V(%1)").arg(net.nodeOf(probeId, 0))}});
    QVERIFY(live.isLive());

    // 推进到至少 10 ms 仿真时间
    QTRY_VERIFY_WITH_TIMEOUT(live.liveTime() > 0.01, 30000);

    auto* view = live.findChild<QChartView*>();
    QVERIFY(view && view->chart());
    double last = 0.0;
    for (QAbstractSeries* series : view->chart()->series())
        if (auto* line = qobject_cast<QLineSeries*>(series))
            if (!line->points().isEmpty())
                last = line->points().last().y();
    QVERIFY2(last > 5.0, qPrintable(QStringLiteral("输出 %1 V").arg(last)));
}

void UiWaveformTest::liveScopeWorksWithReferenceGround()
{
    // 测试点以 AGND 为参考时，实时示波器也要有波形（信号名是 V(a,b)）
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
    source->setRotationAngle(90);
    Component src = source->component();
    src.dcValue = 5.0;
    source->setComponent(src);
    auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(250, 200));
    Component r = resistor->component();
    r.value = 1000.0;
    resistor->setComponent(r);
    auto* probe = scene->placeComponent(ComponentType::Probe, QPointF(400, 200));
    QVERIFY(probe);
    Component p = probe->component();
    p.groundKind = GroundKind::Agnd;
    probe->setComponent(p);
    scene->placeComponent(ComponentType::Ground, QPointF(100, 400));
    auto* agnd = scene->placeComponent(ComponentType::Ground, QPointF(400, 400));
    Component g = agnd->component();
    g.groundKind = GroundKind::Agnd;
    agnd->setComponent(g);

    scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(220, 200)});
    scene->addWire({QPointF(280, 200), QPointF(400, 200), QPointF(400, 300)});
    scene->addWire({QPointF(100, 330), QPointF(100, 400)});
    scene->addWire({QPointF(400, 300), QPointF(400, 400)});
    settle();

    QAction* run = window.findChild<QAction*>(QStringLiteral("actRun"));
    QVERIFY(run);
    run->trigger();
    settle();

    auto* live = livePanel(window);
    QVERIFY(live);
    QVERIFY(live->isLive());
    QTRY_VERIFY_WITH_TIMEOUT(live->liveTime() > 0.005, 20000);

    auto* view = live->findChild<QChartView*>();
    QVERIFY(view && view->chart());
    int points = 0;
    for (QAbstractSeries* series : view->chart()->series())
        if (auto* line = qobject_cast<QLineSeries*>(series))
            points = std::max(points, int(line->count()));
    QVERIFY2(points > 5, qPrintable(QStringLiteral("点数=%1").arg(points)));

    run->trigger();
    settle();
}

void UiWaveformTest::constantSignalKeepsUsableAxis()
{
    // 纯直流电路：波形是水平直线，Y 轴不能塌成一条线
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
    source->setRotationAngle(90);
    Component src = source->component();
    src.dcValue = 5.0;
    source->setComponent(src);

    auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(250, 200));
    Component r = resistor->component();
    r.value = 1000.0;
    resistor->setComponent(r);

    auto* probe = scene->placeComponent(ComponentType::Probe, QPointF(400, 250));
    QVERIFY(probe);
    scene->placeComponent(ComponentType::Ground, QPointF(100, 400));
    scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(220, 200)});
    scene->addWire({QPointF(280, 200), QPointF(400, 200), QPointF(400, 250)});
    scene->addWire({QPointF(100, 330), QPointF(100, 400)});
    settle();

    window.runSimulation();
    settle();
    QAction* monitor = window.findChild<QAction*>(QStringLiteral("actMonitor"));
    QVERIFY(monitor);
    monitor->setChecked(true);
    settle();

    QTabWidget* tabs = waveTabs(window);
    QVERIFY(tabs);
    tabs->setCurrentIndex(0);
    settle();

    auto* plot = qobject_cast<WaveformWindow*>(tabs->widget(0));
    QVERIFY(plot);
    auto* view = plot->findChild<QChartView*>();
    QVERIFY(view);
    QChart* chart = view->chart();
    QVERIFY(chart);

    // 找出纵向数值轴：上下限必须拉开，否则曲线会贴着底边
    bool foundVertical = false;
    // Qt6 里 QChart::axes() 无参版本返回全部轴，带参数版本按方位筛选
    const QList<QAbstractAxis*> all = chart->axes();
    for (QAbstractAxis* axis : all) {
        if (axis->type() != QAbstractAxis::AxisTypeValue)
            continue;
        if (axis->alignment() != Qt::AlignLeft && axis->alignment() != Qt::AlignRight)
            continue; // 跳过横轴
        auto* value = static_cast<QValueAxis*>(axis);
        foundVertical = true;
        QVERIFY2(value->max() > value->min(),
                 qPrintable(QStringLiteral("Y 轴塌成一条线: %1..%2")
                                .arg(value->min())
                                .arg(value->max())));
        QVERIFY2(value->max() - value->min() > 0.5,
                 qPrintable(QStringLiteral("Y 轴范围过窄: %1..%2")
                                .arg(value->min())
                                .arg(value->max())));
    }
    QVERIFY2(foundVertical, "应当有纵向数值轴");
}

void UiWaveformTest::scopeScalesAreRemembered()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().contains(QStringLiteral("RC 充电"))) {
            action->trigger();
            break;
        }
    }
    settle();

    QAction* run = window.findChild<QAction*>(QStringLiteral("actRun"));
    QVERIFY(run);
    run->trigger();
    settle();

    auto* live = livePanel(window);
    QVERIFY(live);
    QVERIFY(live->isLive());

    QComboBox* timeDiv = nullptr;
    for (QComboBox* box : live->findChildren<QComboBox*>()) {
        if (box->count() > 0 && box->itemText(0).startsWith(QStringLiteral("5 ns")))
            timeDiv = box;
    }
    QVERIFY(timeDiv);

    // 改成 1 ms/div
    int target = -1;
    for (int i = 0; i < timeDiv->count(); ++i) {
        if (timeDiv->itemText(i) == QStringLiteral("1 ms/div"))
            target = i;
    }
    QVERIFY(target >= 0);
    timeDiv->setCurrentIndex(target);
    settle();
    QCOMPARE(live->liveTimeDiv(), 1e-3);

    // 停止再启动，刻度应当沿用（已经存进工程参数里）
    run->trigger();
    settle();
    QVERIFY(!live->isLive());
    run->trigger();
    settle();
    QVERIFY(live->isLive());
    QCOMPARE(live->liveTimeDiv(), 1e-3);
    QCOMPARE(timeDiv->currentText(), QStringLiteral("1 ms/div"));

    run->trigger();
    settle();
}

void UiWaveformTest::analysisFieldsFollowFile()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    // 载入示例：示例文件里的 analysis 段应当反映到静态波形页的输入框
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().contains(QStringLiteral("RC 充电"))) {
            action->trigger();
            break;
        }
    }
    settle();

    QTabWidget* tabs = waveTabs(window);
    QVERIFY(tabs);
    auto* plot = qobject_cast<WaveformWindow*>(tabs->widget(0));
    QVERIFY(plot);

    // 示例文件里 tStop = 5 ms、tStep = 10 µs
    QLineEdit* stopEdit = nullptr;
    QLineEdit* stepEdit = nullptr;
    for (QLineEdit* edit : plot->findChildren<QLineEdit*>()) {
        if (edit->text().contains(QStringLiteral("5 ms")) || edit->text().contains(QStringLiteral("5ms")))
            stopEdit = edit;
        if (edit->text().contains(QStringLiteral("10 µs")) || edit->text().contains(QStringLiteral("10µs")))
            stepEdit = edit;
    }
    QVERIFY2(stopEdit, "停止时间应从文件读取");
    QVERIFY2(stepEdit, "最大步长应从文件读取");

    // 分析类型也应当是文件里的瞬态
    QComboBox* kind = nullptr;
    for (QComboBox* box : plot->findChildren<QComboBox*>()) {
        if (box->count() == 3 && box->itemText(0) == QStringLiteral("瞬态"))
            kind = box;
    }
    QVERIFY(kind);
    QCOMPARE(kind->currentText(), QStringLiteral("瞬态"));
}

void UiWaveformTest::liveScopePicksUsableRange()
{
    // 220 V 正弦整流：默认 5 V/格 会把波形顶出屏幕，启动时应自动选到合适档位
    Circuit circuit;
    Component v;
    v.id = 1;
    v.type = ComponentType::VoltageSource;
    v.pos = QPointF(100, 300);
    v.rotation = 90;
    v.refdes = QStringLiteral("V1");
    v.function = SourceFunction::Sine;
    v.amplitude = 220.0 / std::sqrt(2.0); // 峰值 220.0 V
    v.frequency = 50.0;
    circuit.components.append(v);

    Component r;
    r.id = 2;
    r.type = ComponentType::Resistor;
    r.pos = QPointF(200, 200);
    r.refdes = QStringLiteral("R1");
    r.value = 1000.0;
    circuit.components.append(r);

    Component r2 = r;
    r2.id = 3;
    r2.pos = QPointF(300, 300);
    r2.rotation = 90;
    r2.refdes = QStringLiteral("R2");
    r2.value = 1000.0;
    circuit.components.append(r2);

    Component g;
    g.id = 4;
    g.type = ComponentType::Ground;
    g.pos = QPointF(200, 400);
    g.refdes = QStringLiteral("GND");
    circuit.components.append(g);

    circuit.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(170, 200)}});
    circuit.wires.append(Wire{{QPointF(230, 200), QPointF(300, 200), QPointF(300, 270)}});
    circuit.wires.append(Wire{{QPointF(300, 330), QPointF(300, 400), QPointF(100, 400),
                               QPointF(100, 330)}});

    const Netlist net = circuit.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    const int node = net.nodeOf(2, 1);
    QVERIFY(node > 0);

    WaveformWindow wave;
    wave.resize(1000, 600);
    wave.startLive(circuit,
                   {{QStringLiteral("TP1 (V(%1))").arg(node), QStringLiteral("V(%1)").arg(node)}});
    wave.show();
    settle();
    QVERIFY(wave.isLive());

    QComboBox* voltDiv = nullptr;
    for (QComboBox* box : wave.findChildren<QComboBox*>()) {
        if (box->count() > 0 && box->itemText(0) == QStringLiteral("1 mV"))
            voltDiv = box;
    }
    QVERIFY(voltDiv);
    // 峰值约 110 V，需要 3 格能容下。
    // 量程要等实时仿真攒够采样点才会自动选定，这里轮询而不是固定睡眠。
    QTRY_VERIFY_WITH_TIMEOUT(voltDiv->currentData().toDouble() * 3.0 >= 110.0, 5000);
}

void UiWaveformTest::liveScopeFromMenu()
{
    MainWindow window;
    window.resize(1400, 880);
    window.show();
    waitShown(window);

    QAction* rcExample = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().contains(QStringLiteral("RC 充电")))
            rcExample = action;
    }
    QVERIFY(rcExample);
    rcExample->trigger();
    settle();

    // 「波形监控」面板里切到动态示波器标签页
    openMonitor(window, 1);

    auto* wave = livePanel(window);
    QVERIFY(wave);
    QVERIFY(wave->isVisible());
    QVERIFY(wave->isLive());
    // 实时仿真的定时器每 33ms 推进一步。等它攒到半个窗口（约 2ms）再截图，
    // 否则截出来是一张空图；轮询在条件满足后立刻返回，不会多等。
    QTRY_VERIFY_WITH_TIMEOUT(wave->liveTime() > 2e-3, 5000);
    QVERIFY(saveShot(wave, QStringLiteral("live_scope.png")));

    // 时间窗下拉应覆盖 5 ns ~ 100 s
    QComboBox* windowBox = nullptr;
    for (QComboBox* box : wave->findChildren<QComboBox*>()) {
        if (box->count() > 0 && box->itemText(0).startsWith(QStringLiteral("5 ns")))
            windowBox = box;
    }
    QVERIFY(windowBox);
    QCOMPARE(windowBox->count(), 32);
    windowBox->setCurrentIndex(windowBox->count() - 1);
    settle();
    QVERIFY(wave->isLive());
}

void UiWaveformTest::captureAcSweep()
{
    MainWindow window;
    window.resize(1400, 880);
    window.show();
    waitShown(window);

    QAction* acExample = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().contains(QStringLiteral("低通")))
            acExample = action;
    }
    QVERIFY(acExample);
    acExample->trigger();
    settle();

    window.runSimulation();
    openStaticWave(window);
    auto* wave = waitWaveWindow(window);
    QVERIFY(saveShot(wave, QStringLiteral("waveform_ac.png")));
}

void UiWaveformTest::captureDiodeRectifier()
{
    MainWindow window;
    window.resize(1400, 880);
    window.show();
    waitShown(window);

    QAction* diodeExample = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().contains(QStringLiteral("整流")))
            diodeExample = action;
    }
    QVERIFY(diodeExample);
    diodeExample->trigger();
    settle();

    window.runSimulation();
    openStaticWave(window);
    auto* wave = waitWaveWindow(window);
    QVERIFY(saveShot(wave, QStringLiteral("waveform_diode.png")));
}

void UiWaveformTest::captureOpAmp()
{
    MainWindow window;
    window.resize(1400, 880);
    window.show();
    waitShown(window);

    QAction* opAmpExample = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().contains(QStringLiteral("运放")))
            opAmpExample = action;
    }
    QVERIFY(opAmpExample);
    opAmpExample->trigger();
    settle();

    const QString dir = QCoreApplication::applicationDirPath();
    QVERIFY(saveShot(&window, QStringLiteral("opamp_schematic.png")));

    // 选中运放后，属性面板应显示“开环增益”
    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    for (ComponentItem* item : scene->componentItems()) {
        if (item->component().type == ComponentType::OpAmp)
            item->setSelected(true);
    }
    settle();
    bool gainLabelFound = false;
    for (QLabel* label : window.findChildren<QLabel*>()) {
        if (label->text() == QStringLiteral("开环增益"))
            gainLabelFound = true;
    }
    QVERIFY2(gainLabelFound, "属性面板缺少开环增益字段");

    window.runSimulation();
    openStaticWave(window);
    auto* wave = waitWaveWindow(window);

    auto* list = wave->findChild<QListWidget*>();
    QVERIFY(list);
    QCOMPARE(list->count(), 4);
    QVERIFY(list->item(0)->text().startsWith(QStringLiteral("TP1")));
    QVERIFY(list->item(1)->text().startsWith(QStringLiteral("TP2")));
    QVERIFY(list->item(2)->text().startsWith(QStringLiteral("TP3")));
    QVERIFY(list->item(3)->text().startsWith(QStringLiteral("IP1")));
    for (int i = 0; i < list->count(); ++i) {
        QCOMPARE(list->item(i)->checkState(), Qt::Checked);
        QVERIFY2(!list->item(i)->text().startsWith(QStringLiteral("V(")),
                 qPrintable(list->item(i)->text()));
    }

    QVERIFY(saveShot(wave, QStringLiteral("waveform_opamp.png")));
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // QTest 的日志默认写 stdout，而 Windows CI 上**这个进程的 stdout 收不到**
    // （实测一行都没进 ctest 的日志，QTest 的 PASS/FAIL 和 qwarn 全丢）。
    // 所以让它写到文件，跑完原样倒到 stderr —— stderr 各平台都通。
    QStringList args;
    for (int i = 0; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    const QString logPath = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("waveform-qtest.log"));
    QFile::remove(logPath);
    args << QStringLiteral("-o") << logPath + QStringLiteral(",txt");

    UiWaveformTest testCase;
    const int failures = QTest::qExec(&testCase, args);

    QFile logFile(logPath);
    if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray data = logFile.readAll();
        logFile.close();
        std::fwrite(data.constData(), 1, size_t(data.size()), stderr);
        std::fflush(stderr);
    }
    QFile::remove(logPath);
    return failures;
}

#include "test_ui_waveform.moc"
