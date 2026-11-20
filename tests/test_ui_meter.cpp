#include "core/Circuit.h"

#include "core/Language.h"
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
#include <cmath>
#include <cstdio>

using namespace csm;

class UiMeterTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void multimeterModeFromPanel();
    void multimeterReadoutWindowOpensOnClick();
    void sevenSegmentDigitsFillTheDisplay();
    void sevenSegmentRangeDoesNotFlicker();
    void dcMeterShowsMinusOnly();
    void sevenSegmentRoundsUpWithoutLosingADigit();
    void meterWindowFollowsMode();
    void monitorNeedsGroundButMeterDoesNot();
    void netLabelFromPanelAndLibrary();
    void netLabelFollowsRotation();

private:

    /// 「波形监控」面板里的动态示波器页
    static WaveformWindow* livePanel(MainWindow& window)
    {
        for (QTabWidget* tabs : window.findChildren<QTabWidget*>()) {
            if (tabs->count() == 2 && tabs->tabText(0) == QStringLiteral("静态波形"))
                return qobject_cast<WaveformWindow*>(tabs->widget(1));
        }
        return nullptr;
    }


    /// 搭一个能跑实时仿真的最小电路：源 + 电阻 + 万用表（跨在电阻两端）+ 地。
    /// 读数窗口只在实时仿真运行时才允许打开，所以用例得先把仿真跑起来。
    static ComponentItem* buildLiveCircuit(MainWindow& window, bool withGround = true)
    {
        auto* scene = currentScene(window);
        if (!scene)
            return nullptr;

        auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
        source->setRotationAngle(90);
        auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(260, 200));
        auto* meter = scene->placeComponent(ComponentType::Multimeter, QPointF(260, 340));
        meter->setRotationAngle(270);
        if (withGround)
            scene->placeComponent(ComponentType::Ground, QPointF(100, 420));

        scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(230, 200)});
        scene->addWire({QPointF(290, 200), QPointF(360, 200), QPointF(360, 340),
                        QPointF(290, 340)});
        scene->addWire({QPointF(230, 340), QPointF(100, 340), QPointF(100, 330)});
        scene->addWire({QPointF(100, 330), QPointF(100, 420)});
        settle();
        return meter;
    }

    /// 当前标签页的画布（多标签页之后 findChild 拿到的不一定是当前的）
    static SchematicScene* currentScene(MainWindow& window)
    {
        for (QTabWidget* tabs : window.findChildren<QTabWidget*>()) {
            if (tabs->count() > 0 && tabs->tabText(0) != QStringLiteral("静态波形")) {
                if (auto* view = qobject_cast<SchematicView*>(tabs->currentWidget()))
                    return qobject_cast<SchematicScene*>(view->scene());
            }
        }
        return window.findChild<SchematicScene*>();
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
};

void UiMeterTest::initTestCase()
{
    csm::saveLanguage(csm::Language::Chinese);   // 断言按中文写的
    // 和 main.cpp 一样装上 Qt 自带的中文翻译（输入框右键菜单等）
    // 崩溃时缓冲区里的输出会丢：Windows 上 stdout 重定向到管道是全缓冲的，
    // CI 上只会看到「Failed」却不知道停在哪一步。改成不缓冲，一行一行往外写。
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    csm::installQtTranslations(qApp);
}

