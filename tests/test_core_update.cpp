#include "core/Circuit.h"
#include "core/Units.h"
#include "core/UpdateChecker.h"
#include "csm_version.h"
#include "sim/Simulator.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <cmath>

using namespace csm;

class CoreUpdateTest : public QObject {
    Q_OBJECT

private slots:
    void updateCheckerReadsManifest();
    void updatePickerChoosesPlatformAsset();
    void downloadUrlsPreferWorkingMirror();
    void manifestRecordsMirrorForDownloads();
    void versionComparison();
    void changelogSectionsAreUnique();
};

void CoreUpdateTest::updateCheckerReadsManifest()
{
    // 本地起一个最小 HTTP 服务，模拟 Release 上的 latest.json
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QByteArray payload;
    connect(&server, &QTcpServer::newConnection, this, [&server, &payload] {
        QTcpSocket* socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket, &payload] {
            socket->readAll();
            const QByteArray body = payload;
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                          "Content-Length: "
                          + QByteArray::number(body.size())
                          + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    });

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/latest.json").arg(server.serverPort()));

    auto manifest = [](const QString& version, const QString& tag, const QString& page) {
        QJsonObject object;
        object.insert(QStringLiteral("version"), version);
        object.insert(QStringLiteral("tag"), tag);
        object.insert(QStringLiteral("name"), QStringLiteral("circuit-master %1").arg(tag));
        object.insert(QStringLiteral("notes"), QStringLiteral("### 新增\n\n- 条目"));
        object.insert(QStringLiteral("published_at"), QStringLiteral("2026-12-01T10:00:00Z"));
        if (!page.isEmpty())
            object.insert(QStringLiteral("page"), page);
        return QJsonDocument(object).toJson(QJsonDocument::Compact);
    };

    // 1) 清单版本更新 → updateAvailable，且不可信的发布页地址被丢弃
    payload = manifest(QStringLiteral("99.0.0"), QStringLiteral("v99.0.0"),
                       QStringLiteral("https://evil.example.com/x"));
    {
        UpdateChecker checker;
        checker.setMirrorPrefixes({});
        checker.setManifestUrl(url);
        QSignalSpy spy(&checker, &UpdateChecker::updateAvailable);
        QSignalSpy failedSpy(&checker, &UpdateChecker::failed);
        QVERIFY(checker.check());
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(failedSpy.count(), 0);

        const auto release = spy.first().at(0).value<ReleaseInfo>();
        QCOMPARE(release.version, QStringLiteral("99.0.0"));
        QCOMPARE(release.tagName, QStringLiteral("v99.0.0"));
        QCOMPARE(release.pageUrl.host(), QStringLiteral("github.com"));
        QVERIFY(release.pageUrl.path().contains(QStringLiteral("/lhdjply/circuit-master/")));
    }

    // 2) 清单版本更旧 → upToDate
    payload = manifest(QStringLiteral("0.0.1"), QStringLiteral("v0.0.1"), QString());
    {
        UpdateChecker checker;
        checker.setMirrorPrefixes({});
        checker.setManifestUrl(url);
        QSignalSpy spy(&checker, &UpdateChecker::upToDate);
        QVERIFY(checker.check());
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral(CSM_VERSION_NUMBER));
    }

    // 3) 正文不是 JSON（转发服务常见的 HTML 错误页）→ failed，不能当成“没有更新”
    payload = QByteArray("<html><body>502 Bad Gateway</body></html>");
    {
        UpdateChecker checker;
        checker.setMirrorPrefixes({});
        checker.setManifestUrl(url);
        QSignalSpy spy(&checker, &UpdateChecker::failed);
        QSignalSpy upSpy(&checker, &UpdateChecker::upToDate);
        QVERIFY(checker.check());
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(upSpy.count(), 0);
        QVERIFY(!spy.first().at(0).toString().isEmpty());
    }
}

