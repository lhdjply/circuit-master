#include "core/UpdateChecker.h"

#include "csm_version.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

namespace csm {
namespace {

// 固定仓库。清单是 Release 的一个资产：CDN 直出、不占配额，而且只有 Release
// 建好之后才存在，不会撞上「tag 已推、CI 还在编译」的占位条目。
constexpr char kManifestUrl[] =
    "https://github.com/lhdjply/circuit-master/releases/latest/download/latest.json";

constexpr char kRepoPath[] = "/lhdjply/circuit-master/";

// 单次尝试的硬上限。正常响应 300~500ms，6 秒足以覆盖一次重传造成的 2~3 秒
// 停顿；再长也只是让用户多等 —— 已经有多条并行连接，不必再给单条更多耐心。
constexpr int kTimeoutMs = 6000;

// 对冲延迟：首条请求超过这个时间还没有任何结果，就并行再发一条。
// 取值介于「正常完成」与「明显卡住」之间 —— 正常情况不会多花一个请求。
constexpr int kHedgeDelayMs = 700;

// 一次检查的总预算。三个镜像逐个失败时最多跑到这里，避免「超时 × 尝试次数」
// 叠加成用户无法接受的长尾。
constexpr int kTotalBudgetMs = 12000;

// 剩余预算低于此值就不再发起新的尝试（连 TCP 都不够握完）
constexpr int kMinBudgetMs = 2000;

// 只有一条地址时（镜像列表被清空）沿用原来的规矩：原始一条，卡住时再补两条对冲
constexpr int kSingleUrlAttempts = 3;

/// "v0.2.0-rc1" / "0.2.0+build" → "0.2.0"：只留数字与点
QString normalizeVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        version.remove(0, 1);

    const int cut = version.indexOf(QRegularExpression(QStringLiteral("[^0-9.]")));
    if (cut >= 0)
        version = version.left(cut);
    while (version.endsWith(QLatin1Char('.')))
        version.chop(1);
    return version;
}

QVector<int> versionParts(const QString& version)
{
    QVector<int> parts;
    const QStringList fields = version.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    parts.reserve(fields.size());
    for (const QString& field : fields) {
        bool ok = false;
        const int value = field.toInt(&ok);
        parts.append(ok ? value : 0);
    }
    return parts;
}

/// Debian 的架构名（发布产物的文件名里就是这个）
QString debArch(HostArch arch)
{
    switch (arch) {
    case HostArch::Amd64:
        return QStringLiteral("amd64");
    case HostArch::Arm64:
        return QStringLiteral("arm64");
    case HostArch::Loong64:
        return QStringLiteral("loong64");
    case HostArch::Riscv64:
        return QStringLiteral("riscv64");
    case HostArch::Other:
        break;
    }
    return QString();
}

/// Windows 产物里 x86_64 写作 x64
QString winArch(HostArch arch)
{
    switch (arch) {
    case HostArch::Amd64:
        return QStringLiteral("x64");
    case HostArch::Arm64:
        return QStringLiteral("arm64");
    case HostArch::Loong64:
        return QStringLiteral("loong64");
    case HostArch::Riscv64:
        return QStringLiteral("riscv64");
    case HostArch::Other:
        break;
    }
    return QString();
}

} // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_manifestUrl(QUrl(QString::fromLatin1(kManifestUrl)))
    , m_mirrors(builtinMirrors())
{
}

UpdateChecker::~UpdateChecker()
{
    // 析构时还在飞：先掐断，避免 finished 落到已销毁的对象上
    m_running = false;
    cancelInflight();
}

namespace {
QStringList g_mirrorOverride;
bool g_hasMirrorOverride = false;
}

void UpdateChecker::setBuiltinMirrors(const QStringList& mirrors)
{
    g_mirrorOverride = mirrors;
    g_hasMirrorOverride = true;
}

