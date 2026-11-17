#include "ui/MultimeterWindow.h"

#include "core/Component.h"

#include <QComboBox>
#include <QCloseEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace csm {

namespace {

// 七段码：a 顶、b 右上、c 右下、d 底、e 左下、f 左上、g 中
struct SegmentLayout {
    bool a = false, b = false, c = false, d = false, e = false, f = false, g = false;
};

SegmentLayout segmentsFor(QChar ch)
{
    SegmentLayout s;
    if (ch == QLatin1Char('-')) {
        s.g = true;
        return s;
    }
    switch (ch.unicode()) {
    case '0': s = {true, true, true, true, true, true, false}; break;
    case '1': s = {false, true, true, false, false, false, false}; break;
    case '2': s = {true, true, false, true, true, false, true}; break;
    case '3': s = {true, true, true, true, false, false, true}; break;
    case '4': s = {false, true, true, false, false, true, true}; break;
    case '5': s = {true, false, true, true, false, true, true}; break;
    case '6': s = {true, false, true, true, true, true, true}; break;
    case '7': s = {true, true, true, false, false, false, false}; break;
    case '8': s = {true, true, true, true, true, true, true}; break;
    case '9': s = {true, true, true, true, false, true, true}; break;
    default: break;
    }
    return s;
}

int fullDigitsOf(MultimeterDigits digits)
{
    switch (digits) {
    case MultimeterDigits::Half3: return 3;
    case MultimeterDigits::Half4: return 4;
    case MultimeterDigits::Half5: return 5;
    case MultimeterDigits::Half6: return 6;
    }
    return 4;
}

// 量程按「工程指数」表示：每 3 个数量级一档。
// 换挡必须带迟滞，否则读数在 1 V 附近轻微抖动时，会在
// 「999.9 mV」与「1.000 V」之间来回跳，根本看不清。
constexpr int kMinExp = -12; // p
constexpr int kMaxExp = 9;   // G

int expOfMagnitude(double magnitude)
{
    if (!(magnitude > 0.0) || !std::isfinite(magnitude))
        return 0;
    int e = int(std::floor(std::log10(magnitude) / 3.0)) * 3;
    return std::clamp(e, kMinExp, kMaxExp);
}

} // namespace

// ---------------------------------------------------------------- 七段码显示

