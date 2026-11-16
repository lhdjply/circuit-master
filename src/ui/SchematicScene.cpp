#include "ui/SchematicScene.h"

#include "ui/ComponentItem.h"
#include "ui/Symbols.h"
#include "ui/WireItem.h"

#include <QGraphicsItem>
#include <QPointer>
#include <QTimer>
#include <QGraphicsSceneMouseEvent>
#include <QHash>
#include <QSet>
#include <QLineF>
#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace csm {

namespace {

bool strictlyInsideSegment(const QLineF& line, const QPointF& p)
{
    const QPointF d = line.p2() - line.p1();
    const QPointF q = p - line.p1();
    const double cross = d.x() * q.y() - d.y() * q.x();
    if (std::abs(cross) > 1e-6)
        return false;
    const double dot = d.x() * q.x() + d.y() * q.y();
    const double len2 = d.x() * d.x() + d.y() * d.y();
    return dot > 1e-6 && dot < len2 - 1e-6;
}

QPoint gridKey(const QPointF& p)
{
    return QPoint(int(std::lround(p.x() / 10.0)), int(std::lround(p.y() / 10.0)));
}

} // namespace

SchematicScene::SchematicScene(QObject* parent)
    : QGraphicsScene(parent)
{
    setSceneRect(-2000.0, -1500.0, 4000.0, 3000.0);
    setBackgroundBrush(Qt::NoBrush);
}

void SchematicScene::clearAll()
{
    clear();
    m_junctions.clear();
    update();
}

void SchematicScene::load(const Circuit& circuit)
{
    clearAll();

    QVector<Component> comps = circuit.components;
    std::sort(comps.begin(), comps.end(), [](const Component& a, const Component& b) {
        return a.id < b.id;
    });
    for (const Component& comp : comps)
        addComponent(comp);

    for (const Wire& wire : circuit.wires) {
        if (wire.points.size() >= 2)
            addWire(wire.points);
    }
    refresh();
}

Circuit SchematicScene::toCircuit() const
{
    Circuit circuit;
    QVector<ComponentItem*> comps = componentItems();
    std::sort(comps.begin(), comps.end(), [](ComponentItem* a, ComponentItem* b) {
        return a->component().id < b->component().id;
    });
    for (ComponentItem* item : comps)
        circuit.components.append(item->component());

    for (WireItem* wire : wireItems()) {
        Wire w;
        w.points = wire->scenePoints();
        if (w.points.size() >= 2)
            circuit.wires.append(w);
    }
    return circuit;
}

ComponentItem* SchematicScene::addComponent(const Component& component)
{
    Component comp = component;
    if (comp.id <= 0) {
        int maxId = 0;
        for (ComponentItem* existing : componentItems())
            maxId = std::max(maxId, existing->component().id);
        comp.id = maxId + 1;
    }
    auto* item = new ComponentItem(comp);
    connect(item, &ComponentItem::componentChanged, this, [this] {
        refresh();
        emit circuitChanged();
    });
    connect(item, &ComponentItem::editRequested, this, [this, item] { emit editRequested(item); });
    connect(item, &ComponentItem::movedBy, this,
            [this, item](const QPointF& delta) { followComponent(item, delta); });
    addItem(item);
    return item;
}

ComponentItem* SchematicScene::placeComponent(ComponentType type, const QPointF& pos)
{
    Component comp;
    comp.type = type;
    comp.pos = snap(pos);
    comp.refdes = nextRefdes(type);
    comp.rotation = 0;

    switch (type) {
    case ComponentType::Resistor:
        comp.value = 1000.0;
        break;
    case ComponentType::Capacitor:
        comp.value = 100e-9;
        break;
    case ComponentType::Inductor:
        comp.value = 10e-3;
        break;
    case ComponentType::VoltageSource:
        comp.function = SourceFunction::Constant;
        comp.dcValue = 5.0;
        break;
    case ComponentType::CurrentSource:
        comp.function = SourceFunction::Constant;
        comp.dcValue = 1e-3;
        break;
    case ComponentType::OpAmp:
        comp.value = 1e5;
        break;
    case ComponentType::Led:
        comp.vf = 2.0;
        break;
    case ComponentType::Npn:
    case ComponentType::Pnp:
        comp.beta = 100.0;
        break;
    case ComponentType::Nmos:
    case ComponentType::Pmos:
        comp.vth = 2.0;
        comp.k = 2e-3;
        break;
    case ComponentType::Igbt:
        comp.vth = 5.0;
        comp.k = 0.05;
        comp.vf = 0.7;
        break;
    case ComponentType::Optocoupler:
        // 默认按 LTV-817S-TA1-C：Vf 典型 1.2 V，CTR 档位 C 为 200~400%（取 300%）
        comp.vf = 1.2;
        comp.ctr = 3.0;
        break;
    case ComponentType::Switch:
        comp.closed = true;
        break;
    case ComponentType::Probe:
    case ComponentType::CurrentProbe:
    case ComponentType::Diode:
    case ComponentType::Ground:
        break;
    }

    ComponentItem* item = addComponent(comp);
    refresh();
    emit circuitChanged();
    commitEdit();
    return item;
}