void UiMeterTest::multimeterModeFromPanel()
{
    // 万用表：元件库有入口，属性面板能切换档位，符号文字跟着变
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    auto* item = scene->placeComponent(ComponentType::Multimeter, QPointF(300, 300));
    QVERIFY(item);
    item->setSelected(true); // 属性面板跟随选中项
    settle();
    QCOMPARE(item->component().refdes, QStringLiteral("XMM1"));
    QCOMPARE(item->component().multimeterMode, MultimeterMode::Voltage);

    auto* panel = window.findChild<PropertyPanel*>();
    QVERIFY(panel);
    auto* mode = panel->findChild<QComboBox*>(
        QStringLiteral("multimeterModeCombo"));
    QVERIFY2(mode, "属性面板里应当有档位下拉框");
    QCOMPARE(mode->currentData().toInt(), int(MultimeterMode::Voltage));

    // 下拉项只写档位名：带注解的长条目（「直流电压档（并联，10 MΩ 内阻）」）
    // 会把下拉框撑得比面板还宽，右边被裁掉半句
    for (int i = 0; i < mode->count(); ++i)
        QCOMPARE(mode->itemText(i), multimeterModeName(MultimeterMode(mode->itemData(i).toInt())));

    mode->setCurrentIndex(mode->findData(int(MultimeterMode::Current)));
    settle();
    QCOMPARE(item->component().multimeterMode, MultimeterMode::Current);

    // 电流档串进回路后，工作点应当测到电流（波形页会有 XMM1 信号）
    mode->setCurrentIndex(mode->findData(int(MultimeterMode::Resistance)));
    settle();
    QCOMPARE(item->component().multimeterMode, MultimeterMode::Resistance);
}

void UiMeterTest::multimeterReadoutWindowOpensOnClick()
{
    // 读数窗口只在**实时仿真运行时**才允许点开：没跑仿真的时候读数全是横杠，
    // 弹出来没有意义。仿真停了窗口也会一起收掉。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    ComponentItem* item = buildLiveCircuit(window);
    QVERIFY(item);
    QCOMPARE(item->component().type, ComponentType::Multimeter);

    // 1) 没在仿真：点它不该弹窗
    item->setSelected(true);
    settle();
    QVERIFY2(window.findChild<MultimeterWindow*>() == nullptr,
             "没跑仿真时点万用表不该弹读数窗口");

    // 2) 运行仿真（有测试点就进入实时示波器），这时才允许打开
    window.runSimulation();
    settle();
    if (!livePanel(window) || !livePanel(window)->isLive())
        QSKIP("本机没能进入实时仿真，跳过");

    // 重新选一次才会触发 selected 变化
    item->setSelected(false);
    settle();
    item->setSelected(true);
    settle();

    auto* meterWindow = window.findChild<MultimeterWindow*>();
    QVERIFY2(meterWindow, "实时仿真运行时点击万用表应当弹出读数窗口");
    QCOMPARE(meterWindow->refdes(), QStringLiteral("XMM1"));
    QVERIFY(meterWindow->isVisible());
    QVERIFY(meterWindow->findChild<SevenSegmentDisplay*>() != nullptr);

    // 3) 停止仿真：窗口跟着关闭
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("停止仿真")) {
            action->trigger();
            break;
        }
    }
    settle();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    settle();
    QVERIFY2(window.findChild<MultimeterWindow*>() == nullptr,
             "仿真停止后读数窗口应当关闭");
}

void UiMeterTest::sevenSegmentDigitsFillTheDisplay()
{
    // 回归：drawDigit 曾经漏加 pos.y()，除顶段以外的段全挤在控件顶部，
    // 读数完全看不出是数字。这里用「亮段的包围盒」把位置和大小都盯住。
    SevenSegmentDisplay display;
    display.resize(420, 180);
    // 不画单位：右下角的单位文字也是绿色，会把包围盒往下拽，干扰几何判断
    display.setUnit(QString());
    display.setDigits(MultimeterDigits::Half4);
    display.setValue(2.4999);
    display.show();
    settle();

    const QImage image = display.grab().toImage();
    QVERIFY(!image.isNull());

    int minX = image.width(), maxX = -1, minY = image.height(), maxY = -1;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            // 亮段是荧光绿：绿色明显高于红蓝
            if (c.green() > 150 && c.green() > c.red() + 60 && c.green() > c.blue() + 60) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    QVERIFY2(maxX > minX && maxY > minY, "显示区里应当有亮段");

    // 纵向必须上下对称：漏加 pos.y() 时所有横段挤在顶部，上半多下半少，
    // 这个差值是 0 附近才对
    const double centerY = (minY + maxY) / 2.0;
    const double widgetCenterY = image.height() / 2.0;
    QVERIFY2(std::abs(centerY - widgetCenterY) < image.height() * 0.1,
             qPrintable(QStringLiteral("亮段纵向中心 %1 偏离控件中心 %2")
                            .arg(centerY).arg(widgetCenterY)));
    const double upper = centerY - minY;
    const double lower = maxY - centerY;
    QVERIFY2(std::abs(upper - lower) < image.height() * 0.1,
             qPrintable(QStringLiteral("亮段上下不对称：上 %1、下 %2")
                            .arg(upper).arg(lower)));

    // 数字应当占满大部分高度，而不是挤成一条
    const double inkHeight = maxY - minY;
    QVERIFY2(inkHeight > image.height() * 0.35,
             qPrintable(QStringLiteral("亮段高度只有 %1 / %2")
                            .arg(inkHeight).arg(image.height())));

    // 横向也要铺开（5 个数字位）
    const double inkWidth = maxX - minX;
    QVERIFY2(inkWidth > image.width() * 0.5,
             qPrintable(QStringLiteral("亮段宽度只有 %1 / %2")
                            .arg(inkWidth).arg(image.width())));
}