QStringList UpdateChecker::builtinMirrors()
{
    if (g_hasMirrorOverride)
        return g_mirrorOverride;
    // 公共的 GitHub 转发服务。它们把 `<前缀>/https://github.com/...` 原样转发到
    // GitHub，因此清单这种「github.com 上的文件」能直接穿过去。
    //
    // gh-proxy.org 有多个边缘节点（v4 / cdn / axisnow 等），同一时刻的可用性并不
    // 一致，所以全列上、按实测最快的 v4 打头；ghfast.top 与 ghproxy.net 留作退路。
    // 按「下载安装包」的实测速度排，而不是清单的速度 —— 用户疼的是下载。
    // 1.8 MB 的 deb 实测（同一网络、同一时段）：
    //   v4.gh-proxy.org   1.1 s (1.7 MB/s)
    //   ghfast.top        3.7 s (500 KB/s)
    //   gh-proxy.org      5.4 s (340 KB/s)
    //   cdn.gh-proxy.org  60 s 还没下完 (28 KB/s)   ← 取清单最快，下大文件却是坑
    //   axisnow           超时
    //   github 直连       5 KB/s
    // cdn 取清单快是为了「查版本」用的，排第 4 只当退路；axisnow 一直不通，删掉。
    return {QStringLiteral("https://v4.gh-proxy.org"), QStringLiteral("https://ghfast.top"),
            QStringLiteral("https://gh-proxy.org"), QStringLiteral("https://cdn.gh-proxy.org")};
}

void UpdateChecker::setMirrorPrefixes(const QStringList& prefixes)
{
    m_mirrors = prefixes;
}

void UpdateChecker::setManifestUrl(const QUrl& url)
{
    m_manifestUrl = url;
}

int UpdateChecker::compareVersions(const QString& a, const QString& b)
{
    const QVector<int> left = versionParts(normalizeVersion(a));
    const QVector<int> right = versionParts(normalizeVersion(b));

    const int count = qMax(left.size(), right.size());
    for (int i = 0; i < count; ++i) {
        const int l = i < left.size() ? left.at(i) : 0;
        const int r = i < right.size() ? right.at(i) : 0;
        if (l != r)
            return l > r ? 1 : -1;
    }
    return 0;
}

QVector<PackageFormat> UpdateChecker::formatsFor(HostPlatform platform, InstallKind kind)
{
    // 优先给「和当前安装方式一致」的形态，另一种作为退路
    switch (platform) {
    case HostPlatform::Linux:
        return kind == InstallKind::Portable
            ? QVector<PackageFormat>{PackageFormat::PortableTarGz, PackageFormat::Deb}
            : QVector<PackageFormat>{PackageFormat::Deb, PackageFormat::PortableTarGz};
    case HostPlatform::Windows:
        return kind == InstallKind::Portable
            ? QVector<PackageFormat>{PackageFormat::PortableZip, PackageFormat::SetupExe}
            : QVector<PackageFormat>{PackageFormat::SetupExe, PackageFormat::PortableZip};
    case HostPlatform::MacOS:
        // macOS 只有一种形态：带 .app 的 dmg。挂载后把 .app 拖进「应用程序」，
        // 所以安装方式（系统/便携）在这里没有区别。
        return QVector<PackageFormat>{PackageFormat::MacDmg};
    default:
        return {};
    }
}

InstallKind UpdateChecker::detectInstallKind()
{
    const QString forced = qEnvironmentVariable("CSM_INSTALL_KIND").trimmed().toLower();
    if (forced == QLatin1String("portable"))
        return InstallKind::Portable;
    if (forced == QLatin1String("system"))
        return InstallKind::System;

#if defined(Q_OS_WIN)
    // Inno Setup 会在安装目录留一个 unins000.exe：有它就是安装版，没有就是解压版
    const QDir dir(QCoreApplication::applicationDirPath());
    const QStringList uninstallers = dir.entryList({QStringLiteral("unins*.exe")}, QDir::Files);
    return uninstallers.isEmpty() ? InstallKind::Portable : InstallKind::System;
#else
    // deb 装在 /usr 下；解压 tar.gz 的人通常放在家目录或 /opt 之外的地方
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& prefix : {QStringLiteral("/usr/"), QStringLiteral("/snap/"),
                                  QStringLiteral("/app/")}) {
        if (appDir.startsWith(prefix))
            return InstallKind::System;
    }
    return InstallKind::Portable;
#endif
}

QString UpdateChecker::suffixFor(HostPlatform platform, HostArch arch, PackageFormat format)
{
    switch (format) {
    case PackageFormat::Deb:
        if (platform != HostPlatform::Linux || debArch(arch).isEmpty())
            return QString();
        return QStringLiteral("_%1.deb").arg(debArch(arch));
    case PackageFormat::PortableTarGz:
        if (platform != HostPlatform::Linux || debArch(arch).isEmpty())
            return QString();
        return QStringLiteral("-linux-%1.tar.gz").arg(debArch(arch));
    case PackageFormat::SetupExe:
        if (platform != HostPlatform::Windows || winArch(arch).isEmpty())
            return QString();
        return QStringLiteral("-windows-%1-setup.exe").arg(winArch(arch));
    case PackageFormat::PortableZip:
        if (platform != HostPlatform::Windows || winArch(arch).isEmpty())
            return QString();
        return QStringLiteral("-windows-%1.zip").arg(winArch(arch));
    case PackageFormat::MacDmg: {
        if (platform != HostPlatform::MacOS)
            return QString();
        // 发布脚本里 macOS 的架构写法与 Windows 一致：arm64 / x64
        QString name;
        switch (arch) {
        case HostArch::Arm64: name = QStringLiteral("arm64"); break;
        case HostArch::Amd64: name = QStringLiteral("x64"); break;
        default: return QString();
        }
        return QStringLiteral("-macos-%1.dmg").arg(name);
    }
    }
    return QString();
}