WireItem* SchematicScene::addWire(const QVector<QPointF>& points)
{
    if (points.size() < 2)
        return nullptr;
    auto* item = new WireItem(Circuit::normalizeWire(points));
    hookWire(item);
    addItem(item);
    refresh();
    emit circuitChanged();
    return item;
}

QString SchematicScene::nextRefdes(ComponentType type) const
{
    const QString prefix = refPrefix(type);
    if (type == ComponentType::Ground)
        return QStringLiteral("GND");

    QSet<int> used;
    for (ComponentItem* item : componentItems()) {
        const QString ref = item->component().refdes;
        if (!ref.startsWith(prefix))
            continue;
        bool ok = false;
        const int n = ref.mid(prefix.size()).toInt(&ok);
        if (ok)
            used.insert(n);
    }
    int n = 1;
    while (used.contains(n))
        ++n;
    return prefix + QString::number(n);
}

QVector<ComponentItem*> SchematicScene::componentItems() const
{
    QVector<ComponentItem*> out;
    const QList<QGraphicsItem*> all = items();
    for (QGraphicsItem* item : all) {
        if (item->type() == ComponentItem::Type)
            out.append(static_cast<ComponentItem*>(item));
    }
    return out;
}

QVector<WireItem*> SchematicScene::wireItems() const
{
    QVector<WireItem*> out;
    const QList<QGraphicsItem*> all = items();
    for (QGraphicsItem* item : all) {
        if (item->type() == WireItem::Type)
            out.append(static_cast<WireItem*>(item));
    }
    return out;
}

void SchematicScene::deleteSelection()
{
    const QList<QGraphicsItem*> selected = selectedItems();
    if (selected.isEmpty())
        return;
    for (QGraphicsItem* item : selected) {
        if (item->type() != ComponentItem::Type && item->type() != WireItem::Type)
            continue;
        removeItem(item);
        delete item;
    }
    refresh();
    emit circuitChanged();
    commitEdit();
}

void SchematicScene::copySelection()
{
    m_clipboardComponents.clear();
    m_clipboardWires.clear();

    for (QGraphicsItem* item : selectedItems()) {
        if (item->type() == ComponentItem::Type) {
            m_clipboardComponents.append(static_cast<ComponentItem*>(item)->component());
        } else if (item->type() == WireItem::Type) {
            Wire wire;
            wire.points = static_cast<WireItem*>(item)->scenePoints();
            if (wire.points.size() >= 2)
                m_clipboardWires.append(wire);
        }
    }
}

bool SchematicScene::hasClipboard() const
{
    return !m_clipboardComponents.isEmpty() || !m_clipboardWires.isEmpty();
}

void SchematicScene::cutSelection()
{
    if (selectedItems().isEmpty())
        return;
    copySelection();
    deleteSelection();
    commitEdit();
}

void SchematicScene::pasteClipboard()
{
    if (!hasClipboard())
        return;

    // 落点：优先用鼠标位置（把复制内容的左上角对到这里），
    // 没有鼠标位置信息时退回原来的「右下偏移一点」
    QPointF offset(20.0, 20.0);
    if (m_hasCursorPos) {
        QRectF bounds;
        bool first = true;
        for (const Component& comp : m_clipboardComponents) {
            const QRectF box(comp.pos.x() - 30.0, comp.pos.y() - 30.0, 60.0, 60.0);
            bounds = first ? box : bounds.united(box);
            first = false;
        }
        for (const Wire& wire : m_clipboardWires) {
            for (const QPointF& p : wire.points) {
                const QRectF box(p.x(), p.y(), 0.0, 0.0);
                bounds = first ? box : bounds.united(box);
                first = false;
            }
        }
        if (!first) {
            // 让粘贴内容的中心落在鼠标处，单个元件就是元件本身对准光标
            const QPointF target = snap(m_cursorScenePos);
            offset = target - bounds.center();
        }
    }

    clearSelection();

    for (Component comp : m_clipboardComponents) {
        comp.id = 0; // 交给 addComponent 分配新 id
        comp.pos = snap(comp.pos + offset);
        comp.refdes = nextRefdes(comp.type);
        ComponentItem* item = addComponent(comp);
        item->setSelected(true);
    }

    for (Wire wire : m_clipboardWires) {
        for (QPointF& point : wire.points)
            point += offset;
        WireItem* item = addWire(wire.points);
        if (item)
            item->setSelected(true);
    }

    refresh();
    emit circuitChanged();
    commitEdit();
}

