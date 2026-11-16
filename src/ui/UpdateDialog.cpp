#include "ui/UpdateDialog.h"

#include "csm_version.h"

#include <QDateTime>
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

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);

    m_folderButton = new QPushButton(QStringLiteral("打开所在文件夹"), this);
    m_folderButton->setVisible(false);
    actions->addWidget(m_folderButton);
    actions->addStretch(1);

    m_laterButton = new QPushButton(QStringLiteral("稍后"), this);
    m_downloadButton = new QPushButton(QStringLiteral("下载安装包…"), this);
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

    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QDir::homePath();

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存安装包"), QDir(dir).filePath(m_asset.name),
        QStringLiteral("安装包 (*)"));
    if (path.isEmpty())
        return;
    downloadTo(path);
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
    m_progress->setRange(0, 0); // 大小未知时先转着
    if (m_asset.size > 0)
        m_progress->setRange(0, int(m_asset.size / 1024));
    m_downloadButton->setEnabled(false);
    m_status->setText(QStringLiteral("正在下载 %1 …").arg(m_asset.name));
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
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("circuit-master/%1").arg(QLatin1String(CSM_VERSION_NUMBER)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

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
    if (m_reply && m_file)
        m_file->write(m_reply->readAll());
}

void UpdateDialog::onFinished()
{
    QNetworkReply* reply = m_reply;
    if (!reply)
        return;
    m_reply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
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
    m_status->setText(QStringLiteral("已保存到 %1").arg(m_savedPath));
    m_folderButton->setVisible(true);
    m_downloadButton->setEnabled(true);
    m_downloadButton->setText(QStringLiteral("重新下载…"));
    emit downloadFinished(m_savedPath);
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