void CoreUpdateTest::updatePickerChoosesPlatformAsset()
{
    ReleaseInfo release;
    release.version = QStringLiteral("0.2.0");
    const auto add = [&release](const QString& name) {
        ReleaseAsset asset;
        asset.name = name;
        asset.url = QStringLiteral("https://example.invalid/") + name;
        asset.size = 1024;
        release.assets.append(asset);
    };
    add(QStringLiteral("circuit-master_0.2.0_amd64.deb"));
    add(QStringLiteral("circuit-master_0.2.0_arm64.deb"));
    add(QStringLiteral("circuit-master_0.2.0_loong64.deb"));
    add(QStringLiteral("circuit-master-0.2.0-linux-amd64.tar.gz"));
    add(QStringLiteral("circuit-master-0.2.0-windows-x64.zip"));
    add(QStringLiteral("circuit-master-0.2.0-windows-x64-setup.exe"));
    add(QStringLiteral("circuit-master-0.2.0-windows-arm64-setup.exe"));
    add(QStringLiteral("circuit-master-0.2.0-macos-arm64.dmg"));
    add(QStringLiteral("circuit-master-0.2.0-macos-x64.dmg"));

    // Linux：按架构挑 deb，优先 deb 而不是 tar.gz
    const auto pick = [&release](HostPlatform os, HostArch arch,
                                 InstallKind kind = InstallKind::System) {
        return UpdateChecker::pickAsset(release, os, arch, kind).name;
    };
    QCOMPARE(pick(HostPlatform::Linux, HostArch::Amd64),
             QStringLiteral("circuit-master_0.2.0_amd64.deb"));
    QCOMPARE(pick(HostPlatform::Linux, HostArch::Arm64),
             QStringLiteral("circuit-master_0.2.0_arm64.deb"));
    QCOMPARE(pick(HostPlatform::Linux, HostArch::Loong64),
             QStringLiteral("circuit-master_0.2.0_loong64.deb"));

    // Windows：优先安装包
    QCOMPARE(pick(HostPlatform::Windows, HostArch::Amd64),
             QStringLiteral("circuit-master-0.2.0-windows-x64-setup.exe"));
    QCOMPARE(pick(HostPlatform::Windows, HostArch::Arm64),
             QStringLiteral("circuit-master-0.2.0-windows-arm64-setup.exe"));

    // macOS 架构对不上时也有一条退路（随便给个 dmg，系统会拒绝运行错的架构）
    QCOMPARE(UpdateChecker::pickAsset(release, HostPlatform::MacOS, HostArch::Riscv64,
                                      InstallKind::System).name,
             QStringLiteral("circuit-master-0.2.0-macos-arm64.dmg"));

    // macOS：只有一种形态，按架构挑 dmg
    QCOMPARE(pick(HostPlatform::MacOS, HostArch::Arm64),
             QStringLiteral("circuit-master-0.2.0-macos-arm64.dmg"));
    QCOMPARE(pick(HostPlatform::MacOS, HostArch::Amd64),
             QStringLiteral("circuit-master-0.2.0-macos-x64.dmg"));

    // 某架构没出包时退回 portable
    ReleaseInfo only = release;
    only.assets.clear();
    ReleaseAsset zip;
    zip.name = QStringLiteral("circuit-master-0.2.0-windows-x64.zip");
    zip.url = QStringLiteral("https://example.invalid/x.zip");
    only.assets.append(zip);
    QCOMPARE(UpdateChecker::pickAsset(only, HostPlatform::Windows, HostArch::Amd64,
                                      InstallKind::System).name,
             QStringLiteral("circuit-master-0.2.0-windows-x64.zip"));

    // 包形态枚举：Linux 出 deb + 便携 tar.gz，Windows 出 setup + 便携 zip
    const auto formats = [](HostPlatform os, InstallKind kind) {
        return UpdateChecker::formatsFor(os, kind);
    };
    QCOMPARE(formats(HostPlatform::Linux, InstallKind::System).size(), 2);
    QCOMPARE(formats(HostPlatform::Windows, InstallKind::System).size(), 2);
    // macOS 只有带 .app 的 dmg 一种形态
    QCOMPARE(formats(HostPlatform::MacOS, InstallKind::System).size(), 1);
    QCOMPARE(formats(HostPlatform::MacOS, InstallKind::Portable).size(), 1);

    // 便携版运行时要优先给便携包，和安装方式保持一致
    QCOMPARE(formats(HostPlatform::Linux, InstallKind::System).first(), PackageFormat::Deb);
    QCOMPARE(formats(HostPlatform::Linux, InstallKind::Portable).first(),
             PackageFormat::PortableTarGz);
    QCOMPARE(formats(HostPlatform::Windows, InstallKind::System).first(),
             PackageFormat::SetupExe);
    QCOMPARE(formats(HostPlatform::Windows, InstallKind::Portable).first(),
             PackageFormat::PortableZip);

    // 挑包跟着安装方式走
    QCOMPARE(pick(HostPlatform::Linux, HostArch::Amd64, InstallKind::Portable),
             QStringLiteral("circuit-master-0.2.0-linux-amd64.tar.gz"));
    QCOMPARE(pick(HostPlatform::Linux, HostArch::Amd64, InstallKind::System),
             QStringLiteral("circuit-master_0.2.0_amd64.deb"));

    QCOMPARE(UpdateChecker::suffixFor(HostPlatform::Linux, HostArch::Amd64, PackageFormat::Deb),
             QStringLiteral("_amd64.deb"));
    QCOMPARE(UpdateChecker::suffixFor(HostPlatform::Linux, HostArch::Riscv64,
                                      PackageFormat::PortableTarGz),
             QStringLiteral("-linux-riscv64.tar.gz"));
    QCOMPARE(UpdateChecker::suffixFor(HostPlatform::Windows, HostArch::Arm64,
                                      PackageFormat::SetupExe),
             QStringLiteral("-windows-arm64-setup.exe"));
    QCOMPARE(UpdateChecker::suffixFor(HostPlatform::Windows, HostArch::Amd64,
                                      PackageFormat::PortableZip),
             QStringLiteral("-windows-x64.zip"));
    QCOMPARE(UpdateChecker::suffixFor(HostPlatform::MacOS, HostArch::Arm64,
                                      PackageFormat::MacDmg),
             QStringLiteral("-macos-arm64.dmg"));
    QCOMPARE(UpdateChecker::suffixFor(HostPlatform::MacOS, HostArch::Amd64,
                                      PackageFormat::MacDmg),
             QStringLiteral("-macos-x64.dmg"));
    // 平台与形态不匹配时没有后缀
    QVERIFY(UpdateChecker::suffixFor(HostPlatform::Linux, HostArch::Amd64,
                                     PackageFormat::SetupExe).isEmpty());
    QVERIFY(UpdateChecker::suffixFor(HostPlatform::Windows, HostArch::Amd64,
                                     PackageFormat::MacDmg).isEmpty());

    // 同一平台两种形态都要能列出来
    const auto linuxPackages = UpdateChecker::hostPackagesFor(
        release, HostPlatform::Linux, HostArch::Amd64, InstallKind::System);
    QCOMPARE(linuxPackages.size(), 2);
    QCOMPARE(linuxPackages.at(0).format, PackageFormat::Deb);
    QCOMPARE(linuxPackages.at(1).format, PackageFormat::PortableTarGz);
    const auto winPackages = UpdateChecker::hostPackagesFor(
        release, HostPlatform::Windows, HostArch::Amd64, InstallKind::System);
    QCOMPARE(winPackages.size(), 2);
    QCOMPARE(winPackages.at(0).format, PackageFormat::SetupExe);
    QCOMPARE(winPackages.at(1).format, PackageFormat::PortableZip);

    // 便携版只看得到便携包在最前
    const auto portable = UpdateChecker::hostPackagesFor(
        release, HostPlatform::Linux, HostArch::Amd64, InstallKind::Portable);
    QCOMPARE(portable.at(0).format, PackageFormat::PortableTarGz);

    // 环境变量可覆盖检测结果
    qputenv("CSM_INSTALL_KIND", "portable");
    QCOMPARE(UpdateChecker::detectInstallKind(), InstallKind::Portable);
    qputenv("CSM_INSTALL_KIND", "system");
    QCOMPARE(UpdateChecker::detectInstallKind(), InstallKind::System);
    qunsetenv("CSM_INSTALL_KIND");

    // 不支持的平台：返回空，界面回退到发布页
    QVERIFY(UpdateChecker::pickAsset(release, HostPlatform::Other, HostArch::Amd64,
                                     InstallKind::System).name.isEmpty());
    // 当前构建目标一定能挑到自己平台的包：三平台都在 fixture 里
    QVERIFY2(!UpdateChecker::pickAssetForHost(release).name.isEmpty(),
             qPrintable(QStringLiteral("平台 %1 没挑到包")
                            .arg(int(csm::kHostPlatform))));
}

