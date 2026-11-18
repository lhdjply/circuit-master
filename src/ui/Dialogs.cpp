#include "ui/Dialogs.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
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
    return QStringLiteral(R"(
QDialog {
    background: #ffffff;
}
QLabel {
    color: #1e293b;
    font-size: 13px;
}
QPushButton {
    min-width: 56px;
    padding: 6px 12px;
    border: 1px solid #d5dde8;
    border-radius: 6px;
    background: #ffffff;
    color: #1e293b;
}
QPushButton:hover {
    background: #f1f5f9;
    border-color: #b6c2d1;
}
QPushButton:pressed {
    background: #e2e8f0;
}
QPushButton:default {
    background: #2563eb;
    border-color: #2563eb;
    color: #ffffff;
}
QPushButton:default:hover {
    background: #1d4ed8;
    border-color: #1d4ed8;
}
)");
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

    dialog.exec();

    if (result == QMessageBox::NoButton)
        result = QMessageBox::Cancel; // 直接关窗口（点 X）
    return result;
}

QMessageBox::StandardButton runInfo(QWidget* parent, const QString& title, const QString& text,
                                    const QPixmap& icon)
{
    return runMessage(parent, icon, title, text,
                      {{QMessageBox::Ok, QObject::tr("确定"), true}}, QMessageBox::Ok);
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
               {{QMessageBox::Ok, QObject::tr("知道了"), true}}, QMessageBox::Ok,
               460, 560);
}

QMessageBox::StandardButton Dialogs::askSaveDiscardCancel(QWidget* parent, const QString& title,
                                                          const QString& text)
{
    return runMessage(parent, badge(QStringLiteral("?"), kAccent), title, text,
                      {{QMessageBox::Save, QObject::tr("保存"), true},
                       {QMessageBox::Discard, QObject::tr("丢弃"), false},
                       {QMessageBox::Cancel, QObject::tr("取消"), false}},
                      QMessageBox::Save);
}

void Dialogs::about(QWidget* parent, const QString& version)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("关于电路仿真大师"));
    dialog.setModal(true);
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.setStyleSheet(dialogStyleSheet() + QStringLiteral(R"(
QLabel#aboutTitle {
    color: #1e293b;
    font-size: 18px;
    font-weight: 600;
}
QLabel#aboutVersion {
    color: #1d4ed8;
    background: #e0ebff;
    border-radius: 8px;
    padding: 2px 8px;
    font-size: 11px;
    font-weight: 600;
}
QLabel#aboutBody {
    color: #334155;
    font-size: 12.5px;
}
QLabel#aboutFoot {
    color: #94a3b8;
    font-size: 11.5px;
}
)"));

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
    auto* title = new QLabel(QObject::tr("电路仿真大师"), &dialog);
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
    body->setText(QStringLiteral(
        "跨平台的电路原理图与仿真软件。<br><br>"
        "<b>求解器</b>　改进节点法（MNA）+ 牛顿-拉夫逊迭代，<br>"
        "　　　　　　支持直流工作点、瞬态与交流小信号扫频。<br>"
        "<b>元件</b>　　电阻、电容、电感、变压器、二极管、三极管、<br>"
        "　　　　　　MOS、运放、开关、电源、万用表、测试点、接地。"));
    right->addWidget(body);
    top->addLayout(right, 1);
    outer->addLayout(top);

    auto* foot = new QLabel(
        QStringLiteral("基于 Qt6 / C++17 / CMake　·　自由软件，可作为 Multisim 在 Linux 上的替代方案"),
        &dialog);
    foot->setObjectName(QStringLiteral("aboutFoot"));
    foot->setWordWrap(true);
    outer->addWidget(foot);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    auto* ok = new QPushButton(QObject::tr("确定"), &dialog);
    ok->setDefault(true);
    QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonRow->addWidget(ok);
    outer->addLayout(buttonRow);

    dialog.setMinimumWidth(540);
    dialog.exec();
}

} // namespace csm