SevenSegmentDisplay::SevenSegmentDisplay(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(240, 84);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void SevenSegmentDisplay::setValue(double value)
{
    m_value = value;
    updateRange(value);
    update();
}

void SevenSegmentDisplay::updateRange(double value)
{
    if (!std::isfinite(value)) {
        // 没读数：下次有值时重新选一个合适的量程
        m_hasRange = false;
        return;
    }
    const double magnitude = std::abs(value);
    if (!m_hasRange) {
        m_rangeExp = expOfMagnitude(magnitude);
        m_hasRange = true;
        return;
    }

    // 迟滞：尾数涨到 1000 才升档（1000 mV → 1 V），掉到 0.2 才降档。
    // 于是「0.2 ~ 1000」这一大段里当前量程保持不变 —— 读数在 1 V 附近抖动时
    // 不会在「999.9 mV」和「1.000 V」之间来回跳；同时最多只出现一位前导零
    // （0.2000），不会退化成「0.0012 mV」那种看不清的写法。
    double d = magnitude / std::pow(10.0, m_rangeExp);
    while (d >= 1000.0 && m_rangeExp < kMaxExp) {
        m_rangeExp += 3;
        d /= 1000.0;
    }
    while (d < 0.2 && m_rangeExp > kMinExp) {
        m_rangeExp -= 3;
        d *= 1000.0;
    }
}

QString SevenSegmentDisplay::displayedUnit() const
{
    if (!std::isfinite(m_value))
        return m_unit;
    const int exp = m_hasRange ? m_rangeExp : expOfMagnitude(std::abs(m_value));
    return prefixForExp(exp) + m_unit;
}

QString SevenSegmentDisplay::prefixForExp(int exp)
{
    switch (exp) {
    case -12: return QStringLiteral("p");
    case -9:  return QStringLiteral("n");
    case -6:  return QStringLiteral("µ");
    case -3:  return QStringLiteral("m");
    case 3:   return QStringLiteral("k");
    case 6:   return QStringLiteral("M");
    case 9:   return QStringLiteral("G");
    default:  return QString();
    }
}

void SevenSegmentDisplay::setDigits(MultimeterDigits digits)
{
    m_digits = digits;
    update();
}

void SevenSegmentDisplay::setUnit(const QString& unit)
{
    m_unit = unit;
    update();
}

void SevenSegmentDisplay::setSignSlot(bool enabled)
{
    m_signSlot = enabled;
    update();
}

SevenSegmentDisplay::Reading SevenSegmentDisplay::formatReading(double value) const
{
    Reading reading;
    if (!std::isfinite(value)) {
        // 无数据：一排横杠
        reading.digitsText = QStringLiteral("-").repeated(fullDigitsOf(m_digits));
        return reading;
    }

    reading.valid = true;
    reading.negative = value < 0.0;
    const double magnitude = std::abs(value);
    // 量程由 updateRange() 决定（带迟滞），这里只负责按量程排版
    const int exp = m_hasRange ? m_rangeExp : expOfMagnitude(magnitude);
    reading.prefix = prefixForExp(exp);
    const double mantissa = magnitude / std::pow(10.0, exp);

    // N½ 位：共 N+1 个数字位，最高位只能是 1（满量程 1.999…）
    const int total = fullDigitsOf(m_digits) + 1;
    const int intDigits = mantissa >= 1000.0 ? 4
        : mantissa >= 100.0                  ? 3
        : mantissa >= 10.0                   ? 2
                                             : 1;
    const int decimals = std::max(0, total - intDigits);
    // 按「数字位总数 = total」重排：QString::number 的字符串里小数点也算一位，
    // 直接截断会少一位数字；这里去掉点、截够 total 位数字、再按整数位数插回点
    QString raw = QString::number(mantissa, 'f', decimals);
    QString digitsOnly = raw;
    digitsOnly.remove(QLatin1Char('.'));
    if (digitsOnly.size() > total)
        digitsOnly = digitsOnly.left(total);
    QString out;
    for (int i = 0; i < digitsOnly.size(); ++i) {
        out.append(digitsOnly.at(i));
        if (i + 1 == intDigits && i + 1 < digitsOnly.size())
            out.append(QLatin1Char('.'));
    }
    reading.digitsText = out;
    return reading;
}

void SevenSegmentDisplay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 表壳：深色底 + 圆角边框
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x10, 0x18, 0x10));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);

    const Reading reading = formatReading(m_value);

    const QChar dot = QLatin1Char('.');
    // 直流档为负号预留一格；交流档与电阻档只有负数才画「−」
    const bool needSign = reading.valid && (m_signSlot || reading.negative);
    int cells = needSign ? 1 : 0;
    for (const QChar ch : reading.digitsText) {
        if (ch != dot)
            ++cells;
    }

    // 一位数字占 cellW 宽、数字本身净宽 digitW = 0.80 cellW（其余是字距），
    // 段粗 t = 0.12 cellW，数字高 1.28 cellW。
    // 横段左右各内缩 t/2、竖段贴数字外缘 —— 四个角的间隙因此完全对称。
    constexpr qreal kDigitWRatio = 0.80;
    constexpr qreal kThickRatio = 0.12;
    constexpr qreal kDigitHRatio = 1.28;

    const bool hasDot = reading.digitsText.contains(QLatin1Char('.'));
    const qreal wAvail = qreal(std::max(width() - 32, 60));
    const qreal hAvail = qreal(std::max(height() - 24, 40));
    const qreal totalUnits = cells + (hasDot ? 0.45 : 0.0);
    qreal cellW = wAvail / std::max(totalUnits, 1.0);
    cellW = std::min(cellW, hAvail / kDigitHRatio);

    const qreal digitW = cellW * kDigitWRatio;
    const qreal thickness = std::max(cellW * kThickRatio, 2.0);
    const qreal digitH = cellW * kDigitHRatio;
    const qreal rowW = cellW * cells + (hasDot ? 0.45 * cellW : 0.0);

    const QColor on(0x39, 0xff, 0x5c);  // 亮段（荧光绿）
    const QColor off(0x1c, 0x33, 0x1f); // 熄灭段（LCD 底影）
    const QPointF origin((width() - rowW) / 2.0, (height() - digitH) / 2.0);

    // 段坐标全部相对 pos 计算：漏掉 pos.y() 会让除顶段以外的段全挤在控件顶部
    // 段坐标全部相对 pos 计算：漏掉 pos.y() 会让除顶段以外的段全挤在控件顶部。
    // 横段 x ∈ [t/2, W−t/2]，竖段 x ∈ [0, t] 或 [W−t, W]，上下两半对称，
    // 所以四个角的缺口一致。
    // 每段画成两端带 45° 尖角的六边形（经典 LCD 段形）：相邻两段以「尖对尖」
    // 相接、各让出 gap，所以既不重叠、看起来又几乎连成一体 —— 用矩形的话
    // 想不重叠就必须大幅缩短，段会显得太短。
    auto hSeg = [&](qreal x0, qreal x1, qreal cy, qreal t) {
        const qreal c = t / 2.0;
        QPolygonF poly;
        poly << QPointF(x0 + c, cy - t / 2.0) << QPointF(x1 - c, cy - t / 2.0)
             << QPointF(x1, cy) << QPointF(x1 - c, cy + t / 2.0)
             << QPointF(x0 + c, cy + t / 2.0) << QPointF(x0, cy);
        return poly;
    };
    auto vSeg = [&](qreal cx, qreal y0, qreal y1, qreal t) {
        const qreal c = t / 2.0;
        QPolygonF poly;
        poly << QPointF(cx - t / 2.0, y0 + c) << QPointF(cx - t / 2.0, y1 - c)
             << QPointF(cx, y1) << QPointF(cx + t / 2.0, y1 - c)
             << QPointF(cx + t / 2.0, y0 + c) << QPointF(cx, y0);
        return poly;
    };

    auto drawDigit = [&](const QPointF& pos, const SegmentLayout& seg) {
        const qreal t = thickness;
        const qreal W = digitW;
        const qreal H = digitH;
        const qreal gap = t * 0.15;          // 尖角之间留的缝
        const qreal leftC = t / 2.0;         // 左竖段的中心线
        const qreal rightC = W - t / 2.0;    // 右竖段的中心线
        const qreal topC = t / 2.0;
        const qreal midC = H / 2.0;
        const qreal botC = H - t / 2.0;

        struct Shape {
            QPolygonF poly;
            bool lit;
        };
        const Shape shapes[] = {
            {hSeg(leftC + gap, rightC - gap, topC, t), seg.a},
            {hSeg(leftC + gap, rightC - gap, midC, t), seg.g},
            {hSeg(leftC + gap, rightC - gap, botC, t), seg.d},
            {vSeg(leftC, topC + gap, midC - gap, t), seg.f},
            {vSeg(rightC, topC + gap, midC - gap, t), seg.b},
            {vSeg(leftC, midC + gap, botC - gap, t), seg.e},
            {vSeg(rightC, midC + gap, botC - gap, t), seg.c},
        };

        painter.save();
        painter.translate(pos);
        painter.setPen(Qt::NoPen);
        // 先铺熄灭段、再盖点亮段：两者互不重叠，顺序其实无所谓，
        // 但这样即使以后几何再有交叠也不会出现暗色缺口
        for (int pass = 0; pass < 2; ++pass) {
            const bool wantLit = pass == 1;
            painter.setBrush(wantLit ? on : off);
            for (const Shape& sh : shapes) {
                if (sh.lit != wantLit)
                    continue;
                painter.drawPolygon(sh.poly);
            }
        }
        painter.restore();
    };

    // 负号单独画一条横杠。七段里没有「+」的形状，而真表也只在负数时显示「−」，
    // 所以正数这一格留空（位置仍然占着，位数不会跳）。
    auto drawMinus = [&](const QPointF& pos) {
        const qreal t = thickness;
        const qreal cx = pos.x() + digitW / 2.0;
        const qreal cy = pos.y() + digitH / 2.0;
        const qreal len = digitW * 0.62;
        painter.setPen(Qt::NoPen);
        painter.setBrush(on);
        painter.drawRoundedRect(QRectF(cx - len / 2.0, cy - t / 2.0, len, t), t / 2.0, t / 2.0);
    };

    qreal x = origin.x();
    const qreal y = origin.y();
    if (needSign) {
        if (reading.negative)
            drawMinus(QPointF(x, y));
        x += cellW;
    }
    for (const QChar ch : reading.digitsText) {
        if (ch == dot) {
            // 小数点：贴在当前位置的底部
            painter.setBrush(reading.valid ? on : off);
            const qreal r = thickness * 0.6;
            painter.drawEllipse(QPointF(x + r, y + digitH - r), r, r);
            x += cellW * 0.45;
            continue;
        }
        drawDigit(QPointF(x, y), segmentsFor(ch));
        x += cellW;
    }

    // 单位（工程前缀 + 基本单位），普通字体画在右下角
    const QString unitText = reading.prefix + m_unit;
    if (!unitText.isEmpty()) {
        painter.setPen(reading.valid ? on : off);
        QFont font = painter.font();
        font.setPointSizeF(11.0);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRectF(0.0, height() - 24.0, width() - 12.0, 20.0),
                         Qt::AlignRight | Qt::AlignVCenter, unitText);
    }
}

