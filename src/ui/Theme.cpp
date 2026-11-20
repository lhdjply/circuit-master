#include "ui/Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QPointer>
#include <QEvent>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QProxyStyle>
#include <QStyleOption>
#include <QVector>
#include <QStyleHints>
#include <QWidget>

namespace csm {
namespace Theme {

namespace {

/// 关掉禁用文字浮雕的 Fusion 样式
class AppStyle : public QProxyStyle
{
public:
    AppStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr, QStyleHintReturn* returnData = nullptr) const override
    {
        if (hint == SH_EtchDisabledText)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

Mode g_mode = Mode::System;
std::function<void()> g_changeHandler;
bool g_watchingScheme = false;
bool g_watchingSchemeHints = false;
bool g_schemeApplyPending = false;
bool g_systemDark = false;
bool g_systemDarkCached = false;

/// 登记过的控件 + 它们的样式生成器
QVector<QPair<QPointer<QWidget>, std::function<QString()>>>& styledWidgets()
{
    static QVector<QPair<QPointer<QWidget>, std::function<QString()>>> list;
    return list;
}

/// Qt 6.5 以下的兜底：直接问桌面环境。
/// 返回 -1 表示问不出来。
int desktopPrefersDark()
{
#ifdef Q_OS_LINUX
    // GNOME 42+ 以及遵循它的桌面（color-scheme 是 freedesktop 的事实标准）
    QProcess gsettings;
    gsettings.start(QStringLiteral("gsettings"),
                    {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                     QStringLiteral("color-scheme")});
    if (gsettings.waitForFinished(400)) {
        const QString out = QString::fromUtf8(gsettings.readAllStandardOutput()).trimmed();
        if (out.contains(QLatin1String("prefer-dark")))
            return 1;
        if (out.contains(QLatin1String("prefer-light")) || out.contains(QLatin1String("default")))
            return 0;
    }

    // KDE：kdeglobals 里的配色方案名字
    const QString kdePath = QStandardPaths::locate(
        QStandardPaths::GenericConfigLocation, QStringLiteral("kdeglobals"));
    if (!kdePath.isEmpty()) {
        QSettings kde(kdePath, QSettings::IniFormat);
        const QString scheme = kde.value(QStringLiteral("General/ColorScheme")).toString();
        if (!scheme.isEmpty())
            return scheme.contains(QLatin1String("dark"), Qt::CaseInsensitive) ? 1 : 0;
    }

    // GTK3 / GTK4
    for (const QString& dir : {QStringLiteral("gtk-3.0"), QStringLiteral("gtk-4.0")}) {
        const QString path = QStandardPaths::locate(
            QStandardPaths::GenericConfigLocation, dir + QStringLiteral("/settings.ini"));
        if (path.isEmpty())
            continue;
        QSettings gtk(path, QSettings::IniFormat);
        const QVariant prefer =
            gtk.value(QStringLiteral("Settings/gtk-application-prefer-dark-theme"));
        if (prefer.isValid())
            return prefer.toBool() ? 1 : 0;
    }
#endif
    return -1;
}

/// 启动时（我们还没装自己的调色板之前）系统给的调色板。
/// 有些平台插件根本不报 colorScheme（返回 Unknown），这时只能看它。
QPalette g_startupPalette;
bool g_startupPaletteSampled = false;

bool systemIsDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::styleHints()) {
        switch (QGuiApplication::styleHints()->colorScheme()) {
        case Qt::ColorScheme::Dark:
            return true;
        case Qt::ColorScheme::Light:
            return false;
        case Qt::ColorScheme::Unknown:
            break;
        }
    }
#endif
    // Qt 6.5 以下或平台不报配色：直接问桌面环境
    if (const int pref = desktopPrefersDark(); pref >= 0)
        return pref == 1;

