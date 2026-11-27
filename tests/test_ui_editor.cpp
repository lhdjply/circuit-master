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

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QGraphicsSceneMouseEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QSet>
#include <QTabWidget>
#include <QtTest/QTest>
#include <QTest>
#include <QWidget>
#include <cmath>
#include <cstdio>

using namespace csm;

class UiEditorTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void componentLabelsCanBeDragged();
    void logicGatePinsStayOnGrid();
    void alignAndDistributeDoTheExpectedThing();
    void switchTogglesOnClickOnlyWhileSimulating();
    void pushButtonActsOnlyWhileHeld();
    void hitAreaFollowsRotation();
    void probeStaysClickableWhenRotated();
    void splitDrawnWiresKeepJunction();
    void crossingCanBecomeJunction();
    void pasteKeepsWiresStraight();
    void snapWiresRepairsSkewedGeometry();
    void pastedWiresSnapToPins();
    void spaceRotatesSelection();
    void undoRedoEdits();
    void dragComponentFromLibrary();
    void copyPasteAndMirror();
    void multiSelectionDrag();
    void movingComponentDragsItsWires();
    void draggingAnchoredWireStretchesInsteadOfDetaching();
    void clickingWireHighlightsOnlyThatSegment();
    void clickingWireKeepsItSelected();
    void drawnWireMergesWithNeighbour();
    void cutCopyPasteFromEditMenu();
    void wireCanBeSelectedAndDeleted();
    void dragWireEndpointKeepsOtherEnd();
    void drawWireWithMouse();
    void escCancelsWireThroughTheAction();
    void measureProbeReadsTheWire();
    void measureProbeUsesTheNearerWireEnd();
    void measureProbeSumsTheSegmentCurrent();
    void measureProbeBoxStaysReadableWhenRotated();
    void shortcutsWorkOnANewTab();
    void draggingAComponentDoesNotPullUnrelatedWires();

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


    /// 当前标签页对应的画布。
    /// 「新建」「打开」都是开新标签页，而 findChild 拿到的是第一个（可能已在后台），
    /// 所以要按「当前可见的 view」反查它自己的 scene。
    static SchematicScene* currentScene(MainWindow& window)
    {
        SchematicView* view = currentView(window);
        return view ? qobject_cast<SchematicScene*>(view->scene()) : nullptr;
    }

    static SchematicView* currentView(MainWindow& window)
    {
        for (QTabWidget* tabs : window.findChildren<QTabWidget*>()) {
            if (tabs->count() > 0 && tabs->tabText(0) != QStringLiteral("静态波形")) {
                auto* view = qobject_cast<SchematicView*>(tabs->currentWidget());
                if (view)
                    return view;
            }
        }
        return nullptr;
    }


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
};

void UiEditorTest::initTestCase()
{
    csm::saveLanguage(csm::Language::Chinese);   // 断言按中文写的
    // 和 main.cpp 一样装上 Qt 自带的中文翻译（输入框右键菜单等）
    // 崩溃时缓冲区里的输出会丢：Windows 上 stdout 重定向到管道是全缓冲的，
    // CI 上只会看到「Failed」却不知道停在哪一步。改成不缓冲，一行一行往外写。
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    csm::installQtTranslations(qApp);
}

void UiEditorTest::pushButtonActsOnlyWhileHeld()
{
    // 按钮开关是瞬时的：按住才动作，松手立刻回到静止状态。
    // 电路和开关那条用例一样（5 V → R1 → 按钮 → 地，测点在 R1 与按钮之间）
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    SchematicScene* scene = currentScene(window);
    SchematicView* view = currentView(window);
    QVERIFY(scene);
    QVERIFY(view);

    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(150, 300));
    source->setRotationAngle(90);
    Component src = source->component();
    src.dcValue = 5.0;
    source->setComponent(src);

    scene->placeComponent(ComponentType::Resistor, QPointF(300, 200));
    auto* button = scene->placeComponent(ComponentType::PushButton, QPointF(450, 200));
    scene->placeComponent(ComponentType::Probe, QPointF(330, 140));
    scene->placeComponent(ComponentType::Ground, QPointF(150, 500));

    scene->addWire({QPointF(150, 270), QPointF(150, 200), QPointF(270, 200)});
    scene->addWire({QPointF(330, 200), QPointF(330, 140)});
    scene->addWire({QPointF(330, 200), QPointF(420, 200)});
    scene->addWire({QPointF(480, 200), QPointF(560, 200), QPointF(560, 480),
                    QPointF(150, 480), QPointF(150, 500)});
    scene->addWire({QPointF(150, 330), QPointF(150, 500)});
    settle();

    // 常开：放下来就不通
    QVERIFY(!button->component().closed);
    QCOMPARE(button->component().displayValue(), QStringLiteral("常开"));

    const QPoint clickPos = view->mapFromScene(button->pos());

    // 没跑仿真：按住也不动作
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);
    settle();
    QVERIFY2(!button->component().closed, "没在仿真时按住按钮不该接通");
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);
    settle();

    window.runSimulation();
    settle();
    WaveformWindow* live = livePanel(window);
    if (!live || !live->isLive())
        QSKIP("本机没能进入实时仿真，跳过");

    // 测点在 R1 与按钮之间，也就是按钮的 0 号引脚（1 号引脚已经接到地了）
    const int node = scene->toCircuit().buildNetlist().nodeOf(button->component().id, 0);
    QVERIFY(node > 0);

    // 测点在 R1 与按钮之间：按钮断开时没有电流，这一点被 R1 抬到 5 V；
    // 接通时等于地的电位。
    QTRY_VERIFY_WITH_TIMEOUT(live->liveValue(QStringLiteral("V(%1)").arg(node)) > 4.0, 3000);

    // 按住 → 接通 → 测点被拉到 0 V
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);
    settle();
    QVERIFY2(button->component().closed, "仿真中按住按钮应当接通");
    QTRY_VERIFY_WITH_TIMEOUT(live->liveValue(QStringLiteral("V(%1)").arg(node)) < 0.5, 3000);

    // 松手 → 回到常开状态 → 恢复 5 V
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);
    settle();
    QVERIFY2(!button->component().closed, "松手后按钮应当回到常开状态");
    QTRY_VERIFY_WITH_TIMEOUT(live->liveValue(QStringLiteral("V(%1)").arg(node)) > 4.0, 3000);

    // 常闭型：静止就通，按住才断开
    Component comp = button->component();
    comp.normallyClosed = true;
    comp.closed = true;
    button->setComponent(comp);
    settle();
    QCOMPARE(button->component().displayValue(), QStringLiteral("常闭"));

    QTRY_VERIFY_WITH_TIMEOUT(live->liveValue(QStringLiteral("V(%1)").arg(node)) < 0.5, 3000);

    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);
    settle();
    QVERIFY2(!button->component().closed, "常闭型按住应当断开");
    QTRY_VERIFY_WITH_TIMEOUT(live->liveValue(QStringLiteral("V(%1)").arg(node)) > 4.0, 3000);

    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);
    settle();
    QVERIFY2(button->component().closed, "常闭型松手应当恢复接通");
    QTRY_VERIFY_WITH_TIMEOUT(live->liveValue(QStringLiteral("V(%1)").arg(node)) < 0.5, 3000);
}

void UiEditorTest::switchTogglesOnClickOnlyWhileSimulating()
{
    // 仿真运行中左键点开关 = 拨动它；没跑仿真时点击还是普通的选中 / 拖动
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    SchematicScene* scene = currentScene(window);
    SchematicView* view = currentView(window);
    QVERIFY(scene);
    QVERIFY(view);

    // 5 V → R1 → 开关 → 地；测试点接在 R1 与开关之间：
    // 开关闭合时这一点是 0 V，断开时等于 5 V，正好用来验证仿真真的跟着变了
    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(150, 300));
    source->setRotationAngle(90);
    Component src = source->component();
    src.dcValue = 5.0;
    source->setComponent(src);

    scene->placeComponent(ComponentType::Resistor, QPointF(300, 200));
    auto* toggle = scene->placeComponent(ComponentType::Switch, QPointF(450, 200));
    scene->placeComponent(ComponentType::Probe, QPointF(330, 140));
    scene->placeComponent(ComponentType::Ground, QPointF(150, 500));

    scene->addWire({QPointF(150, 270), QPointF(150, 200), QPointF(270, 200)});
    scene->addWire({QPointF(330, 200), QPointF(330, 140)});                  // 测试点引脚
    scene->addWire({QPointF(330, 200), QPointF(420, 200)});
    scene->addWire({QPointF(480, 200), QPointF(560, 200), QPointF(560, 480),
                    QPointF(150, 480), QPointF(150, 500)});
    scene->addWire({QPointF(150, 330), QPointF(150, 500)});
    settle();

    QVERIFY(toggle->component().closed);
    const QPoint clickPos = view->mapFromScene(toggle->pos());

    // 1) 没跑仿真：点一下不拨动
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);
    settle();
    QVERIFY2(toggle->component().closed, "没在仿真时点开关不该拨动它");

    // 2) 跑起来：点一下拨动
    const int node = scene->toCircuit().buildNetlist().nodeOf(toggle->component().id, 0);
    QVERIFY(node > 0);

    window.runSimulation();
    settle();
    WaveformWindow* live = livePanel(window);
    if (!live || !live->isLive())
        QSKIP("本机没能进入实时仿真，跳过");

    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);
    settle();
    QVERIFY2(!toggle->component().closed, "仿真运行中点开关应当把它断开");

    // 3) 实时引擎要跟着换拓扑：等去抖定时器（200 ms）过去
    QTRY_VERIFY_WITH_TIMEOUT(live->liveValue(QStringLiteral("V(%1)").arg(node)) > 4.0, 3000);

    // 4) 再点一次，闭合回去
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);
    settle();
    QVERIFY(toggle->component().closed);
    QTRY_VERIFY_WITH_TIMEOUT(live->liveValue(QStringLiteral("V(%1)").arg(node)) < 0.5, 3000);
}