void SchematicScene::commitEdit()
{
    emit editCommitted();
}

void SchematicScene::hookWire(WireItem* item)
{
    connect(item, &WireItem::wireChanged, this, [this, item] {
        refresh();
        emit circuitChanged();
        // 拖动/改完几何后再整理一次：让共线相接的导线并起来。
        // 推迟到下个事件循环，因为合并会删掉当前正在发信号的图元。
        QPointer<WireItem> guard(item);
        QTimer::singleShot(0, this, [this, guard] {
            if (guard && guard->scene() == this)
                mergeWireWithNeighbors(guard.data());
        });
    });
}

WireItem* SchematicScene::addWireRaw(const QVector<QPointF>& points)
{
    auto* item = new WireItem(points);
    hookWire(item);
    addItem(item);
    return item;
}

bool SchematicScene::splitWireAt(WireItem* wire, const QPointF& point)
{
    QVector<QPointF> pts = wire->scenePoints();
    if (pts.size() < 2)
        return false;

    int index = -1;
    for (int i = 0; i < pts.size(); ++i) {
        if (QLineF(pts.at(i), point).length() <= 0.5) {
            index = i;
            break;
        }
    }
    // 落在某段中间：插一个顶点再切
    for (int i = 1; i < pts.size() && index < 0; ++i) {
        const QPointF a = pts.at(i - 1);
        const QPointF b = pts.at(i);
        const QPointF ab = b - a;
        const double length2 = ab.x() * ab.x() + ab.y() * ab.y();
        if (length2 < 1e-9)
            continue;
        const QPointF ap = point - a;
        const double t = (ap.x() * ab.x() + ap.y() * ab.y()) / length2;
        if (t <= 1e-6 || t >= 1.0 - 1e-6)
            continue;
        if (QLineF(a + ab * t, point).length() > 0.5)
            continue;
        pts.insert(i, point);
        index = i;
    }
    if (index <= 0 || index >= pts.size() - 1)
        return false; // 端点或不在线上

    QVector<QPointF> first(pts.begin(), pts.begin() + index + 1);
    QVector<QPointF> second(pts.begin() + index, pts.end());
    wire->setScenePoints(first);
    addWireRaw(second);
    return true;
}

