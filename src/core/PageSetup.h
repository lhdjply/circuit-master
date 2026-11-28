#pragma once

// ---------------------------------------------------------------------------
//  图纸设置（纸张大小与方向）
//
//  只描述「纸」本身：多大、横着还是竖着。画布坐标与毫米的换算也放在这里，
//  这样场景、页面设置对话框和存盘用的是同一套数字。
// ---------------------------------------------------------------------------

#include "core/Language.h"

#include <QJsonObject>
#include <QRectF>
#include <QSizeF>
#include <QString>

namespace csm {

enum class PaperSize {
    A5,
    A4,
    A3,
    A2,
    A1,
    Custom,
};

QString paperSizeName(PaperSize size);

struct PageSetup {
    PaperSize size = PaperSize::A4;
    /// 横向（宽 > 高）。默认横向 —— 原理图基本都是横着画的
    bool landscape = true;
    /// 自定义尺寸，**竖版**下的宽 × 高（mm）；切换方向时对调生效
    double customWidthMm = 297.0;
    double customHeightMm = 210.0;

    /// 纸张尺寸（mm），已按方向处理：横向时宽 > 高
    QSizeF sizeMm() const;
    /// 纸张的竖版基础尺寸（mm）：打印时用「竖版尺寸 + 明确方向」交给驱动，
    /// 直接塞横版尺寸的话驱动会当成竖版内容去转，出来是竖的
    QSizeF portraitSizeMm() const;
    /// 画布坐标下的纸张矩形，以原点为中心（老电路的坐标不用挪）
    QRectF sceneRect() const;
    /// 界面上显示的一行摘要，例如「A3 横向 (420 × 297 mm)」
    QString summary() const;

    /// 1 mm 对应多少画布单位。取 96 dpi，和屏幕上的物理尺寸对得上
    static double pixelsPerMm();
    /// 各规格**竖版**的原始尺寸（mm）；Custom 返回 customWidthMm × customHeightMm
    static QSizeF paperSizeMm(PaperSize size);

    /// 是不是「默认图纸」（A4 横向）
    bool isDefault() const;

    /// 返回一张「装得下 content」的纸：base 装得下就原样返回，否则依次试
    /// A4→A3→A2→A1（先横向再纵向），都装不下就用自定义尺寸兜底。
    static PageSetup fittedTo(const QRectF& content, const PageSetup& base,
                              double marginMm = 10.0);

    QJsonObject toJson() const;
    static PageSetup fromJson(const QJsonObject& object);
};

} // namespace csm