void UiEditorTest::alignAndDistributeDoTheExpectedThing()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    SchematicScene* scene = currentScene(window);
    QVERIFY(scene);

    // 三个同样的电阻，位置错开
    QVector<ComponentItem*> items;
    const QVector<QPointF> spots = {QPointF(200, 200), QPointF(400, 260), QPointF(620, 380)};
    for (const QPointF& spot : spots) {
        auto* item = scene->placeComponent(ComponentType::Resistor, spot);
        QVERIFY(item);
        item->setSelected(true);
        items.append(item);
    }
    settle();

    // 左对齐：三块的左缘应当一模一样（同尺寸元件，网格吸附后仍然对齐）
    scene->alignSelection(SchematicScene::AlignMode::Left);
    settle();
    const double left = items.first()->symbolRect().left();
    for (ComponentItem* item : items)
        QVERIFY2(std::abs(item->symbolRect().left() - left) < 0.01,
                 qPrintable(QStringLiteral("%1 的左缘 %2 ≠ %3")
                                .arg(item->component().refdes)
                                .arg(item->symbolRect().left())
                                .arg(left)));

    // 垂直等距：首尾不动，中间的按中心线平均分开
    scene->distributeSelection(Qt::Vertical);
    settle();
    QVector<double> centers;
    for (ComponentItem* item : items)
        centers.append(item->symbolRect().center().y());
    std::sort(centers.begin(), centers.end());
    QCOMPARE(centers.size(), 3);
    const double gap1 = centers.at(1) - centers.at(0);
    const double gap2 = centers.at(2) - centers.at(1);
    QVERIFY2(gap1 > 1.0, qPrintable(QString::number(gap1)));
    QVERIFY2(std::abs(gap1 - gap2) < 0.01,
             qPrintable(QStringLiteral("间距不匀：%1 / %2").arg(gap1).arg(gap2)));

    // 水平等距同理
    scene->distributeSelection(Qt::Horizontal);
    settle();
    QVector<double> xs;
    for (ComponentItem* item : items)
        xs.append(item->symbolRect().center().x());
    std::sort(xs.begin(), xs.end());
    const double dx1 = xs.at(1) - xs.at(0);
    const double dx2 = xs.at(2) - xs.at(1);
    QVERIFY2(std::abs(dx1 - dx2) < 0.01,
             qPrintable(QStringLiteral("间距不匀：%1 / %2").arg(dx1).arg(dx2)));

    // 菜单项可用性：对齐 ≥2 个，分布 ≥3 个
    QMenu* arrange = window.findChild<QMenu*>(QStringLiteral("menuArrange"));
    QVERIFY2(arrange, "编辑菜单里应当有「对齐与分布」子菜单");

    auto actionsByText = [arrange](const QString& text) -> QAction* {
        for (QAction* action : arrange->actions()) {
            if (action->text() == text)
                return action;
        }
        return nullptr;
    };
    QAction* alignLeft = actionsByText(QStringLiteral("左对齐"));
    QAction* distributeH = actionsByText(QStringLiteral("水平等距"));
    QVERIFY(alignLeft);
    QVERIFY(distributeH);
    QVERIFY(alignLeft->isEnabled());
    QVERIFY(distributeH->isEnabled());

    // 只留一个选中：两者都该灰掉
    items.at(1)->setSelected(false);
    items.at(2)->setSelected(false);
    settle();
    QVERIFY2(!alignLeft->isEnabled(), "只选一个时对齐不该可用");
    QVERIFY2(!distributeH->isEnabled(), "只选一个时分布不该可用");

    items.at(1)->setSelected(true);
    settle();
    QVERIFY2(alignLeft->isEnabled(), "选两个时对齐应当可用");
    QVERIFY2(!distributeH->isEnabled(), "选两个时分布还不够");
}

void UiEditorTest::logicGatePinsStayOnGrid()
{
    // 逻辑门的引脚数随门类型变（非门 2 个、其余 3 个），引脚位置也必须落在
    // 10 px 网格上 —— 否则斜放或旋转之后导线接不上去
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    SchematicScene* scene = currentScene(window);
    QVERIFY(scene);

    const QVector<QPair<GateKind, int>> expected = {
        {GateKind::And, 3}, {GateKind::Or, 3}, {GateKind::Not, 2},
        {GateKind::Xor, 3}, {GateKind::Xnor, 3}};

    int index = 0;
    for (const auto& entry : expected) {
        auto* item = scene->placeComponent(ComponentType::LogicGate,
                                           QPointF(200 + index * 120, 200));
        QVERIFY(item);
        Component comp = item->component();
        comp.gateKind = entry.first;
        QCOMPARE(gateInputCount(entry.first) + 1, entry.second);
        item->setComponent(comp);
        settle();

        for (int rotation : {0, 90, 45}) {
            Component c = item->component();
            c.rotation = rotation;
            item->setComponent(c);
            settle();

            const QVector<QPointF> pins = item->component().pinPositions();
            QCOMPARE(pins.size(), entry.second);
            for (const QPointF& pin : pins) {
                QVERIFY2(std::abs(pin.x() / 10.0 - std::round(pin.x() / 10.0)) < 1e-9
                             && std::abs(pin.y() / 10.0 - std::round(pin.y() / 10.0)) < 1e-9,
                         qPrintable(QStringLiteral("%1 的引脚没在网格上：(%2, %3)")
                                        .arg(gateKindName(entry.first))
                                        .arg(pin.x())
                                        .arg(pin.y())));
            }
        }
        ++index;
    }
}

void UiEditorTest::componentLabelsCanBeDragged()
{
    // 标号与数值可以单独拖动，拖完之后元件本身不动、连线也不动
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    auto* view = window.findChild<SchematicView*>();
    QVERIFY(scene && view);

    auto* item = scene->placeComponent(ComponentType::Resistor, QPointF(300, 300));
    QVERIFY(item);
    settle();
    const QPointF before = item->pos();
    QCOMPARE(item->component().refLabelOffset, QPointF());

    // 标号只有在元件选中之后才算进命中区（否则周围一大片空白都能点中）
    item->setSelected(true);
    settle();

    // 在标号上按下并拖动
    const QRectF refRect = item->refLabelRect();
    QVERIFY(!refRect.isEmpty());
    const QPointF from = item->mapToScene(refRect.center());
    const QPointF to = from + QPointF(40.0, -50.0);

    auto* press = new QGraphicsSceneMouseEvent(QEvent::GraphicsSceneMousePress);
    press->setButton(Qt::LeftButton);
    press->setButtons(Qt::LeftButton);
    press->setPos(from);
    press->setScenePos(from);
    press->setScreenPos(view->mapFromScene(from));
    press->setButtonDownScenePos(Qt::LeftButton, from);
    QApplication::sendEvent(scene, press);
    delete press;

    auto* move = new QGraphicsSceneMouseEvent(QEvent::GraphicsSceneMouseMove);
    move->setButton(Qt::LeftButton);
    move->setButtons(Qt::LeftButton);
    move->setPos(to);
    move->setScenePos(to);
    move->setScreenPos(view->mapFromScene(to));
    move->setButtonDownScenePos(Qt::LeftButton, from);
    QApplication::sendEvent(scene, move);
    delete move;

    auto* release = new QGraphicsSceneMouseEvent(QEvent::GraphicsSceneMouseRelease);
    release->setButton(Qt::LeftButton);
    release->setButtons(Qt::NoButton);
    release->setPos(to);
    release->setScenePos(to);
    release->setScreenPos(view->mapFromScene(to));
    release->setButtonDownScenePos(Qt::LeftButton, from);
    QApplication::sendEvent(scene, release);
    delete release;
    settle();

    // 标号被挪走了，元件位置没变
    QVERIFY2(item->component().refLabelOffset != QPointF(), "标号应当被拖动");
    QCOMPARE(item->pos(), before);
    QCOMPARE(item->component().valueLabelOffset, QPointF());

    // 旋转之后标签位置要还原 —— 旋转会换掉默认摆放位置，旧偏移没意义了
    item->setRotationAngle(90);
    settle();
    QCOMPARE(item->component().refLabelOffset, QPointF());
    QCOMPARE(item->component().valueLabelOffset, QPointF());

    // 手动「还原标签位置」同样有效
    Component c = item->component();
    c.refLabelOffset = QPointF(30.0, 30.0);
    item->setComponent(c);
    QVERIFY(item->component().refLabelOffset != QPointF());
    item->resetLabelOffsets();
    settle();
    QCOMPARE(item->component().refLabelOffset, QPointF());
}

