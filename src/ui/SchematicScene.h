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
    void commitEdit();
    /// 画线时用：新画的线如果和已有导线共线且首尾相接，就并成一条。
    /// 只作用于刚画出来的这根线 —— 已经存在工程里的独立导线不会被改变。
    void mergeWireWithNeighbors(WireItem* wire);
    void copySelection();
    void pasteClipboard();
    bool hasClipboard() const;
    void refresh();

    void setShowGrid(bool show);
    bool showGrid() const { return m_showGrid; }

    QPointF snap(const QPointF& point) const;
    QPointF snapToPin(const QPointF& point, double tolerance = 12.0) const;
    /// 元件移动后，把连在它引脚上的导线顶点一起带过去
    void followComponent(ComponentItem* item, const QPointF& delta);

signals:
    void circuitChanged();
    void editCommitted();
    void editRequested(ComponentItem* item);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    /// 只建图元、不发信号、不清理：给导线规范化时批量重建用
    WireItem* addWireRaw(const QVector<QPointF>& points);
    /// 给导线接上信号：几何改动后顺带做一次合并（合并推迟到事件循环，避免删掉正在发信号的对象）
    void hookWire(WireItem* item);
    /// 在 point 处把导线切成两段（point 落在段中间时先插入顶点）
    bool splitWireAt(WireItem* wire, const QPointF& point);
    void recomputeJunctions();
    QVector<QPointF> m_junctions;
    QVector<Component> m_clipboardComponents;
    QVector<Wire> m_clipboardWires;
    bool m_geometryDirty = false;  // 导线几何被改过，commitEdit 时值得整理一次
    bool m_showGrid = true;
};

} // namespace csm