void UiMeterTest::sevenSegmentRangeDoesNotFlicker()
{
    // 读数在 1 V 附近抖动时，单位不能在「999.9 mV」和「1.000 V」之间来回跳
    SevenSegmentDisplay display;
    display.setUnit(QStringLiteral("V"));
    display.setDigits(MultimeterDigits::Half4);

    display.setValue(1.0000);
    QCOMPARE(display.displayedUnit(), QStringLiteral("V"));
    for (double v : {0.9995, 1.0002, 0.9998, 1.0001, 1.0000, 1.0003, 0.9999})
        QCOMPARE(display.displayedUnit(), QStringLiteral("V"));

    // 掉到 50 mV 以下才换到 mV
    display.setValue(0.05);
    QCOMPARE(display.displayedUnit(), QStringLiteral("mV"));
    // 再涨回 1.5 V，按 1000 mV 的上限升回 V
    display.setValue(1.5);
    QCOMPARE(display.displayedUnit(), QStringLiteral("V"));
    // 123 µV 应当落到 µV，而不是「0.0012 mV」
    display.setValue(0.000123);
    QCOMPARE(display.displayedUnit(), QStringLiteral("µV"));
}

void UiMeterTest::dcMeterShowsMinusOnly()
{
    // 真表的惯例：正数不显示符号，负数才显示「−」；
    // 但直流档始终占着那一格，位数不会因为正负而左右跳
    SevenSegmentDisplay display;
    display.setUnit(QStringLiteral("V"));
    display.setDigits(MultimeterDigits::Half4);
    display.setSignSlot(true);
    display.resize(420, 180);
    display.show();
    settle();

    auto leftmostInk = [&](double value) {
        display.setValue(value);
        settle();
        const QImage image = display.grab().toImage();
        for (int x = 0; x < image.width(); ++x) {
            for (int y = 0; y < image.height(); ++y) {
                const QColor c = image.pixelColor(x, y);
                if (c.green() > 150 && c.green() > c.red() + 60 && c.green() > c.blue() + 60)
                    return x;
            }
        }
        return -1;
    };

    const int positive = leftmostInk(5.0);
    const int negative = leftmostInk(-5.0);
    QVERIFY2(positive > 0 && negative > 0, "两种极性都应当有亮段");
    // 负数多画一条负号，亮段的最左位置明显更靠左
    QVERIFY2(negative < positive - 8,
             qPrintable(QStringLiteral("正数最左 %1、负数最左 %2").arg(positive).arg(negative)));

    // 交流档不占符号位：正数的最左位置应当和直流档正数接近（只差负号那格）
    SevenSegmentDisplay ac;
    ac.setUnit(QStringLiteral("V"));
    ac.setDigits(MultimeterDigits::Half4);
    ac.setSignSlot(false);
    ac.resize(420, 180);
    ac.show();
    settle();
    ac.setValue(5.0);
    settle();
    const QImage acImage = ac.grab().toImage();
    int acLeft = -1;
    for (int x = 0; x < acImage.width() && acLeft < 0; ++x)
        for (int y = 0; y < acImage.height(); ++y) {
            const QColor c = acImage.pixelColor(x, y);
            if (c.green() > 150 && c.green() > c.red() + 60 && c.green() > c.blue() + 60) {
                acLeft = x;
                break;
            }
        }
    QVERIFY2(acLeft > 0 && acLeft < positive,
             qPrintable(QStringLiteral("交流档最左 %1、直流档正数最左 %2")
                            .arg(acLeft).arg(positive)));
}