void UiEditorTest::hitAreaFollowsRotation()
{
    // paint() 里整块符号是被 painter->rotate() 转过去的，shape() 必须套同一套
    // 变换。否则元件一转，画出来的图形和可点范围就错开了：
    // 测试点最明显 —— 小圈画到 +x 方向，命中区却还在 -y 方向。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    auto* item = scene->placeComponent(ComponentType::Probe, QPointF(400, 400));
    QVERIFY(item);
    settle();

    // 符号在 item 坐标里的位置：0° 时小圈在 (0, -16) 附近
    const QPointF localProbe(0.0, -16.0);

    for (int rot : {0, 90, 180, 270}) {
        item->setRotationAngle(rot);
        settle();

        // 按 paint() 的变换把「画出来的位置」算出来
        QTransform t;
        t.scale(item->mirroredX() ? -1.0 : 1.0, item->mirroredY() ? -1.0 : 1.0);
        t.rotate(double(rot));
        const QPointF drawn = t.map(localProbe);

        QVERIFY2(item->contains(drawn),
                 qPrintable(QStringLiteral("%1°：画出来的符号在 (%2, %3)，却点不中")
                                .arg(rot)
                                .arg(drawn.x())
                                .arg(drawn.y())));

        const QPointF inScene = item->mapToScene(drawn);
        QGraphicsItem* hit = scene->itemAt(inScene, QTransform());
        QVERIFY2(hit == item,
                 qPrintable(QStringLiteral("%1°：符号位置拾取到的是 %2")
                                .arg(rot)
                                .arg(hit ? hit->type() : -1)));
    }
}

void UiEditorTest::probeStaysClickableWhenRotated()
{
    // 测试点的标号写在符号外几十像素处。两条要求同时成立：
    //  · 点「TP1」这行字能选中元件（不是一片空白都能点中）
    //  · 旋转之后，按符号本体绝不能变成拖标号
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    auto* item = scene->placeComponent(ComponentType::Probe, QPointF(400, 400));
    QVERIFY(item);
    item->setComponent([&] {
        Component c = item->component();
        c.refdes = QStringLiteral("TP1");
        return c;
    }());
    settle();

    for (int rot : {0, 90, 180, 270}) {
        item->setRotationAngle(rot);
        item->setSelected(false);
        settle();

        const QRectF label = item->refLabelRect();
        QVERIFY2(!label.isEmpty(), "标号矩形不该是空的");

        // 1) 点标号中心能选中元件
        const QPointF labelScene = item->mapToScene(label.center());
        QVERIFY2(scene->itemAt(labelScene, QTransform()) == item,
                 qPrintable(QStringLiteral("%1°：点标号应当选中元件").arg(rot)));

        // 2) 标号矩形不能盖住符号 —— 否则按本体就变成拖标号
        QTransform t;
        t.rotate(double(rot));
        for (const QPointF& body : {t.map(QPointF(0.0, -16.0)),   // 小圈
                                    t.map(QPointF(0.0, -4.0)),    // 短棍
                                    QPointF(0.0, 0.0)}) {         // 引脚
            QVERIFY2(!label.contains(body),
                     qPrintable(QStringLiteral("%1°：标号矩形盖住了符号 (%2, %3)")
                                    .arg(rot)
                                    .arg(body.x())
                                    .arg(body.y())));
        }

        // 3) 符号本体仍然点得中
        const QPointF circle = t.map(QPointF(0.0, -16.0));
        QVERIFY2(scene->itemAt(item->mapToScene(circle), QTransform()) == item,
                 qPrintable(QStringLiteral("%1°：符号本体点不中").arg(rot)));

        // 4) 包围盒装得下标号
        QVERIFY2(item->boundingRect().contains(label),
                 qPrintable(QStringLiteral("%1° 时标号露出包围盒").arg(rot)));
    }
}

void UiEditorTest::splitDrawnWiresKeepJunction()
{
    // 竖线一次穿过、横线分两次画到同一点：这里应当是一个交点，
    // 不能把两段横线自动并成一条（并掉顶点交点就没了）
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    const auto draw = [scene](QVector<QPointF> pts) {
        WireItem* wire = scene->addWire(pts);
        scene->mergeWireWithNeighbors(wire);
    };

    draw({QPointF(250, 100), QPointF(250, 300)}); // 竖线直接穿过
    draw({QPointF(150, 200), QPointF(250, 200)}); // 横线左半段
    draw({QPointF(250, 200), QPointF(350, 200)}); // 横线右半段
    settle();

    bool hasJunction = false;
    for (const QPointF& p : scene->junctions())
        if (QLineF(p, QPointF(250, 200)).length() < 0.01)
            hasJunction = true;
    QVERIFY2(hasJunction, "分开画出来的交点应当保留");

    // 左半段和右半段不能被合成一条（否则顶点消失、交点也就没了）
    int horizontalPieces = 0;
    for (WireItem* wire : scene->wireItems()) {
        const QVector<QPointF> pts = wire->scenePoints();
        if (qAbs(pts.first().y() - 200.0) < 0.01 && qAbs(pts.last().y() - 200.0) < 0.01)
            ++horizontalPieces;
    }
    QVERIFY2(horizontalPieces >= 2,
             qPrintable(QStringLiteral("横线被并成了 %1 段").arg(horizontalPieces)));

    // 交点处一共三条线：竖线直穿（没有顶点）+ 两段横线各收一个端点，
    // 电气上仍然连在一起（网表会把「端点落在别的线中间」也算作连接）
    const Circuit circuit = scene->toCircuit();
    int endsAtPoint = 0;
    for (const Wire& wire : circuit.wires)
        for (const QPointF& p : wire.points)
            if (QLineF(p, QPointF(250, 200)).length() < 0.01)
                ++endsAtPoint;
    QVERIFY2(endsAtPoint >= 2, qPrintable(QStringLiteral("交点处线端 %1 个").arg(endsAtPoint)));
}

void UiEditorTest::crossingCanBecomeJunction()
{
    // 两条线十字交叉：默认不相连（不画交点）；打上交点后才连起来
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    // 水平线 y=200，x 100→400；竖直线上 x=250，y 100→300（两者只在 (250,200) 交叉）
    scene->addWire({QPointF(100, 200), QPointF(400, 200)});
    scene->addWire({QPointF(250, 100), QPointF(250, 300)});
    settle();
    QCOMPARE(scene->wireItems().size(), 2);
    QCOMPARE(scene->junctions().size(), 0); // 交叉处不画交点

    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 400));
    source->setRotationAngle(90);
    scene->placeComponent(ComponentType::Ground, QPointF(100, 500));
    scene->addWire({QPointF(100, 430), QPointF(100, 500)});
    settle();

    QVERIFY(scene->hasCrossingAt(QPointF(250, 200)));
    scene->connectWiresAt(QPointF(250, 200));
    settle();

    // 切开之后四条线共点 → 画交点
    bool hasJunction = false;
    for (const QPointF& p : scene->junctions())
        if (QLineF(p, QPointF(250, 200)).length() < 0.01)
            hasJunction = true;
    QVERIFY2(hasJunction, "打上交点后应当画出交点");

    // 电气上也连起来了
    const Circuit circuit = scene->toCircuit();
    const Netlist net = circuit.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    int nodeAtCrossing = -1;
    int count = 0;
    for (const Wire& wire : circuit.wires)
        for (const QPointF& p : wire.points)
            if (QLineF(p, QPointF(250, 200)).length() < 0.01)
                ++count;
    Q_UNUSED(nodeAtCrossing);
    QVERIFY2(count >= 4, qPrintable(QStringLiteral("交点处只有 %1 个线端").arg(count)));
}

void UiEditorTest::pasteKeepsWiresStraight()
{
    // 粘贴的偏移量必须是 10 px 整数倍：否则元件被 snap 到网格、导线只做平移，
    // 两者步长不一致，粘出来的线就歪了
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
    source->setRotationAngle(90);
    auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(250, 200));
    QVERIFY(source && resistor);
    scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(220, 200)});
    settle();

    for (QGraphicsItem* item : scene->items())
        item->setSelected(true);
    settle();
    scene->copySelection();
    // 故意给一个「不整齐」的光标位置
    scene->setCursorScenePos(QPointF(1237, 763));
    scene->pasteClipboard();
    settle();

    // 每条导线的每一段都必须是水平或竖直，且所有顶点都在网格上
    for (WireItem* wire : scene->wireItems()) {
        const QVector<QPointF> pts = wire->scenePoints();
        for (int i = 1; i < pts.size(); ++i) {
            const QPointF d = pts.at(i) - pts.at(i - 1);
            QVERIFY2(qAbs(d.x()) < 1e-6 || qAbs(d.y()) < 1e-6,
                     qPrintable(QStringLiteral("出现斜段 (%1,%2)→(%3,%4)")
                                    .arg(pts.at(i-1).x()).arg(pts.at(i-1).y())
                                    .arg(pts.at(i).x()).arg(pts.at(i).y())));
        }
        for (const QPointF& p : pts) {
            QVERIFY2(qAbs(p.x() - qRound(p.x() / 10.0) * 10.0) < 1e-6
                         && qAbs(p.y() - qRound(p.y() / 10.0) * 10.0) < 1e-6,
                     qPrintable(QStringLiteral("顶点 (%1,%2) 不在网格上")
                                    .arg(p.x()).arg(p.y())));
        }
    }
}

