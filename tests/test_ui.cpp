#include "ui/MainWindow.h"
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
#include <QIcon>
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
        QCOMPARE(scene->componentItems().size(), 4);
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
            if (edit->placeholderText().contains(QStringLiteral("4.7u")))
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
        rotateAction->trigger();
        QCOMPARE(resistor->component().rotation, 90);
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
        const QVector<QPointF> points = scene->wireItems().first()->points();
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
    }
};

QTEST_MAIN(UiTest)

#include "test_ui.moc"
