#pragma once

// ---------------------------------------------------------------------------
//  打印分页方案：一张图纸要画到几页上、每页画哪一块
//
//  单独抽出来是为了能测：几何算错了（多切一页、页间漏一条、纸和图纸一样大时
//  两种比例不一致）在界面上只是「看着别扭」，光靠肉眼很难发现。
// ---------------------------------------------------------------------------

#include <QRectF>
#include <QVector>

namespace csm {

/// 一页怎么画：把位图的 src 区域画到页面的 dst 区域
struct PrintPageRect {
    QRectF dst;
    QRectF src;
};

struct PrintPlan {
    double scale = 1.0;
    QVector<PrintPageRect> pages;
};

/// 计算分页方案
///   imagePx     ：整张图纸的位图尺寸（按 300 dpi 渲染）
///   pagePx      ：整页的像素尺寸
///   printablePx ：可打印区的像素尺寸（不含打印机边距）
///   scaleMode   ：0 = 适应图纸，1 = 实际大小(1:1)，2 = 自定义百分比
///   exactScale  ：1:1 时「位图像素 → 设备像素」的系数（打印分辨率 / 300）
///   percent     ：自定义百分比（仅 scaleMode == 2 用）
PrintPlan planPrint(const QSizeF& imagePx, const QSizeF& pagePx, const QSizeF& printablePx,
                    int scaleMode, double exactScale, double percent = 100.0);

} // namespace csm
