#include "ui/WireItem.h"

#include "ui/Symbols.h"

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>

#include "ui/ComponentItem.h"
#include "ui/SchematicScene.h"

#include <cmath>
#include <QStyleOptionGraphicsItem>

namespace csm {

WireItem::WireItem(const QVector<QPointF>& points, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_points(points)
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setZValue(0.0);
    rebuild();
}

int WireItem::activeSegment() const
{
    return m_hasPress ? segmentAt(m_pressLocal) : -1;
}

int WireItem::segmentAt(const QPointF& itemPos) const
{
    int best = -1;
    double bestDist = 8.0; // 抓取半径
    for (int i = 1; i < m_points.size(); ++i) {
        const QPointF a = m_points.at(i - 1);
        const QPointF b = m_points.at(i);
        const QPointF ab = b - a;
        const double length2 = ab.x() * ab.x() + ab.y() * ab.y();
        if (length2 < 1e-9)
            continue;
        const QPointF ap = itemPos - a;
        double t = (ap.x() * ab.x() + ap.y() * ab.y()) / length2;
        t = std::max(0.0, std::min(1.0, t));
        const QLineF dist(a + ab * t, itemPos);
        if (dist.length() < bestDist) {
            bestDist = dist.length();
            best = i - 1;
        }
    }
    return best;
}

int WireItem::endpointAt(const QPointF& itemPos) const
{
    // 只有首尾两个端点可单独拖动；中间拐点跟着整条线走
    constexpr double kGrabRadius = 9.0;
    if (m_points.size() < 2)
        return -1;
    if (QLineF(itemPos, m_points.first()).length() <= kGrabRadius)
        return 0;
    if (QLineF(itemPos, m_points.last()).length() <= kGrabRadius)
        return m_points.size() - 1;
    return -1;
}

void WireItem::rebuild()
{
    prepareGeometryChange();

    QPainterPath path;
    if (!m_points.isEmpty()) {
        path.moveTo(m_points.first());
        for (int i = 1; i < m_points.size(); ++i)
            path.lineTo(m_points.at(i));
    }
    m_path = path;

    QPainterPathStroker stroker;
    stroker.setWidth(10.0);
    const QPainterPath hit = stroker.createStroke(m_path);
    m_bounds = hit.boundingRect().adjusted(-3.0, -3.0, 3.0, 3.0);
}

QVector<QPointF> WireItem::scenePoints() const
{
    QVector<QPointF> points;
    points.reserve(m_points.size());
    for (const QPointF& point : m_points)
        points.append(mapToScene(point));
    return points;
}

QVariant WireItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        const QPointF p = value.toPointF();
        return QPointF(std::round(p.x() / 10.0) * 10.0, std::round(p.y() / 10.0) * 10.0);
    }
    if (change == ItemSelectedHasChanged) {
        if (!value.toBool()) {
            m_hasPress = false;
            m_pressLocal = QPointF();
        }
        update();
    }
    if (change == ItemPositionHasChanged)
        emit wireChanged();
    return QGraphicsObject::itemChange(change, value);
}

QRectF WireItem::boundingRect() const
{
    return m_bounds;
}

QPainterPath WireItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(10.0);
    return stroker.createStroke(m_path);
}

void WireItem::setPoints(const QVector<QPointF>& points)
{
    m_points = points;
    rebuild();
    update();
}

void WireItem::setScenePoints(const QVector<QPointF>& points)
{
    QVector<QPointF> local;
    local.reserve(points.size());
    for (const QPointF& p : points)
        local.append(mapFromScene(p));
    setPoints(local);
}

/// 把折线整理成「只有横竖段」：遇到斜段就补一个直角拐点。
/// 拐点方向尽量延续上一段，避免走成楼梯。
static QVector<QPointF> withRightAngles(QVector<QPointF> points)
{
    if (points.size() < 2)
        return points;

    QVector<QPointF> out;
    out.append(points.first());
    for (int i = 1; i < points.size(); ++i) {
        const QPointF prev = out.last();
        const QPointF cur = points.at(i);
        const bool diagonal = std::abs(prev.x() - cur.x()) > 0.5
            && std::abs(prev.y() - cur.y()) > 0.5;
        if (diagonal) {
            const bool previousHorizontal = out.size() < 2
                || std::abs(out.at(out.size() - 2).y() - prev.y()) < 0.5;
            out.append(previousHorizontal ? QPointF(cur.x(), prev.y())
                                          : QPointF(prev.x(), cur.y()));
        }
        out.append(cur);
    }
    return out;
}

