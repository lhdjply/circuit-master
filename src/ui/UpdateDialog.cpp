#include "ui/UpdateDialog.h"
#include "core/Language.h"

#include "ui/Theme.h"

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
    setWindowTitle(CSM_TR("New version available"));
    setModal(true);
    setMinimumSize(620, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(12);

    auto* heading = new QLabel(CSM_TR("Version %1 has been released").arg(release.tagName), this);
    heading->setStyleSheet(QStringLiteral("font-size: 17px; font-weight: 600;"));
    root->addWidget(heading);

    auto* subheading = new QLabel(
        CSM_TR("Current version v%1. The download is your call; the application never replaces itself.")
            .arg(QLatin1String(CSM_VERSION_NUMBER)),
        this);
    subheading->setWordWrap(true);
    Theme::styleWidget(subheading, [] { return QStringLiteral("color:%1;").arg(Theme::textMuted().name()); });
    root->addWidget(subheading);

    // 正文：Release 的 body 就是作者写的更新日志
    // 英文界面优先用 notes_en；清单里没有（老版本清单）就退回中文那份
    const bool englishUi = (csm::language() == csm::Language::English);
    QString notesText = englishUi && !release.notesEn.trimmed().isEmpty()
                            ? release.notesEn.trimmed()
                            : release.notes.trimmed();
    const QString title = release.title.trimmed();
    const bool titleIsVersion =
        QRegularExpression(QStringLiteral("^[vV]?\\d+(\\.\\d+)*([-+].*)?$")).match(title).hasMatch();
    if (!title.isEmpty() && !titleIsVersion && !notesText.startsWith(title))
        notesText = QStringLiteral("## %1\n\n%2").arg(title, notesText);
    if (notesText.isEmpty())
        notesText = CSM_TR(" (This release has no changelog; open the release page.)");

    auto* notes = new QTextBrowser(this);
    notes->setOpenExternalLinks(false);
    notes->setMarkdown(notesText);
    root->addWidget(notes, 1);

    const QDateTime published = QDateTime::fromString(release.publishedAt, Qt::ISODate).toLocalTime();
    const QString runKind = UpdateChecker::detectInstallKind() == InstallKind::Portable
        ? CSM_TR("Portable")
        : CSM_TR("Installed");
    QString hint = m_asset.name.isEmpty()
        ? CSM_TR("No package matches this system; download it manually from the release page: %1")
              .arg(m_release.pageUrl.toString())
        : CSM_TR("Detected a %1 build; will download: %2%3")
              .arg(runKind, m_asset.name, humanSize(m_asset.size).isEmpty()
                                     ? QString()
                                     : QStringLiteral("（%1）").arg(humanSize(m_asset.size)));
    if (published.isValid())
        hint = CSM_TR("Published %1. %2").arg(published.toString(QStringLiteral("yyyy-MM-dd")), hint);

    m_status = new QLabel(hint, this);
    m_status->setWordWrap(true);
    Theme::styleWidget(m_status, [] { return QStringLiteral("color:%1;").arg(Theme::textMuted().name()); });
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
    Theme::styleWidget(m_transfer, [] { return QStringLiteral("color:%1; font-size:11px;").arg(Theme::textMuted().name()); });
    root->addWidget(m_transfer);

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);

    m_folderButton = new QPushButton(CSM_TR("Open Containing Folder"), this);
    m_folderButton->setVisible(false);
    actions->addWidget(m_folderButton);
    actions->addStretch(1);

    m_laterButton = new QPushButton(CSM_TR("Later"), this);
    m_downloadButton = new QPushButton(CSM_TR("Download and Install"), this);
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
        m_status->setText(CSM_TR("Downloading %1; it installs automatically when done…").arg(m_asset.name));
}

