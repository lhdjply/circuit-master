#include "ui/ComponentItem.h"

#include "ui/Symbols.h"

#include <QFontMetrics>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
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
    const int rot = ((m_comp.rotation % 360) + 360) % 360;
    if (rot == 90 || rot == 270)
        return QRectF(-168.0, -42.0, 336.0, 84.0);
    return QRectF(-86.0, -48.0, 172.0, 96.0);
}

QPainterPath ComponentItem::shape() const
{
    return symbolShape(m_comp.type);
}

void ComponentItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const bool selected = isSelected();

    painter->save();
    painter->rotate(double(m_comp.rotation));
    paintSymbol(painter, m_comp, selected, true);
    painter->restore();

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
    QVector<QPointF> out;
    const QVector<QPointF> offsets = pinOffsets(m_comp.type);
    out.reserve(offsets.size());
    for (const QPointF& off : offsets)
        out.append(mapToScene(off));
    return out;
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