void UiEditorTest::snapWiresRepairsSkewedGeometry()
{
    // 老版本粘贴留下的歪线：菜单「导线对齐网格」应当能修好
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    scene->placeComponent(ComponentType::Resistor, QPointF(200, 200));
    // 造一条歪线（终点偏 5 px）
    scene->addWire({QPointF(200, 230), QPointF(200, 305), QPointF(280, 305)});
    settle();
    QCOMPARE(scene->wireItems().size(), 1);

    scene->snapWiresToGrid();
    settle();
    for (WireItem* wire : scene->wireItems()) {
        for (const QPointF& p : wire->scenePoints()) {
            QVERIFY2(qAbs(p.x() - qRound(p.x() / 10.0) * 10.0) < 1e-6
                         && qAbs(p.y() - qRound(p.y() / 10.0) * 10.0) < 1e-6,
                     qPrintable(QStringLiteral("顶点 (%1,%2) 不在网格上")
                                    .arg(p.x()).arg(p.y())));
        }
    }
}

void UiEditorTest::pastedWiresSnapToPins()
{
    // 斜放元件的引脚原本带小数，复制粘贴后导线端点会对不上（看着像「连错」）
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    // 一对 45° 斜放二极管 + 一条连接它们的导线
    auto* d1 = scene->placeComponent(ComponentType::Diode, QPointF(300, 300));
    auto* d2 = scene->placeComponent(ComponentType::Diode, QPointF(400, 300));
    QVERIFY(d1 && d2);
    d1->setRotationAngle(45);
    d2->setRotationAngle(225);
    settle();
    scene->addWire({d1->pinScenePositions().at(1), d2->pinScenePositions().at(0)});
    settle();

    QCOMPARE(scene->wireItems().size(), 1);
    QCOMPARE(scene->componentItems().size(), 2);

    // 全选 → 复制 → 光标放到别处 → 粘贴
    for (QGraphicsItem* item : scene->items())
        item->setSelected(true);
    settle();
    scene->copySelection();
    scene->setCursorScenePos(QPointF(700, 300));
    scene->pasteClipboard();
    settle();

    QCOMPARE(scene->componentItems().size(), 4);
    QCOMPARE(scene->wireItems().size(), 2);

    // 粘贴出来的导线端点必须落在元件的引脚上
    QVector<QPointF> pins;
    for (ComponentItem* comp : scene->componentItems())
        for (const QPointF& p : comp->pinScenePositions())
            pins.append(p);
    for (WireItem* wire : scene->wireItems()) {
        for (const QPointF& end : {wire->scenePoints().first(), wire->scenePoints().last()}) {
            double best = 1e9;
            for (const QPointF& pin : pins)
                best = std::min(best, QLineF(end, pin).length());
            QVERIFY2(best < 0.01, qPrintable(QStringLiteral("导线端点离引脚 %1 px").arg(best)));
        }
    }
}

void UiEditorTest::spaceRotatesSelection()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);
    window.activateWindow();
    settle();

    // 快捷键应当绑定在空格上
    QAction* rotate = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().contains(QStringLiteral("旋转 90°")))
            rotate = action;
    }
    QVERIFY(rotate);
    QCOMPARE(rotate->shortcut(), QKeySequence(Qt::Key_Space));

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    ComponentItem* item = scene->placeComponent(ComponentType::Resistor, QPointF(200, 200));
    QVERIFY(item);
    item->setSelected(true);
    settle();

    const int before = item->component().rotation;
    QTest::keyClick(&window, Qt::Key_Space);
    settle();
    // 旋转是逆时针：角度减 90
    QCOMPARE(item->component().rotation, ((before - 90) % 360 + 360) % 360);

    QTest::keyClick(&window, Qt::Key_Space);
    settle();
    QCOMPARE(item->component().rotation, ((before - 180) % 360 + 360) % 360);

    // 在数值输入框里按空格必须输入空格，不能触发旋转
    QLineEdit* valueEdit = nullptr;
    for (QLineEdit* edit : window.findChildren<QLineEdit*>()) {
        if (edit->placeholderText().contains(QStringLiteral("10kΩ")))
            valueEdit = edit;
    }
    QVERIFY(valueEdit);
    valueEdit->setFocus();
    settle();
    valueEdit->setText(QStringLiteral("2.2k"));
    const int beforeTyping = item->component().rotation;
    QTest::keyClick(valueEdit, Qt::Key_Space);
    settle();
    QVERIFY2(valueEdit->text().contains(QLatin1Char(' ')),
             qPrintable(valueEdit->text()));
    QCOMPARE(item->component().rotation, beforeTyping);
}

void UiEditorTest::undoRedoEdits()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);
    QCOMPARE(scene->componentItems().size(), 0);

    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("撤销"))
            undoAction = action;
        if (action->text() == QStringLiteral("重做"))
            redoAction = action;
    }
    QVERIFY(undoAction && redoAction);
    QVERIFY(!undoAction->isEnabled());
    QVERIFY(!redoAction->isEnabled());

    // 放置两个元件 → 两次历史
    ComponentItem* r1 = scene->placeComponent(ComponentType::Resistor, QPointF(200, 200));
    QVERIFY(r1);
    QCOMPARE(scene->componentItems().size(), 1);
    QVERIFY(undoAction->isEnabled());

    scene->placeComponent(ComponentType::Capacitor, QPointF(300, 300));
    QCOMPARE(scene->componentItems().size(), 2);

    undoAction->trigger();
    settle();
    QCOMPARE(scene->componentItems().size(), 1);
    QVERIFY(redoAction->isEnabled());

    undoAction->trigger();
    settle();
    QCOMPARE(scene->componentItems().size(), 0);
    QVERIFY(!undoAction->isEnabled());

    redoAction->trigger();
    settle();
    QCOMPARE(scene->componentItems().size(), 1);

    redoAction->trigger();
    settle();
    QCOMPARE(scene->componentItems().size(), 2);

    // 移动后撤销应回到原位
    ComponentItem* moved = nullptr;
    for (ComponentItem* item : scene->componentItems()) {
        if (item->component().type == ComponentType::Resistor)
            moved = item;
    }
    QVERIFY(moved);
    const QPointF original = moved->pos();
    moved->setPos(QPointF(420, 420));
    scene->commitEdit();
    settle();
    QCOMPARE(moved->pos(), QPointF(420, 420));

    undoAction->trigger();
    settle();
    ComponentItem* restored = nullptr;
    for (ComponentItem* item : scene->componentItems()) {
        if (item->component().type == ComponentType::Resistor)
            restored = item;
    }
    QVERIFY(restored);
    QCOMPARE(restored->pos(), original);

    // 删除后撤销应恢复
    scene->clearSelection();
    restored->setSelected(true);
    scene->deleteSelection();
    settle();
    QCOMPARE(scene->componentItems().size(), 1);

    undoAction->trigger();
    settle();
    QCOMPARE(scene->componentItems().size(), 2);
}

void UiEditorTest::dragComponentFromLibrary()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    auto* view = window.findChild<SchematicView*>();
    QVERIFY(scene && view);
    QCOMPARE(scene->componentItems().size(), 0);

    QMimeData mime;
    mime.setData(QStringLiteral("application/x-csm-component"),
                 QByteArray::number(int(ComponentType::Resistor)));

    const QPoint targetInView = view->mapFromScene(QPointF(200, 200));
    const QPointF target = view->viewport()->mapFrom(view, targetInView);
    // Qt 6.16 起 QPoint 版构造被标为废弃，那边用 QPointF 版；
    // 更早的 Qt（本项目最低支持 6.5）只有 QPoint 版，只是不产生警告
