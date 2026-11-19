#include "ui/ComponentItem.h"

#include "ui/Symbols.h"

#include <QFontMetrics>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

#include <cmath>

namespace csm {

ComponentItem::ComponentItem(const Component& comp, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_comp(comp)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setZValue(1.0);
    setPos(comp.pos);
    m_lastPos = comp.pos;
}

QRectF ComponentItem::boundingRect() const
{
    if (m_comp.type == ComponentType::Probe) {
        // 符号本体 + 标号（标号会随旋转跑到四个方向，或被拖到别处）
        QRectF r(-20.0, -30.0, 40.0, 40.0);
        const QRectF label = refLabelRect();
        if (!label.isEmpty())
            r = r.united(label);
        return r.adjusted(-8.0, -8.0, 8.0, 8.0);
    }
    if (m_comp.type == ComponentType::Ground)
        return QRectF(-60.0, -60.0, 120.0, 120.0); // 四个方向都要放得下符号与文字
    if (m_comp.type == ComponentType::NetLabel) {
        // 旗标可能朝四个方向伸出（0/90/180/270），按最长的名字留一个正方形
        const qreal half = 22.0 + 9.0 * m_comp.netLabel.size();
        return QRectF(-half, -half, 2.0 * half, 2.0 * half);
    }

    // 三绕组变压器更高（绕组 y −40…40，标签在 −62 与 +50），单独给一个包围盒
    if (m_comp.type == ComponentType::Transformer
        && m_comp.transformerKind == TransformerKind::PrimaryAuxSecondary)
        return QRectF(-95.0, -84.0, 190.0, 156.0);

    const int rot = ((m_comp.rotation % 360) + 360) % 360;
    QRectF base = (rot == 90 || rot == 270) ? QRectF(-168.0, -42.0, 336.0, 84.0)
                                            : QRectF(-86.0, -48.0, 172.0, 96.0);
    // 标号 / 数值可以被拖到别处，包围盒要跟着放大，否则会被裁掉
    if (!m_comp.refdes.isEmpty())
        base = base.united(refLabelRect());
    if (!m_comp.displayValue().isEmpty())
        base = base.united(valueLabelRect());
    if (!m_blown)
        return base;
    return base.united(QRectF(-70.0, -125.0, 140.0, 150.0)); // 蘑菇云往上冒
}

QPainterPath ComponentItem::shape() const
{
    // 符号本体 + 两个标签的可点范围。
    //
    // 符号本体必须套上和 paint() 完全一样的变换：paint 里是
    //   scale(镜像) → rotate(角度) → paintSymbol(...)
    // 整块符号都被旋转过。这里只返回未旋转的 symbolShape() 的话，命中区还停在
    // 0° 的位置 —— 元件一转，可点范围就和画出来的图形错开了。测试点最明显：
    // 小圈画到 +x 方向去了，命中区还留在 -y 方向，差着三十多像素。
    QTransform symbolTransform;
    symbolTransform.scale(m_comp.mirrorX ? -1.0 : 1.0, m_comp.mirrorY ? -1.0 : 1.0);
    symbolTransform.rotate(double(m_comp.rotation));
    QPainterPath path = symbolTransform.map(symbolShape(m_comp.type));

    // 标号也并进命中区 —— 点「TP1」这种文字应当能选中元件。
    // 关键是 refLabelRect() 给的是「贴着文字」的小矩形，
    // 而不是画文字用的那个 80 px 宽的空盒子，否则符号周围一大片空白都能点中。
    const QRectF refRect = refLabelRect();
    if (!refRect.isEmpty()) {
        QPainterPath extra;
        extra.addRect(refRect);
        path = path.united(extra);
    }
    const QRectF valueRect = valueLabelRect();
    if (!valueRect.isEmpty()) {
        QPainterPath extra;
        extra.addRect(valueRect);
        path = path.united(extra);
    }
    return path;
}

void ComponentItem::setBlown(bool blown)
{
    if (m_blown == blown)
        return;
    prepareGeometryChange(); // 边界要跟着变，否则云会被裁掉
    m_blown = blown;
    if (!blown)
        m_blastProgress = 0.0;
    update();
}

void ComponentItem::setBlastProgress(double progress)
{
    const double clamped = std::max(0.0, std::min(progress, 1.0));
    if (qFuzzyCompare(clamped + 1.0, m_blastProgress + 1.0))
        return;
    m_blastProgress = clamped;
    update();
}

void ComponentItem::setLedBrightness(double brightness)
{
    const double clamped = std::max(0.0, std::min(brightness, 1.0));
    if (qFuzzyCompare(clamped + 1.0, m_ledBrightness + 1.0))
        return;
    m_ledBrightness = clamped;
    update();
}

void ComponentItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const bool selected = isSelected();

