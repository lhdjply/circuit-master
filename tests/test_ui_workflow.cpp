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

using namespace csm;

class UiWorkflowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void updateDialogDownloadsPackage();
    void updateDownloadSkipsStalledMirror();
    void updateDialogDownloadsToTempAndInstalls();
    void updateDialogRenders();
    void versionComesFromVersionFile();
    void builtinMenusAreChinese();
    void toolbarIconsAvailable();
    void opensFileFromCommandLinePath();
    void tl431ExampleRunsWithOperatingPoint();
    void runningOperatingPointStillEntersRunState();
    void runStartsAndStopEndsSimulation();
    void runWithoutProbesStillRuns();
    void buildAndSimulate();
    void ledLightsUpAfterSimulation();
    void switchToggleUpdatesLedWhileRunning();
    void reversedPolarizedCapacitorExplodes();
    void captureMainWindow();

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

void UiWorkflowTest::initTestCase()
{
    // 和 main.cpp 一样装上 Qt 自带的中文翻译（输入框右键菜单等）
    csm::installQtTranslations(qApp);
}

void UiWorkflowTest::updateDialogDownloadsPackage()
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

void UiWorkflowTest::updateDownloadSkipsStalledMirror()
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

void UiWorkflowTest::updateDialogDownloadsToTempAndInstalls()
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

void UiWorkflowTest::updateDialogRenders()
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

    QVERIFY(saveShot(&dialog, QStringLiteral("update_dialog.png")));

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

void UiWorkflowTest::versionComesFromVersionFile()
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

void UiWorkflowTest::builtinMenusAreChinese()
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

void UiWorkflowTest::toolbarIconsAvailable()
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

void UiWorkflowTest::opensFileFromCommandLinePath()
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

void UiWorkflowTest::tl431ExampleRunsWithOperatingPoint()
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

void UiWorkflowTest::runningOperatingPointStillEntersRunState()
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

void UiWorkflowTest::runStartsAndStopEndsSimulation()
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
    QVERIFY(saveShot(monitorWindow, QStringLiteral("monitor_running.png")));

    // 只有「停止仿真」才停
    run->trigger();
    settle();
    QVERIFY2(!live->isLive(), "点击停止后应当结束");
    QCOMPARE(run->text(), QStringLiteral("运行仿真"));
    QVERIFY2(!monitor->isEnabled(), "停止后波形监控应不可点击");
}

void UiWorkflowTest::runWithoutProbesStillRuns()
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

    // 不应该弹模态提示框。
    // 提示框现在是自绘的 QDialog（见 ui/Dialogs.cpp），不再用 QMessageBox，
    // 所以这里按「有没有可见的模态对话框」判断，而不是只找 QMessageBox。
    for (QDialog* box : window.findChildren<QDialog*>())
        QVERIFY2(!box->isVisible(), "不应弹出提示框打断");

    // 面板里要有说明
    auto* list = live->findChild<QListWidget*>();
    QVERIFY(list);
    QCOMPARE(list->count(), 0);

    run->trigger();
    settle();
    QVERIFY(!live->isLive());
}

void UiWorkflowTest::buildAndSimulate()
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

void UiWorkflowTest::ledLightsUpAfterSimulation()
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
    QVERIFY(saveShot(&window, QStringLiteral("led_on.png")));

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

void UiWorkflowTest::switchToggleUpdatesLedWhileRunning()
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

void UiWorkflowTest::reversedPolarizedCapacitorExplodes()
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
    QVERIFY(saveShot(&window, QStringLiteral("cap_explosion.png")));

    // 停止仿真后特效清除
    QAction* run = window.findChild<QAction*>(QStringLiteral("actRun"));
    QVERIFY(run);
    run->trigger();
    settle();
    QVERIFY2(!cap->isBlown(), "停止仿真后应当清除爆炸特效");
}

void UiWorkflowTest::captureMainWindow()
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

    QVERIFY(saveShot(&window, QStringLiteral("main.png")));

    // 选中 R1，检查属性面板
    for (ComponentItem* item : scene->componentItems()) {
        if (item->component().refdes == QLatin1String("R1")) {
            item->setSelected(true);
            break;
        }
    }
    settle();
    QVERIFY(saveShot(&window, QStringLiteral("main_selected.png")));

    window.runSimulation();
    openStaticWave(window);
    auto* wave = waitWaveWindow(window);
    QVERIFY(saveShot(wave, QStringLiteral("waveform.png")));
    QVERIFY(QFileInfo(QCoreApplication::applicationDirPath()
                      + QStringLiteral("/shots/waveform.png")).size() > 5000);
}

// 测试入口放在按文件名排序的最后一个文件里（原来那个只做入口的
QTEST_MAIN(UiWorkflowTest)

#include "test_ui_workflow.moc"
