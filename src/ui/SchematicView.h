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
    /// 提示文字里的按键取自真实快捷键 —— 用户改过设置后要跟着变
    void setKeyHints(const QString& rotate, const QString& run);
    /// 由主窗口在实时仿真启停时同步：决定点击开关是「拨动」还是「选中」
    void setLiveRunning(bool running) { m_liveRunning = running; }
    bool liveRunning() const { return m_liveRunning; }
    Tool tool() const { return m_tool; }

    void zoomBy(double factor);
    void zoomFit();
    /// 适应图纸：把整张纸（页面设置里的纸张）铺满视图
    void zoomFitPage();
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
    /// 右键菜单里选了「页面设置」：由主窗口弹对话框并写回场景
    void pageSetupRequested();
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
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    /// 在 pos（控件坐标）处弹出右键菜单：右键没拖动时也算一次普通点击
    void showContextMenuAt(const QPoint& pos);
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
    QString m_rotateKey;   // 旋转的按键文本，如「空格」
    QString m_runKey;      // 运行仿真的按键文本，如「F5」
    /// 实时仿真是否在跑。跑的时候左键点开关就是拨动它，不进入拖动
    bool m_liveRunning = false;
    /// 当前被按住的那个按钮开关（松开时用来还原）；同时只会有一个
    ComponentItem* m_pressedButton = nullptr;

    QVector<QPointF> m_wirePoints;
    QGraphicsPathItem* m_preview = nullptr;

    bool m_panning = false;
    QPoint m_panStart;
    /// 正在拖动的是哪个键（中键 / 右键）
    Qt::MouseButton m_panButton = Qt::NoButton;
    /// 右键按下时的位置与「是否真的拖动过」：没拖动就当成普通右键菜单
    QPoint m_rightPressPos;
    bool m_rightMoved = false;
    /// 右键按下时如果先来了 contextMenuEvent，先记下位置，松手时再弹
    QPoint m_pendingMenuPos;
    bool m_menuPending = false;
};

} // namespace csm
