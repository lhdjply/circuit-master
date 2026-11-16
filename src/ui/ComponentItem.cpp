#include "ui/ComponentItem.h"

#include "ui/Symbols.h"

#include <QFontMetrics>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

#include <cmath>

namespace csm {

ComponentItem::ComponentItem(const Component& comp, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_comp(comp)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setZValue(1.0);
    setPos(comp.pos);
}

QRectF ComponentItem::boundingRect() const
{
    if (m_comp.type == ComponentType::Probe)
        return QRectF(-75.0, -60.0, 150.0, 120.0);
    if (m_comp.type == ComponentType::Ground)
        return QRectF(-40.0, -50.0, 80.0, 84.0);

    const int rot = ((m_comp.rotation % 360) + 360) % 360;
    const QRectF base = (rot == 90 || rot == 270) ? QRectF(-168.0, -42.0, 336.0, 84.0)
                                                  : QRectF(-86.0, -48.0, 172.0, 96.0);
    if (!m_blown)
        return base;
    return base.united(QRectF(-70.0, -125.0, 140.0, 150.0)); // 蘑菇云往上冒
}

QPainterPath ComponentItem::shape() const
{
    return symbolShape(m_comp.type);
}

void ComponentItem::setBlown(bool blown)
{
    if (m_blown == blown)
        return;
    prepareGeometryChange(); // 边界要跟着变，否则云会被裁掉
    m_blown = blown;
    if (!blown)
        m_blastProgress = 0.0;
    update();
}

void ComponentItem::setBlastProgress(double progress)
{
    const double clamped = std::max(0.0, std::min(progress, 1.0));
    if (qFuzzyCompare(clamped + 1.0, m_blastProgress + 1.0))
        return;
    m_blastProgress = clamped;
    update();
}

void ComponentItem::setLedBrightness(double brightness)
{
    const double clamped = std::max(0.0, std::min(brightness, 1.0));
    if (qFuzzyCompare(clamped + 1.0, m_ledBrightness + 1.0))
        return;
    m_ledBrightness = clamped;
    update();
}

void ComponentItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const bool selected = isSelected();

    // 击穿动画分两段：先「红温」把电容烧红，再突然炸开成蘑菇云
    constexpr double kHeatPhase = 0.45;
    const double blast = m_blown ? m_blastProgress : 0.0;
    double heat = 0.0;
    double cloud = 0.0;
    if (m_blown) {
        if (blast < kHeatPhase) {
            // 红温：亮度一路攀升，带一点抖动
            const double ramp = blast / kHeatPhase;
            heat = ramp * (0.82 + 0.18 * std::sin(blast * 90.0));
        } else {
            const double t = (blast - kHeatPhase) / (1.0 - kHeatPhase);
            heat = std::max(0.0, 1.0 - t / 0.18); // 炸开瞬间红温迅速退去
            cloud = t;
        }
    }

    // 红温：电容周围先烧出一圈红光
    if (heat > 0.01) {
        painter->save();
        painter->setPen(Qt::NoPen);
        const double radius = 24.0 + 12.0 * heat;
        QRadialGradient red(QPointF(0.0, 0.0), radius);
        red.setColorAt(0.0, QColor(255, 70, 30, int(190 * heat)));
        red.setColorAt(0.55, QColor(210, 40, 10, int(110 * heat)));
        red.setColorAt(1.0, QColor(140, 20, 0, 0));
        painter->setBrush(red);
        painter->drawEllipse(QPointF(0.0, 0.0), radius, radius);
        painter->restore();
    }

    // 点亮的 LED：先画一圈暖色光晕，符号再叠在上面
    if (m_comp.type == ComponentType::Led && m_ledBrightness > 0.01) {
        painter->save();
        painter->setPen(Qt::NoPen);
        const double radius = 26.0;
        QRadialGradient glow(QPointF(0.0, 0.0), radius);
        QColor core(255, 96, 40);
        core.setAlphaF(0.55 * m_ledBrightness);
        QColor edge(255, 140, 40);
        edge.setAlphaF(0.0);
        glow.setColorAt(0.0, core);
        glow.setColorAt(0.45, core);
        glow.setColorAt(1.0, edge);
        painter->setBrush(glow);
        painter->drawEllipse(QPointF(0.0, 0.0), radius, radius);
        painter->restore();
    }

    painter->save();
    // 屏幕坐标系镜像后再旋转：M = S · R
    painter->scale(m_comp.mirrorX ? -1.0 : 1.0, m_comp.mirrorY ? -1.0 : 1.0);
    painter->rotate(double(m_comp.rotation));
    paintSymbol(painter, m_comp, selected, true, m_comp.rotation, m_comp.mirrorX,
                m_comp.mirrorY, m_ledBrightness, heat);
    painter->restore();

