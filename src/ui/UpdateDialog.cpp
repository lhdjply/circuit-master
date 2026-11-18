#include "ui/UpdateDialog.h"

#include "csm_version.h"
#include "ui/Dialogs.h"

#include <QApplication>
#include <QProcess>
#include <QSettings>
#include <QStringList>
#include <QDateTime>
#include <QTimer>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace csm {
namespace {

// 下载时的「换地址」阈值：这些镜像连得上却常只吐几 KB/s，
// 实测 1.8 MB 的 deb 有的一分钟都下不完（直连 github 更是只有 5 KB/s）。
// 阈值可以通过环境变量调小（测试用；CSM_STALL_CHECK_MS / CSM_STALL_LIMIT_MS /
// CSM_SLOW_GRACE_MS / CSM_SLOW_FLOOR_BYTES）
int envMs(const char* name, int fallback)
{
    bool ok = false;
    const int v = qEnvironmentVariableIntValue(name, &ok);
    return ok && v > 0 ? v : fallback;
}


/// 下载过程的跟踪输出。默认关闭，只有 CSM_DOWNLOAD_TRACE=1 时才打印 ——
/// 走 qWarning 而不是 printf：它进 stderr，测试和 CI 都能原样收到，
/// 不受 stdout 缓冲影响（出问题时正需要这个）。
void trace(const QString& message)
{
    static const bool enabled = qEnvironmentVariableIsSet("CSM_DOWNLOAD_TRACE");
    if (enabled)
        qWarning("[download] %s", qPrintable(message));
}

int stallCheckMs() { return envMs("CSM_STALL_CHECK_MS", 2000); }
int stallLimitMs() { return envMs("CSM_STALL_LIMIT_MS", 10000); }
int slowGraceMs() { return envMs("CSM_SLOW_GRACE_MS", 8000); }
qint64 slowFloorBytes()
{
    bool ok = false;
    const qlonglong v = qEnvironmentVariableIntValue("CSM_SLOW_FLOOR_BYTES", &ok);
    return ok && v > 0 ? v : 200 * 1024;
}


QString humanSize(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    const double mb = double(bytes) / (1024.0 * 1024.0);
    if (mb >= 1.0)
        return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 0);
}

} // namespace

UpdateDialog::UpdateDialog(const ReleaseInfo& release, QWidget* parent)
    : QDialog(parent)
    , m_release(release)
{
    // 包形态由编译期平台决定：Linux 取 deb，Windows 取 setup.exe，
    // 该形态缺失时 pickAsset 会自动退回同平台的便携包
    m_asset = UpdateChecker::pickAssetForHost(release);
    setWindowTitle(QStringLiteral("发现新版本"));
    setModal(true);
    setMinimumSize(620, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(12);

    auto* heading = new QLabel(QStringLiteral("新版本 %1 已发布").arg(release.tagName), this);
    heading->setStyleSheet(QStringLiteral("font-size: 17px; font-weight: 600;"));
    root->addWidget(heading);

    auto* subheading = new QLabel(
        QStringLiteral("当前版本 v%1。下载由你决定，本程序不会自行替换自身。")
            .arg(QLatin1String(CSM_VERSION_NUMBER)),
        this);
    subheading->setWordWrap(true);
    subheading->setStyleSheet(QStringLiteral("color:#64748b;"));
    root->addWidget(subheading);

    // 正文：Release 的 body 就是作者写的更新日志
    QString notesText = release.notes.trimmed();
    const QString title = release.title.trimmed();
    const bool titleIsVersion =
        QRegularExpression(QStringLiteral("^[vV]?\\d+(\\.\\d+)*([-+].*)?$")).match(title).hasMatch();
    if (!title.isEmpty() && !titleIsVersion && !notesText.startsWith(title))
        notesText = QStringLiteral("## %1\n\n%2").arg(title, notesText);
    if (notesText.isEmpty())
        notesText = QStringLiteral("（该版本没有更新日志，请打开发布页查看。）");

    auto* notes = new QTextBrowser(this);
    notes->setOpenExternalLinks(false);
    notes->setMarkdown(notesText);
    root->addWidget(notes, 1);

    const QDateTime published = QDateTime::fromString(release.publishedAt, Qt::ISODate).toLocalTime();
    const QString runKind = UpdateChecker::detectInstallKind() == InstallKind::Portable
        ? QStringLiteral("便携版")
        : QStringLiteral("安装版");
    QString hint = m_asset.name.isEmpty()
        ? QStringLiteral("没有找到适合当前系统的安装包，可到发布页手动下载：%1")
              .arg(m_release.pageUrl.toString())
        : QStringLiteral("检测到当前是%1运行，将下载：%2%3")
              .arg(runKind, m_asset.name, humanSize(m_asset.size).isEmpty()
                                     ? QString()
                                     : QStringLiteral("（%1）").arg(humanSize(m_asset.size)));
    if (published.isValid())
        hint = QStringLiteral("发布于 %1。%2").arg(published.toString(QStringLiteral("yyyy-MM-dd")), hint);

    m_status = new QLabel(hint, this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#64748b;"));
    root->addWidget(m_status);

    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    m_progress->setTextVisible(true);
    root->addWidget(m_progress);

    // 下载地址与实时速度：镜像有好有坏，让人看得见当前走的哪条、跑多快
    m_transfer = new QLabel(this);
    m_transfer->setVisible(false);
    m_transfer->setWordWrap(true);
    m_transfer->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_transfer->setStyleSheet(QStringLiteral("color:#64748b; font-size:11px;"));
    root->addWidget(m_transfer);

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);

    m_folderButton = new QPushButton(QStringLiteral("打开所在文件夹"), this);
    m_folderButton->setVisible(false);
    actions->addWidget(m_folderButton);
    actions->addStretch(1);

    m_laterButton = new QPushButton(QStringLiteral("稍后"), this);
    m_downloadButton = new QPushButton(QStringLiteral("下载并安装"), this);
    m_downloadButton->setEnabled(!m_asset.name.isEmpty());
    m_downloadButton->setDefault(!m_asset.name.isEmpty());
    actions->addWidget(m_laterButton);
    actions->addWidget(m_downloadButton);
    root->addLayout(actions);

    connect(m_laterButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_downloadButton, &QPushButton::clicked, this, &UpdateDialog::chooseAndDownload);
    connect(m_folderButton, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_savedPath).absolutePath()));
    });
}

