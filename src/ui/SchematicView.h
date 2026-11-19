#pragma once

#include "core/Component.h"

#include <QGraphicsView>
#include <QPointF>
#include <QVector>

class QGraphicsPathItem;

namespace csm {

class SchematicScene;
class ComponentItem;

/// 原理图视图：工具切换、连线交互、缩放与平移。
class SchematicView : public QGraphicsView {
    Q_OBJECT
public:
    enum class Tool { Select, Wire };

    explicit SchematicView(SchematicScene* scene, QWidget* parent = nullptr);

    void setTool(Tool tool);
    /// 由主窗口在实时仿真启停时同步：决定点击开关是「拨动」还是「选中」
    void setLiveRunning(bool running) { m_liveRunning = running; }
    bool liveRunning() const { return m_liveRunning; }
    Tool tool() const { return m_tool; }

    void zoomBy(double factor);
    void zoomFit();
    void rotateSelection();
    void deleteSelection();
    void cancelWire();

signals:
    void scenePositionChanged(const QPointF& pos);
    void hintChanged(const QString& hint);
    void componentPlaced(ComponentItem* item);
    void scaleChanged(double scale);
    void toolChanged();
    void componentEditRequested(ComponentItem* item);
    /// 仿真运行中点了一下开关：由上层决定怎么改电路
    void switchToggled(ComponentItem* item);
    /// 仿真运行中按住 / 松开按钮开关
    void pushButtonPressed(ComponentItem* item);
    void pushButtonReleased(ComponentItem* item);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
#if defined(Q_OS_ANDROID)
    /// 双指捏合缩放（手机上唯一的缩放手段：没有滚轮）
    bool viewportEvent(QEvent* event) override;
#endif
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void startWire(const QPointF& scenePos);
    void addWirePoint(const QPointF& scenePos);
    void updatePreview(const QPointF& scenePos);
    void finishWire();
    void updateCursorAndHint();

    SchematicScene* m_scene = nullptr;
    Tool m_tool = Tool::Select;
    /// 实时仿真是否在跑。跑的时候左键点开关就是拨动它，不进入拖动
    bool m_liveRunning = false;
#if defined(Q_OS_ANDROID)
    /// 双指捏合：记住上一次的两指距离与中心，用来算缩放比和平移量
    bool m_pinching = false;
    double m_pinchDistance = 0.0;
    QPointF m_pinchCenter;
    /// 长按 = 右键：手机上只有手指，没有右键
    void startLongPress(const QPoint& pos);
    QTimer* m_longPressTimer = nullptr;
    QPoint m_longPressPos;
    bool m_longPressFired = false;
#endif
    /// 当前被按住的那个按钮开关（松开时用来还原）；同时只会有一个
    ComponentItem* m_pressedButton = nullptr;

    QVector<QPointF> m_wirePoints;
    QGraphicsPathItem* m_preview = nullptr;

    bool m_panning = false;
    QPoint m_panStart;
};

} // namespace csm