void SchematicScene::mergeWireWithNeighbors(WireItem* wire)
{
    if (!wire)
        return;

    const auto keyOf = [](const QPointF& p) {
        return QStringLiteral("%1,%2").arg(qRound(p.x())).arg(qRound(p.y()));
    };

    // 引脚处是真正的连接点：不从它上面动刀
    QSet<QString> pinKeys;
    for (ComponentItem* comp : componentItems()) {
        for (const QPointF& pin : comp->pinScenePositions())
            pinKeys.insert(keyOf(pin));
    }

    // 点是否落在导线上（端点、顶点或某段中间）
    const auto liesOn = [&keyOf](const QVector<QPointF>& pts, const QPointF& point) {
        for (int i = 0; i < pts.size(); ++i) {
            if (QLineF(pts.at(i), point).length() <= 0.5)
                return true;
        }
        for (int i = 1; i < pts.size(); ++i) {
            const QPointF a = pts.at(i - 1);
            const QPointF b = pts.at(i);
            const QPointF ab = b - a;
            const double length2 = ab.x() * ab.x() + ab.y() * ab.y();
            if (length2 < 1e-9)
                continue;
            const QPointF ap = point - a;
            const double t = (ap.x() * ab.x() + ap.y() * ab.y()) / length2;
            if (t <= 1e-6 || t >= 1.0 - 1e-6)
                continue;
            if (QLineF(a + ab * t, point).length() <= 0.5)
                return true;
        }
        return false;
    };
    // 点是否在这根导线的「内部」（不是它能当端点的位置）
    const auto passesThrough = [&keyOf](const QVector<QPointF>& pts, const QPointF& point) {
        if (pts.size() < 2)
            return false;
        if (QLineF(pts.first(), point).length() <= 0.5
            || QLineF(pts.last(), point).length() <= 0.5)
            return false;
        return true;
    };

    // 本次涉及的所有连接点：新线的每个顶点
    const QVector<QPointF> joints = wire->scenePoints();

    for (const QPointF& joint : joints) {
        if (pinKeys.contains(keyOf(joint)))
            continue;

        // 该点得有别的导线参与，否则不用管
        bool shared = false;
        for (WireItem* other : wireItems()) {
            if (other == wire || !other->scene())
                continue;
            if (liesOn(other->scenePoints(), joint)) {
                shared = true;
                break;
            }
        }
        if (!shared)
            continue;

        // 1) 把「穿过」这个点的导线都切开，让每根线在这里都有端点
        const QVector<WireItem*> snapshot = wireItems();
        for (WireItem* candidate : snapshot) {
            if (!candidate->scene())
                continue;
            const QVector<QPointF> pts = candidate->scenePoints();
            if (!liesOn(pts, joint) || !passesThrough(pts, joint))
                continue;
            splitWireAt(candidate, joint);
        }

        // 2) 在这个点上反复合并「共线」的一对，直到没有可并的
        bool merged = true;
        int guard = 0;
        while (merged && guard++ < 32) {
            merged = false;
            QList<QPair<WireItem*, bool>> ends;
            for (WireItem* other : wireItems()) {
                const QVector<QPointF> pts = other->scenePoints();
                if (pts.size() < 2)
                    continue;
                const bool front = QLineF(pts.first(), joint).length() <= 0.5;
                const bool back = QLineF(pts.last(), joint).length() <= 0.5;
                if (front)
                    ends.append({other, true});
                else if (back)
                    ends.append({other, false});
            }

            for (int i = 0; i < ends.size() && !merged; ++i) {
                for (int j = i + 1; j < ends.size() && !merged; ++j) {
                    WireItem* wa = ends.at(i).first;
                    WireItem* wb = ends.at(j).first;
                    if (wa == wb)
                        continue;

                    QVector<QPointF> sa = wa->scenePoints();
                    QVector<QPointF> sb = wb->scenePoints();
                    if (ends.at(i).second)
                        std::reverse(sa.begin(), sa.end());
                    if (!ends.at(j).second)
                        std::reverse(sb.begin(), sb.end());
                    if (sa.size() < 2 || sb.size() < 2)
                        continue;

                    const QPointF d1 = sa.at(sa.size() - 1) - sa.at(sa.size() - 2);
                    const QPointF d2 = sb.at(1) - sb.at(0);
                    const double cross = d1.x() * d2.y() - d1.y() * d2.x();
                    const bool collinear = std::abs(cross) < 1e-6
                        && (d1.x() * d2.x() + d1.y() * d2.y()) > 0.0;
                    // 共线（直通）总并；只剩两段相接（拐角）也并成折线；
                    // 还有支路时只并共线的那一对，支路保持独立
                    if (!collinear && ends.size() > 2)
                        continue;

                    const QPointF keepStart = wb->scenePoints().first();
                    QVector<QPointF> combined = sa;
                    for (int k = 1; k < sb.size(); ++k)
                        combined.append(sb.at(k));

                    removeItem(wa);
                    wa->deleteLater();
                    QVector<QPointF> mergedPts = Circuit::normalizeWire(combined);
                    if (!mergedPts.isEmpty()
                        && QLineF(mergedPts.first(), keepStart).length() > 0.5
                        && QLineF(mergedPts.last(), keepStart).length() <= 0.5) {
                        std::reverse(mergedPts.begin(), mergedPts.end());
                    }
                    wb->setScenePoints(mergedPts);
                    merged = true;
                }
            }
        }
    }

    refresh();
    emit circuitChanged();
}



void SchematicScene::setShowGrid(bool show)
{
    if (m_showGrid == show)
        return;
    m_showGrid = show;
    update();
}

QPointF SchematicScene::snap(const QPointF& point) const
{
    return QPointF(std::round(point.x() / 10.0) * 10.0, std::round(point.y() / 10.0) * 10.0);
}

