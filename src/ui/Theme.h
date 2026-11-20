#pragma once

// ---------------------------------------------------------------------------
//  浅色 / 深色主题
//
//  颜色一律从这里取，不要在别处写死十六进制 —— 否则切主题时会漏。
//  mode 是用户在「视图 → 主题」里选的（跟随系统 / 浅色 / 深色），
//  存进 QSettings；isDark() 才是实际生效的那个（跟随系统时会看系统配色）。
// ---------------------------------------------------------------------------

#include <QColor>
#include <QString>

#include <functional>

class QApplication;
class QWidget;

class QStyle;
class QWidget;

namespace csm {
namespace Theme {

enum class Mode {
    System, ///< 跟随系统
    Light,
    Dark,
};

/// 启动时从 QSettings 读回来（没存过就用「跟随系统」）
void load();
/// 用户选择的模式（存盘的那个）
Mode mode();
void setMode(Mode mode);
/// 把模式存进 QSettings 并立即应用
void save(Mode mode);

/// 实际生效的是不是深色
bool isDark();

/// 应用样式：Fusion + 关掉禁用文字的浮雕。
/// Fusion 会给禁用态文字加一层浅色偏移阴影（SH_EtchDisabledText），浅底上看着
/// 像凹进去，深底上就变成明显的**重影** —— 所以这里直接关掉。
QStyle* appStyle();

/// 按当前模式给 QApplication 装调色板，并把登记过的控件样式重建一遍
/// （main 启动时、切换主题时都要调）
void apply(QApplication& app);

/// 登记一个「样式依赖主题色」的控件。切换主题时会重新调用 builder 生成样式。
/// 直接用 setStyleSheet(Theme::...) 的话，颜色在构造那一刻就固化了，
/// 运行中切主题不会变 —— 所以这里统一走登记。
void styleWidget(QWidget* widget, std::function<QString()> builder);

// ---- 画布与符号 ----
QColor canvas();
QColor gridMinor();
QColor gridMajor();
QColor symbol();
QColor symbolFill();
QColor wire();
QColor selected();
QColor pin();
QColor junction();

// ---- 界面 ----
QColor surface();       // 卡片 / 输入框底色
QColor surfaceAlt();    // 次级底色（消息区、波形区）
QColor border();
QColor textPrimary();
QColor textSecondary();
QColor textMuted();
QColor accent();
QColor accentStrong();
QColor danger();

} // namespace Theme
} // namespace csm
