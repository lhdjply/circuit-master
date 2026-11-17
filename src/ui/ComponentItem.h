#pragma once

#include "core/Component.h"

#include <QGraphicsObject>

namespace csm {

/// 原理图上的一个元件图元：负责绘制符号、捕捉网格、显示编号与数值。
class ComponentItem : public QGraphicsObject {
    Q_OBJECT
public:
    enum { Type = UserType + 100 };

    explicit ComponentItem(const Component& comp, QGraphicsItem* parent = nullptr);

    int type() const override { return Type; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    const Component& component() const { return m_comp; }
    void setComponent(const Component& comp);
    void setRefdes(const QString& refdes);
    void setValue(double value);
    void setRotationAngle(int degrees);
    /// 把标号 / 数值挪回默认位置（旋转、翻转时也会自动调用）
    void resetLabelOffsets();
    void setMirrored(bool horizontal, bool vertical);
    void setClosed(bool closed);
    /// 极性电容反接击穿：画爆炸特效，progress 0→1 是动画进度
    void setBlown(bool blown);
    bool isBlown() const { return m_blown; }
    void setBlastProgress(double progress);
    double blastProgress() const { return m_blastProgress; }

    /// 发光二极管的亮度（0 = 灭，1 = 最亮），仿真后由主窗口写入
    void setLedBrightness(double brightness);
    double ledBrightness() const { return m_ledBrightness; }

    int rotationAngle() const { return m_comp.rotation; }
    bool mirroredX() const { return m_comp.mirrorX; }
    bool mirroredY() const { return m_comp.mirrorY; }
    QVector<QPointF> pinScenePositions() const;

    /// 标号 / 数值标签在 item 坐标里的可点击范围（拖动与命中测试用）
    QRectF refLabelRect() const;
    QRectF valueLabelRect() const;

signals:
    void componentChanged();
    /// 元件被拖动后的位移增量（场景坐标），场景据此带着连线一起走
    void movedBy(const QPointF& delta);
    void editRequested();

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    /// 标签的默认摆放位置（已叠加用户拖动出来的偏移）
    void labelLayout(QRectF* refRect, int* refAlign, QRectF* valueRect, int* valueAlign) const;

    enum class LabelDrag { None, Refdes, Value };

    Component m_comp;
    LabelDrag m_labelDrag = LabelDrag::None;
    QPointF m_labelPressPos;    // 按下时的 item 坐标
    QPointF m_labelStartOffset; // 按下时该标签的偏移
    bool m_hover = false;
    double m_ledBrightness = 0.0;
    bool m_blown = false;
    double m_blastProgress = 0.0;
    QPointF m_lastPos; // 上一次的位置，用来算增量
};

} // namespace csm
