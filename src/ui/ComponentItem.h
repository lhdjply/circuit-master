#pragma once

#include "core/Component.h"

#include <QGraphicsObject>
#include <QStringList>

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

private:
    QRectF probePanelRect() const;
    void paintProbeReadout(QPainter* painter) const;
    void paintProbeArrow(QPainter* painter) const;

public:

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

    /// 测量探针的读数面板（每行一条，例如 "V(峰-峰): 3.42 V"）。
    /// 传空列表即隐藏面板。
    void setProbeReadout(const QStringList& lines);
    /// 测量探针的占位读数（横杠）。还没仿真、或某几项算不出来时用它，
    /// 保证黄框始终存在、行数固定。
    static QStringList placeholderProbeReadout();
    /// 测量探针是否落在导线上，以及它读到的是哪个元件的电流。
    /// 落线上时符号旁会画一个绿色箭头（和 Multisim 的探头一样）。
    void setProbeAttachment(bool onWire, const QPointF& arrowDir = QPointF(1.0, 0.0),
                            const QVector<QPair<int, double>>& currentWeights = {});
    /// 电流权重：导线在探针处切开后，一侧注入的元件电流。
    /// 值为 +1 表示该元件的 I(引脚1注入) 计入，-1 表示 I(引脚0注入)。
    /// 这样「两根支路汇合后的那一段」也能算对，而不是硬取某个元件。
    const QVector<QPair<int, double>>& probeCurrentWeights() const { return m_probeCurrentWeights; }
    bool probeOnWire() const { return m_probeOnWire; }
    int probeTargetId() const { return m_probeTargetId; }
    /// 箭头方向（沿导线）与电流符号：正号表示电流沿箭头方向为正
    QPointF probeArrowDir() const { return m_probeArrowDir; }
    QStringList probeReadout() const { return m_probeReadout; }
    double ledBrightness() const { return m_ledBrightness; }

    int rotationAngle() const { return m_comp.rotation; }
    bool mirroredX() const { return m_comp.mirrorX; }
    bool mirroredY() const { return m_comp.mirrorY; }
    QVector<QPointF> pinScenePositions() const;

    /// 标号 / 数值标签在 item 坐标里的可点击范围（拖动与命中测试用）
    /// 符号本体在场景坐标下的矩形（不含标号 / 数值标签）。
    /// 对齐、分布都按它算 —— 用 boundingRect() 的话，标号文字的长短会影响对齐结果
    QRectF symbolRect() const;
    QRectF refLabelRect() const;
    QRectF valueLabelRect() const;

signals:
    void componentChanged();
    /// 元件被拖动后的位移增量（场景坐标），场景据此带着连线一起走
    void movedBy(const QPointF& delta);
    /// 一次拖动开始 / 结束。场景在这两点之间锁定「哪些导线顶点要跟着走」——
    /// 若每步都重新判断，拖过一根线时它的顶点会被临时「认领」，看起来像把线吸过来。
    void moveStarted();
    void moveFinished();
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
    /// 测试点标号的锚点（随旋转方向摆，再叠加用户拖动出来的偏移）
    QPointF probeLabelAnchor() const;
    /// 点是否落在符号本体上（套用旋转/镜像变换）
    bool symbolContains(const QPointF& itemPos) const;

    enum class LabelDrag { None, Refdes, Value };

    Component m_comp;
    LabelDrag m_labelDrag = LabelDrag::None;
    QPointF m_labelPressPos;    // 按下时的 item 坐标
    QPointF m_labelStartOffset; // 按下时该标签的偏移
    bool m_hover = false;
    double m_ledBrightness = 0.0;
    bool m_blown = false;
    QStringList m_probeReadout;
    bool m_probeOnWire = false;
    int m_probeTargetId = -1;
    QPointF m_probeArrowDir{1.0, 0.0};
    QVector<QPair<int, double>> m_probeCurrentWeights;
    double m_blastProgress = 0.0;
    QPointF m_lastPos; // 上一次的位置，用来算增量
};

} // namespace csm
