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
    /// 重新计算每个测量探针贴在哪个导线上、读哪个元件的电流。
    /// 放元件、拖元件、改导线之后都要调（场景自己会在这些时机调用）。
    void updateProbeAttachments();
    /// 拖动元件前锁定「哪些导线顶点跟着走」：只在真正接着引脚的那些顶点上生效，
    /// 拖动过程中经过的其它导线不会被顺手牵走。
    QVector<QPointF> m_dragAnchors;   // 本次拖动要带着走的导线顶点
    bool m_dragActive = false;        // 是否处在一次用户拖动中
    void moveAttachedWireVertices(QVector<QPointF>& anchors, const QPointF& delta);
    void beginComponentDrag();
    void endComponentDrag();
    ComponentItem* placeComponent(ComponentType type, const QPointF& pos);
    WireItem* addWire(const QVector<QPointF>& points);

    QString nextRefdes(ComponentType type) const;
    QVector<ComponentItem*> componentItems() const;
    /// 当前选中的元件（按图元顺序，不含导线）
    QVector<ComponentItem*> selectedComponents() const;
    QVector<WireItem*> wireItems() const;

    /// 对齐方式：多条选中元件按符号本体的外框对齐
    enum class AlignMode { Left, CenterH, Right, Top, CenterV, Bottom };
    /// 把选中的元件对齐（至少 2 个）。位移会被 10 px 网格吸附，
    /// 所以同尺寸的元件能完全对齐，尺寸不同时最多差半个网格
    void alignSelection(AlignMode mode);
    /// 等距分布：首尾不动，中间的按中心线平均分开（至少 3 个）
    void distributeSelection(Qt::Orientation orientation);

    void deleteSelection();
    void commitEdit();
    /// 把所有导线顶点吸到 10 px 网格（修早期版本粘贴留下的歪线）
    void snapWiresToGrid();
    /// 在指定点把交叉的导线切成一个连接点（画交点用）；返回是否有改动
    bool connectWiresAt(const QPointF& pos);
    /// 该点是否是导线交叉（可以打交点）
    bool hasCrossingAt(const QPointF& pos) const;
    /// 当前画出的交点（测试与导出用）
    QVector<QPointF> junctions() const { return m_junctions; }
    /// 画线时用：新画的线如果和已有导线共线且首尾相接，就并成一条。
    /// 只作用于刚画出来的这根线 —— 已经存在工程里的独立导线不会被改变。
    void mergeWireWithNeighbors(WireItem* wire);
    void copySelection();
    /// 剪切：先复制再删除选中项
    void cutSelection();
    void pasteClipboard();
    /// 记录鼠标在画布上的场景坐标，粘贴时把内容放到这里
    void setCursorScenePos(const QPointF& pos)
    {
        m_cursorScenePos = pos;
        m_hasCursorPos = true;
    }
    QPointF cursorScenePos() const { return m_cursorScenePos; }
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
    QPointF m_cursorScenePos;   // 最近的鼠标位置，粘贴时作为落点
    bool m_hasCursorPos = false;
    bool m_geometryDirty = false;  // 导线几何被改过，commitEdit 时值得整理一次
    bool m_showGrid = true;
};

} // namespace csm