#if QT_VERSION >= QT_VERSION_CHECK(6, 16, 0)
    QDragEnterEvent enter(target, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QDropEvent drop(target, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
#else
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QDragEnterEvent enter(target.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QDropEvent drop(target, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QT_WARNING_POP
#endif
    QApplication::sendEvent(view->viewport(), &enter);
    QApplication::sendEvent(view->viewport(), &drop);
    settle();

    QCOMPARE(scene->componentItems().size(), 1);
    ComponentItem* item = scene->componentItems().first();
    QCOMPARE(item->component().type, ComponentType::Resistor);
    QCOMPARE(item->pos(), QPointF(200, 200));
    QVERIFY(item->isSelected());
}

void UiEditorTest::copyPasteAndMirror()
{
    MainWindow window;
    window.resize(1200, 800);
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

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    ComponentItem* resistor = nullptr;
    for (ComponentItem* item : scene->componentItems()) {
        if (item->component().refdes == QLatin1String("R1"))
            resistor = item;
    }
    QVERIFY(resistor);

    scene->clearSelection();
    resistor->setSelected(true);
    const int before = scene->componentItems().size();
    scene->copySelection();
    scene->pasteClipboard();
    settle();

    QCOMPARE(scene->componentItems().size(), before + 1);
    ComponentItem* pasted = nullptr;
    for (ComponentItem* item : scene->componentItems()) {
        if (item->component().refdes == QLatin1String("R2"))
            pasted = item;
    }
    QVERIFY(pasted);
    QCOMPARE(pasted->component().value, 1000.0);
    QCOMPARE(pasted->pos(), QPointF(220, 220));
    QVERIFY(pasted->isSelected());

    // 水平翻转：左右引脚交换
    resistor->setMirrored(true, false);
    QCOMPARE(resistor->pinScenePositions().at(0), QPointF(230, 200));
    QCOMPARE(resistor->pinScenePositions().at(1), QPointF(170, 200));
    QCOMPARE(resistor->component().rotation, 0);

    // 翻转后再旋转
    resistor->setRotationAngle(90);
    QCOMPARE(resistor->pinScenePositions().at(0), QPointF(200, 170));

    const Circuit circuit = scene->toCircuit();
    const Component* saved = circuit.find(resistor->component().id);
    QVERIFY(saved);
    QVERIFY(saved->mirrorX);
    QCOMPARE(saved->rotation, 90);
}

void UiEditorTest::multiSelectionDrag()
{
    MainWindow window;
    window.resize(1200, 800);
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

    auto* scene = window.findChild<SchematicScene*>();
    auto* view = window.findChild<SchematicView*>();
    QVERIFY(scene && view);

    ComponentItem* r1 = nullptr;
    ComponentItem* c1 = nullptr;
    for (ComponentItem* item : scene->componentItems()) {
        if (item->component().refdes == QLatin1String("R1"))
            r1 = item;
        if (item->component().refdes == QLatin1String("C1"))
            c1 = item;
    }
    QVERIFY(r1 && c1);

    // 框选整块电路
    const QPoint from = view->mapFromScene(QPointF(40, 100));
    const QPoint to = view->mapFromScene(QPointF(460, 520));
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(view->viewport(), to, 20);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
    settle();

    QVERIFY(r1->isSelected());
    QVERIFY(c1->isSelected());

    WireItem* wire = scene->wireItems().first();
    QVERIFY(wire->isSelected());

    const QPointF r1Start = r1->pos();
    const QPointF c1Start = c1->pos();
    const QPointF wireStart = wire->scenePoints().first();

    // 拖动 R1：整组（含导线）一起移动
    const QPoint pressPoint = view->mapFromScene(r1->scenePos());
    const QPoint dragTo = view->mapFromScene(r1->pos() + QPointF(60, 0));
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, pressPoint);
    QTest::mouseMove(view->viewport(), dragTo, 20);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, dragTo);
    settle();

    QCOMPARE(r1->pos(), r1Start + QPointF(60, 0));
    QCOMPARE(c1->pos(), c1Start + QPointF(60, 0));
    QCOMPARE(wire->scenePoints().first(), wireStart + QPointF(60, 0));

    // 移动后连接关系仍保持
    const Netlist net = scene->toCircuit().buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QCOMPARE(net.nodeCount, 2);
    QVERIFY2(net.warnings.isEmpty(), qPrintable(net.warnings.join(QStringLiteral("; "))));

    // 连续第二次拖动
    const QPointF r1Mid = r1->pos();
    const QPointF c1Mid = c1->pos();
    const QPoint secondPress = view->mapFromScene(r1->scenePos());
    const QPoint secondTo = view->mapFromScene(r1->pos() + QPointF(40, 0));
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, secondPress);
    QTest::mouseMove(view->viewport(), secondTo, 20);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, secondTo);
    settle();
    QCOMPARE(r1->pos(), r1Mid + QPointF(40, 0));
    QCOMPARE(c1->pos(), c1Mid + QPointF(40, 0));

    // 也可以直接抓导线拖动整组
    const QPointF wireBefore = wire->scenePoints().first();
    const QPoint wirePress = view->mapFromScene(wire->scenePoints().first());
    const QPoint wireTo = view->mapFromScene(wire->scenePoints().first() + QPointF(0, 30));
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, wirePress);
    QTest::mouseMove(view->viewport(), wireTo, 20);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, wireTo);
    settle();
    QCOMPARE(wire->scenePoints().first(), wireBefore + QPointF(0, 30));
}

void UiEditorTest::movingComponentDragsItsWires()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    // 电阻 R1 在 (200,200)，导线从它的引脚引出到右下角
    ComponentItem* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(200, 200));
    QVERIFY(resistor);
    const QVector<QPointF> pins = resistor->pinScenePositions();
    QCOMPARE(pins.size(), 2);
    WireItem* wire = scene->addWire({pins.at(1), QPointF(pins.at(1).x() + 80, pins.at(1).y())});
    QVERIFY(wire);
    settle();

    const QVector<QPointF> before = wire->scenePoints();
    QCOMPARE(before.first(), pins.at(1));

    // 把电阻往右下挪 60/40：连着它的那端要跟着走，另一端不动
    resistor->setPos(resistor->pos() + QPointF(60, 40));
    settle();

    const QVector<QPointF> after = wire->scenePoints();
    QVERIFY(after.size() >= 2);
    QCOMPARE(after.first(), before.first() + QPointF(60, 40));
    QCOMPARE(after.last(), before.last());
    // 端点跟着元件走，但走线要保持横平竖直（会补出直角拐点）
    for (int i = 1; i < after.size(); ++i) {
        const QPointF d = after.at(i) - after.at(i - 1);
        QVERIFY2(std::abs(d.x()) < 0.5 || std::abs(d.y()) < 0.5,
                 qPrintable(QStringLiteral("第 %1 段是斜线").arg(i)));
    }
}

void UiEditorTest::draggingAnchoredWireStretchesInsteadOfDetaching()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    // 两个电阻之间接一条导线，两端都落在引脚上
    ComponentItem* left = scene->placeComponent(ComponentType::Resistor, QPointF(200, 200));
    ComponentItem* right = scene->placeComponent(ComponentType::Resistor, QPointF(400, 200));
    QVERIFY(left && right);
    const QPointF a = left->pinScenePositions().at(1);
    const QPointF b = right->pinScenePositions().at(0);
    WireItem* wire = scene->addWire({a, b});
    QVERIFY(wire);
    settle();

    auto* view = window.findChild<SchematicView*>();
    QVERIFY(view);
    scene->clearSelection();
    wire->setSelected(true);
    settle();

    // 抓导线中间往下拖：两端应当钉在引脚上，导线被拉成折线
    const QPointF mid = (a + b) / 2.0;
    const QPoint pressAt = view->mapFromScene(mid);
    const QPoint dragTo = view->mapFromScene(mid + QPointF(0, 60));
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, pressAt);
    QMouseEvent move(QEvent::MouseMove, QPointF(dragTo), view->viewport()->mapToGlobal(dragTo),
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &move);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, dragTo);
    settle();

    const QVector<QPointF> after = wire->scenePoints();
    QVERIFY2(after.size() >= 3, "应当插入拐点，把导线拉长而不是整条挪走");
    QCOMPARE(after.first(), a);
    QCOMPARE(after.last(), b);
    // 中间确实往下走了
    double lowest = a.y();
    for (const QPointF& p : after)
        lowest = std::max(lowest, p.y());
    QVERIFY2(lowest >= mid.y() + 50, qPrintable(QString::number(lowest)));

    QVERIFY(saveShot(&window, QStringLiteral("wire_orthogonal.png")));

    // 走线必须是直角：每段要么水平要么竖直
    for (int i = 1; i < after.size(); ++i) {
        const QPointF d = after.at(i) - after.at(i - 1);
        const bool horizontal = std::abs(d.y()) < 0.5;
        const bool vertical = std::abs(d.x()) < 0.5;
        QVERIFY2(horizontal || vertical,
                 qPrintable(QStringLiteral("第 %1 段是斜线：(%2,%3) → (%4,%5)")
                                .arg(i)
                                .arg(after.at(i - 1).x())
                                .arg(after.at(i - 1).y())
                                .arg(after.at(i).x())
                                .arg(after.at(i).y())));
    }
}

void UiEditorTest::clickingWireHighlightsOnlyThatSegment()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    auto* view = window.findChild<SchematicView*>();
    QVERIFY(scene && view);

    // 一条带拐点的折线：横段 + 竖段
    WireItem* wire = scene->addWire({QPointF(200, 200), QPointF(400, 200), QPointF(400, 320)});
    QVERIFY(wire);
    settle();
    scene->clearSelection();
    settle();

    // 点横段：只高亮第 0 段
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      view->mapFromScene(QPointF(300, 200)));
    settle();
    QVERIFY(wire->isSelected());
    QCOMPARE(wire->activeSegment(), 0);

    // 点竖段：高亮切到第 1 段
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      view->mapFromScene(QPointF(400, 270)));
    settle();
    QCOMPARE(wire->activeSegment(), 1);

    // 取消选中后恢复整条高亮的状态
    scene->clearSelection();
    settle();
    QCOMPARE(wire->activeSegment(), -1);

    // 只点一下（没有拖动）不应该把导线改脏、触发整理重建
    QCOMPARE(scene->wireItems().size(), 1);
    QCOMPARE(scene->wireItems().first(), wire);
}

void UiEditorTest::clickingWireKeepsItSelected()
{
    // 电路里有分叉点时，整理导线会重建图元；点击之后的 commitEdit
    // 不应该把刚选中的导线弄丢（否则界面上会「红一下就消失」）
    SchematicScene scene;
    WireItem* vertical = scene.addWire({QPointF(0, 0), QPointF(0, 200)});
    scene.addWire({QPointF(0, 100), QPointF(100, 100)}); // T 型支路
    QVERIFY(vertical);

    scene.clearSelection();
    vertical->setSelected(true);
    QVERIFY(vertical->isSelected());

    scene.commitEdit(); // 相当于松开鼠标
    // 可能因为整理而重建图元：选中状态要跟到新的那根线上，不能凭空消失
    bool stillSelected = false;
    for (WireItem* wire : scene.wireItems())
        stillSelected = stillSelected || wire->isSelected();
    QVERIFY2(stillSelected, "commitEdit 之后选中状态应当保留");
    QCOMPARE(scene.wireItems().size(), 2); // 竖线 + 支路

    // 真的改了几何之后才需要整理，并且整理完仍然选中
    vertical->setScenePoints({QPointF(0, 0), QPointF(0, 220)});
    scene.commitEdit();
    bool anySelected = false;
    for (WireItem* wire : scene.wireItems())
        anySelected = anySelected || wire->isSelected();
    QVERIFY2(anySelected, "整理后仍应保留选中状态");
}

