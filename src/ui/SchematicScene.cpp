#include "ui/SchematicScene.h"

#include "ui/ComponentItem.h"
#include "ui/Symbols.h"
#include "ui/WireItem.h"

#include <QGraphicsItem>
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
    connect(item, &WireItem::wireChanged, this, [this] {
        refresh();
        emit circuitChanged();
    });
    addItem(item);
    refresh();
    emit circuitChanged();
    commitEdit();
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

void SchematicScene::pasteClipboard()
{
    if (!hasClipboard())
        return;

    const QPointF offset(20.0, 20.0);
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