UpdateDialog::~UpdateDialog()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    delete m_file;
}

QVector<QUrl> UpdateDialog::downloadCandidates() const
{
    // 优先走「刚才取清单成功的那条通路」，再试其它镜像，最后才直连 GitHub
    return UpdateChecker::downloadUrls(m_asset.url, m_release.mirror);
}

void UpdateDialog::chooseAndDownload()
{
    if (m_asset.name.isEmpty())
        return;

    // 不再问保存路径：下到临时目录，装完就删
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    QDir().mkpath(dir);

    m_installAfterDownload = true;
    if (downloadTo(QDir(dir).filePath(m_asset.name)))
        m_status->setText(QStringLiteral("正在下载 %1，完成后会自动安装…").arg(m_asset.name));
}

bool UpdateDialog::downloadTo(const QString& path)
{
    if (m_reply) {
        m_status->setText(QStringLiteral("正在下载，请稍候…"));
        return false;
    }

    m_savePath = path;
    m_urls = downloadCandidates();
    m_attempt = 0;
    if (m_urls.isEmpty()) {
        abortDownload(QStringLiteral("清单里没有下载地址"));
        return false;
    }

    if (!m_net)
        m_net = new QNetworkAccessManager(this);

    m_file = new QSaveFile(path, this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        const QString reason = m_file->errorString();
        delete m_file;
        m_file = nullptr;
        abortDownload(reason);
        return false;
    }

    m_progress->setVisible(true);
    m_transfer->setVisible(true);
    m_progress->setRange(0, 0); // 大小未知时先转着
    if (m_asset.size > 0)
        m_progress->setRange(0, int(m_asset.size / 1024));
    m_downloadButton->setEnabled(false);
    startAttempt();
    return true;
}