    // 问不出来：退回「启动时系统调色板的明暗」
    const QPalette& palette = g_startupPaletteSampled ? g_startupPalette
                                                     : QGuiApplication::palette();
    return palette.color(QPalette::Window).lightness() < 128;
}

} // namespace

void load()
{
    g_systemDarkCached = false;
    const QString saved = QSettings().value(QStringLiteral("theme/mode"),
                                            QStringLiteral("system")).toString();
    if (saved == QLatin1String("light"))
        g_mode = Mode::Light;
    else if (saved == QLatin1String("dark"))
        g_mode = Mode::Dark;
    else
        g_mode = Mode::System;
}

Mode mode()
{
    return g_mode;
}

bool isDark()
{
    if (g_mode == Mode::Dark)
        return true;
    if (g_mode == Mode::Light)
        return false;
    // 跟随系统：判断一次要 fork 一个 gsettings（十几毫秒），而 isDark() 是每次
    // 绘制都会调的（取画布色、网格色…），所以必须缓存，只在主题变化时失效。
    if (!g_systemDarkCached) {
        g_systemDark = systemIsDark();
        g_systemDarkCached = true;
    }
    return g_systemDark;
}

void setChangeHandler(std::function<void()> handler)
{
    g_changeHandler = std::move(handler);
}

QStyle* appStyle()
{
    static QStyle* style = new AppStyle;
    return style;
}

void apply(QApplication& app)
{
    // 跟随系统模式：系统深浅色一变就重新应用一次。不挂这个监听的话，
    // 系统切到深色时只有画布会变（它绘制时读 isDark()），其余控件仍用旧调色板 ——
    // 结果就是「深色画布 + 浅色界面」。
    // 系统主题变了会发 QEvent::ThemeChange 给所有控件（Qt 6.0 就有），
    // 这条路径与 Qt 版本无关 —— 6.4 上也能跟着系统变。
    // 注意只认 ThemeChange，不能认 ApplicationPaletteChange：
    // 那是我们自己 setPalette() 触发的，会无限递归。
    if (!g_watchingScheme) {
        g_watchingScheme = true;
        class ThemeWatcher : public QObject
        {
        public:
            bool eventFilter(QObject* watched, QEvent* event) override
            {
                if (event->type() == QEvent::ThemeChange && g_mode == Mode::System
                    && !g_schemeApplyPending) {
                    g_schemeApplyPending = true;
                    // 一个主题变化会给每个控件各发一次，合并成一拍
                    QTimer::singleShot(0, qApp, [] {
                        g_schemeApplyPending = false;
                        g_systemDarkCached = false;   // 重新探测系统配色
                        if (g_mode != Mode::System)
                            return;
                        apply(*qApp);
                        if (g_changeHandler)
                            g_changeHandler();
                    });
                }
                return QObject::eventFilter(watched, event);
            }
        };
        qApp->installEventFilter(new ThemeWatcher);
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::styleHints() && !g_watchingSchemeHints) {
        g_watchingSchemeHints = true;
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                         [](Qt::ColorScheme) {
                             if (g_mode != Mode::System)
                                 return;
                             apply(*qApp);
                             if (g_changeHandler)
                                 g_changeHandler();
                         });
    }
#endif

    if (!g_startupPaletteSampled) {
        g_startupPalette = app.palette();
        g_startupPaletteSampled = true;
    }

    const bool dark = isDark();
    QPalette palette;
    if (dark) {
        palette.setColor(QPalette::Window, QColor(0x11, 0x18, 0x27));
        palette.setColor(QPalette::WindowText, QColor(0xe2, 0xe8, 0xf0));
        palette.setColor(QPalette::Base, QColor(0x0f, 0x17, 0x2a));
        palette.setColor(QPalette::AlternateBase, QColor(0x1e, 0x29, 0x3b));
        palette.setColor(QPalette::Text, QColor(0xe2, 0xe8, 0xf0));
        palette.setColor(QPalette::Button, QColor(0x1e, 0x29, 0x3b));
        palette.setColor(QPalette::ButtonText, QColor(0xe2, 0xe8, 0xf0));
        palette.setColor(QPalette::ToolTipBase, QColor(0x1e, 0x29, 0x3b));
        palette.setColor(QPalette::ToolTipText, QColor(0xe2, 0xe8, 0xf0));
        palette.setColor(QPalette::Highlight, QColor(0x3b, 0x82, 0xf6));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::PlaceholderText, QColor(0x94, 0xa3, 0xb8));
    } else {
        palette.setColor(QPalette::Window, QColor(0xf5, 0xf7, 0xfa));
        palette.setColor(QPalette::WindowText, QColor(0x1e, 0x29, 0x3b));
        palette.setColor(QPalette::Base, QColor(Qt::white));
        palette.setColor(QPalette::AlternateBase, QColor(0xf1, 0xf5, 0xf9));
        palette.setColor(QPalette::Text, QColor(0x1e, 0x29, 0x3b));
        palette.setColor(QPalette::Button, QColor(0xf1, 0xf5, 0xf9));
        palette.setColor(QPalette::ButtonText, QColor(0x1e, 0x29, 0x3b));
        palette.setColor(QPalette::ToolTipBase, QColor(Qt::white));
        palette.setColor(QPalette::ToolTipText, QColor(0x1e, 0x29, 0x3b));
        palette.setColor(QPalette::Highlight, QColor(0x25, 0x63, 0xeb));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::PlaceholderText, QColor(0x94, 0xa3, 0xb8));
    }

    // 禁用态：不写这一组的话，上面那些 setColor(role, ...) 已经把 Disabled 组
    // 也设成了正常文字色 —— 灰掉的「撤销 / 重做」看起来和可用的一模一样。
    const QColor disabledInk = dark ? QColor(0x5b, 0x6b, 0x82) : QColor(0xa3, 0xaf, 0xbf);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledInk);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledInk);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledInk);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, palette.color(QPalette::Active, QPalette::Highlight));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                     palette.color(QPalette::Active, QPalette::HighlightedText));

    app.setPalette(palette);

    // 光设 QApplication 的调色板不够：已经 polish 过的菜单栏 / 工具栏 / 标签栏
    // 不会自动重新取色，运行中切主题时它们会保持旧颜色（实测：深色启动正常，
    // 但浅色启动后切深色，顶部那一条仍是白的）。给每个顶层窗口再显式设一遍，
    // 子控件没有自己的调色板就会跟着走。
    for (QWidget* top : QApplication::topLevelWidgets()) {
        if (!top)
            continue;
        top->setPalette(palette);
        // 注意不要按 WA_SetPalette 过滤：setPalette() 自己会把这个属性打上，
        // 第二次切主题时过滤就会把上次设过的全跳过 —— 表现是深色能切过去、
        // 切回浅色却回不来。这里一律重设。
        const QList<QWidget*> children = top->findChildren<QWidget*>();
        for (QWidget* child : children)
            child->setPalette(palette);
    }

    // 显式设过样式表的控件不会跟着调色板走，这里把它们重建一遍
    auto& list = styledWidgets();
    for (int i = list.size() - 1; i >= 0; --i) {
        QWidget* widget = list.at(i).first;
        if (!widget) {
            list.removeAt(i);
            continue;
        }
        widget->setStyleSheet(list.at(i).second());
    }
}