QVector<UpdateChecker::HostPackage> UpdateChecker::hostPackagesFor(const ReleaseInfo& release,
                                                                 HostPlatform platform, HostArch arch,
                                                                 InstallKind kind)
{
    QVector<HostPackage> result;
    for (PackageFormat format : formatsFor(platform, kind)) {
        const QString suffix = suffixFor(platform, arch, format);
        if (suffix.isEmpty())
            continue;
        for (const ReleaseAsset& asset : release.assets) {
            if (asset.name.endsWith(suffix, Qt::CaseInsensitive)) {
                result.append({format, asset});
                break;
            }
        }
    }
    return result;
}

ReleaseAsset UpdateChecker::pickAsset(const ReleaseInfo& release, HostPlatform platform,
                                      HostArch arch, InstallKind kind)
{
    const QVector<HostPackage> packages = hostPackagesFor(release, platform, arch, kind);
    if (!packages.isEmpty())
        return packages.first().asset;

    // 架构对不上时的退路：同平台随便给一个（apt / 安装程序自己会拒绝装错架构）。
    // macOS 没有安装程序，退路是任意一个 dmg —— 解压出来架构不对，
    // 系统会直接拒绝运行，同样不会装错。
    const char* loose = platform == HostPlatform::Linux ? ".deb"
        : platform == HostPlatform::Windows             ? "-setup.exe"
        : platform == HostPlatform::MacOS               ? ".dmg"
                                                        : nullptr;
    if (loose) {
        for (const ReleaseAsset& asset : release.assets) {
            if (asset.name.endsWith(QLatin1String(loose), Qt::CaseInsensitive))
                return asset;
        }
    }
    return ReleaseAsset();
}

QVector<QUrl> UpdateChecker::downloadUrls(const QString& assetUrl, const QString& preferredMirror)
{
    QVector<QUrl> urls;
    if (assetUrl.isEmpty())
        return urls;

    const auto withPrefix = [&assetUrl](QString prefix) {
        while (prefix.endsWith(QLatin1Char('/')))
            prefix.chop(1);
        return QUrl(prefix + QLatin1Char('/') + assetUrl);
    };

    // 先把「取清单时成功过的那条路」放最前
    const QString preferred = preferredMirror.trimmed();
    if (!preferred.isEmpty())
        urls.append(withPrefix(preferred));

    for (const QString& prefix : builtinMirrors()) {
        if (prefix.trimmed() == preferred)
            continue;
        urls.append(withPrefix(prefix));
    }

    // 直链放最后：能连上 GitHub 时它最快，连不上时前面几家已经试过了
    urls.append(QUrl(assetUrl));
    return urls;
}

ReleaseAsset UpdateChecker::pickAssetForHost(const ReleaseInfo& release)
{
    return pickAsset(release, kHostPlatform, kHostArch, detectInstallKind());
}

QVector<QUrl> UpdateChecker::candidates() const
{
    QVector<QUrl> urls;
    if (m_manifestUrl.isEmpty())
        return urls;

    // 转发服务的用法是「前缀 + 完整的 github.com 地址」
    for (const QString& prefix : m_mirrors) {
        QString trimmed = prefix.trimmed();
        if (trimmed.isEmpty())
            continue;
        while (trimmed.endsWith(QLatin1Char('/')))
            trimmed.chop(1);
        urls.append(QUrl(trimmed + QLatin1Char('/') + m_manifestUrl.toString()));
    }
    // 没有镜像时直接访问（测试与自建镜像场景）
    if (urls.isEmpty())
        urls.append(m_manifestUrl);
    return urls;
}

int UpdateChecker::attemptLimit() const
{
    return qMax(int(m_urls.size()), kSingleUrlAttempts);
}

void UpdateChecker::noteError(const QString& reason)
{
    if (reason.isEmpty() || m_errors.contains(reason))
        return;
    m_errors.append(reason);
}