void CoreUpdateTest::downloadUrlsPreferWorkingMirror()
{
    const QString asset = QStringLiteral(
        "https://github.com/lhdjply/circuit-master/releases/download/v0.2.0/a.deb");

    // 取清单时用的是 ghfast.top：下载也应当先走它
    const QVector<QUrl> preferred =
        UpdateChecker::downloadUrls(asset, QStringLiteral("https://ghfast.top"));
    QVERIFY(preferred.size() >= 2);
    QCOMPARE(preferred.first().toString(),
             QStringLiteral("https://ghfast.top/") + asset);
    // 直链放在最后
    QCOMPARE(preferred.last().toString(), asset);
    // 同样的镜像不会重复出现
    int ghfast = 0;
    for (const QUrl& url : preferred) {
        if (url.toString().startsWith(QStringLiteral("https://ghfast.top/")))
            ++ghfast;
    }
    QCOMPARE(ghfast, 1);

    // 没记录通路时：按内置镜像的顺序试，第一个是 v4.gh-proxy.org，直链在最后
    const QVector<QUrl> plain = UpdateChecker::downloadUrls(asset, QString());
    QCOMPARE(plain.size(), UpdateChecker::builtinMirrors().size() + 1);
    QCOMPARE(plain.first().toString(),
             QStringLiteral("https://v4.gh-proxy.org/") + asset);
    QCOMPARE(plain.last().toString(), asset);

    // 内置镜像表本身：按实测速度排序，v4 打头
    const QStringList mirrors = UpdateChecker::builtinMirrors();
    QCOMPARE(mirrors.first(), QStringLiteral("https://v4.gh-proxy.org"));
    // 整张表按「下载安装包」的实测速度排好了
    const QStringList expected{
        QStringLiteral("https://v4.gh-proxy.org"),
        QStringLiteral("https://ghfast.top"),
        QStringLiteral("https://gh-proxy.org"),
        QStringLiteral("https://cdn.gh-proxy.org")};
    QCOMPARE(mirrors, expected);
    // 实测常年超时的两个已经去掉了
    QVERIFY(!mirrors.contains(QStringLiteral("https://ghproxy.net")));
    QVERIFY(!mirrors.contains(QStringLiteral("https://axisnow.gh-proxy.org")));
    // 不能有重复，否则会对着同一个地址重试
    QCOMPARE(QSet<QString>(mirrors.begin(), mirrors.end()).size(), mirrors.size());

    // 空资产地址返回空列表
    QVERIFY(UpdateChecker::downloadUrls(QString(), QString()).isEmpty());
}

