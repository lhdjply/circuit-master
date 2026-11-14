#include "ui/MainWindow.h"
#include "ui/SimulationSettingsDialog.h"
#include "ui/SchematicScene.h"
#include "ui/SchematicView.h"
#include "ui/WireItem.h"
#include "ui/ComponentItem.h"
#include "core/Netlist.h"
#include "core/Circuit.h"
#include "ui/WaveformWindow.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QComboBox>
#include <QIcon>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QTest>

using namespace csm;

class UiTest : public QObject {
    Q_OBJECT

private slots:
    void toolbarIconsAvailable()
    {
        const QStringList names = {QStringLiteral("new"),    QStringLiteral("open"),
                                   QStringLiteral("save"),   QStringLiteral("select"),
                                   QStringLiteral("wire"),   QStringLiteral("run"),
                                   QStringLiteral("settings"), QStringLiteral("zoom_in"),
                                   QStringLiteral("zoom_out"), QStringLiteral("zoom_fit"),
                                   QStringLiteral("delete"), QStringLiteral("grid")};
        for (const QString& name : names) {
            const QIcon icon(QStringLiteral(":/icons/%1.svg").arg(name));
            QVERIFY2(!icon.isNull(), qPrintable(QStringLiteral("缺少图标: ") + name));

            // 资源存在不代表能渲染：检查生成的位图确实有像素
            const QImage image = icon.pixmap(22, 22).toImage();
            int opaque = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (qAlpha(image.pixel(x, y)) > 0)
                        ++opaque;
                }
            }
            QVERIFY2(opaque > 0, qPrintable(QStringLiteral("图标渲染为空: ") + name));
        }

        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QTest::qWait(200);

        QAction* wireAction = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text() == QStringLiteral("连线"))
                wireAction = action;
        }
        QVERIFY(wireAction);
        QVERIFY(!wireAction->icon().isNull());
    }

    void captureMainWindow()
    {
        MainWindow window;
        window.resize(1400, 880);
        window.show();
        QTest::qWait(400);

        QVERIFY2(!window.windowIcon().isNull(), "应用图标未能从资源加载");

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);
        // 启动应为空白电路
        QCOMPARE(scene->componentItems().size(), 0);
        QCOMPARE(scene->wireItems().size(), 0);

        // 为截图载入 RC 示例
        QAction* rcExample = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("RC 充电")))
                rcExample = action;
        }
        QVERIFY(rcExample);
        rcExample->trigger();
        QTest::qWait(200);

        QCOMPARE(scene->componentItems().size(), 6); // 4 个元件 + 2 个测试点
        QCOMPARE(scene->wireItems().size(), 3);

        QDir dir(QCoreApplication::applicationDirPath());
        QVERIFY(dir.mkpath(QStringLiteral("shots")));
        const QString mainShot = dir.filePath(QStringLiteral("shots/main.png"));
        QVERIFY(window.grab().save(mainShot));

        // 选中 R1，检查属性面板
        for (ComponentItem* item : scene->componentItems()) {
            if (item->component().refdes == QLatin1String("R1")) {
                item->setSelected(true);
                break;
            }
        }
        QTest::qWait(150);
        QVERIFY(window.grab().save(dir.filePath(QStringLiteral("shots/main_selected.png"))));

        window.runSimulation();
        QTest::qWait(500);

        auto* wave = window.findChild<WaveformWindow*>();
        QVERIFY(wave);
        QVERIFY(wave->isVisible());
        const QString waveShot = dir.filePath(QStringLiteral("shots/waveform.png"));
        QVERIFY(wave->grab().save(waveShot));
        QVERIFY(QFileInfo(waveShot).size() > 5000);
    }

    void captureAcSweep()
    {
        MainWindow window;
        window.resize(1400, 880);
        window.show();
        QTest::qWait(300);

        QAction* acExample = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("低通")))
                acExample = action;
        }
        QVERIFY(acExample);
        acExample->trigger();
        QTest::qWait(200);

        window.runSimulation();
        QTest::qWait(500);

        auto* wave = window.findChild<WaveformWindow*>();
        QVERIFY(wave);
        QVERIFY(wave->isVisible());
        QVERIFY(wave->grab().save(
            QCoreApplication::applicationDirPath() + QStringLiteral("/shots/waveform_ac.png")));
    }

    void captureDiodeRectifier()
    {
        MainWindow window;
        window.resize(1400, 880);
        window.show();
        QTest::qWait(300);

        QAction* diodeExample = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("整流")))
                diodeExample = action;
        }
        QVERIFY(diodeExample);
        diodeExample->trigger();
        QTest::qWait(200);

        window.runSimulation();
        QTest::qWait(600);

        auto* wave = window.findChild<WaveformWindow*>();
        QVERIFY(wave);
        QVERIFY(wave->isVisible());
        QVERIFY(wave->grab().save(
            QCoreApplication::applicationDirPath() + QStringLiteral("/shots/waveform_diode.png")));
    }

    void editPropertiesAndRotate()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QTest::qWait(300);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);

        QAction* rcExample = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("RC 充电")))
                rcExample = action;
        }
        QVERIFY(rcExample);
        rcExample->trigger();
        QTest::qWait(200);

        ComponentItem* resistor = nullptr;
        for (ComponentItem* item : scene->componentItems()) {
            if (item->component().refdes == QLatin1String("R1"))
                resistor = item;
        }
        QVERIFY(resistor);
        resistor->setSelected(true);
        QTest::qWait(100);

        QLineEdit* valueEdit = nullptr;
        for (QLineEdit* edit : window.findChildren<QLineEdit*>()) {
            if (edit->placeholderText().contains(QStringLiteral("10kΩ")))
                valueEdit = edit;
        }
        QVERIFY(valueEdit);
        valueEdit->setText(QStringLiteral("2k"));
        emit valueEdit->editingFinished();
        QTest::qWait(50);
        QCOMPARE(resistor->component().value, 2000.0);

        QAction* rotateAction = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("旋转")))
                rotateAction = action;
        }
        QVERIFY(rotateAction);
        // 示例中的 R1 初始为水平放置，旋转一次后应为 90°
        QCOMPARE(resistor->component().rotation, 0);
        rotateAction->trigger();
        QCOMPARE(resistor->component().rotation, 90);
    }

    void captureOpAmp()
    {
        MainWindow window;
        window.resize(1400, 880);
        window.show();
        QTest::qWait(300);

        QAction* opAmpExample = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("运放")))
                opAmpExample = action;
        }
        QVERIFY(opAmpExample);
        opAmpExample->trigger();
        QTest::qWait(200);

        const QString dir = QCoreApplication::applicationDirPath();
        QVERIFY(window.grab().save(dir + QStringLiteral("/shots/opamp_schematic.png")));

        // 选中运放后，属性面板应显示“开环增益”
        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);
        for (ComponentItem* item : scene->componentItems()) {
            if (item->component().type == ComponentType::OpAmp)
                item->setSelected(true);
        }
        QTest::qWait(100);
        bool gainLabelFound = false;
        for (QLabel* label : window.findChildren<QLabel*>()) {
            if (label->text() == QStringLiteral("开环增益"))
                gainLabelFound = true;
        }
        QVERIFY2(gainLabelFound, "属性面板缺少开环增益字段");

        window.runSimulation();
        QTest::qWait(600);

        auto* wave = window.findChild<WaveformWindow*>();
        QVERIFY(wave);
        QVERIFY(wave->isVisible());

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

        QVERIFY(wave->grab().save(dir + QStringLiteral("/shots/waveform_opamp.png")));
    }

    void copyPasteAndMirror()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QTest::qWait(300);

        QAction* rcExample = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("RC 充电")))
                rcExample = action;
        }
        QVERIFY(rcExample);
        rcExample->trigger();
        QTest::qWait(200);

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
        QTest::qWait(50);

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

    void multiSelectionDrag()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QTest::qWait(300);

        QAction* rcExample = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("RC 充电")))
                rcExample = action;
        }
        QVERIFY(rcExample);
        rcExample->trigger();
        QTest::qWait(200);

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
        QTest::qWait(100);

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
        QTest::qWait(100);

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
        QTest::qWait(100);
        QCOMPARE(r1->pos(), r1Mid + QPointF(40, 0));
        QCOMPARE(c1->pos(), c1Mid + QPointF(40, 0));

        // 也可以直接抓导线拖动整组
        const QPointF wireBefore = wire->scenePoints().first();
        const QPoint wirePress = view->mapFromScene(wire->scenePoints().first());
        const QPoint wireTo = view->mapFromScene(wire->scenePoints().first() + QPointF(0, 30));
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, wirePress);
        QTest::mouseMove(view->viewport(), wireTo, 20);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, wireTo);
        QTest::qWait(100);
        QCOMPARE(wire->scenePoints().first(), wireBefore + QPointF(0, 30));
    }

    void settingsHideTimingForScope()
    {
        SimulationOptions options;
        options.kind = AnalysisKind::Transient;
        options.display = DisplayMode::Scope;
        options.tStop = 3e-3;
        options.tStep = 1e-5;

        SimulationSettingsDialog dialog(options);
        dialog.show();
        QTest::qWait(150);

        QLineEdit* stopEdit = nullptr;
        QLineEdit* stepEdit = nullptr;
        for (QLineEdit* edit : dialog.findChildren<QLineEdit*>()) {
            if (edit->text() == QString::number(options.tStop, 'g', 6))
                stopEdit = edit;
            if (edit->text() == QString::number(options.tStep, 'g', 6))
                stepEdit = edit;
        }
        QVERIFY(stopEdit);
        QVERIFY(stepEdit);
        QVERIFY(!stopEdit->isVisible());
        QVERIFY(!stepEdit->isVisible());

        bool hintVisible = false;
        for (QLabel* label : dialog.findChildren<QLabel*>()) {
            if (label->text().contains(QStringLiteral("自动选择步长")) && label->isVisible())
                hintVisible = true;
        }
        QVERIFY(hintVisible);

        // 切回静态波形图后恢复
        for (QComboBox* box : dialog.findChildren<QComboBox*>()) {
            if (box->count() == 2 && box->itemText(0) == QStringLiteral("静态波形图"))
                box->setCurrentIndex(0);
        }
        QTest::qWait(100);
        QVERIFY(stopEdit->isVisible());
        QVERIFY(stepEdit->isVisible());

        // 回到动态模式并截图
        for (QComboBox* box : dialog.findChildren<QComboBox*>()) {
            if (box->count() == 2 && box->itemText(1) == QStringLiteral("动态示波器"))
                box->setCurrentIndex(1);
        }
        QTest::qWait(100);
        QVERIFY(dialog.grab().save(QCoreApplication::applicationDirPath()
                                   + QStringLiteral("/shots/settings_scope.png")));
    }

    void liveScopeFromMenu()
    {
        MainWindow window;
        window.resize(1400, 880);
        window.show();
        QTest::qWait(300);

        QAction* rcExample = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("RC 充电")))
                rcExample = action;
        }
        QVERIFY(rcExample);
        rcExample->trigger();
        QTest::qWait(200);

        QAction* scopeAction = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text() == QStringLiteral("动态示波器"))
                scopeAction = action;
        }
        QVERIFY(scopeAction);
        scopeAction->trigger();
        QTest::qWait(1200);

        auto* wave = window.findChild<WaveformWindow*>();
        QVERIFY(wave);
        QVERIFY(wave->isVisible());
        QVERIFY(wave->isLive());
        QVERIFY(wave->liveTime() > 0.0);
        QVERIFY(wave->grab().save(QCoreApplication::applicationDirPath()
                                  + QStringLiteral("/shots/live_scope.png")));

        // 时间窗下拉应覆盖 5 ns ~ 100 s
        QComboBox* windowBox = nullptr;
        for (QComboBox* box : wave->findChildren<QComboBox*>()) {
            if (box->count() > 0 && box->itemText(0).startsWith(QStringLiteral("5 ns")))
                windowBox = box;
        }
        QVERIFY(windowBox);
        QCOMPARE(windowBox->count(), 32);
        windowBox->setCurrentIndex(windowBox->count() - 1);
        QTest::qWait(300);
        QVERIFY(wave->isLive());
    }

    void valueWithUnits()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QTest::qWait(300);

        QAction* rcExample = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("RC 充电")))
                rcExample = action;
        }
        QVERIFY(rcExample);
        rcExample->trigger();
        QTest::qWait(200);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);

        ComponentItem* resistor = nullptr;
        for (ComponentItem* item : scene->componentItems()) {
            if (item->component().refdes == QLatin1String("R1"))
                resistor = item;
        }
        QVERIFY(resistor);
        resistor->setSelected(true);
        QTest::qWait(100);

        QLineEdit* valueEdit = nullptr;
        for (QLineEdit* edit : window.findChildren<QLineEdit*>()) {
            if (edit->placeholderText().contains(QStringLiteral("10kΩ")))
                valueEdit = edit;
        }
        QVERIFY(valueEdit);

        valueEdit->setText(QStringLiteral("2.2kΩ"));
        emit valueEdit->editingFinished();
        QTest::qWait(50);
        QCOMPARE(resistor->component().value, 2200.0);

        // 单位不匹配：拒绝并标红
        valueEdit->setText(QStringLiteral("5V"));
        emit valueEdit->editingFinished();
        QTest::qWait(50);
        QCOMPARE(resistor->component().value, 2200.0);
        QVERIFY(valueEdit->styleSheet().contains(QStringLiteral("dc2626")));

        // 不带单位按默认单位（Ω）
        valueEdit->setText(QStringLiteral("2.2k"));
        emit valueEdit->editingFinished();
        QTest::qWait(50);
        QCOMPARE(resistor->component().value, 2200.0);
        QVERIFY(valueEdit->styleSheet().isEmpty());

        // 电容带单位
        ComponentItem* capacitor = nullptr;
        for (ComponentItem* item : scene->componentItems()) {
            if (item->component().refdes == QLatin1String("C1"))
                capacitor = item;
        }
        QVERIFY(capacitor);
        scene->clearSelection();
        capacitor->setSelected(true);
        QTest::qWait(100);
        QVERIFY(valueEdit->text().contains(QStringLiteral("µF")));

        valueEdit->setText(QStringLiteral("0.0001F"));
        emit valueEdit->editingFinished();
        QTest::qWait(50);
        QVERIFY(std::abs(capacitor->component().value - 100e-6) < 1e-15);

        // 重新选中后数值以工程记数法显示
        capacitor->setSelected(false);
        capacitor->setSelected(true);
        QTest::qWait(50);
        QVERIFY(valueEdit->text().contains(QStringLiteral("100")));
    }

    void drawWireWithMouse()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QTest::qWait(300);

        auto* scene = window.findChild<SchematicScene*>();
        auto* view = window.findChild<SchematicView*>();
        QVERIFY(scene && view);

        QAction* newAction = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().startsWith(QStringLiteral("新建")))
                newAction = action;
        }
        QVERIFY(newAction);
        newAction->trigger();
        QTest::qWait(100);

        scene->placeComponent(ComponentType::Resistor, QPointF(300, 300));
        scene->placeComponent(ComponentType::VoltageSource, QPointF(300, 420));

        view->setTool(SchematicView::Tool::Wire);
        QTest::qWait(50);

        const QPoint start = view->mapFromScene(QPointF(270, 300));
        const QPoint end = view->mapFromScene(QPointF(270, 420));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, end);
        QTest::mouseClick(view->viewport(), Qt::RightButton, Qt::NoModifier, end);
        QTest::qWait(100);

        QCOMPARE(scene->wireItems().size(), 1);
        const QVector<QPointF> points = scene->wireItems().first()->scenePoints();
        QVERIFY(points.size() >= 2);
        QCOMPARE(points.first(), QPointF(270, 300));
        QCOMPARE(points.last(), QPointF(270, 420));
    }

    void buildAndSimulate()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QTest::qWait(300);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);

        QAction* newAction = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().startsWith(QStringLiteral("新建")))
                newAction = action;
        }
        QVERIFY(newAction);
        newAction->trigger();
        QTest::qWait(50);

        auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(200, 200));
        auto* capacitor = scene->placeComponent(ComponentType::Capacitor, QPointF(300, 300));
        capacitor->setRotationAngle(90);
        auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
        source->setRotationAngle(90);
        scene->placeComponent(ComponentType::Ground, QPointF(200, 400));

        Component sourceComponent = source->component();
        sourceComponent.function = SourceFunction::Pulse;
        sourceComponent.pulseV1 = 0.0;
        sourceComponent.pulseV2 = 5.0;
        sourceComponent.pulseRise = 1e-6;
        sourceComponent.pulseFall = 1e-6;
        sourceComponent.pulseWidth = 4e-3;
        sourceComponent.pulsePeriod = 10e-3;
        source->setComponent(sourceComponent);

        scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(170, 200)});
        scene->addWire({QPointF(230, 200), QPointF(300, 200), QPointF(300, 270)});
        scene->addWire({QPointF(300, 330), QPointF(300, 400), QPointF(100, 400),
                        QPointF(100, 330)});

        const Circuit circuit = scene->toCircuit();
        QCOMPARE(circuit.components.size(), 4);

        QSet<int> ids;
        for (const Component& comp : circuit.components) {
            QVERIFY(comp.id > 0);
            ids.insert(comp.id);
        }
        QCOMPARE(ids.size(), circuit.components.size());

        const Netlist net = circuit.buildNetlist();
        QVERIFY2(net.valid, qPrintable(net.error));
        QCOMPARE(net.nodeCount, 2);
        QCOMPARE(net.nodeOf(resistor->component().id, 0), 1);
        QCOMPARE(net.nodeOf(resistor->component().id, 1), 2);
        QCOMPARE(net.nodeOf(capacitor->component().id, 1), 0);

        window.runSimulation();
        QTest::qWait(500);

        auto* wave = window.findChild<WaveformWindow*>();
        QVERIFY(wave);
        QVERIFY(wave->isVisible());

        // 没有测试点时波形为空
        auto* list = wave->findChild<QListWidget*>();
        QVERIFY(list);
        QCOMPARE(list->count(), 0);
    }
};

QTEST_MAIN(UiTest)

#include "test_ui.moc"