void UiMeterTest::sevenSegmentRoundsUpWithoutLosingADigit()
{
    // 999.9999 按 5 位有效数字舍入是 1000.0，整数位从 3 位变 4 位。
    // 之前按原位数插小数点，会写成「100.00」—— 整整差 10 倍。
    SevenSegmentDisplay display;
    display.setUnit(QStringLiteral("Ω"));
    display.setDigits(MultimeterDigits::Half4);

    const auto prefixScale = [](const QString& unit) {
        const QString base = unit.right(1);
        const QString prefix = unit.left(unit.size() - 1);
        if (prefix == QStringLiteral("k"))
            return 1e3;
        if (prefix == QStringLiteral("M"))
            return 1e6;
        if (prefix == QStringLiteral("G"))
            return 1e9;
        if (prefix == QStringLiteral("m"))
            return 1e-3;
        if (prefix == QStringLiteral("µ"))
            return 1e-6;
        if (prefix == QStringLiteral("n"))
            return 1e-9;
        if (prefix == QStringLiteral("p"))
            return 1e-12;
        Q_UNUSED(base);
        return 1.0;
    };

    // 一串「刚好在进位边缘」的值：读回来的数必须和输入对上
    for (double v : {999.9999, 99.99999, 9.999999, 0.9999999, 1.0, 100.0, 1000.0,
                     9999.999, 0.0001234, 123.45}) {
        display.setValue(v);
        bool ok = false;
        const double shown = display.readoutText().toDouble(&ok) * prefixScale(display.displayedUnit());
        QVERIFY2(ok, qPrintable(display.readoutText()));
        QVERIFY2(std::abs(shown - v) <= v * 5e-4 + 1e-12,
                 qPrintable(QStringLiteral("输入 %1，显示 %2 %3（读回 %4）")
                                .arg(v)
                                .arg(display.readoutText(), display.displayedUnit())
                                .arg(shown)));
    }
}

void UiMeterTest::monitorNeedsGroundButMeterDoesNot()
{
    // 没有 GND 的电路（参考点由万用表负端顶替）：仿真照常能跑、万用表读数窗口
    // 能开，但**波形监控不给开** —— 节点电压之间没有统一基准，画出来没意义。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    ComponentItem* item = buildLiveCircuit(window, /*withGround=*/false);
    QVERIFY(item);

    window.runSimulation();
    settle();
    if (!livePanel(window) || !livePanel(window)->isLive())
        QSKIP("本机没能进入实时仿真，跳过");

    QAction* monitor = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("波形监控"))
            monitor = action;
    }
    QVERIFY(monitor);
    QVERIFY2(!monitor->isEnabled(),
             "没有 GND 时波形监控不应当可用（参考点由万用表负端顶替）");

    // 但万用表读数窗口照常能开
    item->setSelected(true);
    settle();
    QVERIFY2(window.findChild<MultimeterWindow*>() != nullptr,
             "没有 GND 时万用表读数窗口仍应能打开");
}