void UiEditorTest::drawnWireMergesWithNeighbour()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    auto* view = window.findChild<SchematicView*>();
    QVERIFY(scene && view);

    // 已有导线：(200,200) → (400,200)
    WireItem* first = scene->addWire({QPointF(200, 200), QPointF(400, 200)});
    QVERIFY(first);
    settle();
    QCOMPARE(scene->wireItems().size(), 1);

    // 画一条接着它往右延伸的线：应当并成一条
    WireItem* second = scene->addWire({QPointF(400, 200), QPointF(600, 200)});
    QVERIFY(second);
    scene->mergeWireWithNeighbors(second);
    settle();
    QCOMPARE(scene->wireItems().size(), 1);
    const QVector<QPointF> merged = scene->wireItems().first()->scenePoints();
    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.first(), QPointF(200, 200));
    QCOMPARE(merged.last(), QPointF(600, 200));

    // 再画一条垂直的线接在末端：拐角相接，并成一条折线（文件里同样是一条线）
    WireItem* third = scene->addWire({QPointF(600, 200), QPointF(600, 320)});
    QVERIFY(third);
    scene->mergeWireWithNeighbors(third);
    settle();
    QCOMPARE(scene->wireItems().size(), 1);
    QCOMPARE(scene->wireItems().first()->scenePoints().size(), 3);

    // 先画 C 形（上、左、下三段），再画中间支路：左边那条竖线应当并成一条
    {
        SchematicScene scene;
        // 分三次画：上横、左竖、下横（各自独立导线）
        WireItem* top = scene.addWire({QPointF(0, 0), QPointF(200, 0)});
        WireItem* left = scene.addWire({QPointF(0, 0), QPointF(0, 100)});
        WireItem* lower = scene.addWire({QPointF(0, 100), QPointF(0, 200)});
        QVERIFY(top && left && lower);
        // 画完左竖线时，两个端点都接得上（上横、下横）：整条 C 并成一条
        scene.mergeWireWithNeighbors(left);
        QCOMPARE(scene.wireItems().size(), 1);
        // 上横 + 竖 + 下横：竖线中间的重复点会被规整掉，剩 3 个顶点
        const QVector<QPointF> cshape = scene.wireItems().first()->scenePoints();
        QCOMPARE(cshape.size(), 3);
        // 走向（哪头是首）不影响结论，两端点应当是 C 的两个开口端
        const QPointF a = cshape.first();
        const QPointF b = cshape.last();
        const bool openEnds = (a == QPointF(200, 0) && b == QPointF(0, 200))
            || (a == QPointF(0, 200) && b == QPointF(200, 0));
        QVERIFY2(openEnds, qPrintable(QStringLiteral("(%1,%2)-(%3,%4)")
                                          .arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y())));

        // 再画中间支路接到竖线中点：支路独立，C 仍然是一条线
        WireItem* branch = scene.addWire({QPointF(0, 100), QPointF(150, 100)});
        QVERIFY(branch);
        scene.mergeWireWithNeighbors(branch);

        QCOMPARE(scene.wireItems().size(), 2); // C 形一条 + 支路一条
        int withVerticalRun = 0;
        for (WireItem* w : scene.wireItems()) {
            const QVector<QPointF> pts = w->scenePoints();
            bool spans = false;
            for (const QPointF& p : pts) {
                if (QPointF(p) == QPointF(0, 0) || QPointF(p) == QPointF(0, 200))
                    spans = true;
            }
            if (spans)
                ++withVerticalRun;
        }
        QCOMPARE(withVerticalRun, 1); // 左边那条竖线整条在同一根导线里
    }

    // 用户给的例子：第一笔画「上横+左竖到中点+中横」，第二笔从该点继续往下，
    // 画完后左边竖线应当是一条（中横保持独立）
    {
        SchematicScene scene;
        WireItem* first = scene.addWire({QPointF(100, 50), QPointF(0, 50),
                                         QPointF(0, 150), QPointF(100, 150)});
        QVERIFY(first);
        scene.mergeWireWithNeighbors(first);
        WireItem* second = scene.addWire({QPointF(0, 150), QPointF(0, 250),
                                          QPointF(100, 250)});
        QVERIFY(second);
        scene.mergeWireWithNeighbors(second);

        const Circuit drawn = scene.toCircuit();
        QCOMPARE(drawn.wires.size(), 2);

        bool verticalWhole = false;
        bool branchAlone = false;
        for (const Wire& wire : drawn.wires) {
            if (wire.points.size() == 4 && wire.points.first() == QPointF(100, 50)
                && wire.points.last() == QPointF(100, 250))
                verticalWhole = true;
            if (wire.points.size() == 2 && wire.points.contains(QPointF(0, 150))
                && wire.points.contains(QPointF(100, 150)))
                branchAlone = true;
        }
        QVERIFY2(verticalWhole, "左竖线应当是一条（上横—竖—下横连在一起）");
        QVERIFY2(branchAlone, "中间横线应当独立");
    }

    // 3.csm 的画法：第一笔「上横+上竖到中点」，第二笔「下横+下竖到中点再右拐」，
    // 画完应当是 2.csm 那样：一条完整竖线（带上下横）+ 一条独立的中横
    {
        SchematicScene scene;
        WireItem* first = scene.addWire({QPointF(100, 50), QPointF(0, 50), QPointF(0, 150)});
        QVERIFY(first);
        scene.mergeWireWithNeighbors(first);
        WireItem* second = scene.addWire({QPointF(100, 250), QPointF(0, 250),
                                          QPointF(0, 150), QPointF(100, 150)});
        QVERIFY(second);
        scene.mergeWireWithNeighbors(second);

        const Circuit drawn = scene.toCircuit();
        QCOMPARE(drawn.wires.size(), 2);
        bool verticalWhole = false;
        bool branchAlone = false;
        for (const Wire& wire : drawn.wires) {
            if (wire.points.size() == 4 && wire.points.first() == QPointF(100, 50)
                && wire.points.last() == QPointF(100, 250))
                verticalWhole = true;
            if (wire.points.size() == 2 && wire.points.contains(QPointF(0, 150))
                && wire.points.contains(QPointF(100, 150)))
                branchAlone = true;
        }
        QVERIFY2(verticalWhole, "竖线应当合成一条");
        QVERIFY2(branchAlone, "中间横线应当独立");
    }

    // test2 → 往左拖动 → test1：拖动后共线相接的两段自动并成一条
    {
        SchematicScene scene;
        WireItem* top = scene.addWire({QPointF(150, 200), QPointF(120, 200),
                                       QPointF(120, 300)});
        QVERIFY(top);
        scene.addWire({QPointF(150, 400), QPointF(100, 400), QPointF(100, 300),
                       QPointF(150, 300)});
        QCOMPARE(scene.toCircuit().wires.size(), 2);

        // 模拟把上方导线往左拖：竖段落到 x=100，与下方导线共线相接
        top->setScenePoints({QPointF(150, 200), QPointF(100, 200), QPointF(100, 300)});
        emit top->wireChanged();
        settle();
        settle();

        const Circuit after = scene.toCircuit();
        QCOMPARE(after.wires.size(), 2);
        bool verticalWhole = false;
        bool stubAlone = false;
        for (const Wire& wire : after.wires) {
            if (wire.points.size() == 4 && wire.points.first() == QPointF(150, 200)
                && wire.points.last() == QPointF(150, 400))
                verticalWhole = true;
            if (wire.points.size() == 2 && wire.points.contains(QPointF(100, 300))
                && wire.points.contains(QPointF(150, 300)))
                stubAlone = true;
        }
        QVERIFY2(verticalWhole, "拖动后竖线应当合成一条");
        QVERIFY2(stubAlone, "中间横线应当独立");
    }

    // 已存在的两根共线导线，载入/提交时不应被强行合并
    Circuit circuit;
    circuit.wires.append(Wire{{QPointF(0, 0), QPointF(100, 0)}});
    circuit.wires.append(Wire{{QPointF(100, 0), QPointF(200, 0)}});
    scene->load(circuit);
    settle();
    QCOMPARE(scene->wireItems().size(), 2);
    scene->commitEdit();
    settle();
    QCOMPARE(scene->wireItems().size(), 2);
}

