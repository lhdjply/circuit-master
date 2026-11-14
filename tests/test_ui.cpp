#include "csm_version.h"
#include "ui/MainWindow.h"
#include "ui/Symbols.h"
#include "ui/UpdateDialog.h"
#include "ui/SchematicScene.h"
#include "ui/SchematicView.h"
#include "ui/WireItem.h"
#include "ui/ComponentItem.h"
#include "core/Netlist.h"
#include "core/Circuit.h"
#include "ui/WaveformWindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QTabWidget>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QComboBox>
#include <QTabWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QIcon>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QElapsedTimer>
#include <QSet>
#include <QTest>

using namespace csm;

class UiTest : public QObject {
    Q_OBJECT

private slots:
    void updateDialogRenders()
    {
        ReleaseInfo release;
        release.version = QStringLiteral("0.2.0");
        release.tagName = QStringLiteral("v0.2.0");
        release.title = QStringLiteral("circuit-master v0.2.0");
        release.notes = QStringLiteral("### 新增\n\n- 新增三极管模型\n- 修复波形导出\n");
        release.pageUrl = QUrl(QStringLiteral(
            "https://github.com/lhdjply/circuit-master/releases/tag/v0.2.0"));
        release.publishedAt = QStringLiteral("2026-12-01T10:00:00Z");

        UpdateDialog dialog(release);
        dialog.show();
        QTRY_VERIFY_WITH_TIMEOUT(dialog.isVisible(), 3000);
        settle();

        QVERIFY(dialog.isVisible());

        // 截图目录由 captureMainWindow 创建，而本用例在它之前运行
        QDir dir(QCoreApplication::applicationDirPath());
        QVERIFY(dir.mkpath(QStringLiteral("shots")));
        QVERIFY(dialog.grab().save(dir.filePath(QStringLiteral("shots/update_dialog.png"))));

        // 帮助菜单里应当有「检查更新…」
        MainWindow window;
        window.show();
        waitShown(window);
        bool found = false;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text() == QStringLiteral("检查更新…"))
                found = true;
        }
        QVERIFY(found);
    }

    void versionComesFromVersionFile()
    {
        // 版本号唯一来源是仓库根目录的 VERSION，这里校验编译进来的宏与之一致
        const QString expected = QStringLiteral(CSM_VERSION_STRING);
        QVERIFY(!expected.isEmpty());
        QVERIFY2(expected.startsWith(QStringLiteral("v")),
                 qPrintable(QStringLiteral("VERSION 应带 v 前缀：") + expected));

        const QString number = QStringLiteral(CSM_VERSION_NUMBER);
        QVERIFY2(!number.contains(QLatin1Char('v')),
                 qPrintable(QStringLiteral("CSM_VERSION_NUMBER 不应含 v：") + number));
        QVERIFY(expected.mid(1) == number);

        QFile file(QStringLiteral(CSM_SOURCE_DIR "/VERSION"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
                 "无法读取仓库根目录的 VERSION");
        const QString onDisk = QString::fromUtf8(file.readAll()).trimmed();
        QCOMPARE(onDisk, expected);
    }

    void toolbarIconsAvailable()
    {
        const QStringList names = {QStringLiteral("new"),    QStringLiteral("open"),
                                   QStringLiteral("save"),   QStringLiteral("select"),
                                   QStringLiteral("wire"),   QStringLiteral("run"),
                                   QStringLiteral("settings"), QStringLiteral("zoom_in"),
                                   QStringLiteral("zoom_out"), QStringLiteral("zoom_fit"),
                                   QStringLiteral("delete"), QStringLiteral("grid"),
                                   QStringLiteral("undo"),   QStringLiteral("redo")};
        for (const QString& name : names) {
            // 用应用真正的取图标入口，而不是裸 QIcon(path)：后者要求系统装了
            // Qt 的 SVG 图标插件，测的就变成环境而不是代码了
            const QString path = QStringLiteral(":/icons/%1.svg").arg(name);
            const QIcon icon = svgIcon(path);
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

            // 系统没有 Qt SVG 图标插件时（CI 的最小化安装），必须走 QSvgRenderer
            // 兜底，否则界面图标全空
            const QIcon fallback = svgIcon(QStringLiteral(":/icons/%1.svg").arg(name), true);
            QVERIFY2(!fallback.isNull(),
                     qPrintable(QStringLiteral("兜底渲染失败: ") + name));
            const QImage fallbackImage = fallback.pixmap(22, 22).toImage();
            int fallbackOpaque = 0;
            for (int y = 0; y < fallbackImage.height(); ++y) {
                for (int x = 0; x < fallbackImage.width(); ++x) {
                    if (qAlpha(fallbackImage.pixel(x, y)) > 0)
                        ++fallbackOpaque;
                }
            }
            QVERIFY2(fallbackOpaque > 0,
                     qPrintable(QStringLiteral("兜底渲染为空: ") + name));
        }

        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

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
        waitShown(window);

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
        settle();

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
        settle();
        QVERIFY(window.grab().save(dir.filePath(QStringLiteral("shots/main_selected.png"))));

        window.runSimulation();
        openStaticWave(window);
        auto* wave = waitWaveWindow(window);
        const QString waveShot = dir.filePath(QStringLiteral("shots/waveform.png"));
        QVERIFY(wave->grab().save(waveShot));
        QVERIFY(QFileInfo(waveShot).size() > 5000);
    }

    void captureAcSweep()
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
        QVERIFY(wave->grab().save(
            QCoreApplication::applicationDirPath() + QStringLiteral("/shots/waveform_ac.png")));
    }

    void captureDiodeRectifier()
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
        QVERIFY(wave->grab().save(
            QCoreApplication::applicationDirPath() + QStringLiteral("/shots/waveform_diode.png")));
    }

    void runStartsAndStopEndsSimulation()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        QAction* monitor = window.findChild<QAction*>(QStringLiteral("actMonitor"));
        QAction* run = window.findChild<QAction*>(QStringLiteral("actRun"));
        QVERIFY(monitor);
        QVERIFY(run);

        QVERIFY(!monitor->isEnabled());
        QCOMPARE(run->text(), QStringLiteral("运行仿真"));

        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("RC 充电"))) {
                action->trigger();
                break;
            }
        }
        settle();

        // 运行：开始仿真，但不自动打开波形监控
        run->trigger();
        settle();

        auto* live = livePanel(window);
        QVERIFY(live);
        QVERIFY2(live->isLive(), "运行仿真后应当连续运行");
        QVERIFY2(!live->isVisible(), "运行仿真不应自动打开波形监控");
        QCOMPARE(run->text(), QStringLiteral("停止仿真"));
        QVERIFY2(monitor->isEnabled(), "运行时波形监控应可用");

        // 点「波形监控」才打开窗口
        auto* monitorWindow = window.findChild<QWidget*>(QStringLiteral("waveMonitorWindow"));
        QVERIFY(monitorWindow);
        monitor->setChecked(true);
        settle();
        QVERIFY(monitorWindow->isVisible());
        QTabWidget* tabs = waveTabs(window);
        QVERIFY(tabs);
        tabs->setCurrentIndex(1);
        settle();
        QVERIFY(live->isVisible());

        // 关闭窗口不停止仿真，只是收起界面
        monitorWindow->close();
        settle();
        QVERIFY2(!monitorWindow->isVisible(), "关闭后窗口应当隐藏");
        QVERIFY2(live->isLive(), "关闭波形监控不应停止仿真");
        QVERIFY(!monitor->isChecked());

        // 重新打开还能看到正在跑的波形
        monitor->setChecked(true);
        settle();
        QVERIFY(monitorWindow->isVisible());
        QVERIFY(live->isLive());
        QTRY_VERIFY_WITH_TIMEOUT(live->liveTime() > 5e-4, 5000);
        QVERIFY(monitorWindow->grab().save(QCoreApplication::applicationDirPath()
                                           + QStringLiteral("/shots/monitor_running.png")));

        // 只有「停止仿真」才停
        run->trigger();
        settle();
        QVERIFY2(!live->isLive(), "点击停止后应当结束");
        QCOMPARE(run->text(), QStringLiteral("运行仿真"));
        QVERIFY2(!monitor->isEnabled(), "停止后波形监控应不可点击");
    }

    void scopeScalesAreRemembered()
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

    void analysisFieldsFollowFile()
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

    void runWithoutProbesStillRuns()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);

        // 只有电源、电阻、接地：没有测试点
        auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
        source->setRotationAngle(90);
        Component src = source->component();
        src.dcValue = 5.0;
        source->setComponent(src);
        auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(250, 200));
        Component r = resistor->component();
        r.value = 1000.0;
        resistor->setComponent(r);
        scene->placeComponent(ComponentType::Ground, QPointF(100, 400));
        scene->placeComponent(ComponentType::Ground, QPointF(300, 400));
        scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(220, 200)});
        scene->addWire({QPointF(280, 200), QPointF(300, 200), QPointF(300, 400)});
        scene->addWire({QPointF(100, 330), QPointF(100, 400)});
        settle();

        QAction* run = window.findChild<QAction*>(QStringLiteral("actRun"));
        QVERIFY(run);
        run->trigger();
        settle();

        // 没有测试点也要真的跑起来，只是波形为空
        auto* live = livePanel(window);
        QVERIFY(live);
        QVERIFY2(live->isLive(), "没有测试点也应当启动仿真");
        QCOMPARE(run->text(), QStringLiteral("停止仿真"));

        // 不应该弹模态提示框
        for (QMessageBox* box : window.findChildren<QMessageBox*>())
            QVERIFY2(!box->isVisible(), "不应弹出提示框打断");

        // 面板里要有说明
        auto* list = live->findChild<QListWidget*>();
        QVERIFY(list);
        QCOMPARE(list->count(), 0);

        run->trigger();
        settle();
        QVERIFY(!live->isLive());
    }

    void ledLightsUpAfterSimulation()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);

        auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
        source->setRotationAngle(90);
        Component src = source->component();
        src.function = SourceFunction::Constant;
        src.dcValue = 5.0;
        source->setComponent(src);

        auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(250, 200));
        Component r = resistor->component();
        r.value = 1000.0;
        resistor->setComponent(r);

        auto* led = scene->placeComponent(ComponentType::Led, QPointF(400, 300));
        led->setRotationAngle(90);
        scene->placeComponent(ComponentType::Ground, QPointF(100, 400));
        scene->placeComponent(ComponentType::Ground, QPointF(400, 400));

        scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(220, 200)});
        scene->addWire({QPointF(280, 200), QPointF(400, 200), QPointF(400, 270)});
        scene->addWire({QPointF(400, 330), QPointF(400, 400)});
        scene->addWire({QPointF(100, 330), QPointF(100, 400)});
        settle();

        QCOMPARE(led->ledBrightness(), 0.0);
        window.runSimulation();
        settle();

        // 5 V 经 1 kΩ → 约 3 mA，应当点亮
        QVERIFY2(led->ledBrightness() > 0.3,
                 qPrintable(QString::number(led->ledBrightness())));
        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath()
                                   + QStringLiteral("/shots/led_on.png")));

        // 停止仿真后应当熄灭
        QAction* run = window.findChild<QAction*>(QStringLiteral("actRun"));
        QVERIFY(run);
        run->trigger(); // 此时是「停止仿真」
        settle();
        QCOMPARE(led->ledBrightness(), 0.0);

        // 把 LED 反接再跑：不导通，仍然不亮
        led->setRotationAngle(270);
        settle();
        window.runSimulation();
        settle();
        QCOMPARE(led->ledBrightness(), 0.0);
        run->trigger(); // 收尾，停掉仿真
    }

    void spaceRotatesSelection()
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
        QCOMPARE(item->component().rotation, (before + 90) % 360);

        QTest::keyClick(&window, Qt::Key_Space);
        settle();
        QCOMPARE(item->component().rotation, (before + 180) % 360);

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

    void editPropertiesAndRotate()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);

        QAction* rcExample = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("RC 充电")))
                rcExample = action;
        }
        QVERIFY(rcExample);
        rcExample->trigger();
        settle();

        ComponentItem* resistor = nullptr;
        for (ComponentItem* item : scene->componentItems()) {
            if (item->component().refdes == QLatin1String("R1"))
                resistor = item;
        }
        QVERIFY(resistor);
        resistor->setSelected(true);
        settle();

        QLineEdit* valueEdit = nullptr;
        for (QLineEdit* edit : window.findChildren<QLineEdit*>()) {
            if (edit->placeholderText().contains(QStringLiteral("10kΩ")))
                valueEdit = edit;
        }
        QVERIFY(valueEdit);
        valueEdit->setText(QStringLiteral("2k"));
        emit valueEdit->editingFinished();
        settle();
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
        QVERIFY(window.grab().save(dir + QStringLiteral("/shots/opamp_schematic.png")));

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

        QVERIFY(wave->grab().save(dir + QStringLiteral("/shots/waveform_opamp.png")));
    }

    void libraryDragPixmapHasNoSelectionBackground()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        QListWidget* library = nullptr;
        for (QListWidget* list : window.findChildren<QListWidget*>()) {
            if (list->count() > 0 && list->item(0)->text().contains(QStringLiteral("电阻")))
                library = list;
        }
        QVERIFY(library);
        library->setCurrentRow(0);
        library->item(0)->setSelected(true);

        // 拖影应当只包含图标：取该行的图标按 72x48 渲染，背景透明
        const QIcon icon = library->item(0)->icon();
        QPixmap pixmap(72, 48);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        icon.paint(&painter, QRect(QPoint(0, 0), QSize(72, 48)), Qt::AlignCenter);
        painter.end();

        const QImage image = pixmap.toImage();
        int opaque = 0;
        int bluish = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                if (c.alpha() > 0) {
                    ++opaque;
                    // 选中背景是接近 #2563eb 的实心蓝
                    if (c.blue() > 180 && c.red() < 90 && c.green() < 130)
                        ++bluish;
                }
            }
        }
        QVERIFY2(opaque > 0, "拖影为空");
        QVERIFY2(bluish < opaque / 2, "拖影仍带有蓝色选中背景");
    }

    void libraryFiltersByCategory()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        QListWidget* library = nullptr;
        for (QListWidget* list : window.findChildren<QListWidget*>()) {
            if (list->count() > 0 && list->item(0)->text().contains(QStringLiteral("电阻")))
                library = list;
        }
        QVERIFY(library);
        const int allCount = library->count();
        QVERIFY(allCount >= 16);

        QComboBox* category = nullptr;
        for (QComboBox* box : window.findChildren<QComboBox*>()) {
            if (box->count() > 0 && box->itemText(0) == QStringLiteral("全部"))
                category = box;
        }
        QVERIFY(category);
        QCOMPARE(category->count(), 6);

        // 每个分类都应当有内容，且加起来不超过全部
        QMap<QString, int> counts;
        for (int i = 1; i < category->count(); ++i) {
            category->setCurrentIndex(i);
            settle();
            counts.insert(category->currentText(), library->count());
            QVERIFY2(library->count() > 0,
                     qPrintable(QStringLiteral("分类为空: ") + category->currentText()));
        }

        QCOMPARE(counts.value(QStringLiteral("基础")), 3);
        QCOMPARE(counts.value(QStringLiteral("电源")), 2);
        QCOMPARE(counts.value(QStringLiteral("半导体")), 8);

        int sum = 0;
        for (int n : counts)
            sum += n;
        QCOMPARE(sum, allCount);

        // 切回「全部」应恢复完整列表
        category->setCurrentIndex(0);
        settle();
        QCOMPARE(library->count(), allCount);
    }

    void transistorSymbolsRender()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);

        // 五种三端器件摆一排，确认符号能画出来、引脚数正确
        const QVector<ComponentType> types = {
            ComponentType::Npn,  ComponentType::Pnp,  ComponentType::Nmos,
            ComponentType::Pmos, ComponentType::Igbt, ComponentType::Led,
            ComponentType::Diode};
        for (int i = 0; i < types.size(); ++i) {
            // 两行摆放，避免被右侧属性面板挡住
            const QPointF pos(150 + (i % 3) * 120, i < 3 ? 250 : 400);
            ComponentItem* item = scene->placeComponent(types.at(i), pos);
            QVERIFY(item);
            QCOMPARE(item->pinScenePositions().size(), pinCount(types.at(i)));
            QCOMPARE(item->component().refdes.left(1), typeSymbol(types.at(i)));
        }
        settle();

        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath()
                                   + QStringLiteral("/shots/transistors.png")));
    }

    void undoRedoEdits()
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

    void dragComponentFromLibrary()
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
        const QPoint target = view->viewport()->mapFrom(view, targetInView);
        QDragEnterEvent enter(target, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &enter);
        QDropEvent drop(QPointF(target), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &drop);
        settle();

        QCOMPARE(scene->componentItems().size(), 1);
        ComponentItem* item = scene->componentItems().first();
        QCOMPARE(item->component().type, ComponentType::Resistor);
        QCOMPARE(item->pos(), QPointF(200, 200));
        QVERIFY(item->isSelected());
    }

    void copyPasteAndMirror()
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

    void multiSelectionDrag()
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

    void liveScopePicksUsableRange()
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
        v.amplitude = 220.0;
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

    void liveScopeFromMenu()
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
        settle();
        QVERIFY(wave->isLive());
    }

    void valueWithUnits()
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
        resistor->setSelected(true);
        settle();

        QLineEdit* valueEdit = nullptr;
        for (QLineEdit* edit : window.findChildren<QLineEdit*>()) {
            if (edit->placeholderText().contains(QStringLiteral("10kΩ")))
                valueEdit = edit;
        }
        QVERIFY(valueEdit);

        valueEdit->setText(QStringLiteral("2.2kΩ"));
        emit valueEdit->editingFinished();
        settle();
        QCOMPARE(resistor->component().value, 2200.0);

        // 单位不匹配：拒绝并标红
        valueEdit->setText(QStringLiteral("5V"));
        emit valueEdit->editingFinished();
        settle();
        QCOMPARE(resistor->component().value, 2200.0);
        QVERIFY(valueEdit->styleSheet().contains(QStringLiteral("dc2626")));

        // 不带单位按默认单位（Ω）
        valueEdit->setText(QStringLiteral("2.2k"));
        emit valueEdit->editingFinished();
        settle();
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
        settle();
        QVERIFY(valueEdit->text().contains(QStringLiteral("µF")));

        valueEdit->setText(QStringLiteral("0.0001F"));
        emit valueEdit->editingFinished();
        settle();
        QVERIFY(std::abs(capacitor->component().value - 100e-6) < 1e-15);

        // 重新选中后数值以工程记数法显示
        capacitor->setSelected(false);
        capacitor->setSelected(true);
        settle();
        QVERIFY(valueEdit->text().contains(QStringLiteral("100")));
    }

    void drawWireWithMouse()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

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
        settle();

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

    void buildAndSimulate()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);

        QAction* newAction = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().startsWith(QStringLiteral("新建")))
                newAction = action;
        }
        QVERIFY(newAction);
        newAction->trigger();
        settle();

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
        openStaticWave(window);
        auto* wave = waitWaveWindow(window);

        // 没有测试点时波形为空
        auto* list = wave->findChild<QListWidget*>();
        QVERIFY(list);
        QCOMPARE(list->count(), 0);
    }
private:
    /// 让事件循环跑几轮：处理 show()、布局请求，以及 QTimer::singleShot(0) 排队的任务。
    /// 比固定 qWait(N) 快得多，也不受机器快慢影响。
    static void settle(int rounds = 3)
    {
        for (int i = 0; i < rounds; ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    /// 等主窗口画完第一帧。
    static void waitShown(MainWindow& window)
    {
        QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 3000);
        settle();
    }

    /// 「波形监控」面板里的标签页
    static QTabWidget* waveTabs(MainWindow& window)
    {
        for (QTabWidget* tabs : window.findChildren<QTabWidget*>()) {
            if (tabs->count() == 2 && tabs->tabText(0) == QStringLiteral("静态波形"))
                return tabs;
        }
        return nullptr;
    }

    /// 打开「波形监控」并切到指定标签页（0 = 静态波形，1 = 动态示波器）
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

    /// 波形监控里的静态波形页面
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

    /// 等仿真把波形窗口弹出来。
    /// 这里不能直接用 QTRY_VERIFY：宏在失败时执行 `return;`，放在返回指针的
    /// 函数里编译不过。改成自己轮询，超时返回 nullptr 由调用方断言。
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

QTEST_MAIN(UiTest)

#include "test_ui.moc"
