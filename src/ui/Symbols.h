#pragma once

#include "core/Component.h"
#include "ui/Theme.h"

#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <QRectF>
#include <QSize>

class QPainter;

namespace csm {

// 颜色统一由 Theme 提供（浅色 / 深色两套），这里不再写死。

/// 测量探针读数框在符号局部坐标里的位置。
/// 它就是探针这个控件的样子 —— 画布上和元件库图标里都画同一个框，
/// 读数文字填在里面，不需要按场合分别判断。
QRectF probeReadoutBox();

/// 电压源里「不该跟着转」的部分：正弦 / 脉冲的波形图案、以及 + / − 极性标记。
/// 它们要画在未旋转的坐标系里 —— 跟着转的话，90° 时减号会变成一根竖线、
/// 波形也立起来（教科书和 Multisim 里这两样始终保持正立）。
/// 直流源的电池极板是跟着转的，仍在 paintSymbol 里画。
void paintVoltageSourceOverlay(QPainter* p, const Component& comp, const QColor& color);

/// 按旋转角把读数框摆到对应方向（0° 上、90° 右、180° 下、270° 左），
/// 框本身始终是正的 —— 所以旋转后框会挪位置，但不会歪。
QRectF probeReadoutBoxFor(int rotation);

/// 画读数框（淡黄底 + 琥珀边）。画布与元件库图标共用。
void paintProbeBox(QPainter* p, const QRectF& box);

/// 符号在局部坐标里占的范围（含测量探针的读数框）。
/// 元件库图标按它缩放，保证不同元件都完整、居中。
QRectF symbolBounds(const Component& comp);

/// 元件的可点击区域（局部坐标）。
QPainterPath symbolShape(ComponentType type);

/// 在局部坐标系中绘制元件符号（调用者负责旋转/平移）。
/// glow：发光二极管的亮度（0 = 灭，1 = 最亮），其它元件忽略
/// heat：0~1，把符号自身的颜色烧红（击穿前的红温效果）
void paintSymbol(QPainter* painter, const Component& component, bool selected, bool showPins,
                 int rotation = 0, bool mirrorX = false, bool mirrorY = false,
                 double glow = 0.0, double heat = 0.0);

/// 加载内置 SVG 图标。
///
/// QIcon 读 SVG 依赖 Qt 的 iconengines 插件（libqsvgicon.so）。最小化安装的
/// 系统（CI runner、只装了运行时库的环境）没有这个插件，QIcon 会直接是空的，
/// 界面上一片空白。插件缺失时改用 QSvgRenderer 自己渲染若干尺寸兜底。
/// forceFallback 用于测试：跳过插件路径，直接验证兜底渲染。
QIcon svgIcon(const QString& path, bool forceFallback = false);

/// 元件库图标。
QIcon componentIcon(ComponentType type, const QSize& size = QSize(84, 60));
/// 把 SVG 渲染成蒙版再刷一层颜色。仓库里的图标是深藏青（#1e3a5f），放到深色
/// 底上几乎看不见，所以按主题着色：浅色用 symbol（深藏青），深色用浅灰。
/// 调用方在切主题时要重新生成图标，见 MainWindow::applyTheme()。
QIcon themedSvgIcon(const QString& path, int size = 48,
                    const QColor& color = QColor());

/// 把任意单色图标刷成指定颜色
QIcon tintIcon(const QIcon& source, const QColor& color, int size = 48);

/// 深色底上用的图标：保留原有色相（蓝色描边、绿色播放），但把亮度抬到读得清
/// 的程度。中性色（深灰描边 / 浅灰填充）直接深浅对调 —— 直接把图标刷成单色会
/// 丢掉这些区分。
QIcon iconForDarkSurface(const QIcon& source, int size = 48);
/// 按一个具体元件画图标：用来区分「同类型但不同花样」的条目
/// （比如五种逻辑门、五种接地符号）
QIcon componentIcon(const Component& comp, const QSize& size = QSize(84, 60));

} // namespace csm