    // 红温时给符号本身罩一层红光：用径向渐变，边缘化开，像被烧红而不是盖了个红框
    if (heat > 0.01) {
        painter->save();
        painter->setPen(Qt::NoPen);
        QRadialGradient tint(QPointF(0.0, 0.0), 30.0);
        tint.setColorAt(0.0, QColor(255, 105, 45, int(165 * heat)));
        tint.setColorAt(0.55, QColor(255, 70, 25, int(120 * heat)));
        tint.setColorAt(1.0, QColor(220, 40, 10, 0));
        painter->setBrush(tint);
        painter->drawEllipse(QPointF(0.0, 0.0), 30.0, 26.0);
        painter->restore();
    }

    if (m_comp.type == ComponentType::Ground) {
        QFont f = painter->font();
        f.setPointSizeF(8.5);
        f.setBold(false);
        painter->setFont(f);
        painter->setPen(theme::text);
        painter->drawText(QRectF(-30.0, 26.0, 60.0, 16.0), Qt::AlignHCenter | Qt::AlignTop,
                          QStringLiteral("GND"));
        return;
    }

    if (m_comp.type == ComponentType::Probe) {
        // 标签随旋转方向摆放，避免压在导线上
        const double rad = qDegreesToRadians(double(m_comp.rotation));
        const QPointF offset(40.0 * std::sin(rad), -40.0 * std::cos(rad));
        QFont f = painter->font();
        f.setPointSizeF(8.5);
        f.setBold(true);
        painter->setFont(f);
        painter->setPen(selected ? theme::selected : theme::text);
        painter->drawText(QRectF(offset.x() - 40.0, offset.y() - 9.0, 80.0, 18.0),
                          Qt::AlignCenter, m_comp.refdes);
        return;
    }

    if (cloud > 0.001) {
        // 蘑菇云：烟柱往上窜，云帽逐渐变大，之后化为灰烟留在画布上
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);

        const double k = cloud; // 爆炸段自己的进度
        const double rise = std::min(1.0, k * 1.35);
        const double capY = -16.0 - 46.0 * rise;
        const double capR = 9.0 + 19.0 * rise;
        // 淡入但不淡出：爆炸结束后云留在画布上，直到停止仿真才清除
        const int alpha = int(235.0 * std::min(1.0, k * 4.0));
        const double smoke = std::max(0.0, std::min(1.0, (k - 0.5) / 0.5)); // 火焰→烟

        // 烟柱
        const double halfBottom = 5.0 + 5.0 * rise;
        const double halfTop = 7.0 + 6.0 * rise;
        QPainterPath stem;
        stem.moveTo(-halfBottom, 10.0);
        stem.quadTo(-halfTop, (10.0 + capY) * 0.5, -halfTop, capY);
        stem.lineTo(halfTop, capY);
        stem.quadTo(halfTop, (10.0 + capY) * 0.5, halfBottom, 10.0);
        stem.closeSubpath();

        const auto mix = [smoke](int r0, int g0, int b0, int r1, int g1, int b1, int a) {
            return QColor(int(r0 + (r1 - r0) * smoke), int(g0 + (g1 - g0) * smoke),
                          int(b0 + (b1 - b0) * smoke), a);
        };
        QLinearGradient stemFill(QPointF(0.0, 10.0), QPointF(0.0, capY));
        stemFill.setColorAt(0.0, mix(140, 100, 80, 120, 110, 105, alpha / 3));
        stemFill.setColorAt(0.55, mix(210, 125, 60, 165, 150, 145, alpha * 2 / 3));
        stemFill.setColorAt(1.0, mix(255, 175, 75, 205, 190, 185, alpha));
        painter->setBrush(stemFill);
        painter->drawPath(stem);