bool UpdateChecker::check()
{
    if (m_running)
        return false;

    if (!m_hedgeTimer) {
        m_hedgeTimer = new QTimer(this);
        m_hedgeTimer->setSingleShot(true);
        // 每过一拍仍有请求没回来，就再并行补一条（上限见 attemptLimit）。
        // 实测链路是「单条连接偶尔卡 2~5 秒」，多条独立连接同时卡的概率低得多，
        // 因此补齐比死等一条更划算。正常情况首条在这一拍内就完成，不会补发。
        QObject::connect(m_hedgeTimer, &QTimer::timeout, this, [this]() {
            if (!m_running)
                return;
            if (m_attempts >= attemptLimit())
                return;
            if (remainingBudgetMs() < kMinBudgetMs)
                return;

            sendAttempt();
            if (m_attempts < attemptLimit())
                m_hedgeTimer->start(kHedgeDelayMs);
        });
    }

    if (!m_manager)
        m_manager = new QNetworkAccessManager(this);

    m_errors.clear();
    m_attempts = 0;
    m_running = true;
    m_clock.start();
    cancelInflight(); // 防御性清理：正常情况下这里不该有残留

    m_urls = candidates();
    if (m_urls.isEmpty()) {
        // 连清单地址都没有：仍然异步回报，保持「check() 返回后结果通过信号到达」
        // 的一致行为
        m_running = false;
        const QString reason = failureMessage();
        QTimer::singleShot(0, this, [this, reason]() { emit failed(reason); });
        return true;
    }

    sendAttempt();
    if (m_attempts < attemptLimit())
        m_hedgeTimer->start(kHedgeDelayMs);
    return true;
}

void UpdateChecker::sendAttempt()
{
    if (!m_manager || m_urls.isEmpty() || m_attempts >= attemptLimit())
        return;

    const QUrl url = m_urls.at(m_attempts % m_urls.size());
    m_activeUrl = url;
    ++m_attempts;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("circuit-master/%1").arg(QLatin1String(CSM_VERSION_NUMBER)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_manager->get(request);
    m_inflight.append(reply);

    // 预算见底时用剩余时间当超时，避免最后一次尝试把总耗时拖出预算
    const int timeout = qBound(1000, remainingBudgetMs(), kTimeoutMs);
    QTimer::singleShot(timeout, this, [this, reply]() {
        if (m_inflight.contains(reply))
            reply->abort();
    });

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        // 只处理仍在名单里的请求：被 cancelInflight 放弃的那条不再进入状态机
        const bool alive = m_inflight.removeOne(reply);
        if (alive)
            finish(reply);
        reply->deleteLater();
    });
}

void UpdateChecker::cancelInflight()
{
    const QList<QNetworkReply*> replies = m_inflight;
    m_inflight.clear();
    if (m_hedgeTimer)
        m_hedgeTimer->stop();

    for (QNetworkReply* reply : replies) {
        if (!reply)
            continue;
        reply->disconnect(this); // 断开 finished，避免继续驱动状态机
        reply->abort();
        reply->deleteLater();
    }
}

void UpdateChecker::finish(QNetworkReply* reply)
{
    if (!m_running)
        return;

    // 顺序很重要：Qt 把 4xx/5xx 也当成 error（ContentNotFound / InternalServerError），
    // 先看 HTTP 状态码，否则会拿 errorString 当成失败原因。
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    const bool timedOut = reply->error() == QNetworkReply::OperationCanceledError
        || reply->error() == QNetworkReply::TimeoutError;
    const bool httpOk = status == 200;
    const bool netOk = reply->error() == QNetworkReply::NoError;

    if (httpOk && netOk) {
        // 只有真正要解析时才读正文：错误/关闭的 reply 上 readAll 会打印
        // "device not open" 之类的警告，纯噪音
        const QByteArray payload = reply->readAll();

        // 结果已到手，对冲的那条不再需要
        cancelInflight();

        if (parseManifest(payload))
            return;

        // 正文坏了（被截断、被劫持成 HTML 错误页）：换下一个镜像再试
        advanceAfterFailure(true);
        return;
    }

    const QString reason = timedOut ? QStringLiteral("请求超时")
        : status == 404                ? QStringLiteral("该 Release 没有更新清单")
        : status >= 400                ? QStringLiteral("HTTP %1").arg(status)
                                       : reply->errorString();
    noteError(reason);

    // 对冲请求还在飞：等它的结果，不要因为一条失败就判整次检查失败
    if (!m_inflight.isEmpty())
        return;

    advanceAfterFailure(isTransient(reply, timedOut));
}

