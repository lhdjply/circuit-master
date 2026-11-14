#pragma once

#include "core/Circuit.h"

#include <QGraphicsScene>
#include <QHash>
#include <QPoint>

namespace csm {

class ComponentItem;
class WireItem;

/// 原理图画布：负责图元管理、网格、连接点（junction）标记与电路模型导出。
class SchematicScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit SchematicScene(QObject* parent = nullptr);

    void clearAll();
    void load(const Circuit& circuit);
    Circuit toCircuit() const;

    ComponentItem* addComponent(const Component& component);
    ComponentItem* placeComponent(ComponentType type, const QPointF& pos);
    WireItem* addWire(const QVector<QPointF>& points);

    QString nextRefdes(ComponentType type) const;
    QVector<ComponentItem*> componentItems() const;
    QVector<WireItem*> wireItems() const;

    void deleteSelection();
    void refresh();

    void setShowGrid(bool show);
    bool showGrid() const { return m_showGrid; }

    void setShowNodeNumbers(bool show);
    bool showNodeNumbers() const { return m_showNodeNumbers; }

    QPointF snap(const QPointF& point) const;
    QPointF snapToPin(const QPointF& point, double tolerance = 12.0) const;

signals:
    void circuitChanged();
    void editRequested(ComponentItem* item);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    void recomputeJunctions();
    void recomputeNodeNumbers();

    QVector<QPointF> m_junctions;
    QHash<QPoint, int> m_nodeNumbers;
    bool m_showGrid = true;
    bool m_showNodeNumbers = true;
};

} // namespace csm
