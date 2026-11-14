#include "ui/UpdateDialog.h"

#include "csm_version.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace csm {

UpdateDialog::UpdateDialog(const ReleaseInfo& release, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("发现新版本"));
    setModal(true);
    setMinimumSize(620, 520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(12);

    auto* heading = new QLabel(QStringLiteral("新版本 %1 已发布").arg(release.tagName), this);
    heading->setStyleSheet(QStringLiteral("font-size: 17px; font-weight: 600;"));
    root->addWidget(heading);

    auto* subheading = new QLabel(
        QStringLiteral("当前版本 v%1。更新日志来自 GitHub；下载与安装由你决定，"
                       "本程序不会自行替换自身。")
            .arg(QLatin1String(CSM_VERSION_NUMBER)),
        this);
    subheading->setWordWrap(true);
    subheading->setStyleSheet(QStringLiteral("color:#64748b;"));
    root->addWidget(subheading);

    // 正文：Release 的 body 就是作者写的更新日志。标题常常就是版本号本身
    // （GitHub 发布页默认这么填），而版本号已经在对话框标题里了，重复一遍只会
    // 多出一行无意义的「## 0.1.0」。
    QString notesText = release.notes.trimmed();
    const QString title = release.title.trimmed();
    const bool titleIsVersion =
        QRegularExpression(QStringLiteral("^[vV]?\\d+(\\.\\d+)*([-+].*)?$")).match(title).hasMatch();
    if (!title.isEmpty() && !titleIsVersion && !notesText.startsWith(title))
        notesText = QStringLiteral("## %1\n\n%2").arg(title, notesText);
    if (notesText.isEmpty())
        notesText = QStringLiteral("（该版本没有更新日志，请打开发布页查看。）");

    auto* notes = new QTextBrowser(this);
    notes->setOpenExternalLinks(false); // 日志里的链接不自动打开，避免误点
    notes->setMarkdown(notesText);
    root->addWidget(notes, 1);

    const QDateTime published = QDateTime::fromString(release.publishedAt, Qt::ISODate).toLocalTime();

    QString hint;
    if (published.isValid()) {
        hint = QStringLiteral("发布于 %1。安装包请到发布页下载。")
                   .arg(published.toString(QStringLiteral("yyyy-MM-dd")));
    } else {
        hint = QStringLiteral("安装包请到发布页下载。");
    }
    auto* hintLabel = new QLabel(hint, this);
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet(QStringLiteral("color:#64748b;"));
    root->addWidget(hintLabel);

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);
    actions->addStretch(1);

    auto* later = new QPushButton(QStringLiteral("稍后"), this);
    auto* open = new QPushButton(QStringLiteral("打开下载页"), this);
    open->setDefault(true);
    actions->addWidget(later);
    actions->addWidget(open);
    root->addLayout(actions);

    connect(later, &QPushButton::clicked, this, &QDialog::reject);
    connect(open, &QPushButton::clicked, this, [this, release]() {
        QDesktopServices::openUrl(release.pageUrl);
        accept();
    });
}

} // namespace csm
