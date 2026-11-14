#include "ui/WireItem.h"

#include "ui/Symbols.h"

#include <QPainter>
#include <QPainterPathStroker>

#include <cmath>
#include <QStyleOptionGraphicsItem>

namespace csm {

WireItem::WireItem(const QVector<QPointF>& points, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_points(points)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setZValue(0.0);
    rebuild();
}

void WireItem::rebuild()
{
    prepareGeometryChange();

    QPainterPath path;
    if (!m_points.isEmpty()) {
        path.moveTo(m_points.first());
        for (int i = 1; i < m_points.size(); ++i)
            path.lineTo(m_points.at(i));
    }
    m_path = path;

    QPainterPathStroker stroker;
    stroker.setWidth(10.0);
    const QPainterPath hit = stroker.createStroke(m_path);
    m_bounds = hit.boundingRect().adjusted(-3.0, -3.0, 3.0, 3.0);
}

QVector<QPointF> WireItem::scenePoints() const
{
    QVector<QPointF> points;
    points.reserve(m_points.size());
    for (const QPointF& point : m_points)
        points.append(mapToScene(point));
    return points;
}

QVariant WireItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        const QPointF p = value.toPointF();
        return QPointF(std::round(p.x() / 10.0) * 10.0, std::round(p.y() / 10.0) * 10.0);
    }
    if (change == ItemPositionHasChanged)
        emit wireChanged();
    return QGraphicsObject::itemChange(change, value);
}

QRectF WireItem::boundingRect() const
{
    return m_bounds;
}

QPainterPath WireItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(10.0);
    return stroker.createStroke(m_path);
}

void WireItem::setPoints(const QVector<QPointF>& points)
{
    m_points = points;
    rebuild();
    update();
}

void WireItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing, true);
    const QColor color = isSelected() ? theme::selected : theme::wire;
    QPen pen(color, isSelected() ? 3.0 : (m_hover ? 3.0 : 2.2));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(m_path);
}

void WireItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    m_hover = true;
    update();
    QGraphicsObject::hoverEnterEvent(event);
}

void WireItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    m_hover = false;
    update();
    QGraphicsObject::hoverLeaveEvent(event);
}

} // namespace csm
