#pragma once

// ---------------------------------------------------------------------------
//  检查更新
//
//  只读一个来源：随 Release 上传的更新清单
//    https://github.com/lhdjply/circuit-master/releases/latest/download/latest.json
//
//  为什么是它：
//   · 它是 Release 的一个普通资产，由 CDN 直出，不需要认证，也不占 API 配额；
//   · 它是 github.com 上的一个文件，第三方 GitHub 转发服务能原样代理，于是
//     不挂代理也能取到；
//   · 这个路径只在正式 Release 建好之后才存在，不会撞上「tag 已推、CI 还在
//     编译」的占位条目。
//
//  地址按「下载安装包」的实测速度排序试：v4.gh-proxy.org → ghfast.top →
//  gh-proxy.org → cdn.gh-proxy.org，全挂了才报错。抖动对冲：到节点这条链路实测有部分连接会卡 2~5 秒（TCP 重传），单条
//  请求一旦撞上就要等到超时。因此首条请求每 700ms 还没结果就并行再补一条，
//  谁先成功用谁。正常情况（300~500ms）只发一条，不产生额外请求。
//
//  只做「查询 + 展示」两件事：不下载、不安装。安装包由用户自己到发布页取 ——
//  在缺少签名校验与包管理器的前提下让程序替换自己的二进制，风险远大于收益。
//
//  失败必须给出可读原因，不静默 —— 「没动静」最容易被理解成「没有更新」。
// ---------------------------------------------------------------------------

#include "csm_platform.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace csm {

/// 安装包形态。Linux 出 deb 与便携 tar.gz，Windows 出 setup.exe 与便携 zip。
enum class PackageFormat {
    Deb,          // Debian / Ubuntu / Deepin 安装包
    PortableTarGz, // Linux 便携包，解压即用
    SetupExe,     // Windows 安装程序
    PortableZip,  // Windows 便携包，解压即用
    MacDmg,       // macOS 磁盘映像，里面是 .app，挂载后拖进「应用程序」
    AndroidApk,   // Android 安装包，侧载安装（不需要应用商店）
};

/// 当前这份程序是怎么装的。分「系统安装」与「便携解压」两种，
/// 升级时应当下载同一种形态，免得用 deb 的人被塞 tar.gz、用便携版的人被塞 deb。
enum class InstallKind {
    System,   // deb / setup.exe 装的
    Portable, // tar.gz / zip 解压出来的
};

/// Release 里的一个下载资产
struct ReleaseAsset {
    QString name;   // 文件名，例如 circuit-master_0.2.0_amd64.deb
    QString url;    // 直链（github.com/releases/download/<tag>/<file>）
    qint64 size = 0; // 字节数，未知为 0
};

/// 一条 Release 里本功能用得到的字段
struct ReleaseInfo {
    QString version;     // 规范化后的版本号（去掉 tag 的 v 前缀与预发布后缀）
    QString tagName;     // 原始 tag，展示时保留作者的写法（如 v0.1.0）
    QString title;       // Release 标题，可能为空
    QString notes;       // 更新日志正文（清单里是 Markdown）
    QUrl pageUrl;        // 发布页：浏览器打开后可看到安装包
    QString publishedAt; // ISO 8601 时间
    QVector<ReleaseAsset> assets; // 清单里列出的安装包
    /// 这次是从哪个镜像取到清单的（空字符串表示直连 GitHub）。
    /// 下载安装包时优先走同一条通路 —— 能取到清单的地址，下大文件通常也通。
    QString mirror;
};