void UpdateChecker::advanceAfterFailure(bool retryable)
{
    // 还有候选地址（镜像）就换一条路再试：一个镜像失败不代表下一个也失败；
    // 只有一条地址时才按老规矩 —— 抖动才重试，4xx 这类重发只会得到同样的结果。
    if (m_attempts < attemptLimit() && remainingBudgetMs() >= kMinBudgetMs
        && (retryable || m_urls.size() > 1)) {
        sendAttempt();
        return;
    }

    m_running = false;
    emit failed(failureMessage());
}

bool UpdateChecker::isTransient(QNetworkReply* reply, bool timedOut) const
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400 && status < 500)
        return false; // 确定性失败：找不到、被拒
    if (status >= 500)
        return true; // 网关/服务端抖动

    if (timedOut)
        return true;

    switch (reply->error()) {
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::SslHandshakeFailedError:
        return true;
    default:
        return false;
    }
}

int UpdateChecker::remainingBudgetMs() const
{
    if (!m_clock.isValid())
        return kTotalBudgetMs;
    return kTotalBudgetMs - int(m_clock.elapsed());
}

QString UpdateChecker::failureMessage() const
{
    if (m_errors.isEmpty())
        return QStringLiteral("没有可用的更新清单地址");
    // 三个镜像的失败原因可能不同，一并列出，方便判断是网络不通还是文件没发
    return m_errors.join(QStringLiteral("；"));
}

bool UpdateChecker::parseManifest(const QByteArray& payload)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (!document.isObject()) {
        // 转发服务出错时常常回一页 HTML，这里必须当作失败，否则会被当成「没有更新」
        noteError(QStringLiteral("无法解析更新清单：%1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject object = document.object();
    const QString version = normalizeVersion(object.value(QStringLiteral("version")).toString());
    if (version.isEmpty()) {
        noteError(QStringLiteral("更新清单里没有可用的 version 字段"));
        return false;
    }

    ReleaseInfo info;
    info.version = version;
    info.tagName = object.value(QStringLiteral("tag")).toString();
    if (info.tagName.isEmpty())
        info.tagName = QStringLiteral("v%1").arg(version);
    info.title = object.value(QStringLiteral("name")).toString();
    info.notes = object.value(QStringLiteral("notes")).toString();
    info.publishedAt = object.value(QStringLiteral("published_at")).toString();

    // 安装包清单；旧清单没有这一项时留空，界面回退到「打开发布页」
    const QJsonArray assets = object.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue& value : assets) {
        const QJsonObject item = value.toObject();
        ReleaseAsset asset;
        asset.name = item.value(QStringLiteral("name")).toString();
        asset.url = item.value(QStringLiteral("url")).toString();
        asset.size = qint64(item.value(QStringLiteral("size")).toDouble(0.0));
        if (!asset.name.isEmpty() && !asset.url.isEmpty())
            info.assets.append(asset);
    }

    // 清单经过第三方转发，而这里没有签名可验：页面地址只认本仓库的发布页，
    // 免得被篡改的清单把用户带到任意网站。不认就回到按 tag 拼出来的地址。
    const QUrl page(object.value(QStringLiteral("page")).toString());
    const bool trustedPage = page.isValid() && page.scheme() == QLatin1String("https")
        && page.host() == QLatin1String("github.com")
        && page.path().startsWith(QLatin1String(kRepoPath) + QLatin1String("releases/"));
    if (trustedPage) {
        info.pageUrl = page;
    } else {
        info.pageUrl = QUrl(QStringLiteral("https://github.com%1releases/tag/%2")
                                .arg(QLatin1String(kRepoPath), info.tagName));
    }

    // 记下这次是哪条通路取到清单的：下载安装包时优先走同一条
    const QString active = m_activeUrl.toString();
    for (const QString& prefix : m_mirrors) {
        QString trimmed = prefix.trimmed();
        while (trimmed.endsWith(QLatin1Char('/')))
            trimmed.chop(1);
        if (!trimmed.isEmpty() && active.startsWith(trimmed)) {
            info.mirror = prefix;
            break;
        }
    }

    report(info);
    return true;
}

void UpdateChecker::report(const ReleaseInfo& release)
{
    m_running = false;
    if (compareVersions(release.version, QLatin1String(CSM_VERSION_NUMBER)) > 0)
        emit updateAvailable(release);
    else
        emit upToDate(QLatin1String(CSM_VERSION_NUMBER));
}

} // namespace csm
