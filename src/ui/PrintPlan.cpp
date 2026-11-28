#include "ui/PrintPlan.h"

#include <algorithm>
#include <cmath>

namespace csm {

namespace {

/// 纸和图纸尺寸「基本一样」——像素取整会差十几个像素，不宽容就多切一页
bool nearlySame(double a, double b)
{
    return std::abs(a - b) < std::max(a, b) * 0.01 + 1.0;
}

int pageCountFor(double content, double page)
{
    return std::max(1, int(std::ceil(content / page - 1e-3)));
}

} // namespace

PrintPlan planPrint(const QSizeF& imagePx, const QSizeF& pagePx, const QSizeF& printablePx,
                    int scaleMode, double exactScale, double percent)
{
    PrintPlan plan;
    if (imagePx.isEmpty() || pagePx.isEmpty() || printablePx.isEmpty() || exactScale <= 0.0)
        return plan;

    if (scaleMode == 0) {
        // 适应图纸：铺满一页。比例按整页算 —— 图纸 = 纸张时正好 100%，
        // 和 1:1 得到同一张图
        plan.scale = std::min(pagePx.width() / imagePx.width(),
                              pagePx.height() / imagePx.height());
        if (std::abs(plan.scale / exactScale - 1.0) < 0.005)
            plan.scale = exactScale;
    } else {
        plan.scale = exactScale;
        if (scaleMode == 2)
            plan.scale *= percent / 100.0;
    }

    const QSizeF drawn(imagePx.width() * plan.scale, imagePx.height() * plan.scale);

    // 一页就装得下：画在页面左上（和 1:1 完全同一位置）
    if (nearlySame(drawn.width(), pagePx.width()) && nearlySame(drawn.height(), pagePx.height())) {
        plan.pages.append({QRectF(QPointF(0.0, 0.0), drawn), QRectF(QPointF(0.0, 0.0), imagePx)});
        return plan;
    }

    if (scaleMode == 0) {
        // 适应图纸但尺寸不像整页（例如 A5 图纸打 A4 纸）：居中放一页
        const QPointF topLeft((pagePx.width() - drawn.width()) / 2.0,
                              (pagePx.height() - drawn.height()) / 2.0);
        plan.pages.append({QRectF(topLeft, drawn), QRectF(QPointF(0.0, 0.0), imagePx)});
        return plan;
    }

    // 1:1 / 自定义且装不下：按可打印区切块（按整页切的话，每页左/上那圈
    // 落在打印机边距里，相邻页之间会少一条）
    const double stepW = printablePx.width();
    const double stepH = printablePx.height();
    const int columns = pageCountFor(drawn.width(), stepW);
    const int rows = pageCountFor(drawn.height(), stepH);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const QPointF offset(-column * stepW, -row * stepH);
            plan.pages.append({QRectF(offset, drawn), QRectF(QPointF(0.0, 0.0), imagePx)});
        }
    }
    return plan;
}

} // namespace csm