void UiEditorTest::cutCopyPasteFromEditMenu()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    QAction* cut = nullptr;
    QAction* copyAction = nullptr;
    QAction* paste = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("剪切"))
            cut = action;
        else if (action->text() == QStringLiteral("复制"))
            copyAction = action;
        else if (action->text() == QStringLiteral("粘贴"))
            paste = action;
    }
    QVERIFY(cut && copyAction && paste);
    QCOMPARE(cut->shortcut(), QKeySequence(QKeySequence::Cut));
    QVERIFY(!cut->isEnabled());
    QVERIFY(!paste->isEnabled());

    ComponentItem* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(200, 200));
    QVERIFY(resistor);
    scene->clearSelection();
    resistor->setSelected(true);
    settle();
    QVERIFY(cut->isEnabled());
    QVERIFY(copyAction->isEnabled());

    // 剪切：元件消失，但剪贴板里有东西
    cut->trigger();
    settle();
    QCOMPARE(scene->componentItems().size(), 0);
    QVERIFY(scene->hasClipboard());
    QVERIFY(paste->isEnabled());

    // 粘贴回来：落点应当在鼠标位置附近
    scene->setCursorScenePos(QPointF(500, 400));
    paste->trigger();
    settle();
    QCOMPARE(scene->componentItems().size(), 1);
    const QPointF where = scene->componentItems().first()->pos();
    QVERIFY2(QLineF(where, QPointF(500, 400)).length() < 15.0,
             qPrintable(QStringLiteral("粘贴到 (%1,%2)").arg(where.x()).arg(where.y())));
}

void UiEditorTest::wireCanBeSelectedAndDeleted()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    auto* view = window.findChild<SchematicView*>();
    QVERIFY(scene && view);

    WireItem* wire = scene->addWire({QPointF(200, 200), QPointF(400, 200)});
    QVERIFY(wire);
    settle();
    scene->clearSelection();
    settle();

    // 单击导线应当选中它（不能因为自己处理按下事件就吞掉选中）
    const QPointF mid = (wire->scenePoints().first() + wire->scenePoints().last()) / 2.0;
    const QPoint at = view->mapFromScene(mid);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, at);
    settle();
    QVERIFY2(wire->isSelected(), "单击应当选中导线");

    // 选中后能删除（走真正的 Del 快捷键，而不是直接调接口）
    window.activateWindow();
    view->setFocus();
    settle();
    QTest::keyClick(&window, Qt::Key_Delete);
    settle();
    QCOMPARE(scene->wireItems().size(), 0);
    QCOMPARE(scene->toCircuit().wires.size(), 0);
}

void UiEditorTest::dragWireEndpointKeepsOtherEnd()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    auto* scene = window.findChild<SchematicScene*>();
    QVERIFY(scene);

    WireItem* wire = scene->addWire({QPointF(200, 200), QPointF(400, 200)});
    QVERIFY(wire);
    settle();

    const QVector<QPointF> before = wire->scenePoints();
    QCOMPARE(before.size(), 2);

    // 抓末端（400,200）往上拖到 (400,120)
    auto* view = window.findChild<SchematicView*>();
    QVERIFY(view);
    const QPoint end = view->mapFromScene(before.last());
    const QPoint moved = view->mapFromScene(QPointF(400, 120));

    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, end);
    QCOMPARE(wire->draggedEndpoint(), 1);
    QMouseEvent move(QEvent::MouseMove, QPointF(moved), view->viewport()->mapToGlobal(moved),
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &move);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, moved);
    settle();

    const QVector<QPointF> after = wire->scenePoints();
    QCOMPARE(after.size(), 2);
    // 首点不动，末点跟到新位置
    QCOMPARE(after.first(), before.first());
    QCOMPARE(after.last(), QPointF(400, 120));

    // 电路数据也跟着变
    const Circuit circuit = scene->toCircuit();
    QCOMPARE(circuit.wires.size(), 1);
    QCOMPARE(circuit.wires.first().points.last(), QPointF(400, 120));

    // 抓住中间整体平移：两个端点一起动（中点要在导线上）
    const QPointF midpoint = (after.first() + after.last()) / 2.0;
    const QPoint mid = view->mapFromScene(midpoint);
    const QPoint shifted = view->mapFromScene(midpoint + QPointF(40, 0));
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, mid);
    QCOMPARE(wire->draggedEndpoint(), -1);
    QMouseEvent drag(QEvent::MouseMove, QPointF(shifted), view->viewport()->mapToGlobal(shifted),
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &drag);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, shifted);
    settle();

    const QVector<QPointF> moved2 = wire->scenePoints();
    QCOMPARE(moved2.first(), after.first() + QPointF(40, 0));
    QCOMPARE(moved2.last(), after.last() + QPointF(40, 0));
}

void UiEditorTest::drawWireWithMouse()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    QAction* newAction = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().startsWith(QStringLiteral("新建")))
            newAction = action;
    }
    QVERIFY(newAction);
    newAction->trigger();
    settle();

    // 「新建」是开一个新标签页，画面已经换成新文档 —— 得重新取当前的 scene / view，
    // 抓着新建之前那个已经在后台了
    auto* scene = currentScene(window);
    auto* view = currentView(window);
    QVERIFY(scene && view);

    scene->placeComponent(ComponentType::Resistor, QPointF(300, 300));
    scene->placeComponent(ComponentType::VoltageSource, QPointF(300, 420));

    view->setTool(SchematicView::Tool::Wire);
    settle();

    const QPoint start = view->mapFromScene(QPointF(270, 300));
    const QPoint end = view->mapFromScene(QPointF(270, 420));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, end);
    QTest::mouseClick(view->viewport(), Qt::RightButton, Qt::NoModifier, end);
    settle();

    QCOMPARE(scene->wireItems().size(), 1);
    const QVector<QPointF> points = scene->wireItems().first()->scenePoints();
    QVERIFY(points.size() >= 2);
    QCOMPARE(points.first(), QPointF(270, 300));
    QCOMPARE(points.last(), QPointF(270, 420));
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
                                .filePath(QStringLiteral("editor-qtest.log"));
    QFile::remove(logPath);
    args << QStringLiteral("-o") << logPath + QStringLiteral(",txt");

    UiEditorTest testCase;
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


void UiEditorTest::measureProbeReadsTheWire()
{
    // 测量探针：贴在导线上就立刻有黄框（未仿真时是横杠），
    // 跑完瞬态后填上峰峰值 / 有效值 / 直流，并沿导线给一个方向箭头。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);
    window.openPath(QStringLiteral(":/examples/01_rc_charge.csm"));
    settle();

    auto* scene = currentScene(window);
    QVERIFY(scene);

    Component probe;
    probe.type = ComponentType::MeasureProbe;
    probe.refdes = scene->nextRefdes(ComponentType::MeasureProbe);
    probe.pos = QPointF(250.0, 200.0);   // R1 与 C1 之间的横导线
    ComponentItem* item = scene->addComponent(probe);
    QVERIFY(item);
    settle();

    QVERIFY2(item->probeOnWire(), "放在导线上应当识别为已连接");
    QVERIFY2(item->probeReadout().size() >= 6, "未仿真时也该有占位框");
    QVERIFY(item->probeReadout().first().contains(QStringLiteral("--")));
    // 箭头沿导线（这一段是水平的）
    QVERIFY2(std::abs(item->probeArrowDir().x()) > 0.9,
             qPrintable(QStringLiteral("箭头方向 = (%1, %2)")
                            .arg(item->probeArrowDir().x())
                            .arg(item->probeArrowDir().y())));

    window.runSimulation();
    settle(20);

    QVERIFY(item->probeTargetId() > 0);
    const QStringList lines = item->probeReadout();
    QVERIFY2(lines.size() >= 6, "仿真后应填满 V 与 I 两组");
    for (const QString& line : lines)
        QVERIFY2(!line.contains(QStringLiteral("--")), qPrintable(line));

    // 挪到空白处：不再算「贴在导线上」，但黄框要留着（行数不变，值是横杠）
    item->setPos(QPointF(250.0, 420.0));
    settle();
    QVERIFY(!item->probeOnWire());
    QCOMPARE(item->probeReadout().size(), 6);
}


void UiEditorTest::measureProbeUsesTheNearerWireEnd()
{
    // 一根导线两端接着不同元件时，探针取「离自己更近」那一端的元件当电流来源。
    // 否则一根长线（中途还有 T 型分支）会把末端支路的电流当成整根线的电流。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);
    auto* scene = currentScene(window);
    QVERIFY(scene);

    Component v;
    v.type = ComponentType::VoltageSource;
    v.id = 1;
    v.refdes = QStringLiteral("V1");
    v.function = SourceFunction::Constant;
    v.value = 10.0;
    v.pos = QPointF(100.0, 300.0);
    QVERIFY(scene->addComponent(v));

    Component r1;
    r1.type = ComponentType::Resistor;
    r1.id = 2;
    r1.refdes = QStringLiteral("R1");
    r1.value = 1000.0;
    r1.pos = QPointF(600.0, 300.0);
    QVERIFY(scene->addComponent(r1));

    // 一根长线把 V1 正极和 R1 左端连起来。
    // 注意要绕开 V1 自己的另一个引脚 —— 直着连过去会从它身上穿过，
    // 网表按坐标合并节点，等于把电源短路了。
    const QPointF plus = v.pinPosition(0);
    const QPointF left = r1.pinPosition(0);
    scene->addWire({plus, QPointF(plus.x(), 200.0), QPointF(left.x(), 200.0), left});
    settle();

    // 靠 V1 一侧 → 读 V1
    Component nearSource;
    nearSource.type = ComponentType::MeasureProbe;
    nearSource.refdes = QStringLiteral("MP1");
    nearSource.pos = QPointF(plus.x(), 260.0);
    ComponentItem* mp1 = scene->addComponent(nearSource);
    // 靠 R1 一侧 → 读 R1
    Component nearLoad;
    nearLoad.type = ComponentType::MeasureProbe;
    nearLoad.refdes = QStringLiteral("MP2");
    nearLoad.pos = QPointF(left.x(), 260.0);
    ComponentItem* mp2 = scene->addComponent(nearLoad);
    QVERIFY(mp1 && mp2);
    settle();

    QVERIFY(mp1->probeOnWire());
    QVERIFY(mp2->probeOnWire());
    QCOMPARE(mp1->probeTargetId(), 1);
    QCOMPARE(mp2->probeTargetId(), 2);
}