/// 两端固定、中间可以「整体挪一段」的直角走线：
/// 横着画的线变成 A ─┐ ┌─ B 这种 Z 形，竖着画的线同理。
static QVector<QPointF> routedSpan(const QPointF& a, const QPointF& b, const QPointF& mid)
{
    const bool horizontal = std::abs(a.y() - b.y()) < 0.5;
    const bool vertical = std::abs(a.x() - b.x()) < 0.5;
    if (horizontal) {
        const double y = mid.y();
        return {a, QPointF(a.x(), y), QPointF(b.x(), y), b};
    }
    if (vertical) {
        const double x = mid.x();
        return {a, QPointF(x, a.y()), QPointF(x, b.y()), b};
    }
    return withRightAngles({a, mid, b});
}

/// 某个场景点是否落在元件引脚上（判定为「已连接」，整体拖动时不能跟着走）
static bool onAnyPin(SchematicScene* scene, const QPointF& scenePoint)
{
    if (!scene)
        return false;
    for (ComponentItem* item : scene->componentItems()) {
        for (const QPointF& pin : item->pinScenePositions()) {
            if (QLineF(pin, scenePoint).length() <= 0.5)
                return true;
        }
    }
    return false;
}

void WireItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing, true);
    const bool selected = isSelected();
    // 点选某一段时只把这一段画成高亮色：整条线太长，全红会让人以为选错了
    const int activeSegment = segmentAt(m_pressLocal);
    const bool segmentOnly = selected && m_hasPress && activeSegment >= 0
        && activeSegment + 1 < m_points.size();
    QColor color = segmentOnly ? theme::wire : (selected ? theme::selected : theme::wire);
    QPen pen(color, selected && !segmentOnly ? 3.0 : (m_hover ? 3.0 : 2.2));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(m_path);

    if (segmentOnly) {
        QPen hot(theme::selected, 3.2);
        hot.setCapStyle(Qt::RoundCap);
        painter->setPen(hot);
        painter->drawLine(m_points.at(activeSegment), m_points.at(activeSegment + 1));
        painter->setPen(pen);
    }

    // 悬停/拖动端点时画个抓手，提示这里可以单独拖
    const int handle = m_dragIndex >= 0 ? m_dragIndex : m_hoverIndex;
    if (handle >= 0 && handle < m_points.size()) {
        const QPointF p = m_points.at(handle);
        painter->setPen(QPen(color, 1.6));
        painter->setBrush(Qt::white);
        painter->drawRect(QRectF(p.x() - 4.0, p.y() - 4.0, 8.0, 8.0));
    }
}

void WireItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    m_hover = true;
    m_hoverIndex = endpointAt(event->pos());
    update();
    QGraphicsObject::hoverEnterEvent(event);
}

void WireItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    m_hover = false;
    m_hoverIndex = -1;
    update();
    QGraphicsObject::hoverLeaveEvent(event);
}

void WireItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // 自己处理按下事件，就不会走基类的「先选中」流程，这里补上，
        // 否则导线点了不选中，也就删不掉
        m_pressLocal = event->pos();
        m_hasPress = true;
        if (!isSelected()) {
            if (!(event->modifiers() & Qt::ControlModifier)) {
                for (QGraphicsItem* other : scene()->selectedItems())
                    other->setSelected(false);
            }
            setSelected(true);
        }
        const int index = endpointAt(event->pos());
        if (index >= 0) {
            // 抓住端点：这一下不整体移动，改为拖这个顶点（可以主动拖开与引脚的连接）
            m_dragIndex = index;
            m_dragStartPos = event->scenePos();
            m_dragOrigin = m_points;
            event->accept();
            return;
        }

        // 多选时交给基类，让整组一起移动
        auto* sc = qobject_cast<SchematicScene*>(scene());
        const bool groupDrag = !sc || sc->selectedItems().size() > 1;
        if (!groupDrag) {
            // 整体拖动：连着引脚的那一端不能动，只让中间/自由顶点跟着走，
            // 于是导线被拉长或拉高，连接关系保持不变
            m_bodyDrag = true;
            m_bodyOrigin = m_points;
            m_bodyStart = event->scenePos();
            m_freeIndices.clear();
            const QVector<QPointF> scenePts = scenePoints();
            for (int i = 0; i < scenePts.size(); ++i) {
                if (!onAnyPin(sc, scenePts.at(i)))
                    m_freeIndices.append(i);
            }
            if (m_freeIndices.isEmpty()) {
                // 两端都接在引脚上：拖动时按直角走线在中间撑开一段
                m_spanRoute = m_points.size() == 2;
                if (!m_spanRoute)
                    m_bodyDrag = false; // 多边形且全连着：没有可动的顶点
            }
            if (m_bodyDrag) {
                event->accept();
                return;
            }
        }
    }
    QGraphicsObject::mousePressEvent(event);
}

void WireItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_bodyDrag) {
        const QPointF raw = event->scenePos() - m_bodyStart;
        // 位移吸附到网格，保证顶点落在格点上
        const QPointF delta(std::round(raw.x() / 10.0) * 10.0, std::round(raw.y() / 10.0) * 10.0);

        if (m_spanRoute && m_bodyOrigin.size() == 2) {
            // 两端固定：把中间那一段沿着 delta 平移出去，再按直角接回两端
            const QPointF a = m_bodyOrigin.at(0);
            const QPointF b = m_bodyOrigin.at(1);
            setPoints(routedSpan(a, b, (a + b) / 2.0 + delta));
            event->accept();
            return;
        }

        QVector<QPointF> points = m_bodyOrigin;
        for (int index : m_freeIndices) {
            if (index >= 0 && index < points.size())
                points[index] += delta;
        }
        setPoints(withRightAngles(points));
        event->accept();
        return;
    }

    if (m_dragIndex < 0) {
        // 没在拖端点，顺带更新悬停抓手
        const int hovered = endpointAt(event->pos());
        if (hovered != m_hoverIndex) {
            m_hoverIndex = hovered;
            update();
        }
        QGraphicsObject::mouseMoveEvent(event);
        return;
    }

    // 拖端点：吸附到网格，靠近引脚/其它导线顶点时贴上去
    QPointF target = event->scenePos();
    if (auto* sc = qobject_cast<SchematicScene*>(scene()))
        target = sc->snapToPin(target);

    QVector<QPointF> points = m_points;
    points[m_dragIndex] = mapFromScene(target);
    // 终点被拖到与相邻点重合时先保留，松手后由 normalizeWire 去掉重复点
    setPoints(points);
    event->accept();
}

void WireItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_bodyDrag) {
        m_bodyDrag = false;
        m_spanRoute = false;
        m_freeIndices.clear();

        // 只是点了一下、没拖动：不要把「几何变了」传出去，
        // 否则会触发导线整理重建，刚选中的线就丢了点击位置
        bool moved = m_points.size() != m_bodyOrigin.size();
        for (int i = 0; i < m_points.size() && !moved; ++i)
            moved = QLineF(m_points.at(i), m_bodyOrigin.at(i)).length() > 0.5;
        if (!moved) {
            event->accept();
            return;
        }

        QVector<QPointF> local;
        for (const QPointF& p : Circuit::normalizeWire(withRightAngles(scenePoints())))
            local.append(mapFromScene(p));
        setPoints(local);
        emit wireChanged();
        event->accept();
        return;
    }

    if (m_dragIndex >= 0) {
        m_dragIndex = -1;
        bool movedEnd = m_points.size() != m_dragOrigin.size();
        for (int i = 0; i < m_points.size() && !movedEnd; ++i)
            movedEnd = QLineF(m_points.at(i), m_dragOrigin.at(i)).length() > 0.5;
        if (!movedEnd) {
            event->accept();
            return;
        }
        // 端点被拖到与相邻点重合时会产生重复点，这里规整一次（注意用图元坐标）
        QVector<QPointF> local;
        local.reserve(m_points.size());
        for (const QPointF& p : Circuit::normalizeWire(scenePoints()))
            local.append(mapFromScene(p));
        setPoints(local);
        m_hoverIndex = endpointAt(event->pos());
        emit wireChanged(); // 拖动过程中不发信号，松手才记一次历史
        update();
        event->accept();
        return;
    }
    QGraphicsObject::mouseReleaseEvent(event);
}

void WireItem::orthogonalize()
{
    const QVector<QPointF> fixed = Circuit::normalizeWire(withRightAngles(m_points));
    if (fixed.size() == m_points.size()) {
        bool same = true;
        for (int i = 0; i < fixed.size() && same; ++i)
            same = QLineF(fixed.at(i), m_points.at(i)).length() < 0.5;
        if (same)
            return;
    }
    setPoints(fixed);
}

} // namespace csm