void styleWidget(QWidget* widget, std::function<QString()> builder)
{
    if (!widget || !builder)
        return;
    styledWidgets().append({QPointer<QWidget>(widget), builder});
    widget->setStyleSheet(builder());
}

void save(Mode mode)
{
    g_mode = mode;
    g_systemDarkCached = false;
    QSettings settings;
    switch (mode) {
    case Mode::System: settings.setValue(QStringLiteral("theme/mode"), QStringLiteral("system")); break;
    case Mode::Light:  settings.setValue(QStringLiteral("theme/mode"), QStringLiteral("light")); break;
    case Mode::Dark:   settings.setValue(QStringLiteral("theme/mode"), QStringLiteral("dark")); break;
    }
}

// ---------------------------------------------------------------- 颜色表
// 深色不是把浅色反过来，而是重新定一套：底色压到近黑、文字提到浅灰、
// 强调色稍微提亮（深底上 #2563eb 偏暗）。

QColor canvas()      { return isDark() ? QColor(0x0d, 0x14, 0x22) : QColor(0xfb, 0xfc, 0xfe); }
QColor gridMinor()   { return isDark() ? QColor(0x1a, 0x23, 0x35) : QColor(0xef, 0xf4, 0xfa); }
QColor gridMajor()   { return isDark() ? QColor(0x27, 0x33, 0x48) : QColor(0xd8, 0xe2, 0xef); }
QColor symbol()      { return isDark() ? QColor(0xcb, 0xd5, 0xe1) : QColor(0x1e, 0x3a, 0x5f); }
QColor symbolFill()  { return isDark() ? QColor(0x16, 0x20, 0x33) : QColor(0xff, 0xff, 0xff); }
QColor wire()        { return isDark() ? QColor(0x60, 0xa5, 0xfa) : QColor(0x25, 0x63, 0xeb); }
QColor selected()    { return isDark() ? QColor(0xfb, 0x92, 0x3c) : QColor(0xea, 0x58, 0x0c); }
QColor pin()         { return isDark() ? QColor(0x94, 0xa3, 0xb8) : QColor(0x64, 0x74, 0x8b); }
QColor junction()    { return wire(); }

QColor surface()     { return isDark() ? QColor(0x16, 0x20, 0x33) : QColor(0xff, 0xff, 0xff); }
QColor surfaceAlt()  { return isDark() ? QColor(0x0f, 0x17, 0x2a) : QColor(0xf8, 0xfa, 0xfc); }
QColor border()      { return isDark() ? QColor(0x2a, 0x36, 0x4b) : QColor(0xe2, 0xe8, 0xf0); }
QColor textPrimary() { return isDark() ? QColor(0xe2, 0xe8, 0xf0) : QColor(0x1e, 0x29, 0x3b); }
QColor textSecondary() { return isDark() ? QColor(0xcb, 0xd5, 0xe1) : QColor(0x33, 0x41, 0x55); }
QColor textMuted()   { return isDark() ? QColor(0x94, 0xa3, 0xb8) : QColor(0x64, 0x74, 0x8b); }
QColor accent()      { return isDark() ? QColor(0x60, 0xa5, 0xfa) : QColor(0x25, 0x63, 0xeb); }
QColor accentStrong(){ return isDark() ? QColor(0x93, 0xc5, 0xfd) : QColor(0x1d, 0x4e, 0xd8); }
QColor danger()      { return isDark() ? QColor(0xf8, 0x71, 0x71) : QColor(0xdc, 0x26, 0x26); }

} // namespace Theme
} // namespace csm
