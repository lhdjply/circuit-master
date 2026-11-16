#include "csm_version.h"
#include "ui/MainWindow.h"
#include "ui/Symbols.h"
#include "csm_platform.h"
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
#include <QCheckBox>
#include <QAbstractAxis>
#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QValueAxis>
#include <QTabWidget>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QCheckBox>
#include <QAbstractAxis>
#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QValueAxis>
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
#include <QFile>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

using namespace csm;

class UiTest : public QObject {
    Q_OBJECT

private slots:
    void updateDialogDownloadsPackage()
    {
        // 造一个本地「安装包」，用 file:// 直链模拟下载
        const QString payload = QStringLiteral("fake package bytes 1234567890");
        const QString source = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("shots/fake-setup.bin"));
        QDir().mkpath(QFileInfo(source).absolutePath());
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(payload.toUtf8());
        }

        ReleaseInfo release;
        release.version = QStringLiteral("0.2.0");
        release.tagName = QStringLiteral("v0.2.0");
        release.pageUrl = QUrl(QStringLiteral("https://github.com/lhdjply/circuit-master/releases/tag/v0.2.0"));
        // 各平台的包都摆上（都指向同一个本地文件），挑包逻辑与宿主机无关
        const QString localUrl = QUrl::fromLocalFile(source).toString();
        for (const QString& name : {QStringLiteral("circuit-master_0.2.0_amd64.deb"),
                                    QStringLiteral("circuit-master-0.2.0-linux-amd64.tar.gz"),
                                    QStringLiteral("circuit-master-0.2.0-windows-x64-setup.exe"),
                                    QStringLiteral("circuit-master-0.2.0-windows-arm64-setup.exe"),
                                    QStringLiteral("circuit-master-0.2.0-windows-x64.zip")}) {
            ReleaseAsset item;
            item.name = name;
            item.url = localUrl;
            item.size = payload.toUtf8().size();
            release.assets.append(item);
        }

        UpdateDialog dialog(release);
        QVERIFY2(!dialog.asset().name.isEmpty(), "应当挑到当前平台对应的包");
        QVERIFY(dialog.asset().name.startsWith(QStringLiteral("circuit-master")));
        // 包形态按「平台 + 当前安装方式」自动选：安装版给 deb / setup，便携版给便携包
        const InstallKind kind = UpdateChecker::detectInstallKind();
        const QVector<PackageFormat> formats = UpdateChecker::formatsFor(csm::kHostPlatform, kind);
        QVERIFY2(!formats.isEmpty(), "当前平台应当有可下载的包形态");
        const QString expectedSuffix =
            UpdateChecker::suffixFor(csm::kHostPlatform, csm::kHostArch, formats.first());
        QVERIFY2(dialog.asset().name.endsWith(expectedSuffix),
                 qPrintable(QStringLiteral("%1 应当以 %2 结尾")
                                .arg(dialog.asset().name, expectedSuffix)));

        const QString target = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("shots/downloaded.bin"));
        QFile::remove(target);

        QSignalSpy spy(&dialog, &UpdateDialog::downloadFinished);
        QVERIFY(dialog.downloadTo(target));
        QVERIFY(spy.wait(10000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(dialog.savedPath(), target);

        // 便携版运行时应优先下便携包
        qputenv("CSM_INSTALL_KIND", "portable");
        {
            UpdateDialog portable(release);
            const QVector<PackageFormat> kinds =
                UpdateChecker::formatsFor(csm::kHostPlatform, InstallKind::Portable);
            QVERIFY(!kinds.isEmpty());
            const QString suffix =
                UpdateChecker::suffixFor(csm::kHostPlatform, csm::kHostArch, kinds.first());
            QVERIFY2(portable.asset().name.endsWith(suffix),
                     qPrintable(QStringLiteral("%1 应当以 %2 结尾")
                                    .arg(portable.asset().name, suffix)));
        }
        qunsetenv("CSM_INSTALL_KIND");

        QFile saved(target);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(saved.readAll()), payload);
        saved.close();
        QFile::remove(target);
    }

    void constantSignalKeepsUsableAxis()
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

    void reversedPolarizedCapacitorExplodes()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);

        // 5 V 反接在极性电容上（正极接地、负极接 5 V）
        auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
        source->setRotationAngle(90);
        Component src = source->component();
        src.dcValue = 5.0;
        source->setComponent(src);

        auto* cap = scene->placeComponent(ComponentType::Capacitor, QPointF(300, 300));
        cap->setRotationAngle(90);
        Component cc = cap->component();
        cc.value = 100e-6;
        cc.polarized = true;
        cap->setComponent(cc);

        scene->placeComponent(ComponentType::Ground, QPointF(100, 400));
        scene->placeComponent(ComponentType::Ground, QPointF(300, 400));

        // 正极（引脚 0，上端）接地；负极接 5 V，绕开另一只引脚
        scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(380, 200),
                        QPointF(380, 330), QPointF(300, 330)});
        scene->addWire({QPointF(300, 270), QPointF(300, 100), QPointF(60, 100),
                        QPointF(60, 400)});
        scene->addWire({QPointF(60, 400), QPointF(300, 400)});
        scene->addWire({QPointF(100, 330), QPointF(100, 400)});
        settle();

        QVERIFY(!cap->isBlown());
        window.runSimulation();
        settle();

        QVERIFY2(cap->isBlown(), "极性电容反接应当击穿");

        // 等动画播完，云应当留在画布上而不是消失
        QTRY_VERIFY_WITH_TIMEOUT(cap->blastProgress() >= 1.0, 3000);
        QTest::qWait(400);
        settle();
        QCOMPARE(cap->blastProgress(), 1.0);
        QVERIFY2(cap->isBlown(), "动画结束后爆炸特效应保留");
        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath()
                                   + QStringLiteral("/shots/cap_explosion.png")));

        // 停止仿真后特效清除
        QAction* run = window.findChild<QAction*>(QStringLiteral("actRun"));
        QVERIFY(run);
        run->trigger();
        settle();
        QVERIFY2(!cap->isBlown(), "停止仿真后应当清除爆炸特效");
    }

    void capacitorPolarityToggle()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);
        ComponentItem* cap = scene->placeComponent(ComponentType::Capacitor, QPointF(200, 200));
        QVERIFY(cap);
        QCOMPARE(cap->component().polarized, false);
        cap->setSelected(true);
        settle();

        QCheckBox* polarized = nullptr;
        for (QCheckBox* box : window.findChildren<QCheckBox*>()) {
            if (box->text().contains(QStringLiteral("有极性")))
                polarized = box;
        }
        QVERIFY(polarized);
        QVERIFY2(polarized->isVisible(), "电容应当显示极性选项");
        QVERIFY(!polarized->isChecked());

        polarized->setChecked(true);
        settle();
        QVERIFY2(cap->component().polarized, "勾选后元件应当标记为有极性");

        // 电阻不显示这一项
        ComponentItem* res = scene->placeComponent(ComponentType::Resistor, QPointF(400, 200));
        scene->clearSelection(); // 先取消电容的选中，面板才会切到电阻
        res->setSelected(true);
        settle();
        QVERIFY2(!polarized->isVisible(), "电阻不应显示极性选项");
    }

    void switchToggleUpdatesLedWhileRunning()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);

        // 5 V → 开关 → 1 kΩ → LED → 地
        auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
        source->setRotationAngle(90);
        Component src = source->component();
        src.dcValue = 5.0;
        source->setComponent(src);

        auto* sw = scene->placeComponent(ComponentType::Switch, QPointF(200, 200));
        Component swc = sw->component();
        swc.closed = true;
        sw->setComponent(swc);

        auto* resistor = scene->placeComponent(ComponentType::Resistor, QPointF(300, 200));
        Component r = resistor->component();
        r.value = 1000.0;
        resistor->setComponent(r);

        auto* led = scene->placeComponent(ComponentType::Led, QPointF(400, 300));
        led->setRotationAngle(90);

        scene->placeComponent(ComponentType::Ground, QPointF(100, 400));
        scene->placeComponent(ComponentType::Ground, QPointF(400, 400));

        scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(170, 200)});
        scene->addWire({QPointF(230, 200), QPointF(270, 200)});
        scene->addWire({QPointF(330, 200), QPointF(400, 200), QPointF(400, 270)});
        scene->addWire({QPointF(400, 330), QPointF(400, 400)});
        scene->addWire({QPointF(100, 330), QPointF(100, 400)});
        settle();

        QAction* run = window.findChild<QAction*>(QStringLiteral("actRun"));
        QVERIFY(run);
        run->trigger();
        settle();

        auto* live = livePanel(window);
        QVERIFY(live);
        QVERIFY(live->isLive());
        QTRY_VERIFY_WITH_TIMEOUT(led->ledBrightness() > 0.3, 5000);

        // 记下当前时刻：拨开关不应该把时间轴清零
        const double before = live->liveTime();
        QVERIFY(before > 0.0);

        // 断开开关：原地更新引擎，LED 熄灭
        sw->setClosed(false);
        QTRY_VERIFY_WITH_TIMEOUT(led->ledBrightness() == 0.0, 5000);
        QVERIFY(live->isLive());
        QVERIFY2(live->liveTime() >= before, "拨开关不应重置时间轴");

        // 再闭合：重新点亮
        sw->setClosed(true);
        QTRY_VERIFY_WITH_TIMEOUT(led->ledBrightness() > 0.3, 5000);
        QVERIFY2(live->liveTime() >= before, "拨开关不应重置时间轴");

        run->trigger();
        settle();
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
        QCOMPARE(counts.value(QStringLiteral("半导体")), 10); // 含光耦与 TL431

        int sum = 0;
        for (int n : counts)
            sum += n;
        QCOMPARE(sum, allCount);

        // 切回「全部」应恢复完整列表
        category->setCurrentIndex(0);
        settle();
        QCOMPARE(library->count(), allCount);
    }

    void optocouplerSymbolAndPanel()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);
        ComponentItem* opto = scene->placeComponent(ComponentType::Optocoupler, QPointF(250, 300));
        QVERIFY(opto);
        QCOMPARE(opto->pinScenePositions().size(), 4);
        QCOMPARE(opto->component().refdes.left(2), QStringLiteral("OC"));
        // 默认参数：红外 LED 的 Vf 与 CTR
        QCOMPARE(opto->component().vf, 1.2);
        // 默认按 LTV-817S-TA1-C：CTR 档位 C 取典型 300%
        QCOMPARE(opto->component().ctr, 3.0);

        scene->clearSelection();
        opto->setSelected(true);
        settle();

        // 属性面板要有 Vf 与 CTR 两栏
        QLineEdit* ctrEdit = nullptr;
        for (QLineEdit* edit : window.findChildren<QLineEdit*>()) {
            if (edit->text() == QStringLiteral("3") && edit->isVisible())
                ctrEdit = edit;
        }
        QVERIFY2(ctrEdit, "应当显示 CTR 输入框");

        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath()
                                   + QStringLiteral("/shots/optocoupler.png")));
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

    void movingComponentDragsItsWires()
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

    void draggingAnchoredWireStretchesInsteadOfDetaching()
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

        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath()
                                   + QStringLiteral("/shots/wire_orthogonal.png")));

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

    void clickingWireHighlightsOnlyThatSegment()
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

    void clickingWireKeepsItSelected()
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

    void drawnWireMergesWithNeighbour()
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

    void cutCopyPasteFromEditMenu()
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

    void wireCanBeSelectedAndDeleted()
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

    void dragWireEndpointKeepsOtherEnd()
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