void UpdateDialog::startAttempt()
{
    if (m_attempt >= m_urls.size()) {
        abortDownload(QStringLiteral("所有下载地址都失败了"));
        return;
    }

    const QUrl url = m_urls.at(m_attempt++);
    trace(QStringLiteral("尝试 %1/%2  %3").arg(m_attempt).arg(m_urls.size()).arg(url.toString()));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("circuit-master/%1").arg(QLatin1String(CSM_VERSION_NUMBER)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    // 每次尝试重新计时：有的镜像连得上但只往外滴几 KB/s，光靠「请求结束」判断
    // 失败会让用户干等好几分钟。这里改成按吞吐量判，慢或卡住就换下一个地址。
    m_attemptBytes = 0;
    m_lastTickBytes = 0;
    m_idleTicks = 0;
    m_attemptClock.restart();
    m_speedClock.restart();
    m_speedBytes = 0;
    m_speedBps = 0.0;

    // 状态里写清楚这一轮走的是哪条路（镜像 host 一眼能认出来）
    const QString host = url.host().isEmpty() ? url.toString() : url.host();
    m_status->setText(QStringLiteral("正在从 %1 下载 %2 …").arg(host, m_asset.name));
    m_transfer->setToolTip(url.toString());
    updateTransferInfo();
    if (!m_stallTimer) {
        m_stallTimer = new QTimer(this);
        m_stallTimer->setInterval(stallCheckMs());
        connect(m_stallTimer, &QTimer::timeout, this, &UpdateDialog::onStallTick);
    }
    m_stallTimer->start();

    m_reply = m_net->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &UpdateDialog::onReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0) {
            m_progress->setRange(0, int(total / 1024));
            m_progress->setValue(int(got / 1024));
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, &UpdateDialog::onFinished);
}

void UpdateDialog::onReadyRead()
{
    if (!m_reply)
        return;
    const QByteArray chunk = m_reply->readAll();
    m_attemptBytes += chunk.size();
    if (m_file)
        m_file->write(chunk);
    updateTransferInfo();
}

void UpdateDialog::updateTransferInfo()
{
    const qint64 now = m_speedClock.elapsed();
    if (now >= 300) {
        const qint64 grew = m_attemptBytes - m_speedBytes;
        const double instant = grew * 1000.0 / double(now);
        // 指数平滑：单次抖动不至于让数字乱跳，卡住时又能很快掉下来
        m_speedBps = m_speedBps <= 0.0 ? instant : m_speedBps * 0.6 + instant * 0.4;
        m_speedBytes = m_attemptBytes;
        m_speedClock.restart();
    }

    const auto human = [](double bytes) {
        if (bytes >= 1024.0 * 1024.0)
            return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    };

    QStringList parts;
    if (!m_urls.isEmpty() && m_attempt > 0) {
        const QUrl current = m_urls.at(m_attempt - 1);
        parts << QStringLiteral("地址 %1").arg(current.host().isEmpty() ? QStringLiteral("（本地）")
                                                                       : current.host());
    }
    parts << QStringLiteral("速度 %1/s").arg(human(m_speedBps));
    if (m_asset.size > 0) {
        parts << QStringLiteral("已下载 %1 / %2").arg(human(double(m_attemptBytes)),
                                                      human(double(m_asset.size)));
        if (m_speedBps > 1024.0) {
            const double left = (double(m_asset.size) - double(m_attemptBytes)) / m_speedBps;
            if (left > 0.0)
                parts << QStringLiteral("剩余 %1 s").arg(left, 0, 'f', left < 10.0 ? 1 : 0);
        }
    } else {
        parts << QStringLiteral("已下载 %1").arg(human(double(m_attemptBytes)));
    }

    m_transfer->setText(parts.join(QStringLiteral(" · ")));
    m_transfer->setVisible(true);
}

void UpdateDialog::onStallTick()
{
    if (!m_reply)
        return;

    const qint64 grew = m_attemptBytes - m_lastTickBytes;
    m_lastTickBytes = m_attemptBytes;
    m_idleTicks = grew > 0 ? 0 : m_idleTicks + 1;

    // 还有别的地址可试才换；已经是最后一个就只能继续等
    if (m_attempt >= m_urls.size())
        return;

    bool switchAway = false;
    QString why;
    if (m_idleTicks * stallCheckMs() >= stallLimitMs()) {
        switchAway = true;
        why = QStringLiteral("这个地址一直没有数据，换一个");
    } else if (m_attemptClock.elapsed() >= slowGraceMs()
               && m_attemptBytes < slowFloorBytes()) {
        switchAway = true;
        why = QStringLiteral("这个地址太慢，换一个");
    }

    if (!switchAway)
        return;

    trace(QStringLiteral("放弃当前地址（%1）：已收 %2 字节，用时 %3 ms")
              .arg(why)
              .arg(m_attemptBytes)
              .arg(m_attemptClock.elapsed()));
    m_status->setText(why);
    // abort() 会走 finished，那里已经有「换下一条、把已写的字节丢掉」的逻辑
    m_reply->abort();
}

