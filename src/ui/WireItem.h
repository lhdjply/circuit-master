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
    QVector<QPointF> scenePoints() const;
    void setPoints(const QVector<QPointF>& points);
    /// 用场景坐标设置顶点（内部换算到图元坐标）
    void setScenePoints(const QVector<QPointF>& points);
    /// 把斜段整理成直角拐点（拖元件带走端点后调用，保持横平竖直）
    void orthogonalize();

    /// 端点拖拽：抓取半径内的首个/末个顶点会被单独拖动，其余位置整体平移
    int draggedEndpoint() const { return m_dragIndex; }

signals:
    void wireChanged();

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void rebuild();
    /// 事件位置落在哪个端点附近（0 = 首点，size-1 = 末点，-1 = 都不是）
    int endpointAt(const QPointF& itemPos) const;

    QVector<QPointF> m_points;
    QPainterPath m_path;
    QRectF m_bounds;
    bool m_hover = false;
    int m_dragIndex = -1;    // 正在拖的端点下标，-1 表示整体移动
    int m_hoverIndex = -1;   // 鼠标悬停的端点，用于画抓手
    QPointF m_dragStartPos;  // 按下时的场景坐标，用来区分点击与拖拽
    bool m_bodyDrag = false;      // 正在整体拖动（只动没有连到引脚的顶点）
    QPointF m_bodyStart;          // 整体拖动起点（场景坐标）
    QVector<QPointF> m_bodyOrigin; // 整体拖动开始时的顶点快照
    QVector<int> m_freeIndices;    // 整体拖动时允许移动的顶点下标
    bool m_spanRoute = false;      // 两端都钉在引脚上：按直角走线在中间撑开一段
};

} // namespace csm