bool UpdateDialog::downloadTo(const QString& path)
{
    if (m_reply) {
        m_status->setText(CSM_TR("Downloading, please wait…"));
        return false;
    }

    m_savePath = path;
    m_urls = downloadCandidates();
    m_attempt = 0;
    if (m_urls.isEmpty()) {
        abortDownload(CSM_TR("The manifest has no download URL"));
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
        abortDownload(CSM_TR("All download mirrors failed"));
        return;
    }

    const QUrl url = m_urls.at(m_attempt++);
    trace(CSM_TR("Try %1/%2  %3").arg(m_attempt).arg(m_urls.size()).arg(url.toString()));
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
    m_status->setText(CSM_TR("Downloading %2 from %1 …").arg(host, m_asset.name));
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
        parts << CSM_TR("Mirror %1").arg(current.host().isEmpty() ? CSM_TR(" (local)")
                                                                       : current.host());
    }
    parts << CSM_TR("%1/s").arg(human(m_speedBps));
    if (m_asset.size > 0) {
        parts << CSM_TR("Downloaded %1 / %2").arg(human(double(m_attemptBytes)),
                                                      human(double(m_asset.size)));
        if (m_speedBps > 1024.0) {
            const double left = (double(m_asset.size) - double(m_attemptBytes)) / m_speedBps;
            if (left > 0.0)
                parts << CSM_TR("%1 s left").arg(left, 0, 'f', left < 10.0 ? 1 : 0);
        }
    } else {
        parts << CSM_TR("Downloaded %1").arg(human(double(m_attemptBytes)));
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
        why = CSM_TR("This mirror sent no data, switching");
    } else if (m_attemptClock.elapsed() >= slowGraceMs()
               && m_attemptBytes < slowFloorBytes()) {
        switchAway = true;
        why = CSM_TR("This mirror is too slow, switching");
    }

    if (!switchAway)
        return;

    trace(CSM_TR("Giving up on %1: received %2 bytes in %3 ms")
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
    trace(CSM_TR("Finished: HTTP %1, %2 bytes received, error %3 (%4)")
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
            m_status->setText(CSM_TR("Retrying with another mirror (%1/%2)…")
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
    m_downloadButton->setText(CSM_TR("Download and Install Again"));
    m_folderButton->setVisible(true);
    emit downloadFinished(m_savedPath);

    if (m_installAfterDownload) {
        m_installAfterDownload = false;
        installDownloaded(m_savedPath);
    } else {
        m_status->setText(CSM_TR("Saved to %1").arg(m_savedPath));
    }
}

void UpdateDialog::installDownloaded(const QString& path)
{
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();

    // Windows 安装包：直接拉起，装完由下次启动清理
    if (suffix == QLatin1String("exe")) {
        m_status->setText(CSM_TR("Downloaded, starting the installer…"));
        if (!QProcess::startDetached(path, {})) {
            m_status->setText(CSM_TR("Cannot start the installer: %1").arg(path));
            return;
        }
        QSettings().setValue(QStringLiteral("pendingInstaller"), path);
        m_status->setText(CSM_TR("The installer has started; the package is cleaned up automatically once you finish"));
        return;
    }

    // Debian 包：走 pkexec（弹一次授权框），装完删除
    if (suffix == QLatin1String("deb")) {
        const QString tool = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        if (tool.isEmpty()) {
            m_status->setText(CSM_TR("pkexec not found, cannot install automatically: %1").arg(path));
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
            return;
        }
        m_status->setText(CSM_TR("Installing, confirm in the authorization window…"));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QProcess process;
        process.start(tool, {QStringLiteral("dpkg"), QStringLiteral("-i"), path});
        const bool finished = process.waitForFinished(120000);
        const int code = finished ? process.exitCode() : -1;
        QApplication::restoreOverrideCursor();

        if (finished && code == 0) {
            QFile::remove(path);
            m_status->setText(CSM_TR("Install finished and the installer was removed. Restart the application to use the new version."));
        } else {
            const QString detail = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            m_status->setText(CSM_TR("Install did not finish (exit code %1). Installer kept at: %2%3")
                                  .arg(code)
                                  .arg(path, detail.isEmpty() ? QString()
                                                              : QStringLiteral("\n") + detail));
        }
        return;
    }

    // 便携包（tar.gz / zip）：不能自动覆盖正在运行的程序，打开所在文件夹让用户自己解压
    m_status->setText(CSM_TR("Portable package downloaded to %1; unzip it over the original folder").arg(path));
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
    m_status->setText(CSM_TR("Download failed: %1").arg(reason));
}

} // namespace csm