    // 击穿动画分两段：先「红温」把电容烧红，再突然炸开成蘑菇云
    constexpr double kHeatPhase = 0.45;
    const double blast = m_blown ? m_blastProgress : 0.0;
    double heat = 0.0;
    double cloud = 0.0;
    if (m_blown) {
        if (blast < kHeatPhase) {
            // 红温：亮度一路攀升，带一点抖动
            const double ramp = blast / kHeatPhase;
            heat = ramp * (0.82 + 0.18 * std::sin(blast * 90.0));
        } else {
            const double t = (blast - kHeatPhase) / (1.0 - kHeatPhase);
            heat = std::max(0.0, 1.0 - t / 0.18); // 炸开瞬间红温迅速退去
            cloud = t;
        }
    }

    // 红温：电容周围先烧出一圈红光
    if (heat > 0.01) {
        painter->save();
        painter->setPen(Qt::NoPen);
        const double radius = 24.0 + 12.0 * heat;
        QRadialGradient red(QPointF(0.0, 0.0), radius);
        red.setColorAt(0.0, QColor(255, 70, 30, int(190 * heat)));
        red.setColorAt(0.55, QColor(210, 40, 10, int(110 * heat)));
        red.setColorAt(1.0, QColor(140, 20, 0, 0));
        painter->setBrush(red);
        painter->drawEllipse(QPointF(0.0, 0.0), radius, radius);
        painter->restore();
    }

    // 点亮的 LED：先画一圈暖色光晕，符号再叠在上面
    if (m_comp.type == ComponentType::Led && m_ledBrightness > 0.01) {
        painter->save();
        painter->setPen(Qt::NoPen);
        const double radius = 26.0;
        QRadialGradient glow(QPointF(0.0, 0.0), radius);
        QColor core(255, 96, 40);
        core.setAlphaF(0.55 * m_ledBrightness);
        QColor edge(255, 140, 40);
        edge.setAlphaF(0.0);
        glow.setColorAt(0.0, core);
        glow.setColorAt(0.45, core);
        glow.setColorAt(1.0, edge);
        painter->setBrush(glow);
        painter->drawEllipse(QPointF(0.0, 0.0), radius, radius);
        painter->restore();
    }

    painter->save();
    // 屏幕坐标系镜像后再旋转：M = S · R
    painter->scale(m_comp.mirrorX ? -1.0 : 1.0, m_comp.mirrorY ? -1.0 : 1.0);
    painter->rotate(double(m_comp.rotation));
    paintSymbol(painter, m_comp, selected, true, m_comp.rotation, m_comp.mirrorX,
                m_comp.mirrorY, m_ledBrightness, heat);

    painter->restore();

    // 斜放时引脚会吸附到网格，符号画出的引脚端点可能差几像素，补一小段引线
    if (m_comp.rotation % 90 != 0) {
        painter->save();
        painter->setPen(QPen(theme::symbol, 1.6));
        for (int i = 0; i < pinOffsets(m_comp).size(); ++i) {
            const QPointF raw = m_comp.pinPositionRaw(i) - m_comp.pos;
            const QPointF snapped = m_comp.pinPosition(i) - m_comp.pos;
            if (QLineF(raw, snapped).length() > 0.5)
                painter->drawLine(raw, snapped);
        }
        painter->restore();
    }