void SchematicScene::followComponent(ComponentItem* item, const QPointF& delta)
{
    if (!item || (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y())))
        return;

    // 引脚现在的坐标减去增量，就是它移动前的位置：落在那里的顶点才是「接着这个引脚」
    for (WireItem* wire : wireItems()) {
        // 被一起选中的导线由 QGraphicsItem 自己整组平移，
        // 这里再补一次增量会走双倍距离
        if (wire->isSelected())
            continue;
        QVector<QPointF> points = wire->scenePoints();
        bool changed = false;
        for (const QPointF& pin : item->pinScenePositions()) {
            const QPointF oldPin = pin - delta;
            for (QPointF& point : points) {
                if (QLineF(point, oldPin).length() <= 0.5) {
                    point += delta;
                    changed = true;
                }
            }
        }
        if (changed) {
            wire->setScenePoints(points);
            // 端点被带走后可能拉出斜段：立刻整理成横平竖直
            wire->orthogonalize();
        }
    }
    refresh();
}

QPointF SchematicScene::snapToPin(const QPointF& point, double tolerance) const
{
    QPointF best = snap(point);
    double bestDist = tolerance;

    for (ComponentItem* item : componentItems()) {
        for (const QPointF& pin : item->pinScenePositions()) {
            const double d = QLineF(point, pin).length();
            if (d < bestDist) {
                bestDist = d;
                best = pin;
            }
        }
    }
    for (WireItem* wire : wireItems()) {
        for (const QPointF& p : wire->scenePoints()) {
            const double d = QLineF(point, p).length();
            if (d < bestDist) {
                bestDist = d;
                best = p;
            }
        }
    }
    return best;
}

void SchematicScene::refresh()
{
    recomputeJunctions();
    update();
}

void SchematicScene::recomputeJunctions()
{
    m_junctions.clear();

    QHash<QPoint, int> ends;    // 导线端点 + 引脚数量
    QHash<QPoint, int> through; // 穿过该点的线段数量

    QVector<QLineF> segments;
    for (WireItem* wire : wireItems()) {
        const QVector<QPointF> pts = wire->scenePoints();
        for (int i = 0; i < pts.size(); ++i) {
            ends[gridKey(pts.at(i))] += 1;
            if (i > 0)
                segments.append(QLineF(pts.at(i - 1), pts.at(i)));
        }
    }

    for (ComponentItem* item : componentItems()) {
        for (const QPointF& pin : item->pinScenePositions()) {
            const QPoint key = gridKey(pin);
            ends[key] += 1;
            for (const QLineF& seg : segments) {
                if (strictlyInsideSegment(seg, pin))
                    through[key] += 1;
            }
        }
    }

    for (WireItem* wire : wireItems()) {
        for (const QPointF& p : wire->scenePoints()) {
            const QPoint key = gridKey(p);
            for (const QLineF& seg : segments) {
                if (strictlyInsideSegment(seg, p))
                    through[key] += 1;
            }
        }
    }

    QSet<QPoint> keys;
    for (auto it = ends.constBegin(); it != ends.constEnd(); ++it)
        keys.insert(it.key());
    for (auto it = through.constBegin(); it != through.constEnd(); ++it)
        keys.insert(it.key());

    for (const QPoint& key : keys) {
        const int e = ends.value(key);
        const int t = through.value(key);
        const bool dot = (e >= 3) || (e >= 1 && t >= 1);
        if (dot)
            m_junctions.append(QPointF(key.x() * 10.0, key.y() * 10.0));
    }
}

void SchematicScene::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->save();
    painter->fillRect(rect, theme::canvas);

    if (m_showGrid && !rect.isEmpty()) {
        const qreal scale = painter->worldTransform().m11();
        const qreal step = scale < 0.55 ? 50.0 : 10.0;

        QPen minorPen(theme::gridMinor, 0.0);
        QPen majorPen(theme::gridMajor, 0.0);

        const qreal startX = std::floor(rect.left() / step) * step;
        const qreal startY = std::floor(rect.top() / step) * step;

        for (qreal x = startX; x <= rect.right(); x += step) {
            const bool major = std::abs(std::fmod(std::abs(x), 50.0)) < 0.01;
            painter->setPen(major ? majorPen : minorPen);
            painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        }
        for (qreal y = startY; y <= rect.bottom(); y += step) {
            const bool major = std::abs(std::fmod(std::abs(y), 50.0)) < 0.01;
            painter->setPen(major ? majorPen : minorPen);
            painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        }
    }

    painter->restore();
}

void SchematicScene::drawForeground(QPainter* painter, const QRectF& rect)
{
    Q_UNUSED(rect);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(Qt::white, 1.5));
    painter->setBrush(theme::junction);
    for (const QPointF& p : m_junctions)
        painter->drawEllipse(p, 3.4, 3.4);
    painter->restore();
}

} // namespace csm
