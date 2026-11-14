#pragma once

#include "core/Component.h"

#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <QSize>

class QPainter;

namespace csm {

namespace theme {
inline const QColor canvas(0xfb, 0xfc, 0xfe);
inline const QColor gridMinor(0xef, 0xf4, 0xfa);
inline const QColor gridMajor(0xd8, 0xe2, 0xef);
inline const QColor symbol(0x1e, 0x3a, 0x5f);
inline const QColor symbolFill(0xff, 0xff, 0xff);
inline const QColor wire(0x25, 0x63, 0xeb);
inline const QColor selected(0xea, 0x58, 0x0c);
inline const QColor text(0x33, 0x41, 0x55);
inline const QColor pin(0x64, 0x74, 0x8b);
inline const QColor junction(0x25, 0x63, 0xeb);
} // namespace theme

/// 元件的可点击区域（局部坐标）。
QPainterPath symbolShape(ComponentType type);

/// 在局部坐标系中绘制元件符号（调用者负责旋转/平移）。
void paintSymbol(QPainter* painter, const Component& component, bool selected, bool showPins,
                 int rotation = 0, bool mirrorX = false, bool mirrorY = false);

/// 加载内置 SVG 图标。
///
/// QIcon 读 SVG 依赖 Qt 的 iconengines 插件（libqsvgicon.so）。最小化安装的
/// 系统（CI runner、只装了运行时库的环境）没有这个插件，QIcon 会直接是空的，
/// 界面上一片空白。插件缺失时改用 QSvgRenderer 自己渲染若干尺寸兜底。
/// forceFallback 用于测试：跳过插件路径，直接验证兜底渲染。
QIcon svgIcon(const QString& path, bool forceFallback = false);

/// 元件库图标。
QIcon componentIcon(ComponentType type, const QSize& size = QSize(60, 42));

} // namespace csm