        // 云帽
        QRadialGradient capFill(QPointF(0.0, capY), capR * 2.4);
        capFill.setColorAt(0.0, mix(255, 238, 175, 235, 226, 220, alpha));
        capFill.setColorAt(0.40, mix(255, 150, 50, 190, 176, 170, alpha));
        capFill.setColorAt(0.72, mix(150, 68, 40, 130, 118, 112, alpha * 3 / 4));
        capFill.setColorAt(1.0, mix(90, 78, 74, 80, 74, 72, 0));
        painter->setBrush(capFill);
        painter->drawEllipse(QPointF(0.0, capY), capR * 1.5, capR);
        painter->drawEllipse(QPointF(-capR * 1.05, capY + capR * 0.32), capR * 0.8, capR * 0.7);
        painter->drawEllipse(QPointF(capR * 1.05, capY + capR * 0.32), capR * 0.8, capR * 0.7);
        painter->drawEllipse(QPointF(0.0, capY - capR * 0.78), capR * 0.78, capR * 0.6);
        painter->restore();
    }

    // 参考编号与数值：竖直放置时放到两侧，避免压住符号
    const int rot = ((m_comp.rotation % 360) + 360) % 360;
    const bool vertical = (rot == 90 || rot == 270);

    QFont f = painter->font();
    f.setPointSizeF(8.5);
    f.setBold(true);
    painter->setFont(f);
    painter->setPen(selected ? theme::selected : theme::text);

    QRectF refRect;
    QRectF valueRect;
    int refAlign = Qt::AlignCenter;
    int valueAlign = Qt::AlignCenter;

    if (vertical) {
        refRect = QRectF(20.0, -9.0, 140.0, 18.0);
        valueRect = QRectF(-160.0, -9.0, 140.0, 18.0);
        refAlign = Qt::AlignLeft | Qt::AlignVCenter;
        valueAlign = Qt::AlignRight | Qt::AlignVCenter;
    } else {
        // 运放三角形较高，标签再向外让一点
        const bool opamp = m_comp.type == ComponentType::OpAmp;
        refRect = QRectF(-75.0, opamp ? -51.0 : -43.0, 150.0, 18.0);
        valueRect = QRectF(-80.0, opamp ? 33.0 : 25.0, 160.0, 18.0);
    }

    painter->drawText(refRect, refAlign, m_comp.refdes);

    f.setBold(false);
    painter->setFont(f);
    painter->setPen(theme::text);
    const QString value = m_comp.displayValue();
    const QFontMetrics fm(f);
    const QString elided = fm.elidedText(value, Qt::ElideRight, int(valueRect.width()));
    painter->drawText(valueRect, valueAlign, elided);
}

void ComponentItem::setComponent(const Component& comp)
{
    const bool rotationChanged = comp.rotation != m_comp.rotation;
    if (rotationChanged)
        prepareGeometryChange();
    m_comp = comp;
    if (pos() != comp.pos)
        setPos(comp.pos);
    update();
    emit componentChanged();
}

void ComponentItem::setRefdes(const QString& refdes)
{
    if (m_comp.refdes == refdes)
        return;
    m_comp.refdes = refdes;
    update();
    emit componentChanged();
}

void ComponentItem::setValue(double value)
{
    if (qFuzzyCompare(m_comp.value, value))
        return;
    m_comp.value = value;
    update();
    emit componentChanged();
}

void ComponentItem::setRotationAngle(int degrees)
{
    int normalized = ((degrees % 360) + 360) % 360;
    normalized = (normalized / 90) * 90;
    if (!supportsRotation(m_comp.type))
        normalized = 0;
    if (m_comp.rotation == normalized)
        return;
    prepareGeometryChange();
    m_comp.rotation = normalized;
    update();
    emit componentChanged();
}

void ComponentItem::setClosed(bool closed)
{
    if (m_comp.closed == closed)
        return;
    m_comp.closed = closed;
    update();
    emit componentChanged();
}

QVector<QPointF> ComponentItem::pinScenePositions() const
{
    return m_comp.pinPositions();
}

void ComponentItem::setMirrored(bool horizontal, bool vertical)
{
    if (m_comp.mirrorX == horizontal && m_comp.mirrorY == vertical)
        return;
    prepareGeometryChange();
    m_comp.mirrorX = horizontal;
    m_comp.mirrorY = vertical;
    update();
    emit componentChanged();
}

QVariant ComponentItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        const QPointF p = value.toPointF();
        return QPointF(std::round(p.x() / 10.0) * 10.0, std::round(p.y() / 10.0) * 10.0);
    }
    if (change == ItemPositionHasChanged) {
        m_comp.pos = pos();
        emit componentChanged();
    }
    return QGraphicsObject::itemChange(change, value);
}

void ComponentItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    emit editRequested();
    event->accept();
}

void ComponentItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    m_hover = true;
    setCursor(QCursor(Qt::PointingHandCursor));
    update();
    QGraphicsObject::hoverEnterEvent(event);
}

void ComponentItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    m_hover = false;
    unsetCursor();
    update();
    QGraphicsObject::hoverLeaveEvent(event);
}

} // namespace csm