    // 红温时给符号本身罩一层红光：用径向渐变，边缘化开，像被烧红而不是盖了个红框
    if (heat > 0.01) {
        painter->save();
        painter->setPen(Qt::NoPen);
        QRadialGradient tint(QPointF(0.0, 0.0), 30.0);
        tint.setColorAt(0.0, QColor(255, 105, 45, int(165 * heat)));
        tint.setColorAt(0.55, QColor(255, 70, 25, int(120 * heat)));
        tint.setColorAt(1.0, QColor(220, 40, 10, 0));
        painter->setBrush(tint);
        painter->drawEllipse(QPointF(0.0, 0.0), 30.0, 26.0);
        painter->restore();
    }

    if (m_comp.type == ComponentType::Ground) {
        // 文字摆在符号外侧，朝向跟着符号走：
        // 朝下/朝上 → 文字水平（正着写）；朝左/朝右 → 文字竖排（自下而上），
        // 这样既不会压在符号上，也不会倒着写
        const int rot = ((m_comp.rotation % 360) + 360) % 360;
        QPointF at(0.0, 34.0);
        double textRotation = 0.0;
        if (rot == 90) {
            at = QPointF(-34.0, 0.0);
            textRotation = -90.0;
        } else if (rot == 180) {
            at = QPointF(0.0, -34.0);
        } else if (rot == 270) {
            at = QPointF(34.0, 0.0);
            textRotation = -90.0;
        }

        QFont f = painter->font();
        f.setPointSizeF(8.5);
        f.setBold(false);
        painter->setFont(f);
        painter->setPen(theme::text);
        painter->save();
        painter->translate(at);
        painter->rotate(textRotation);
        painter->drawText(QRectF(-34.0, -8.0, 68.0, 16.0), Qt::AlignCenter,
                          groundKindName(m_comp.groundKind));
        painter->restore();
        return;
    }

    if (m_comp.type == ComponentType::Probe) {
        // 标签随旋转方向摆放，避免压在导线上；用户还能再拖开
        const QPointF offset = probeLabelAnchor();
        QFont f = painter->font();
        f.setPointSizeF(8.5);
        f.setBold(true);
        painter->setFont(f);
        painter->setPen(selected ? theme::selected : theme::text);
        painter->drawText(QRectF(offset.x() - 40.0, offset.y() - 9.0, 80.0, 18.0),
                          Qt::AlignCenter, m_comp.refdes);
        return;
    }

    if (cloud > 0.001) {
        // 蘑菇云：烟柱往上窜，云帽逐渐变大，之后化为灰烟留在画布上
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);

        const double k = cloud; // 爆炸段自己的进度
        const double rise = std::min(1.0, k * 1.35);
        const double capY = -16.0 - 46.0 * rise;
        const double capR = 9.0 + 19.0 * rise;
        // 淡入但不淡出：爆炸结束后云留在画布上，直到停止仿真才清除
        const int alpha = int(235.0 * std::min(1.0, k * 4.0));
        const double smoke = std::max(0.0, std::min(1.0, (k - 0.5) / 0.5)); // 火焰→烟

        // 烟柱
        const double halfBottom = 5.0 + 5.0 * rise;
        const double halfTop = 7.0 + 6.0 * rise;
        QPainterPath stem;
        stem.moveTo(-halfBottom, 10.0);
        stem.quadTo(-halfTop, (10.0 + capY) * 0.5, -halfTop, capY);
        stem.lineTo(halfTop, capY);
        stem.quadTo(halfTop, (10.0 + capY) * 0.5, halfBottom, 10.0);
        stem.closeSubpath();

        const auto mix = [smoke](int r0, int g0, int b0, int r1, int g1, int b1, int a) {
            return QColor(int(r0 + (r1 - r0) * smoke), int(g0 + (g1 - g0) * smoke),
                          int(b0 + (b1 - b0) * smoke), a);
        };
        QLinearGradient stemFill(QPointF(0.0, 10.0), QPointF(0.0, capY));
        stemFill.setColorAt(0.0, mix(140, 100, 80, 120, 110, 105, alpha / 3));
        stemFill.setColorAt(0.55, mix(210, 125, 60, 165, 150, 145, alpha * 2 / 3));
        stemFill.setColorAt(1.0, mix(255, 175, 75, 205, 190, 185, alpha));
        painter->setBrush(stemFill);
        painter->drawPath(stem);