void UpdateDialog::onFinished()
{
    QNetworkReply* reply = m_reply;
    if (!reply)
        return;
    m_reply = nullptr;
    if (m_stallTimer)
        m_stallTimer->stop();
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
    trace(QStringLiteral("结束：HTTP %1，已收 %2 字节，错误 %3（%4）")
              .arg(status)
              .arg(m_attemptBytes)
              .arg(int(reply->error()))
              .arg(reply->errorString()));
    if (!ok) {
        // 这一条失败就换下一条，并把已写入的字节丢掉重来
        if (m_file) {
            m_file->cancelWriting();
            delete m_file;
            m_file = nullptr;
        }
        if (m_attempt < m_urls.size()) {
            m_status->setText(QStringLiteral("换个地址重试（%1/%2）…")
                                  .arg(m_attempt + 1)
                                  .arg(m_urls.size()));
            if (!m_file) {
                m_file = new QSaveFile(m_savePath, this);
                if (!m_file->open(QIODevice::WriteOnly)) {
                    abortDownload(m_file->errorString());
                    return;
                }
            }
            startAttempt();
            return;
        }
        abortDownload(reply->errorString());
        return;
    }

    if (m_file && m_file->isOpen()) {
        m_file->write(reply->readAll());
        if (!m_file->commit()) {
            const QString reason = m_file->errorString();
            delete m_file;
            m_file = nullptr;
            abortDownload(reason);
            return;
        }
        delete m_file;
        m_file = nullptr;
    }

    m_savedPath = m_savePath;
    m_progress->setValue(m_progress->maximum());
    m_downloadButton->setEnabled(true);
    m_downloadButton->setText(QStringLiteral("重新下载并安装"));
    m_folderButton->setVisible(true);
    emit downloadFinished(m_savedPath);

    if (m_installAfterDownload) {
        m_installAfterDownload = false;
        installDownloaded(m_savedPath);
    } else {
        m_status->setText(QStringLiteral("已保存到 %1").arg(m_savedPath));
    }
}

void UpdateDialog::installDownloaded(const QString& path)
{
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();

    // Windows 安装包：直接拉起，装完由下次启动清理
    if (suffix == QLatin1String("exe")) {
        m_status->setText(QStringLiteral("已下载，正在启动安装程序…"));
        if (!QProcess::startDetached(path, {})) {
            m_status->setText(QStringLiteral("无法启动安装程序：%1").arg(path));
            return;
        }
        QSettings().setValue(QStringLiteral("pendingInstaller"), path);
        m_status->setText(QStringLiteral("安装程序已启动，按提示完成后会自动清理安装包"));
        return;
    }

    // Debian 包：走 pkexec（弹一次授权框），装完删除
    if (suffix == QLatin1String("deb")) {
        const QString tool = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        if (tool.isEmpty()) {
            m_status->setText(QStringLiteral("未找到 pkexec，无法自动安装：%1").arg(path));
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
            return;
        }
        m_status->setText(QStringLiteral("正在安装，请在授权窗口里确认…"));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QProcess process;
        process.start(tool, {QStringLiteral("dpkg"), QStringLiteral("-i"), path});
        const bool finished = process.waitForFinished(120000);
        const int code = finished ? process.exitCode() : -1;
        QApplication::restoreOverrideCursor();

        if (finished && code == 0) {
            QFile::remove(path);
            m_status->setText(QStringLiteral("安装完成，安装包已删除。重新启动程序即可用新版本。"));
        } else {
            const QString detail = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            m_status->setText(QStringLiteral("安装未完成（退出码 %1）。安装包保留在：%2%3")
                                  .arg(code)
                                  .arg(path, detail.isEmpty() ? QString()
                                                              : QStringLiteral("\n") + detail));
        }
        return;
    }

    // 便携包（tar.gz / zip）：不能自动覆盖正在运行的程序，打开所在文件夹让用户自己解压
    m_status->setText(QStringLiteral("便携包已下载到 %1，解压后覆盖原目录即可").arg(path));
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

void UpdateDialog::cleanupPendingInstaller()
{
    QSettings settings;
    const QString pending = settings.value(QStringLiteral("pendingInstaller")).toString();
    if (pending.isEmpty())
        return;
    // 安装程序可能还在跑（文件被占用），删不掉就留着下次再试
    if (!QFile::exists(pending) || QFile::remove(pending))
        settings.remove(QStringLiteral("pendingInstaller"));
}

void UpdateDialog::abortDownload(const QString& reason)
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_file) {
        m_file->cancelWriting();
        delete m_file;
        m_file = nullptr;
    }
    m_progress->setVisible(false);
    m_downloadButton->setEnabled(!m_asset.name.isEmpty());
    m_status->setText(QStringLiteral("下载失败：%1").arg(reason));
}

} // namespace csm
