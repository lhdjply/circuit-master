#include "csm_version.h"
#include "ui/I18n.h"
#include "ui/MainWindow.h"
#include "ui/Symbols.h"
#include "csm_platform.h"
#include "ui/UpdateDialog.h"
#include "ui/SchematicScene.h"
#include "ui/SchematicView.h"
#include "ui/WireItem.h"
#include "ui/ComponentItem.h"

#include <QGraphicsSceneMouseEvent>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include "ui/PropertyPanel.h"
#include "ui/MultimeterWindow.h"
#include "core/Netlist.h"
#include "core/Circuit.h"
#include "ui/WaveformWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QAbstractAxis>
#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QComboBox>
#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QValueAxis>
#include <QTabWidget>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QPixmap>
#include <QCheckBox>
#include <QAbstractAxis>
#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QComboBox>
#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
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
#include <QMenu>
#include <QElapsedTimer>
#include <QFile>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

using namespace csm;

class UiTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // 和 main.cpp 一样装上 Qt 自带的中文翻译（输入框右键菜单等）
        csm::installQtTranslations(qApp);
    }

    void updateDialogDownloadsPackage()
    {
        // 不依赖外网：让下载直接打原始地址（file://）
        // 还原时用「改动前实际在用的那张表」，别写死 —— 内置镜像会变
        const QStringList savedMirrors = UpdateChecker::builtinMirrors();
        UpdateChecker::setBuiltinMirrors({});
        auto restore = qScopeGuard([savedMirrors] {
            UpdateChecker::setBuiltinMirrors(savedMirrors);
        });
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
        // 三平台都要有：这个用例要验证「挑到当前平台对应的包」，
        // 只放 Linux/Windows 的资产时，在 macOS 上跑就会一个都挑不到
        for (const QString& name : {QStringLiteral("circuit-master_0.2.0_amd64.deb"),
                                    QStringLiteral("circuit-master-0.2.0-linux-amd64.tar.gz"),
                                    QStringLiteral("circuit-master-0.2.0-windows-x64-setup.exe"),
                                    QStringLiteral("circuit-master-0.2.0-windows-arm64-setup.exe"),
                                    QStringLiteral("circuit-master-0.2.0-windows-x64.zip"),
                                    QStringLiteral("circuit-master-0.2.0-macos-arm64.dmg"),
                                    QStringLiteral("circuit-master-0.2.0-macos-x64.dmg")}) {
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

    void updateDownloadSkipsStalledMirror()
    {
        // 有镜像连得上却只吐几 KB/s / 干脆不吐数据。原来只按「请求结束且报错」
        // 才换地址，遇到这种会干等好几分钟。这里用本地 TCP 服务模拟：
        // 第一个镜像发完响应头就不动了，应当被判定为卡住，自动换到第二个。
        const QStringList savedMirrors = UpdateChecker::builtinMirrors();
        auto restore = qScopeGuard([savedMirrors] {
            UpdateChecker::setBuiltinMirrors(savedMirrors);
        });

        const QByteArray payload(300 * 1024, 'x'); // 300 KB
        const QByteArray header =
            "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(payload.size())
            + "\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n";

        // 慢镜像：发完头再吐 1 KB 就不动了
        QTcpServer slow;
        QVERIFY(slow.listen(QHostAddress::LocalHost));
        QList<QTcpSocket*> slowSockets;
        QObject::connect(&slow, &QTcpServer::newConnection, [&] {
            while (QTcpSocket* sock = slow.nextPendingConnection()) {
                slowSockets.append(sock);
                sock->write(header + QByteArray(1024, 'y'));
                sock->flush();
            }
        });

        // 正常镜像：把头和数据都发完
        QTcpServer fast;
        QVERIFY(fast.listen(QHostAddress::LocalHost));
        QObject::connect(&fast, &QTcpServer::newConnection, [&] {
            while (QTcpSocket* sock = fast.nextPendingConnection()) {
                QObject::connect(sock, &QTcpSocket::disconnected, sock,
                                 &QTcpSocket::deleteLater);
                // 不主动断开：Content-Length 满足后 QNAM 自己收尾。
                // 立刻 disconnectFromHost() 会在数据没发完时关连接，客户端报
                // RemoteHostClosedError，用例会误判成下载失败。
                sock->write(header + payload);
                sock->flush();
            }
        });

        const QUrl fastUrl(QStringLiteral("http://127.0.0.1:%1/pkg.bin").arg(fast.serverPort()));
        UpdateChecker::setBuiltinMirrors(
            {QStringLiteral("http://127.0.0.1:%1").arg(slow.serverPort()),
             QStringLiteral("http://127.0.0.1:%1").arg(fast.serverPort())});

        // 把阈值压到毫秒级，测试不必等十秒
        qputenv("CSM_STALL_CHECK_MS", "100");
        qputenv("CSM_STALL_LIMIT_MS", "500");
        qputenv("CSM_SLOW_GRACE_MS", "600000"); // 关掉「太慢」那条，只测「没数据」
        auto restoreEnv = qScopeGuard([] {
            qunsetenv("CSM_STALL_CHECK_MS");
            qunsetenv("CSM_STALL_LIMIT_MS");
            qunsetenv("CSM_SLOW_GRACE_MS");
        });

        ReleaseInfo release;
        release.version = QStringLiteral("0.9.9");
        release.tagName = QStringLiteral("v0.9.9");
        // 三个平台的包名都摆上，让 pickAsset 挑出宿主机对应的那个
        for (const QString& name : {QStringLiteral("circuit-master_0.9.9_amd64.deb"),
                                    QStringLiteral("circuit-master_0.9.9_arm64.deb"),
                                    QStringLiteral("circuit-master-0.9.9-linux-amd64.tar.gz"),
                                    QStringLiteral("circuit-master-0.9.9-linux-arm64.tar.gz"),
                                    QStringLiteral("circuit-master-0.9.9-windows-x64-setup.exe"),
                                    QStringLiteral("circuit-master-0.9.9-windows-arm64-setup.exe"),
                                    QStringLiteral("circuit-master-0.9.9-windows-x64.zip"),
                                    QStringLiteral("circuit-master-0.9.9-macos-arm64.dmg"),
                                    QStringLiteral("circuit-master-0.9.9-macos-x64.dmg")}) {
            ReleaseAsset asset;
            asset.name = name;
            asset.url = fastUrl.toString();
            asset.size = payload.size();
            release.assets.append(asset);
        }

        UpdateDialog dialog(release);
        QVERIFY2(!dialog.asset().name.isEmpty(), "应当挑到当前平台对应的包");
        const QString target = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("shots/stall-target.bin"));
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::remove(target);

        QSignalSpy spy(&dialog, &UpdateDialog::downloadFinished);
        QVERIFY(dialog.downloadTo(target));
        QVERIFY2(spy.wait(15000), "卡住的地址应当被跳过，换到下一个地址后下载完成");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(dialog.savedPath(), target);

        QFile saved(target);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), payload);
        saved.close();
        QFile::remove(target);
        qDeleteAll(slowSockets);
    }

    void tl431ExampleRunsWithOperatingPoint()
    {
        // 用户实际流程：打开 TL431 示例 → 选「工作点」→ 运行 → 关掉报告
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        QAction* example = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().contains(QStringLiteral("TL431")))
                example = action;
        }
        QVERIFY2(example, "示例菜单里应当有 TL431");
        example->trigger();
        settle();

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);
        QVERIFY2(scene->componentItems().size() >= 10, "示例应当已载入");

        auto* plot = plotPanel(window);
        QVERIFY(plot);
        // 示例自带工作点设置，载入后静态波形页应当已经显示「工作点」
        QCOMPARE(plot->options().kind, AnalysisKind::OperatingPoint);

        QAction* run = window.findChild<QAction*>(QStringLiteral("actRun"));
        QAction* monitor = window.findChild<QAction*>(QStringLiteral("actMonitor"));
        QVERIFY(run && monitor);

        run->trigger();
        settle();

        // 运行时不应当自动弹报告对话框
        for (QWidget* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QDialog*>(widget))
                QVERIFY2(!dialog->isVisible(), "工作点不应当自动弹窗");

        // 报告在「波形监控 → 静态波形」里
        monitor->setChecked(true);
        settle();
        QTabWidget* tabs = waveTabs(window);
        QVERIFY(tabs);
        tabs->setCurrentIndex(0);
        settle();
        auto* staticPlot = qobject_cast<WaveformWindow*>(tabs->widget(0));
        QVERIFY(staticPlot);
        QString reportText;
        for (QPlainTextEdit* edit : staticPlot->findChildren<QPlainTextEdit*>()) {
            if (edit->isVisible() && edit->toPlainText().contains(QStringLiteral("节点")))
                reportText = edit->toPlainText();
        }
        QVERIFY2(!reportText.isEmpty(), "静态波形页应当显示工作点报告");
        QVERIFY2(reportText.contains(QStringLiteral("4.9")), qPrintable(reportText.left(200)));

        // 静态页点「重新仿真」也不弹窗，报告就地刷新
        QPushButton* resim = nullptr;
        for (QPushButton* button : staticPlot->findChildren<QPushButton*>())
            if (button->text().contains(QStringLiteral("重新仿真")))
                resim = button;
        QVERIFY(resim);
        resim->click();
        settle();
        for (QWidget* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QDialog*>(widget))
                QVERIFY2(!dialog->isVisible(), "重新仿真不应当弹窗");
        bool stillShown = false;
        for (QPlainTextEdit* edit : staticPlot->findChildren<QPlainTextEdit*>())
            if (edit->isVisible() && edit->toPlainText().contains(QStringLiteral("4.9")))
                stillShown = true;
        QVERIFY2(stillShown, "重新仿真后报告应当仍在静态页里");

        auto* live = livePanel(window);
        QVERIFY(live);
        QVERIFY2(monitor->isEnabled(), "工作点跑完后波形监控应当可用");
        QCOMPARE(run->text(), QStringLiteral("停止仿真"));
        QVERIFY2(live->isLive(), "工作点跑完后应当处于运行状态");

        run->trigger();
        settle();
        QVERIFY(!live->isLive());
    }

    void runningOperatingPointStillEntersRunState()
    {
        // 工作点分析算完会弹报告，但「运行仿真」按钮也应当进入「停止仿真」状态，
        // 否则看起来像「点了就停」
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
        auto* probe = scene->placeComponent(ComponentType::Probe, QPointF(300, 250));
        QVERIFY(probe);
        scene->placeComponent(ComponentType::Ground, QPointF(100, 400));
        scene->addWire({QPointF(100, 270), QPointF(100, 200), QPointF(300, 200),
                        QPointF(300, 250)});
        scene->addWire({QPointF(100, 330), QPointF(100, 400)});
        settle();

        // 切到工作点分析
        auto* plot = plotPanel(window);
        QVERIFY(plot);
        SimulationOptions options = plot->options();
        options.kind = AnalysisKind::OperatingPoint;
        plot->setOptions(options);

        QAction* run = window.findChild<QAction*>(QStringLiteral("actRun"));
        QVERIFY(run);
        run->trigger();
        settle();

        // 关闭可能弹出的报告对话框
        for (QWidget* widget : window.findChildren<QDialog*>())
            if (widget->isVisible())
                widget->close();
        settle();

        auto* live = livePanel(window);
        QVERIFY(live);
        QVERIFY2(live->isLive(), "工作点分析也应当进入运行状态");
        QCOMPARE(run->text(), QStringLiteral("停止仿真"));

        run->trigger();
        settle();
        QVERIFY(!live->isLive());
    }

    void builtinMenusAreChinese()
    {
        // Qt 自带控件（输入框右键菜单）也要是中文，靠启动时装的 qtbase_zh_CN。
        // 找不到 .qm 就直接失败：说明这台机器缺 Qt 的翻译包
        // （Debian/Ubuntu 上是 qt6-translations-l10n），发布包也会缺中文。
        QVERIFY2(csm::qtTranslationsLoaded(),
                 "没加载到 qtbase_zh_CN.qm：请安装 Qt 的翻译包（qt6-translations-l10n）");

        QLineEdit edit;
        edit.setText(QStringLiteral("abc"));
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit) || edit.isVisible());
        settle();

        QScopedPointer<QMenu> menu(edit.createStandardContextMenu());
        QVERIFY(menu);
        QStringList texts;
        for (QAction* action : menu->actions())
            if (!action->isSeparator())
                texts.append(action->text());
        QVERIFY2(!texts.isEmpty(), "右键菜单应当有内容");

        const QString joined = texts.join(QLatin1Char('|'));
        QVERIFY2(joined.contains(QStringLiteral("复制")), qPrintable(joined));
        QVERIFY2(joined.contains(QStringLiteral("粘贴")), qPrintable(joined));
        QVERIFY2(!joined.contains(QStringLiteral("Copy")), qPrintable(joined));
    }

    void multimeterModeFromPanel()
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

        mode->setCurrentIndex(mode->findData(int(MultimeterMode::Current)));
        settle();
        QCOMPARE(item->component().multimeterMode, MultimeterMode::Current);

        // 电流档串进回路后，工作点应当测到电流（波形页会有 XMM1 信号）
        mode->setCurrentIndex(mode->findData(int(MultimeterMode::Resistance)));
        settle();
        QCOMPARE(item->component().multimeterMode, MultimeterMode::Resistance);
    }

    void multimeterReadoutWindowOpensOnClick()
    {
        // 仿真时点击万用表应弹出七段码读数窗口；删掉元件窗口跟着关闭
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);
        auto* item = scene->placeComponent(ComponentType::Multimeter, QPointF(300, 300));
        QVERIFY(item);
        settle();

        // 选中 = 点击：读数窗口应当出现
        item->setSelected(true);
        settle();
        auto* meterWindow = window.findChild<MultimeterWindow*>();
        QVERIFY2(meterWindow, "点击万用表后应当弹出读数窗口");
        QCOMPARE(meterWindow->refdes(), QStringLiteral("XMM1"));
        QVERIFY(meterWindow->isVisible());

        // 窗口里应当有七段码显示和位数选择
        QVERIFY(meterWindow->findChild<SevenSegmentDisplay*>() != nullptr);

        // 删掉元件，窗口自动关闭
        QAction* deleteAction = nullptr;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text() == QStringLiteral("删除"))
                deleteAction = action;
        }
        QVERIFY(deleteAction);
        deleteAction->trigger();
        // 测试用 processEvents 驱动事件循环，DeferredDelete 要显式冲一下才会销毁
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        settle();
        QVERIFY2(window.findChild<MultimeterWindow*>() == nullptr,
                 "删除万用表后读数窗口应当关闭");
    }

    void sevenSegmentDigitsFillTheDisplay()
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

    void sevenSegmentRangeDoesNotFlicker()
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

    void dcMeterShowsMinusOnly()
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

    void netLabelFromPanelAndLibrary()
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

    void netLabelFollowsRotation()
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

    void componentLabelsCanBeDragged()
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

    void meterWindowFollowsModeAndGoesStale()
    {
        // 1) 属性面板改了档位，读数窗口的表头/单位要跟着变
        // 2) 改了电路之后，上一次静态仿真的读数不能继续摆在窗口里
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);
        auto* item = scene->placeComponent(ComponentType::Multimeter, QPointF(300, 300));
        QVERIFY(item);
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
        QCOMPARE(display->displayedUnit(), QStringLiteral("Ω"));

        // 给一个读数，再改电路：读数应当作废成横杠（NaN）
        display->setValue(1000.0);
        QCOMPARE(display->value(), 1000.0);
        c.value = 2200.0;
        item->setComponent(c);
        scene->commitEdit();
        settle();
        QVERIFY2(std::isnan(display->value()), "改电路后旧读数应当作废");
    }

    void sevenSegmentRoundsUpWithoutLosingADigit()
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

    void hitAreaFollowsRotation()
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

    void probeStaysClickableWhenRotated()
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

    void propertyPanelPlaceholderIsCentred()
    {
        // 刚启动时属性面板只有一句提示。以前这句提示被塞进 QStackedWidget 里，
        // 而 stack 的 sizeHint 取所有页里最大的 —— 空态被表单页一起撑到上千像素高，
        // 提示文字被顶到很下面，右边还多出一条滚动条。
        MainWindow window;
        window.resize(1400, 900);
        window.show();
        waitShown(window);

        auto* props = window.findChild<PropertyPanel*>();
        QVERIFY(props);

        QLabel* placeholder = nullptr;
        for (QLabel* label : props->findChildren<QLabel*>()) {
            if (label->text().contains(QStringLiteral("在原理图中选择")))
                placeholder = label;
        }
        QVERIFY2(placeholder, "找不到空态提示");
        QVERIFY2(placeholder->isVisible(), "刚启动时应当显示空态提示");

        // 提示区域应当占满面板，且文字在中线上
        const int centre = placeholder->y() + placeholder->height() / 2;
        const int panelCentre = props->height() / 2;
        QVERIFY2(std::abs(centre - panelCentre) <= 2,
                 qPrintable(QStringLiteral("提示中心 y=%1，面板中心 y=%2")
                                .arg(centre)
                                .arg(panelCentre)));

        // 面板不该因为空态被撑高（表单页那一堆字段不该参与空态的 sizeHint）
        QVERIFY2(props->height() <= 900,
                 qPrintable(QStringLiteral("空态面板高 %1，被撑高了").arg(props->height())));

        // 选中元件后提示隐藏、表单出现
        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);
        auto* item = scene->placeComponent(ComponentType::Resistor, QPointF(300, 300));
        item->setSelected(true);
        settle();
        QVERIFY2(!placeholder->isVisible(), "选中元件后提示应当隐藏");
        QVERIFY2(props->findChild<QStackedWidget*>()->isVisible(), "选中元件后表单应当显示");
    }

    void yAxisAlwaysCoversTheData()
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

    void liveScopeKeepsManualScale()
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

    void autoScaleAlsoPicksTimeBase()
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

    void autoScaleLivesInTheLiveScope()
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

    void transformerHasFourPinsAndPanel()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);
        ComponentItem* t = scene->placeComponent(ComponentType::Transformer, QPointF(300, 300));
        QVERIFY(t);
        QCOMPARE(t->pinScenePositions().size(), 4);
        QCOMPARE(t->component().refdes.left(1), QStringLiteral("T"));
        // 默认 1:2、初级 1 H
        QCOMPARE(t->component().value, 2.0);
        QCOMPARE(t->component().lpri, 1.0);

        scene->clearSelection();
        t->setSelected(true);
        settle();

        // 属性面板里应当有「初级电感 L1」
        bool found = false;
        for (QLabel* label : window.findChildren<QLabel*>())
            if (label->isVisible() && label->text().contains(QStringLiteral("初级电感")))
                found = true;
        QVERIFY2(found, "属性面板应当显示初级电感");
    }

    void transformerWindingKindSwitchable()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);
        ComponentItem* t = scene->placeComponent(ComponentType::Transformer, QPointF(300, 300));
        QVERIFY(t);
        QCOMPARE(t->pinScenePositions().size(), 4);
        QCOMPARE(t->component().transformerKind, TransformerKind::Single);

        // 属性面板里把绕组形式换成「初级 + 辅助绕组 + 次级」→ 引脚变成 6 个
        QComboBox* windings = nullptr;
        for (QComboBox* combo : window.findChildren<QComboBox*>())
            if (combo->count() == 2
                && combo->itemText(1).contains(QStringLiteral("辅助绕组")))
                windings = combo;
        QVERIFY2(windings, "属性面板应当有「绕组形式」下拉框");

        scene->clearSelection();
        t->setSelected(true);
        settle();
        windings->setCurrentIndex(1);
        settle();

        QCOMPARE(t->component().transformerKind, TransformerKind::PrimaryAuxSecondary);
        QCOMPARE(t->pinScenePositions().size(), 6);

        // 匝数比一行写完 N1:N2:N3
        QLineEdit* ratioEdit = nullptr;
        for (QLineEdit* edit : window.findChildren<QLineEdit*>())
            if (edit->isVisible() && edit->text().contains(QLatin1Char(':')))
                ratioEdit = edit;
        QVERIFY2(ratioEdit, "应当有「匝数比 N1:N2:N3」输入框");
        QCOMPARE(ratioEdit->text(), QStringLiteral("1:1:2"));

        ratioEdit->setText(QStringLiteral("1:1:3"));
        emit ratioEdit->editingFinished();
        settle();
        QCOMPARE(t->component().value, 3.0);
        QCOMPARE(t->component().auxRatio, 1.0);

        ratioEdit->setText(QStringLiteral("2:1:4"));
        emit ratioEdit->editingFinished();
        settle();
        QCOMPARE(t->component().value, 2.0);    // 4/2
        QCOMPARE(t->component().auxRatio, 0.5); // 1/2
    }

    void liveScopeKeepsSwitchingAtLargeTimeBase()
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

    void liveScopeWorksWithReferenceGround()
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

    void rotationCanBeSetFromPanel()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);
        ComponentItem* item = scene->placeComponent(ComponentType::Resistor, QPointF(300, 300));
        QVERIFY(item);
        scene->clearSelection();
        item->setSelected(true);
        settle();

        // 属性面板里的「旋转角度」可以填任意角度
        QLineEdit* rotation = nullptr;
        for (QLineEdit* edit : window.findChildren<QLineEdit*>())
            if (edit->text() == QStringLiteral("0") && edit->toolTip().contains(QStringLiteral("角度")))
                rotation = edit;
        QVERIFY2(rotation, "属性面板应当有「旋转角度」输入框");

        const auto setAngle = [&](const QString& text) {
            rotation->setText(text);
            emit rotation->editingFinished();
            settle();
        };

        setAngle(QStringLiteral("90"));
        QCOMPARE(item->component().rotation, 90);
        QCOMPARE(item->pinScenePositions().at(0), QPointF(300, 270));

        setAngle(QStringLiteral("180"));
        QCOMPARE(item->component().rotation, 180);
        QCOMPARE(item->pinScenePositions().at(0), QPointF(330, 300));

        // 自由角度：45° 不再是 90° 的整数倍
        setAngle(QStringLiteral("45"));
        QCOMPARE(item->component().rotation, 45);
        // 引脚吸附到 10 px 网格，斜放也能和导线端点对齐
        const QPointF pin45 = item->pinScenePositions().at(0);
        QCOMPARE(pin45, QPointF(280, 280));

        setAngle(QStringLiteral("0"));
        QCOMPARE(item->component().rotation, 0);
    }

    void splitDrawnWiresKeepJunction()
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

    void crossingCanBecomeJunction()
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

    void pasteKeepsWiresStraight()
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

    void snapWiresRepairsSkewedGeometry()
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

    void pastedWiresSnapToPins()
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

    void groundCanBeRotated()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);

        auto* source = scene->placeComponent(ComponentType::VoltageSource, QPointF(100, 300));
        source->setRotationAngle(90);
        auto* ground = scene->placeComponent(ComponentType::Ground, QPointF(100, 400));
        QVERIFY(ground);
        scene->addWire({QPointF(100, 330), QPointF(100, 400)});
        settle();

        scene->clearSelection();
        ground->setSelected(true);
        settle();

        QAction* rotate = window.findChild<QAction*>(QStringLiteral("actRotate"));
        QVERIFY(rotate);
        QVERIFY2(rotate->isEnabled(), "接地符号应当可以旋转");

        rotate->trigger();
        settle();
        QCOMPARE(ground->component().rotation, 270); // 逆时针 90°

        rotate->trigger();
        settle();
        QCOMPARE(ground->component().rotation, 180);

        // 文字跟着转，包围盒要能放下任意方向的符号与文字
        const QRectF box = ground->boundingRect();
        for (const QPointF& corner : {QPointF(-42, -34), QPointF(42, -34), QPointF(-42, 42),
                                      QPointF(42, 42)})
            QVERIFY2(box.contains(corner), "包围盒应当覆盖旋转后的符号与文字");

        // 转完仍然连着：网表有效、地节点还在
        const Netlist net = scene->toCircuit().buildNetlist();
        QVERIFY2(net.valid, qPrintable(net.error));
        QCOMPARE(net.nodeOf(ground->component().id, 0), 0);
    }

    void updateDialogDownloadsToTempAndInstalls()
    {
        // 还原时用「改动前实际在用的那张表」，别写死 —— 内置镜像会变
        const QStringList savedMirrors = UpdateChecker::builtinMirrors();
        UpdateChecker::setBuiltinMirrors({});
        auto restore = qScopeGuard([savedMirrors] {
            UpdateChecker::setBuiltinMirrors(savedMirrors);
        });
        // 「下载并安装」：下到临时目录，下完调用安装流程，不再弹保存对话框
        const QString payload = QStringLiteral("fake package bytes");
        const QString source = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("shots/fake2.bin"));
        QDir().mkpath(QFileInfo(source).absolutePath());
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(payload.toUtf8());
        }

        ReleaseInfo release;
        release.version = QStringLiteral("0.2.9");
        release.tagName = QStringLiteral("v0.2.9");
        release.pageUrl = QUrl(QStringLiteral("https://example.invalid"));
        const QString localUrl = QUrl::fromLocalFile(source).toString();
        // 同样要覆盖三平台，否则在 macOS 上挑不到包
        for (const QString& name : {QStringLiteral("circuit-master_0.2.9_amd64.deb"),
                                    QStringLiteral("circuit-master-0.2.9-linux-amd64.tar.gz"),
                                    QStringLiteral("circuit-master-0.2.9-windows-x64-setup.exe"),
                                    QStringLiteral("circuit-master-0.2.9-windows-arm64-setup.exe"),
                                    QStringLiteral("circuit-master-0.2.9-windows-x64.zip"),
                                    QStringLiteral("circuit-master-0.2.9-macos-arm64.dmg"),
                                    QStringLiteral("circuit-master-0.2.9-macos-x64.dmg")}) {
            ReleaseAsset item;
            item.name = name;
            item.url = localUrl;
            item.size = payload.toUtf8().size();
            release.assets.append(item);
        }

        UpdateDialog dialog(release);
        QVERIFY(!dialog.asset().name.isEmpty());

        // 临时目录里下载（模拟「下载并安装」的落点）
        const QString temp = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                 .filePath(dialog.asset().name);
        QFile::remove(temp);
        QSignalSpy spy(&dialog, &UpdateDialog::downloadFinished);
        QVERIFY(dialog.downloadTo(temp));
        QVERIFY(spy.wait(10000));
        QVERIFY(QFile::exists(temp));
        QCOMPARE(dialog.savedPath(), temp);
        QVERIFY2(temp.startsWith(QStandardPaths::writableLocation(QStandardPaths::TempLocation)),
                 qPrintable(temp));
        QFile::remove(temp);
    }

    void opensFileFromCommandLinePath()
    {
        // 文件关联靠这个：双击 .csm → CircuitMaster 文件.csm
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        waitShown(window);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("assoc.csm"));

        auto* scene = window.findChild<SchematicScene*>();
        QVERIFY(scene);

        Circuit circuit;
        Component r;
        r.id = 1;
        r.type = ComponentType::Resistor;
        r.pos = QPointF(200, 200);
        r.refdes = QStringLiteral("R1");
        r.value = 1000.0;
        circuit.components.append(r);
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(QJsonDocument(circuit.toJson()).toJson());
        }

        QVERIFY2(window.openPath(path), "存在的 .csm 应当能打开");
        settle();
        QCOMPARE(scene->componentItems().size(), 1);
        QCOMPARE(scene->componentItems().first()->component().refdes, QStringLiteral("R1"));
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
        // 示例中的 R1 初始为水平放置，逆时针旋转一次后应为 270°
        QCOMPARE(resistor->component().rotation, 0);
        rotateAction->trigger();
        QCOMPARE(resistor->component().rotation, 270);
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
        QCOMPARE(category->count(), 7); // 「全部」+ 6 个分类

        // 每个分类都应当有内容，且加起来不超过全部
        QMap<QString, int> counts;
        for (int i = 1; i < category->count(); ++i) {
            category->setCurrentIndex(i);
            settle();
            counts.insert(category->currentText(), library->count());
            QVERIFY2(library->count() > 0,
                     qPrintable(QStringLiteral("分类为空: ") + category->currentText()));
        }

        // 分类按「器件性质」划分，同类元件数量不该悬殊
        QCOMPARE(counts.value(QStringLiteral("基础")), 4);        // R / C / L / 变压器
        QCOMPARE(counts.value(QStringLiteral("半导体")), 7);      // 二极管、LED、三极管、MOS、IGBT
        QCOMPARE(counts.value(QStringLiteral("模拟 IC")), 4);     // 运放 / TL431 / 光耦 / OB2362
        QCOMPARE(counts.value(QStringLiteral("电源")), 2);        // 电压源 / 电流源
        QCOMPARE(counts.value(QStringLiteral("开关与接地")), 3);  // 开关 / 接地 / 网络标签
        QCOMPARE(counts.value(QStringLiteral("测量")), 3);        // TP / IP / 万用表

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
