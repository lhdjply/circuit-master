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
    enum class Tool { Select, Wire, Place };

    explicit SchematicView(SchematicScene* scene, QWidget* parent = nullptr);

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    void setPendingType(ComponentType type);
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

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void startWire(const QPointF& scenePos);
    void addWirePoint(const QPointF& scenePos);
    void updatePreview(const QPointF& scenePos);
    void finishWire();
    void updateCursorAndHint();

    SchematicScene* m_scene = nullptr;
    Tool m_tool = Tool::Select;
    ComponentType m_pendingType = ComponentType::Resistor;

    QVector<QPointF> m_wirePoints;
    QGraphicsPathItem* m_preview = nullptr;

    bool m_panning = false;
    QPoint m_panStart;
};

} // namespace csm