void CoreUpdateTest::manifestRecordsMirrorForDownloads()
{
    // 本地起一个「镜像」，同时提供 latest.json 和安装包
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QByteArray assetPayload = "package-bytes";
    connect(&server, &QTcpServer::newConnection, this, [&server, &assetPayload] {
        QTcpSocket* socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket, &assetPayload] {
            const QByteArray request = socket->readAll();
            const bool isAsset = request.contains("/pkg.bin");
            const QByteArray body = isAsset ? assetPayload : QByteArray();
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                          "Content-Length: "
                          + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n"
                          + body);
            socket->disconnectFromHost();
        });
    });

    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    QJsonObject object;
    object.insert(QStringLiteral("version"), QStringLiteral("99.0.0"));
    object.insert(QStringLiteral("tag"), QStringLiteral("v99.0.0"));
    QJsonObject asset;
    asset.insert(QStringLiteral("name"), QStringLiteral("circuit-master_99.0.0_amd64.deb"));
    asset.insert(QStringLiteral("url"), base + QStringLiteral("/pkg.bin"));
    asset.insert(QStringLiteral("size"), qint64(assetPayload.size()));
    object.insert(QStringLiteral("assets"), QJsonArray{asset});
    const QByteArray manifest = QJsonDocument(object).toJson(QJsonDocument::Compact);

    QTcpServer manifestServer;
    QVERIFY(manifestServer.listen(QHostAddress::LocalHost));
    connect(&manifestServer, &QTcpServer::newConnection, this, [&manifestServer, &manifest] {
        QTcpSocket* socket = manifestServer.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket, &manifest] {
            socket->readAll();
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                          "Content-Length: "
                          + QByteArray::number(manifest.size())
                          + "\r\nConnection: close\r\n\r\n" + manifest);
            socket->disconnectFromHost();
        });
    });

    const QString mirror = QStringLiteral("http://127.0.0.1:%1").arg(manifestServer.serverPort());

    UpdateChecker checker;
    checker.setMirrorPrefixes({mirror});
    checker.setManifestUrl(QUrl(base + QStringLiteral("/latest.json")));
    QSignalSpy spy(&checker, &UpdateChecker::updateAvailable);
    QVERIFY(checker.check());
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.count(), 1);

    const auto release = spy.first().at(0).value<ReleaseInfo>();
    QCOMPARE(release.assets.size(), 1);
    QVERIFY2(!release.mirror.isEmpty(), "应当记录取到清单所用的镜像");
    QVERIFY(release.mirror.startsWith(mirror));
}