        // 云帽
        QRadialGradient capFill(QPointF(0.0, capY), capR * 2.4);
        capFill.setColorAt(0.0, mix(255, 238, 175, 235, 226, 220, alpha));
        capFill.setColorAt(0.40, mix(255, 150, 50, 190, 176, 170, alpha));
        capFill.setColorAt(0.72, mix(150, 68, 40, 130, 118, 112, alpha * 3 / 4));
        capFill.setColorAt(1.0, mix(90, 78, 74, 80, 74, 72, 0));
        painter->setBrush(capFill);
        painter->drawEllipse(QPointF(0.0, capY), capR * 1.5, capR);
        painter->drawEllipse(QPointF(-capR * 1.05, capY + capR * 0.32), capR * 0.8, capR * 0.7);
        painter->drawEllipse(QPointF(capR * 1.05, capY + capR * 0.32), capR * 0.8, capR * 0.7);
        painter->drawEllipse(QPointF(0.0, capY - capR * 0.78), capR * 0.78, capR * 0.6);
        painter->restore();
    }

    // 网络标签的名字画在旗标里，不再另画 refdes / 数值
    if (m_comp.type == ComponentType::NetLabel)
        return;

    // 参考编号与数值：竖直放置时放到两侧，避免压住符号
    const int rot = ((m_comp.rotation % 360) + 360) % 360;
    const bool vertical = (rot == 90 || rot == 270);

    QFont f = painter->font();
    f.setPointSizeF(8.5);
    f.setBold(true);
    painter->setFont(f);
    painter->setPen(selected ? theme::selected : theme::text);

    Q_UNUSED(vertical);
    QRectF refRect;
    QRectF valueRect;
    int refAlign = Qt::AlignCenter;
    int valueAlign = Qt::AlignCenter;
    labelLayout(&refRect, &refAlign, &valueRect, &valueAlign);

    painter->drawText(refRect, refAlign, m_comp.refdes);

    f.setBold(false);
    painter->setFont(f);
    painter->setPen(theme::text);
    const QString value = m_comp.displayValue();
    const QFontMetrics fm(f);
    const QString elided = fm.elidedText(value, Qt::ElideRight, int(valueRect.width()));
    painter->drawText(valueRect, valueAlign, elided);
}

void ComponentItem::labelLayout(QRectF* refRect, int* refAlign, QRectF* valueRect,
                                int* valueAlign) const
{
    // 竖直放置时把标签放到两侧，避免压住符号
    const int rot = ((m_comp.rotation % 360) + 360) % 360;
    const bool vertical = (rot == 90 || rot == 270);
    if (vertical) {
        *refRect = QRectF(20.0, -9.0, 140.0, 18.0);
        *valueRect = QRectF(-160.0, -9.0, 140.0, 18.0);
        *refAlign = Qt::AlignLeft | Qt::AlignVCenter;
        *valueAlign = Qt::AlignRight | Qt::AlignVCenter;
    } else {
        // 运放三角形较高，标签再向外让一点；三绕组变压器更高，再往下让
        const bool opamp = m_comp.type == ComponentType::OpAmp;
        const bool tallTransformer = m_comp.type == ComponentType::Transformer
            && m_comp.transformerKind == TransformerKind::PrimaryAuxSecondary;
        *refRect = QRectF(-75.0, opamp ? -51.0 : (tallTransformer ? -76.0 : -43.0), 150.0, 18.0);
        *valueRect = QRectF(-80.0, opamp ? 33.0 : (tallTransformer ? 62.0 : 25.0), 160.0, 18.0);
        *refAlign = Qt::AlignCenter;
        *valueAlign = Qt::AlignCenter;
    }
    // 用户拖动出来的偏移
    refRect->translate(m_comp.refLabelOffset);
    valueRect->translate(m_comp.valueLabelOffset);
}

/// 把「对齐用的宽矩形」收成「文字实际占的那一块」，用于命中测试
static QRectF tightenLabelRect(const QRectF& base, int align, const QString& text, bool bold)
{
    QFont font;
    font.setPointSizeF(8.5);
    font.setBold(bold);
    const qreal w = QFontMetricsF(font).horizontalAdvance(text) + 10.0;
    QRectF r = base;
    if (align & Qt::AlignLeft) {
        r.setWidth(w);
    } else if (align & Qt::AlignRight) {
        r.setLeft(r.right() - w);
    } else {
        r = QRectF(base.center().x() - w / 2.0, base.top(), w, base.height());
    }
    return r;
}

