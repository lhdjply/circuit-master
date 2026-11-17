#pragma once

// ---------------------------------------------------------------------------
//  更新详情对话框
//
//  展示新版本信息（版本号、发布日期、Release 正文），并提供「下载并安装」：
//  按当前系统与架构从清单里挑出对应的包，下到临时目录，下完直接拉起安装程序，
//  安装完成后删掉安装包（便携包无法自动装，改为打开所在文件夹）。
//
//  下载走和查版本相同的思路：先连直链，失败再依次试内置的 GitHub 转发服务。
// ---------------------------------------------------------------------------

#include "core/UpdateChecker.h"

#include <QDialog>
#include <QUrl>
#include <QVector>

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QSaveFile;

namespace csm {

class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDialog(const ReleaseInfo& release, QWidget* parent = nullptr);
    ~UpdateDialog() override;

    /// 当前选中的安装包（没有则 name 为空）
    ReleaseAsset asset() const { return m_asset; }
    /// 下载完成后的保存路径（未下载则为空）
    QString savedPath() const { return m_savedPath; }

    /// 直接下载到指定路径（不弹文件对话框）；返回是否已启动
    bool downloadTo(const QString& path);
    /// 启动刚下好的安装包（exe 直接拉起、deb 走 pkexec，其余打开所在文件夹）
    void installDownloaded(const QString& path);
    /// 清理上次遗留的安装包（安装程序跑完后删不掉，下次启动再删）
    static void cleanupPendingInstaller();

signals:
    void downloadFinished(const QString& path);

private:
    void chooseAndDownload();   // 下到临时目录并安装
    void startAttempt();
    void onReadyRead();
    void onFinished();
    void abortDownload(const QString& reason);
    QVector<QUrl> downloadCandidates() const;

    ReleaseInfo m_release;
    ReleaseAsset m_asset;

    QString m_savePath;
    QString m_savedPath;
    QVector<QUrl> m_urls;
    int m_attempt = 0;
    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_reply = nullptr;
    QSaveFile* m_file = nullptr;
    bool m_installAfterDownload = false;

    QLabel* m_status = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_downloadButton = nullptr;
    QPushButton* m_folderButton = nullptr;
    QPushButton* m_laterButton = nullptr;
};

} // namespace csm