class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    /// 内置的 GitHub 转发服务，按顺序尝试
    static QStringList builtinMirrors();
    /// 覆盖内置镜像列表（测试与内网环境用）；传空列表就是「不走镜像」，
    /// 让下载直接打原始地址，测试不必依赖外网
    static void setBuiltinMirrors(const QStringList& mirrors);

    /// 镜像前缀；空列表表示直接访问清单地址（测试用，线上默认是三个内置镜像）
    void setMirrorPrefixes(const QStringList& prefixes);
    /// 清单地址。默认是 releases/latest/download/latest.json；测试里指到本地服务
    void setManifestUrl(const QUrl& url);

    /// 同一时刻只允许一次检查（含重试与切镜像的间隔），重复调用直接忽略并返回 false
    bool check();
    /// 是否仍在进行中。重试等待期间 m_reply 为空，所以不能只看 m_reply
    bool busy() const { return m_running; }

    /// 比较版本号：只比数字段（0.10.0 > 0.9.9），忽略 v 前缀、预发布后缀与
    /// 缺失的段（1.2 == 1.2.0）。返回 >0 表示 a 比 b 新
    static int compareVersions(const QString& a, const QString& b);

    /// 按平台挑出最合适的安装包。平台与架构是编译期常量（见 csm_platform.h），
    /// 显式传进来是为了能测各平台的组合。
    /// 挑不到时返回 name 为空的资产，调用方回退到「打开发布页」。
    static ReleaseAsset pickAsset(const ReleaseInfo& release, HostPlatform platform,
                                  HostArch arch, InstallKind kind);

    /// 判断当前这份程序是系统安装还是便携解压：
    /// Windows 看安装目录里有没有 Inno 的卸载程序，Linux 看可执行文件在不在系统前缀下。
    /// 环境变量 CSM_INSTALL_KIND=system|portable 可以覆盖（测试与特殊部署用）。
    static InstallKind detectInstallKind();

    /// 下载安装包时尝试的地址顺序：优先本次取清单成功的那条通路，
    /// 再逐个试其它镜像，最后才直连 GitHub。assetUrl 是清单里的直链。
    static QVector<QUrl> downloadUrls(const QString& assetUrl, const QString& preferredMirror);
    /// 用当前构建目标挑包（首选形态）
    static ReleaseAsset pickAssetForHost(const ReleaseInfo& release);

    /// 当前平台支持哪些包形态，按推荐顺序排列（找不到产物时为空）
    static QVector<PackageFormat> formatsFor(HostPlatform platform, InstallKind kind);
    /// 某个形态在文件名里的后缀，例如 Linux/Amd64 的 deb → "_amd64.deb"
    static QString suffixFor(HostPlatform platform, HostArch arch, PackageFormat format);

    /// 清单里当前平台能用的包，按推荐顺序（安装方式决定顺序）；没有的形态直接跳过
    struct HostPackage {
        PackageFormat format;
        ReleaseAsset asset;
    };
    static QVector<HostPackage> hostPackagesFor(const ReleaseInfo& release, HostPlatform platform,
                                                HostArch arch, InstallKind kind);

signals:
    void upToDate(const QString& currentVersion);
    void updateAvailable(const ReleaseInfo& release);
    void failed(const QString& reason);

private:
    QVector<QUrl> candidates() const;
    int attemptLimit() const;

    void sendAttempt();
    void finish(QNetworkReply* reply);
    void advanceAfterFailure(bool retryable);
    void cancelInflight();

    bool isTransient(QNetworkReply* reply, bool timedOut) const;

    int remainingBudgetMs() const;
    void noteError(const QString& reason);
    QString failureMessage() const;

    bool parseManifest(const QByteArray& payload);
    void report(const ReleaseInfo& release);

    QUrl m_manifestUrl;
    QStringList m_mirrors;
    QVector<QUrl> m_urls;
    QUrl m_activeUrl; // 正在请求的地址，成功时用来记住通路
    int m_attempts = 0;
    bool m_running = false;
    QElapsedTimer m_clock;
    QStringList m_errors;
    QNetworkAccessManager* m_manager = nullptr;
    QList<QNetworkReply*> m_inflight;
    QTimer* m_hedgeTimer = nullptr;
};

} // namespace csm