bool ComponentItem::symbolContains(const QPointF& itemPos) const
{
    // 与 paint()/shape() 相同的变换：先镜像再旋转
    QTransform t;
    t.scale(m_comp.mirrorX ? -1.0 : 1.0, m_comp.mirrorY ? -1.0 : 1.0);
    t.rotate(double(m_comp.rotation));
    return t.map(symbolShape(m_comp.type)).contains(itemPos);
}

QPointF ComponentItem::probeLabelAnchor() const
{
    // 标签放到「和引脚相反」的方向：0° 在上、90° 在右、180° 在下、270° 在左
    const double rad = qDegreesToRadians(double(m_comp.rotation));
    return QPointF(40.0 * std::sin(rad), -40.0 * std::cos(rad)) + m_comp.refLabelOffset;
}

QRectF ComponentItem::symbolRect() const
{
    // 变换必须和 shape() / paint() 一致：先镜像再旋转
    QTransform symbolTransform;
    symbolTransform.scale(m_comp.mirrorX ? -1.0 : 1.0, m_comp.mirrorY ? -1.0 : 1.0);
    symbolTransform.rotate(double(m_comp.rotation));
    const QRectF local = symbolTransform.mapRect(symbolShape(m_comp.type).boundingRect());
    return mapRectToScene(local);
}

QRectF ComponentItem::refLabelRect() const
{
    if (m_comp.refdes.isEmpty())
        return QRectF();
    // 测试点的标号是单独画的（位置随旋转走），这里按同一套规则算出来，
    // 否则光点符号那根小棍儿根本点不中
    if (m_comp.type == ComponentType::Probe) {
        const QPointF at = probeLabelAnchor();
        // 画的时候文字是居中排在 80×18 的盒子里，命中区只取文字本身那么宽：
        // 否则 90° / 270° 时那个大盒子会盖住符号，按本体反倒拖走了标号
        QFont font;
        font.setPointSizeF(8.5);
        font.setBold(true);
        const qreal w = QFontMetricsF(font).horizontalAdvance(m_comp.refdes) + 10.0;
        return QRectF(at.x() - w / 2.0, at.y() - 9.0, w, 18.0);
    }
    QRectF refRect, valueRect;
    int refAlign = 0, valueAlign = 0;
    labelLayout(&refRect, &refAlign, &valueRect, &valueAlign);
    return tightenLabelRect(refRect, refAlign, m_comp.refdes, true);
}

QRectF ComponentItem::valueLabelRect() const
{
    const QString text = m_comp.displayValue();
    if (text.isEmpty())
        return QRectF();
    QRectF refRect, valueRect;
    int refAlign = 0, valueAlign = 0;
    labelLayout(&refRect, &refAlign, &valueRect, &valueAlign);
    return tightenLabelRect(valueRect, valueAlign, text, false);
}

void ComponentItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    // 按在标号 / 数值上时拖的是标签，不是元件本身
    if (event->button() == Qt::LeftButton) {
        const QPointF p = event->pos();
        // 压在符号本体上一律算拖元件；只有落在符号外面、又确实在文字上时才拖标签，
        // 免得旋转之后「拖本体」变成「拖位号」
        if (symbolContains(p)) {
            QGraphicsObject::mousePressEvent(event);
            return;
        }
        if (refLabelRect().contains(p)) {
            m_labelDrag = LabelDrag::Refdes;
            m_labelPressPos = p;
            m_labelStartOffset = m_comp.refLabelOffset;
            event->accept();
            return;
        }
        if (valueLabelRect().contains(p)) {
            m_labelDrag = LabelDrag::Value;
            m_labelPressPos = p;
            m_labelStartOffset = m_comp.valueLabelOffset;
            event->accept();
            return;
        }
    }
    QGraphicsObject::mousePressEvent(event);
}

void ComponentItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_labelDrag == LabelDrag::None) {
        QGraphicsObject::mouseMoveEvent(event);
        return;
    }
    const QPointF offset = m_labelStartOffset + (event->pos() - m_labelPressPos);
    if (m_labelDrag == LabelDrag::Refdes)
        m_comp.refLabelOffset = offset;
    else
        m_comp.valueLabelOffset = offset;

    prepareGeometryChange(); // 标签跑远了包围盒要跟着变大
    update();
    emit componentChanged();
    event->accept();
}

void ComponentItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_labelDrag != LabelDrag::None) {
        m_labelDrag = LabelDrag::None;
        event->accept();
        return;
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

void ComponentItem::setComponent(const Component& comp)
{
    const bool geometryChanged = comp.rotation != m_comp.rotation
        || comp.refLabelOffset != m_comp.refLabelOffset
        || comp.valueLabelOffset != m_comp.valueLabelOffset;
    if (geometryChanged)
        prepareGeometryChange();
    m_comp = comp;
    if (pos() != comp.pos)
        setPos(comp.pos);
    update();
    emit componentChanged();
}

void ComponentItem::setRefdes(const QString& refdes)
{
    if (m_comp.refdes == refdes)
        return;
    m_comp.refdes = refdes;
    update();
    emit componentChanged();
}

void ComponentItem::setValue(double value)
{
    if (qFuzzyCompare(m_comp.value, value))
        return;
    m_comp.value = value;
    update();
    emit componentChanged();
}

void ComponentItem::setRotationAngle(int degrees)
{
    // 允许任意角度（15°、45°…），不再吸附到 90° 的整数倍。
    // 引脚会跟着转；网表按 10 px 网格取整，所以只要导线端点落在这个网格点上就能连上。
    int normalized = ((degrees % 360) + 360) % 360;
    if (!supportsRotation(m_comp.type))
        normalized = 0;
    if (m_comp.rotation == normalized)
        return;
    prepareGeometryChange();
    m_comp.rotation = normalized;
    // 旋转会换掉标签的默认摆放位置（横放时在上/下，竖放时在左/右），
    // 之前拖出来的偏移就失去意义了，这里一并还原
    m_comp.refLabelOffset = QPointF();
    m_comp.valueLabelOffset = QPointF();
    update();
    emit componentChanged();
}

void ComponentItem::setClosed(bool closed)
{
    if (m_comp.closed == closed)
        return;
    m_comp.closed = closed;
    update();
    emit componentChanged();
}

QVector<QPointF> ComponentItem::pinScenePositions() const
{
    return m_comp.pinPositions();
}

void ComponentItem::resetLabelOffsets()
{
    if (m_comp.refLabelOffset.isNull() && m_comp.valueLabelOffset.isNull())
        return;
    prepareGeometryChange();
    m_comp.refLabelOffset = QPointF();
    m_comp.valueLabelOffset = QPointF();
    update();
    emit componentChanged();
}

void ComponentItem::setMirrored(bool horizontal, bool vertical)
{
    if (m_comp.mirrorX == horizontal && m_comp.mirrorY == vertical)
        return;
    prepareGeometryChange();
    m_comp.mirrorX = horizontal;
    m_comp.mirrorY = vertical;
    // 翻转同样会打乱标签的默认位置，一起还原
    m_comp.refLabelOffset = QPointF();
    m_comp.valueLabelOffset = QPointF();
    update();
    emit componentChanged();
}

QVariant ComponentItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        const QPointF p = value.toPointF();
        return QPointF(std::round(p.x() / 10.0) * 10.0, std::round(p.y() / 10.0) * 10.0);
    }
    if (change == ItemPositionHasChanged) {
        const QPointF delta = pos() - m_lastPos;
        m_lastPos = pos();
        m_comp.pos = pos();
        emit componentChanged();
        if (!qFuzzyIsNull(delta.x()) || !qFuzzyIsNull(delta.y()))
            emit movedBy(delta);
    }
    return QGraphicsObject::itemChange(change, value);
}

void ComponentItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    emit editRequested();
    event->accept();
}

void ComponentItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    m_hover = true;
    setCursor(QCursor(Qt::PointingHandCursor));
    update();
    QGraphicsObject::hoverEnterEvent(event);
}

void ComponentItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    m_hover = false;
    unsetCursor();
    update();
    QGraphicsObject::hoverLeaveEvent(event);
}

} // namespace csm
