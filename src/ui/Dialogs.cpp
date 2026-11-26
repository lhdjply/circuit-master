#include "ui/Dialogs.h"
#include "core/Language.h"

#include "ui/Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include <cmath>

namespace csm {

namespace {

// 跟原理图导线 / 选中态同一套颜色
const QColor kAccent(0x25, 0x63, 0xeb);
const QColor kWarn(0xf5, 0x9e, 0x0b);
const QColor kError(0xdc, 0x26, 0x26);

/// 圆形徽标：底色 + 一个白色字形。比 QMessageBox 自带的位图干净，
/// 也不会因为平台不同而变成紫色圆圈之类。
QPixmap badge(const QString& glyph, const QColor& fill, int size = 40)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(int(std::lround(size * dpr)), int(std::lround(size * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawEllipse(QRectF(0.0, 0.0, size, size));

    QFont f = qApp ? qApp->font() : QFont();
    f.setPointSizeF(size * 0.44);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    // 字形本身不一定垂直居中（尤其 i / !），往上提一点点看着更稳
    p.drawText(QRectF(0.0, -size * 0.02, size, size), Qt::AlignCenter, glyph);
    return pm;
}

/// 提示框与「关于」共用的一套按钮 / 正文样式，只作用在单个实例上
QString dialogStyleSheet()
{
    // 颜色从 Theme 取：深色下换成深底浅字，别把浅色写死在这里
    const QString surface = Theme::surface().name();
    const QString text = Theme::textPrimary().name();
    const QString border = Theme::border().name();
    const QString hover = Theme::surfaceAlt().name();
    const QString pressed = Theme::border().name();
    const QString accent = Theme::accent().name();
    const QString accentStrong = Theme::accentStrong().name();

    return QStringLiteral(R"(
QDialog {
    background: %1;
}
QLabel {
    color: %2;
    font-size: 13px;
}
QPushButton {
    min-width: 56px;
    padding: 6px 12px;
    border: 1px solid %3;
    border-radius: 6px;
    background: %1;
    color: %2;
}
QPushButton:hover {
    background: %4;
    border-color: %3;
}
QPushButton:pressed {
    background: %5;
}
QPushButton:default {
    background: %6;
    border-color: %6;
    color: #ffffff;
}
QPushButton:default:hover {
    background: %7;
    border-color: %7;
}
)")
        .arg(surface, text, border, hover, pressed, accent, accentStrong);
}

/// 一个按钮的说明：文字 + 是否是主操作
struct Choice {
    QMessageBox::StandardButton button;
    QString text;
    bool primary = false;
};

/// 自绘的提示框。
///
/// 没有直接用 QMessageBox：它的按钮行由 QDialogButtonBox 掌管控件尺寸，
/// 短消息时会缩成一条、按钮却拉满整行，怎么调样式都不顺眼。
/// 自己排版只有三件事 —— 图标、正文、按钮行 —— 反而好控制，
/// 也能和「关于」窗口共用同一套观感。
/// 提示框的内容是固定的，尺寸按内容算一次就锁死，不让用户拖。
/// 先 adjustSize 再 setFixedSize：直接给布局加 SetFixedSize 约束的话，
/// 带自动换行的 QLabel 会被压成很窄的一条。
void fixDialogSize(QDialog& dialog)
{
    if (QLayout* layout = dialog.layout()) {
        layout->activate();
    }
    dialog.adjustSize();
    dialog.setFixedSize(dialog.size());
}

QMessageBox::StandardButton runMessage(QWidget* parent, const QPixmap& icon,
                                       const QString& title, const QString& text,
                                       const QVector<Choice>& choices,
                                       QMessageBox::StandardButton defaultButton,
                                       int bodyMinWidth = 300, int bodyMaxWidth = 460)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.setStyleSheet(dialogStyleSheet());

    auto* outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(20, 18, 20, 14);
    outer->setSpacing(16);

    auto* row = new QHBoxLayout;
    row->setSpacing(14);

    auto* iconLabel = new QLabel(&dialog);
    iconLabel->setPixmap(icon);
    const QSize logical = icon.size() / qreal(icon.devicePixelRatio() > 0.0
                                                  ? icon.devicePixelRatio()
                                                  : 1.0);
    iconLabel->setFixedSize(logical);
    iconLabel->setAlignment(Qt::AlignTop);
    row->addWidget(iconLabel, 0, Qt::AlignTop);

    auto* body = new QLabel(text, &dialog);
    body->setWordWrap(true);
    body->setTextFormat(Qt::AutoText);
    // 太窄会挤成两三行，太宽读起来费劲；两者之间取个舒服的范围
    body->setMinimumWidth(bodyMinWidth);
    body->setMaximumWidth(bodyMaxWidth);
    body->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    row->addWidget(body, 1);
    outer->addLayout(row);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(10);
    buttonRow->addStretch(1);

    QMessageBox::StandardButton result = QMessageBox::NoButton;
    for (const Choice& choice : choices) {
        auto* button = new QPushButton(choice.text, &dialog);
        button->setAutoDefault(false);
        button->setDefault(choice.primary && choice.button == defaultButton);
        if (choice.primary)
            button->setDefault(true);
        QObject::connect(button, &QPushButton::clicked, &dialog, [&dialog, &result, choice] {
            result = choice.button;
            dialog.accept();
        });
        buttonRow->addWidget(button);
    }
    outer->addLayout(buttonRow);

    // 回车 = 主操作，Esc = 取消
    QObject::connect(&dialog, &QDialog::rejected, [&result] {
        if (result == QMessageBox::NoButton)
            result = QMessageBox::Cancel;
    });

    // 这类提示框的内容是固定的，尺寸按内容一次算好，不让用户拖
    fixDialogSize(dialog);

    dialog.exec();

    if (result == QMessageBox::NoButton)
        result = QMessageBox::Cancel; // 直接关窗口（点 X）
    return result;
}

QMessageBox::StandardButton runInfo(QWidget* parent, const QString& title, const QString& text,
                                    const QPixmap& icon)
{
    return runMessage(parent, icon, title, text,
                      {{QMessageBox::Ok, CSM_TR("OK"), true}}, QMessageBox::Ok);
}

} // namespace

void Dialogs::info(QWidget* parent, const QString& title, const QString& text)
{
    runInfo(parent, title, text, badge(QStringLiteral("i"), kAccent));
}

void Dialogs::warn(QWidget* parent, const QString& title, const QString& text)
{
    runInfo(parent, title, text, badge(QStringLiteral("!"), kWarn));
}

void Dialogs::error(QWidget* parent, const QString& title, const QString& text)
{
    runInfo(parent, title, text, badge(QStringLiteral("×"), kError));
}

void Dialogs::guide(QWidget* parent, const QString& title, const QString& richText)
{
    runMessage(parent, badge(QStringLiteral("i"), kAccent), title, richText,
               {{QMessageBox::Ok, CSM_TR("Got it"), true}}, QMessageBox::Ok,
               460, 560);
}

bool Dialogs::confirm(QWidget* parent, const QString& title, const QString& text)
{
    const auto answer = runMessage(parent, badge(QStringLiteral("?"), kAccent), title, text,
                                   {{QMessageBox::Yes, CSM_TR("OK"), true},
                                    {QMessageBox::No, CSM_TR("Cancel"), false}},
                                   QMessageBox::Yes);
    return answer == QMessageBox::Yes;
}

QMessageBox::StandardButton Dialogs::askSaveDiscardCancel(QWidget* parent, const QString& title,
                                                          const QString& text)
{
    return runMessage(parent, badge(QStringLiteral("?"), kAccent), title, text,
                      {{QMessageBox::Save, CSM_TR("Save"), true},
                       {QMessageBox::Discard, CSM_TR("Discard"), false},
                       {QMessageBox::Cancel, CSM_TR("Cancel"), false}},
                      QMessageBox::Save);
}

void Dialogs::changelog(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(CSM_TR("Changelog"));
    dialog.setModal(true);
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.setStyleSheet(dialogStyleSheet() + QStringLiteral(R"(
QTextBrowser {
    background: %1;
    border: 1px solid %2;
    border-radius: 8px;
    padding: 10px 14px;
    selection-background-color: %3;
    selection-color: #ffffff;
}
)").arg(Theme::surface().name(), Theme::border().name(), Theme::accent().name()));

    auto* outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(20, 18, 20, 14);
    outer->setSpacing(14);

    auto* viewer = new QTextBrowser(&dialog);
    viewer->setOpenExternalLinks(true);

    // 标题 / 列表的字号颜色统一成界面那套灰阶。必须在灌内容之前设，
    // QTextDocument 会在灌入时就把默认样式表套上去。
    viewer->document()->setDefaultStyleSheet(QStringLiteral(R"(
h1 { font-size: 17px; color: %1; margin: 2px 0 10px 0; }
h2 { font-size: 15px; color: %2; margin: 18px 0 6px 0; }
h3 { font-size: 13px; color: %3; margin: 12px 0 4px 0; }
p, li { color: %3; font-size: 12.5px; }
code { color: %1; background-color: %4; }
a { color: %5; }
)").arg(Theme::textPrimary().name(), Theme::accentStrong().name(),
       Theme::textSecondary().name(), Theme::surfaceAlt().name(),
       Theme::accent().name()));

    // 中英文各一份，跟着界面语言走
    const QString path = csm::language() == csm::Language::Chinese
                             ? QStringLiteral(":/docs/CHANGELOG.md")
                             : QStringLiteral(":/docs/CHANGELOG.en.md");
    QFile file(path);
    const bool loaded = file.open(QIODevice::ReadOnly | QIODevice::Text);
    if (loaded) {
        const QString markdown = QString::fromUtf8(file.readAll());
        file.close();
        if (!markdown.trimmed().isEmpty())
            viewer->setMarkdown(markdown); // Qt 自带 CommonMark 子集的渲染
    }
    if (!loaded || viewer->document()->isEmpty()) {
        viewer->setHtml(CSM_TR("<p style='color:%1'>This build has no bundled changelog.</p>")
                            .arg(Theme::textMuted().name()));
    }
    viewer->moveCursor(QTextCursor::Start);

    outer->addWidget(viewer, 1);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    auto* close = new QPushButton(CSM_TR("Close"), &dialog);
    close->setDefault(true);
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonRow->addWidget(close);
    outer->addLayout(buttonRow);

    // 正文很长、要滚动看，但窗口本身不必可调 —— 固定成这个大小
    dialog.setFixedSize(660, 560);
    dialog.exec();
}

void Dialogs::applyStyle(QDialog& dialog)
{
    dialog.setStyleSheet(dialogStyleSheet());
}

void Dialogs::about(QWidget* parent, const QString& version)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(CSM_TR("About Circuit Master"));
    dialog.setModal(true);
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.setStyleSheet(dialogStyleSheet() + QStringLiteral(R"(
QLabel#aboutTitle {
    color: %1;
    font-size: 18px;
    font-weight: 600;
}
QLabel#aboutVersion {
    color: %2;
    background: %3;
    border-radius: 8px;
    padding: 2px 8px;
    font-size: 11px;
    font-weight: 600;
}
QLabel#aboutBody {
    color: %4;
    font-size: 12.5px;
}
QLabel#aboutFoot {
    color: %5;
    font-size: 11.5px;
}
)").arg(Theme::textPrimary().name(), Theme::accentStrong().name(),
       Theme::surfaceAlt().name(), Theme::textSecondary().name(),
       Theme::textMuted().name()));

    auto* outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(22, 20, 22, 16);
    outer->setSpacing(14);

    auto* top = new QHBoxLayout;
    top->setSpacing(16);

    // 左边：应用图标。取不到就画一个同色系的占位，别留空
    auto* iconLabel = new QLabel(&dialog);
    QPixmap appIcon;
    const QIcon fromResource(QStringLiteral(":/icons/icon.svg"));
    if (!fromResource.isNull())
        appIcon = fromResource.pixmap(QSize(72, 72));
    if (appIcon.isNull())
        appIcon = badge(QStringLiteral("~"), kAccent, 72);
    iconLabel->setPixmap(appIcon);
    iconLabel->setFixedSize(72, 72);
    iconLabel->setScaledContents(true);
    top->addWidget(iconLabel, 0, Qt::AlignTop);

    auto* right = new QVBoxLayout;
    right->setSpacing(6);

    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(8);
    auto* title = new QLabel(CSM_TR("Circuit Master"), &dialog);
    title->setObjectName(QStringLiteral("aboutTitle"));
    titleRow->addWidget(title);

    auto* versionLabel = new QLabel(version, &dialog);
    versionLabel->setObjectName(QStringLiteral("aboutVersion"));
    titleRow->addWidget(versionLabel, 0, Qt::AlignVCenter);
    titleRow->addStretch(1);
    right->addLayout(titleRow);

    auto* body = new QLabel(&dialog);
    body->setObjectName(QStringLiteral("aboutBody"));
    body->setWordWrap(true);
    body->setTextFormat(Qt::RichText);
    // 每段一句、手动断行：中文在 QLabel 里可以任意字间折行，
    // 「运算放大器」被拆成两行很难看，不如自己控制排版
    body->setText(
        CSM_TR("Cross-platform schematic capture and circuit simulation.<br><br>"
               "<b>Solver</b>　modified nodal analysis (MNA) with Newton-Raphson iteration,<br>"
               "　　　　　supporting DC operating point, transient and AC small-signal sweep.<br>"
               "<b>Parts</b>　resistors, capacitors, inductors, transformers, diodes, transistors,<br>"
               "　　　　　MOSFETs, op-amps, logic gates, switches, sources, multimeters, probes, grounds."));
    right->addWidget(body);
    top->addLayout(right, 1);
    outer->addLayout(top);

    auto* foot = new QLabel(
        CSM_TR("Built with Qt6 / C++17 / CMake　·　GPL-3.0 free software, "
               "a replacement for Multisim on Linux"),
        &dialog);
    foot->setObjectName(QStringLiteral("aboutFoot"));
    foot->setWordWrap(true);
    outer->addWidget(foot);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    auto* changelogButton = new QPushButton(CSM_TR("Changelog"), &dialog);
    changelogButton->setAutoDefault(false);
    QObject::connect(changelogButton, &QPushButton::clicked, &dialog,
                     [&dialog] { Dialogs::changelog(&dialog); });
    buttonRow->addWidget(changelogButton);
    auto* ok = new QPushButton(CSM_TR("OK"), &dialog);
    ok->setDefault(true);
    QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonRow->addWidget(ok);
    outer->addLayout(buttonRow);

    dialog.setMinimumWidth(540);
    fixDialogSize(dialog);
    dialog.exec();
}

} // namespace csm