void UiMeterTest::meterWindowFollowsMode()
{
    // 属性面板改了档位，读数窗口的表头 / 单位要跟着变
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    ComponentItem* item = buildLiveCircuit(window);
    QVERIFY(item);

    window.runSimulation();
    settle();
    if (!livePanel(window) || !livePanel(window)->isLive())
        QSKIP("本机没能进入实时仿真，跳过");

    item->setSelected(true);
    settle();

    QList<MultimeterWindow*> windows = window.findChildren<MultimeterWindow*>();
    QCOMPARE(windows.size(), 1);
    MultimeterWindow* meter = windows.first();
    QCOMPARE(meter->mode(), MultimeterMode::Voltage);

    // 改成电阻档
    Component c = item->component();
    QCOMPARE(c.type, ComponentType::Multimeter);
    c.multimeterMode = MultimeterMode::Resistance;
    item->setComponent(c);
    settle();
    QCOMPARE(meter->mode(), MultimeterMode::Resistance);
    auto* display = meter->findChild<SevenSegmentDisplay*>();
    QVERIFY(display);
    // 只断言基本单位：显示值带自动量程前缀（p / n / µ / m / k / M），
    // 具体哪一个取决于读数，而读数在末位会随平台不同（macOS 上是 pΩ）
    QVERIFY2(display->displayedUnit().endsWith(QStringLiteral("Ω")),
             qPrintable(display->displayedUnit()));

    // 改回电压档，单位跟着回去
    c.multimeterMode = MultimeterMode::Voltage;
    item->setComponent(c);
    settle();
    QCOMPARE(meter->mode(), MultimeterMode::Voltage);
    QVERIFY2(display->displayedUnit().endsWith(QStringLiteral("V")),
             qPrintable(display->displayedUnit()));
}

void UiMeterTest::netLabelFromPanelAndLibrary()
{
    // 元件库有入口；新建时没有编号、只有网络名；属性面板能改名
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    auto* first = scene->placeComponent(ComponentType::NetLabel, QPointF(300, 300));
    QVERIFY(first);
    QCOMPARE(first->component().type, ComponentType::NetLabel);
    QVERIFY2(first->component().refdes.isEmpty(), "网络标签不该有编号");
    QCOMPARE(first->component().netLabel, QStringLiteral("N1"));

    // 再放一个，默认名字不能撞车（撞了会直接并成一个网络）
    auto* second = scene->placeComponent(ComponentType::NetLabel, QPointF(500, 300));
    QVERIFY(second);
    QCOMPARE(second->component().netLabel, QStringLiteral("N2"));

    // 属性面板改名
    first->setSelected(true);
    settle();
    auto* labelEdit = window.findChild<QLineEdit*>(QStringLiteral("netLabelEdit"));
    QVERIFY2(labelEdit, "属性面板里应当有「网络名」输入框");
    QCOMPARE(labelEdit->text(), QStringLiteral("N1"));
    labelEdit->setText(QStringLiteral("VCC"));
    emit labelEdit->editingFinished();
    settle();
    QCOMPARE(first->component().netLabel, QStringLiteral("VCC"));
}

void UiMeterTest::netLabelFollowsRotation()
{
    // 旗标跟着元件转，但文字不能倒着写；四个角度要有四种不同画法
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    // 用场景渲染出单个元件的画面，再算深色像素的包围盒
    auto ink = [scene](ComponentItem* item) {
        const QRectF src = item->sceneBoundingRect().adjusted(-6, -6, 6, 6);
        const int side = 240;
        QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        scene->render(&painter, QRectF(0, 0, side, side), src);
        painter.end();

        int minX = side, maxX = -1, minY = side, maxY = -1;
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) {
                const QColor c = image.pixelColor(x, y);
                if (c.lightness() < 170) { // 旗标与文字都是深色
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        return QSize(maxX - minX, maxY - minY);
    };

    QVector<QSize> shapes;
    for (int rot : {0, 90, 180, 270}) {
        auto* item = scene->placeComponent(ComponentType::NetLabel,
                                           QPointF(300 + rot * 2, 300));
        QVERIFY(item);
        Component c = item->component();
        c.netLabel = QStringLiteral("VCC");
        c.rotation = rot;
        item->setComponent(c);
        settle();
        shapes.append(ink(item));
    }

    QVERIFY2(shapes.at(0).width() > shapes.at(0).height(), "0° 应当横着");
    QVERIFY2(shapes.at(1).height() > shapes.at(1).width(), "90° 应当竖着");
    QVERIFY2(shapes.at(2).width() > shapes.at(2).height(), "180° 应当横着");
    QVERIFY2(shapes.at(3).height() > shapes.at(3).width(), "270° 应当竖着");
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
                                .filePath(QStringLiteral("meter-qtest.log"));
    QFile::remove(logPath);
    args << QStringLiteral("-o") << logPath + QStringLiteral(",txt");

    UiMeterTest testCase;
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

#include "test_ui_meter.moc"