void CoreUpdateTest::versionComparison()
{
    QVERIFY(UpdateChecker::compareVersions(QStringLiteral("0.2.0"), QStringLiteral("0.1.0")) > 0);
    QVERIFY(UpdateChecker::compareVersions(QStringLiteral("0.1.0"), QStringLiteral("0.2.0")) < 0);
    QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("0.1.0"), QStringLiteral("0.1.0")), 0);

    // v 前缀、预发布后缀、缺失的段都不影响比较
    QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("v0.1.0"), QStringLiteral("0.1.0")), 0);
    QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("0.2.0-rc1"), QStringLiteral("0.2")), 0);
    QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.2"), QStringLiteral("1.2.0")), 0);

    // 按数字段比较，而不是按字符串：0.10.0 比 0.9.9 新
    QVERIFY(UpdateChecker::compareVersions(QStringLiteral("0.10.0"), QStringLiteral("0.9.9")) > 0);
    QVERIFY(UpdateChecker::compareVersions(QStringLiteral("1.0.0"), QStringLiteral("0.99.99")) > 0);

    // 清单里的 version 一般不带 v，比较时当前版本来自 CSM_VERSION_NUMBER
    QVERIFY(UpdateChecker::compareVersions(QStringLiteral("99.0.0"),
                                           QStringLiteral(CSM_VERSION_NUMBER)) > 0);
}

void CoreUpdateTest::changelogSectionsAreUnique()
{
    // 「未发布」只能有一个：多次插条目时最容易写成两个，发布时会被漏掉
    const QDir repo(QStringLiteral(CSM_EXAMPLE_DIR "/../.."));
    QFile file(repo.filePath(QStringLiteral("CHANGELOG.md")));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "找不到 CHANGELOG.md");
    const QString text = QString::fromUtf8(file.readAll());

    QStringList headings;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("## ")))
            headings.append(line.trimmed());
    }
    QVERIFY(!headings.isEmpty());

    QSet<QString> seen;
    for (const QString& heading : headings) {
        QVERIFY2(!seen.contains(heading),
                 qPrintable(QStringLiteral("重复的小节标题：%1").arg(heading)));
        seen.insert(heading);
    }
    // 刚发版时没有「未发布」小节是正常的，但不能出现两个
    QVERIFY2(headings.count(QStringLiteral("## [未发布]")) <= 1,
             "「未发布」小节最多一个");
}

QTEST_GUILESS_MAIN(CoreUpdateTest)

#include "test_core_update.moc"