void UiEditorTest::measureProbeSumsTheSegmentCurrent()
{
    // 一根导线上有 T 型分支时，分支前后的电流不同 —— 不能取某个元件顶替，
    // 要在探针处切开、把这一侧注入的元件电流加起来。
    // 场景：竖线两端各接 V1 / R1，中间两处抽头接 R2 / R3，探针夹在两个抽头之间。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);
    auto* scene = currentScene(window);
    QVERIFY(scene);

    Component v;
    v.type = ComponentType::VoltageSource;
    v.id = 1;
    v.refdes = QStringLiteral("V1");
    v.function = SourceFunction::Constant;
    v.value = 10.0;
    v.pos = QPointF(100.0, 300.0);
    QVERIFY(scene->addComponent(v));

    auto addResistor = [scene](int id, const char* refdes, const QPointF& pos) {
        Component r;
        r.type = ComponentType::Resistor;
        r.id = id;
        r.refdes = QString::fromLatin1(refdes);
        r.value = 1000.0;
        r.pos = pos;
        QVERIFY(scene->addComponent(r));
    };
    addResistor(2, "R1", QPointF(600.0, 600.0));
    addResistor(3, "R2", QPointF(500.0, 400.0));
    addResistor(4, "R3", QPointF(500.0, 520.0));

    // 主干竖线 + 两处 T 型抽头
    scene->addWire({QPointF(130.0, 300.0), QPointF(400.0, 300.0)});
    scene->addWire({QPointF(400.0, 300.0), QPointF(400.0, 600.0)});
    scene->addWire({QPointF(400.0, 400.0), QPointF(470.0, 400.0)});
    scene->addWire({QPointF(400.0, 520.0), QPointF(470.0, 520.0)});
    scene->addWire({QPointF(400.0, 600.0), QPointF(570.0, 600.0)});
    settle();

    Component probe;
    probe.type = ComponentType::MeasureProbe;
    probe.refdes = QStringLiteral("MP1");
    probe.pos = QPointF(400.0, 430.0);   // R2 抽头与 R3 抽头之间
    ComponentItem* mp = scene->addComponent(probe);
    QVERIFY(mp);
    settle();

    QVERIFY(mp->probeOnWire());
    // 上下两侧各有两个元件贡献，取较近的一侧（V1 + R2）
    const auto weights = mp->probeCurrentWeights();
    QCOMPARE(weights.size(), 2);
    // 多个元件合成时不标来源
    QCOMPARE(mp->probeTargetId(), -1);
}


void UiEditorTest::measureProbeBoxStaysReadableWhenRotated()
{
    // 读数框里是横排的一行行文字，旋转只该改变框摆在哪一侧，
    // 不能把框转成竖条 —— 否则一行字就会被裁掉（曾经就是这样）。
    const QRectF base = probeReadoutBox();
    QSet<QString> centers;
    for (int angle : {0, 90, 180, 270}) {
        const QRectF box = probeReadoutBoxFor(angle);
        QCOMPARE(box.size(), base.size());          // 尺寸不变
        centers.insert(QStringLiteral("%1,%2").arg(qRound(box.center().x()))
                           .arg(qRound(box.center().y())));
    }
    // 四个方位各不相同（都摊在原点同一侧就没意义了）
    QCOMPARE(centers.size(), 4);

    // 0° 在上、90° 在右、180° 在下、270° 在左
    QVERIFY(probeReadoutBoxFor(0).center().y() < 0);
    QVERIFY(probeReadoutBoxFor(90).center().x() > 0);
    QVERIFY(probeReadoutBoxFor(180).center().y() > 0);
    QVERIFY(probeReadoutBoxFor(270).center().x() < 0);

    // 标号不能被框压住 —— 90° / 270° 时曾经整个被盖掉
    Component probe;
    probe.type = ComponentType::MeasureProbe;
    probe.refdes = QStringLiteral("MP1");
    probe.pos = QPointF(400.0, 300.0);
    for (int angle : {0, 90, 180, 270}) {
        probe.rotation = angle;
        ComponentItem item(probe);
        item.setProbeAttachment(true, QPointF(1.0, 0.0));
        const QRectF label = item.refLabelRect();
        const QRectF box = probeReadoutBoxFor(angle);
        QVERIFY2(!label.isEmpty() && !label.intersects(box),
                 qPrintable(QStringLiteral("%1° 时标号与读数框重叠").arg(angle)));
    }
}


void UiEditorTest::shortcutsWorkOnANewTab()
{
    // 「新建」会开一个新标签页，每张图纸有自己的视图。编辑类快捷键
    // （空格旋转、Delete…）是挂到视图上的，新视图没挂上就整个失效 ——
    // 早先就是这样：第一个标签能转，新建的图纸按空格没反应。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    QAction* newAction = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text().startsWith(QStringLiteral("新建")))
            newAction = action;
    }
    QVERIFY(newAction);
    newAction->trigger();
    settle();

    auto* view = currentView(window);
    auto* scene = currentScene(window);
    QVERIFY(view && scene);
    view->setFocus();
    settle();

    ComponentItem* item = scene->placeComponent(ComponentType::Resistor, QPointF(300.0, 300.0));
    QVERIFY(item);
    scene->clearSelection();
    item->setSelected(true);
    settle();

    const int before = item->component().rotation;
    QTest::keyClick(view, Qt::Key_Space);
    settle();
    QCOMPARE(item->component().rotation, ((before - 90) % 360 + 360) % 360);

    // Delete 也应当在新标签页上生效
    QTest::keyClick(view, Qt::Key_Delete);
    settle();
    QVERIFY(scene->componentItems().isEmpty());
}


void UiEditorTest::draggingAComponentDoesNotPullUnrelatedWires()
{
    // 拖动元件时，只有「一开始就接着它的引脚」的导线才跟着走。
    // 早先是每步重新判断「有没有导线顶点压在引脚上」，于是元件扫过一根线时
    // 那根线的顶点会被临时认领、被一路拖走 —— 看起来就是「线被吸过来了」。
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);
    auto* view = currentView(window);
    auto* scene = currentScene(window);
    QVERIFY(view && scene);

    // 一根与元件无关的悬空竖线，自由端正好在元件拖过去要经过的位置
    WireItem* wire = scene->addWire({QPointF(400.0, 600.0), QPointF(400.0, 400.0)});
    QVERIFY(wire);
    ComponentItem* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(430.0, 340.0));
    QVERIFY(resistor);
    scene->clearSelection();
    resistor->setSelected(true);
    settle();

    const QVector<QPointF> before = wire->scenePoints();

    // 往下拖：中途引脚正好压过导线自由端 (400,400)
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      view->mapFromScene(QPointF(430.0, 340.0)));
    for (int y = 360; y <= 520; y += 20) {
        const QPoint p = view->mapFromScene(QPointF(430.0, y));
        QMouseEvent move(QEvent::MouseMove, QPointF(p), view->viewport()->mapToGlobal(p),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &move);
    }
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                        view->mapFromScene(QPointF(430.0, 520.0)));
    settle();

    QVERIFY(resistor->pos().y() > 400.0);   // 元件确实动了
    const QVector<QPointF> after = wire->scenePoints();
    QCOMPARE(after.size(), before.size());
    for (int i = 0; i < before.size(); ++i) {
        QVERIFY2(QLineF(before.at(i), after.at(i)).length() <= 0.5,
                 qPrintable(QStringLiteral("导线第 %1 个点被拖动了").arg(i)));
    }
}


void UiEditorTest::escCancelsWireThroughTheAction()
{
    // Esc 现在是可改键的动作（「取消 / 取消选择」），不再是画布里写死的按键
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    waitShown(window);

    QAction* cancel = window.findChild<QAction*>(QStringLiteral("actCancel"));
    QVERIFY(cancel);
    QCOMPARE(cancel->shortcut(), QKeySequence(Qt::Key_Escape));

    auto* view = currentView(window);
    QVERIFY(view);
    view->setTool(SchematicView::Tool::Wire);
    settle();
    QCOMPARE(view->tool(), SchematicView::Tool::Wire);

    // 连线模式下按 Esc：取消连线并回到选择工具
    view->setFocus();
    settle();
    QTest::keyClick(view, Qt::Key_Escape);
    settle();
    QCOMPARE(view->tool(), SchematicView::Tool::Select);

    // 选择模式下按 Esc：取消选择
    auto* scene = currentScene(window);
    QVERIFY(scene);
    ComponentItem* item = scene->placeComponent(ComponentType::Resistor, QPointF(300.0, 300.0));
    QVERIFY(item);
    item->setSelected(true);
    settle();
    QVERIFY(!scene->selectedItems().isEmpty());
    QTest::keyClick(view, Qt::Key_Escape);
    settle();
    QVERIFY2(scene->selectedItems().isEmpty(), "Esc 应当取消选择");
}

#include "test_ui_editor.moc"
