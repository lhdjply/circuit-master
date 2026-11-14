#pragma once

#include <QGraphicsObject>
#include <QPainterPath>

namespace csm {

/// 原理图导线（折线）。
class WireItem : public QGraphicsObject {
    Q_OBJECT
public:
    enum { Type = UserType + 101 };

    explicit WireItem(const QVector<QPointF>& points, QGraphicsItem* parent = nullptr);

    int type() const override { return Type; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    const QVector<QPointF>& points() const { return m_points; }
    void setPoints(const QVector<QPointF>& points);

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    void rebuild();

    QVector<QPointF> m_points;
    QPainterPath m_path;
    QRectF m_bounds;
    bool m_hover = false;
};

} // namespace csm
