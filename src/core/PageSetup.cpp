#include "core/PageSetup.h"

#include <algorithm>
#include <cmath>

namespace csm {

namespace {

struct PaperEntry {
    PaperSize size;
    double widthMm;   // 竖版
    double heightMm;
};

// ISO 216 的 A 系列（竖版，mm）
constexpr PaperEntry kPapers[] = {
    {PaperSize::A4, 210.0, 297.0},
    {PaperSize::A3, 297.0, 420.0},
    {PaperSize::A2, 420.0, 594.0},
    {PaperSize::A1, 594.0, 841.0},
};

QString tagFor(PaperSize size)
{
    switch (size) {
    case PaperSize::A4:     return QStringLiteral("A4");
    case PaperSize::A3:     return QStringLiteral("A3");
    case PaperSize::A2:     return QStringLiteral("A2");
    case PaperSize::A1:     return QStringLiteral("A1");
    case PaperSize::Custom: return QStringLiteral("custom");
    }
    return QStringLiteral("A4");
}

PaperSize sizeFromTag(const QString& tag)
{
    if (tag == QLatin1String("A3")) return PaperSize::A3;
    if (tag == QLatin1String("A2")) return PaperSize::A2;
    if (tag == QLatin1String("A1")) return PaperSize::A1;
    if (tag == QLatin1String("custom")) return PaperSize::Custom;
    return PaperSize::A4;
}

} // namespace

QString paperSizeName(PaperSize size)
{
    switch (size) {
    case PaperSize::A4:     return QStringLiteral("A4");
    case PaperSize::A3:     return QStringLiteral("A3");
    case PaperSize::A2:     return QStringLiteral("A2");
    case PaperSize::A1:     return QStringLiteral("A1");
    case PaperSize::Custom: return CSM_TR("Custom");
    }
    return QStringLiteral("A4");
}

double PageSetup::pixelsPerMm()
{
    return 96.0 / 25.4;   // ≈ 3.7795
}

QSizeF PageSetup::paperSizeMm(PaperSize size)
{
    for (const PaperEntry& entry : kPapers) {
        if (entry.size == size)
            return QSizeF(entry.widthMm, entry.heightMm);
    }
    return QSizeF();
}

QSizeF PageSetup::sizeMm() const
{
    double w = 0.0;
    double h = 0.0;
    if (size == PaperSize::Custom) {
        w = std::max(customWidthMm, 10.0);
        h = std::max(customHeightMm, 10.0);
    } else {
        const QSizeF paper = paperSizeMm(size);
        w = paper.width();
        h = paper.height();
    }
    // 横向就是把竖版对调；自定义尺寸也按同一规则处理，
    // 否则「选自定义 + 点横向」会毫无反应
    return landscape ? QSizeF(std::max(w, h), std::min(w, h))
                     : QSizeF(std::min(w, h), std::max(w, h));
}

QRectF PageSetup::sceneRect() const
{
    const QSizeF mm = sizeMm();
    const double k = pixelsPerMm();
    // 左上角钉在原点、往右下方铺：和常见画图软件一致，
    // 已有电路的坐标（大多在 100…1300 这个范围）也基本落在纸上
    return QRectF(0.0, 0.0, mm.width() * k, mm.height() * k);
}

QString PageSetup::summary() const
{
    const QSizeF mm = sizeMm();
    return CSM_TR("%1 %2 (%3 × %4 mm)")
        .arg(paperSizeName(size),
             landscape ? CSM_TR("landscape") : CSM_TR("portrait"))
        .arg(std::lround(mm.width()))
        .arg(std::lround(mm.height()));
}

PageSetup PageSetup::fittedTo(const QRectF& content, const PageSetup& base, double marginMm)
{
    if (content.isEmpty() || base.sceneRect().contains(content))
        return base;

    const double margin = marginMm * pixelsPerMm();
    const QRectF need = content.adjusted(-margin, -margin, margin, margin);

    for (bool landscape : {base.landscape, !base.landscape}) {
        for (PaperSize size : {PaperSize::A4, PaperSize::A3, PaperSize::A2, PaperSize::A1}) {
            PageSetup page = base;
            page.size = size;
            page.landscape = landscape;
            if (page.sceneRect().contains(need))
                return page;
        }
    }

    const double k = pixelsPerMm();
    PageSetup page = base;
    page.size = PaperSize::Custom;
    page.landscape = true;
    page.customWidthMm = std::ceil(std::max(need.width(), need.height()) / k);
    page.customHeightMm = std::ceil(std::min(need.width(), need.height()) / k);
    return page;
}

bool PageSetup::isDefault() const
{
    return size == PaperSize::A4 && landscape;
}

QJsonObject PageSetup::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("size"), tagFor(size));
    object.insert(QStringLiteral("landscape"), landscape);
    object.insert(QStringLiteral("widthMm"), customWidthMm);
    object.insert(QStringLiteral("heightMm"), customHeightMm);
    return object;
}

PageSetup PageSetup::fromJson(const QJsonObject& object)
{
    PageSetup page;
    page.size = sizeFromTag(object.value(QStringLiteral("size")).toString());
    page.landscape = object.value(QStringLiteral("landscape")).toBool(true);
    page.customWidthMm = object.value(QStringLiteral("widthMm")).toDouble(297.0);
    page.customHeightMm = object.value(QStringLiteral("heightMm")).toDouble(210.0);
    return page;
}

} // namespace csm