// ---------------------------------------------------------------- 读数窗口

MultimeterWindow::MultimeterWindow(const QString& refdes, MultimeterMode mode, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_refdes(refdes)
    , m_mode(mode)
{
    setWindowTitle(QStringLiteral("%1 — 万用表").arg(refdes));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(330, 150);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 10);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(refdes, this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(11.0);
    title->setFont(titleFont);
    header->addWidget(title);

    auto* modeLabel = new QLabel(
        QStringLiteral("%1 · %2").arg(multimeterModeName(mode),
                                      multimeterModeLetter(mode)),
        this);
    modeLabel->setStyleSheet(QStringLiteral("color:#64748b;"));
    header->addWidget(modeLabel);
    header->addStretch(1);

    auto* digitsLabel = new QLabel(QStringLiteral("位数"), this);
    digitsLabel->setStyleSheet(QStringLiteral("color:#64748b;"));
    header->addWidget(digitsLabel);
    auto* digits = new QComboBox(this);
    digits->addItem(QStringLiteral("3½"), int(MultimeterDigits::Half3));
    digits->addItem(QStringLiteral("4½"), int(MultimeterDigits::Half4));
    digits->addItem(QStringLiteral("5½"), int(MultimeterDigits::Half5));
    digits->addItem(QStringLiteral("6½"), int(MultimeterDigits::Half6));
    digits->setCurrentIndex(1);
    connect(digits, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, digits] {
                m_display->setDigits(
                    static_cast<MultimeterDigits>(digits->currentData().toInt()));
                setValue(m_lastValue); // 换位数后按当前值重画
            });
    header->addWidget(digits);
    root->addLayout(header);

    m_display = new SevenSegmentDisplay(this);
    m_display->setUnit(multimeterModeUnit(mode));
    // 直流电压 / 电流档可能是负的，为负号预留一格；
    // 交流档是有效值、电阻档是阻值，都非负，不需要这一格
    m_display->setSignSlot(mode == MultimeterMode::Voltage
                           || mode == MultimeterMode::Current);
    root->addWidget(m_display, 1);

    setValue(std::numeric_limits<double>::quiet_NaN());
}

void MultimeterWindow::setValue(double value)
{
    m_lastValue = value;
    m_display->setValue(value);
}

void MultimeterWindow::closeEvent(QCloseEvent* event)
{
    emit closed(this);
    QWidget::closeEvent(event);
}

} // namespace csm
