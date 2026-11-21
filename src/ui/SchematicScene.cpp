#include "ui/SchematicScene.h"
#include "core/Grid.h"

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
    const double step = Grid::size();
    return QPoint(int(std::lround(p.x() / step)), int(std::lround(p.y() / step)));
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
    // 载入的工程里可能已经有测量探针：导线是在元件之后才加进来的，
    // 这里必须重算一次，否则它们会一直显示「没贴在导线上」
    updateProbeAttachments();
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

namespace {
/// 把坐标按网格取整成整数键，和 Netlist 的做法保持一致
QPoint probeKey(const QPointF& p)
{
    const double grid = std::max(1e-6, Grid::size());
    return QPoint(int(std::lround(p.x() / grid)), int(std::lround(p.y() / grid)));
}

bool probePointOnSegment(const QPoint& p, const QPoint& a, const QPoint& b)
{
    const int abx = b.x() - a.x();
    const int aby = b.y() - a.y();
    const int apx = p.x() - a.x();
    const int apy = p.y() - a.y();
    const int cross = abx * apy - aby * apx;
    if (cross != 0)
        return false;
    return std::min(a.x(), b.x()) <= p.x() && p.x() <= std::max(a.x(), b.x())
           && std::min(a.y(), b.y()) <= p.y() && p.y() <= std::max(a.y(), b.y());
}
} // namespace

void SchematicScene::updateProbeAttachments()
{
    const QVector<WireItem*> wires = wireItems();
    const QVector<ComponentItem*> components = componentItems();

    // 收集所有导线的折线（按网格取整），后面用它们拼一张「导体图」
    struct Polyline {
        WireItem* wire = nullptr;
        QVector<QPoint> points;
    };
    QVector<Polyline> lines;
    for (WireItem* wire : wires) {
        QVector<QPoint> pts;
        for (const QPointF& p : wire->scenePoints())
            pts.append(probeKey(p));
        if (pts.size() >= 2)
            lines.append({wire, pts});
    }

    // 图的节点：所有导线顶点 + 所有元件引脚 + 探针自己
    QSet<QPoint> nodes;
    for (const Polyline& line : lines)
        for (const QPoint& p : line.points)
            nodes.insert(p);
    for (ComponentItem* item : components) {
        const Component& c = item->component();
        for (int pin = 0; pin < pinCount(c); ++pin)
            nodes.insert(probeKey(c.pinPosition(pin)));
    }
    for (ComponentItem* item : components) {
        if (item->component().type == ComponentType::MeasureProbe)
            nodes.insert(probeKey(item->component().pos));
    }

    // 每条导线段上被节点切开，相邻节点之间连一条边。
    // T 型分支、引脚落在段中间，都会因此成为一个真正的节点。
    QHash<QPoint, QSet<QPoint>> adjacency;
    auto connect = [&adjacency](const QPoint& a, const QPoint& b) {
        if (a == b)
            return;
        adjacency[a].insert(b);
        adjacency[b].insert(a);
    };
    for (const Polyline& line : lines) {
        for (int i = 1; i < line.points.size(); ++i) {
            const QPoint a = line.points.at(i - 1);
            const QPoint b = line.points.at(i);
            QVector<QPoint> onSegment;
            for (const QPoint& p : nodes) {
                if (probePointOnSegment(p, a, b))
                    onSegment.append(p);
            }
            std::sort(onSegment.begin(), onSegment.end(),
                      [&a, &b](const QPoint& u, const QPoint& v) {
                          const int du = std::abs(u.x() - a.x()) + std::abs(u.y() - a.y());
                          const int dv = std::abs(v.x() - a.x()) + std::abs(v.y() - a.y());
                          return du < dv;
                      });
            for (int k = 1; k < onSegment.size(); ++k)
                connect(onSegment.at(k - 1), onSegment.at(k));
        }
    }

    // 每个引脚落在哪个节点上：给后面的「注入电流」用
    struct PinRef {
        int id = -1;
        int pin = 0;
        QPoint at;
    };
    QVector<PinRef> pins;
    for (ComponentItem* item : components) {
        const Component& c = item->component();
        if (c.type == ComponentType::MeasureProbe || c.type == ComponentType::Ground)
            continue;
        for (int pin = 0; pin < pinCount(c); ++pin)
            pins.append({c.id, pin, probeKey(c.pinPosition(pin))});
    }

    for (ComponentItem* item : components) {
        if (item->component().type != ComponentType::MeasureProbe)
            continue;

        // 找最近的导线，记下最靠近探针的那一段
        const QPoint key = probeKey(item->component().pos);
        WireItem* bestWire = nullptr;
        // 距离在「网格键」空间里算：折线已经取整，混用真实坐标会比出几十倍的差
        const double grid = std::max(1e-6, Grid::size());
        double bestDistance = 8.0 / grid;   // 8 px 以内算「放在线上」
        const QPointF at(key.x(), key.y());
        QPoint segA, segB;
        for (const Polyline& line : lines) {
            for (int i = 1; i < line.points.size(); ++i) {
                const QPoint a = line.points.at(i - 1);
                const QPoint b = line.points.at(i);
                const QPointF af(a.x(), a.y());
                const QPointF bf(b.x(), b.y());
                const QPointF ab = bf - af;
                const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
                double t = 0.0;
                if (len2 > 1e-9) {
                    t = ((at.x() - af.x()) * ab.x() + (at.y() - af.y()) * ab.y()) / len2;
                    t = std::clamp(t, 0.0, 1.0);
                }
                const double distance = QLineF(at, af + ab * t).length();
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestWire = line.wire;
                    segA = a;
                    segB = b;
                }
            }
        }
        if (!bestWire) {
            item->setProbeAttachment(false);
            continue;
        }

        // 箭头与「探针所在的那一段导线」平行 —— 折线拐弯时不能拿首尾连线当方向；
        // 但正负仍按整根线的走向统一，免得随线段存储顺序乱翻
        const QVector<QPointF> wirePts = bestWire->scenePoints();
        QPointF direction(segB.x() - segA.x(), segB.y() - segA.y());
        {
            const double length = std::hypot(direction.x(), direction.y());
            direction = (length > 1e-6) ? direction / length : QPointF(1.0, 0.0);
        }
        if (wirePts.size() >= 2) {
            const QPointF chord = wirePts.last() - wirePts.first();
            if (chord.x() * direction.x() + chord.y() * direction.y() < 0.0)
                direction = -direction;
        }
        const int angle = ((item->component().rotation % 360) + 360) % 360;
        if (angle == 180)
            direction = -direction;

        // 在「切开探针」之后，从某一侧能走到的所有节点。
        // 端点本身也要算上 —— 它上面往往就接着一个元件引脚。
        auto sideOf = [&adjacency, &key](const QPoint& seed) {
            QSet<QPoint> visited;
            if (seed == key)
                return visited;
            QVector<QPoint> stack{seed};
            visited.insert(seed);
            while (!stack.isEmpty()) {
                const QPoint cur = stack.takeLast();
                for (const QPoint& next : adjacency.value(cur)) {
                    if (next == key || visited.contains(next))
                        continue;
                    visited.insert(next);
                    stack.append(next);
                }
            }
            return visited;
        };
        // 这一侧的注入电流：从元件引脚 1 流出为正、从引脚 0 流出为负
        auto injectionsOn = [&pins](const QSet<QPoint>& side) {
            QHash<int, double> weights;
            for (const PinRef& ref : pins) {
                if (!side.contains(ref.at))
                    continue;
                weights[ref.id] += (ref.pin == 1) ? 1.0 : -1.0;
            }
            return weights;
        };

        const QSet<QPoint> sideA = sideOf(segA);
        const QSet<QPoint> sideB = sideOf(segB);
        QHash<int, double> weights = injectionsOn(sideA);
        double sideFactor = 1.0;

        // 两侧的电流数值相同、方向相反；挑「涉及的元件更少」的那一侧来报，
        // 这样标签上能写清来源。数量相同时取离探针更近的一端。
        const QHash<int, double> weightsB = injectionsOn(sideB);
        if (weightsB.size() < weights.size()
            || (weightsB.size() == weights.size() && !weightsB.isEmpty()
                && QLineF(QPointF(key), QPointF(segB)).length()
                       < QLineF(QPointF(key), QPointF(segA)).length())) {
            weights = weightsB;
            sideFactor = -1.0;
        }

        // 段方向与箭头方向相反时，正负号整体取反
        QPointF cutDir(segB.x() - segA.x(), segB.y() - segA.y());
        const double cutLen = std::hypot(cutDir.x(), cutDir.y());
        if (cutLen > 1e-9)
            cutDir /= cutLen;
        const bool flipped = (cutDir.x() * direction.x() + cutDir.y() * direction.y()) < 0.0;

        QVector<QPair<int, double>> list;
        for (auto it = weights.constBegin(); it != weights.constEnd(); ++it) {
            double weight = it.value() * sideFactor;
            if (flipped)
                weight = -weight;
            if (qFuzzyIsNull(weight))
                continue;
            list.append({it.key(), weight});
        }
        std::sort(list.begin(), list.end(), [](const QPair<int, double>& a, const QPair<int, double>& b) {
            return std::abs(a.second) > std::abs(b.second);
        });
        item->setProbeAttachment(true, direction, list);
    }
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
        // 旋转 / 改参数都可能改变探针的箭头方向与电流符号，重算一次
        updateProbeAttachments();
        emit circuitChanged();
    });
    connect(item, &ComponentItem::editRequested, this, [this, item] { emit editRequested(item); });
    connect(item, &ComponentItem::movedBy, this,
            [this, item](const QPointF& delta) { followComponent(item, delta); });
    connect(item, &ComponentItem::moveStarted, this, [this] { beginComponentDrag(); });
    connect(item, &ComponentItem::moveFinished, this, [this] { endComponentDrag(); });
    addItem(item);
    updateProbeAttachments();   // 探针可能直接放在导线上
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
    case ComponentType::PushButton:
        comp.closed = comp.normallyClosed; // 静止状态 = 常开断开 / 常闭导通
        break;
    case ComponentType::LogicGate:
        // 门类型与高电平取默认值；从元件库拖进来时会带着具体类型
        break;
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
    case ComponentType::Tl431:
        comp.value = 2.495; // 基准电压
        break;
    case ComponentType::Ob2362:
        comp.frequency = 65000.0; // 内部振荡器 65 kHz
        comp.value = 0.8;         // CS 限流阈值 0.8 V
        break;
    case ComponentType::Transformer:
        comp.value = 2.0;  // 匝数比 1:2
        comp.lpri = 1.0;   // 初级电感 1 H
        break;
    case ComponentType::Optocoupler:
        // 默认按 LTV-817S-TA1-C：Vf 典型 1.2 V，CTR 档位 C 为 200~400%（取 300%）
        comp.vf = 1.2;
        comp.ctr = 3.0;
        break;
    case ComponentType::Switch:
        comp.closed = true;
        break;
    case ComponentType::NetLabel: {
        // 网络标签没有编号，只有网络名。默认给一个不重名的名字，
        // 免得两个新建的标签自动并到一起
        comp.refdes.clear();
        QSet<QString> used;
        for (ComponentItem* other : componentItems()) {
            if (other->component().type == ComponentType::NetLabel)
                used.insert(other->component().netLabel);
        }
        int n = 1;
        while (used.contains(QStringLiteral("N%1").arg(n)))
            ++n;
        comp.netLabel = QStringLiteral("N%1").arg(n);
        break;
    }
    case ComponentType::MeasureProbe:
    case ComponentType::Probe:
    case ComponentType::CurrentProbe:
    case ComponentType::Multimeter:
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
    updateProbeAttachments();
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

QVector<ComponentItem*> SchematicScene::selectedComponents() const
{
    QVector<ComponentItem*> out;
    for (QGraphicsItem* item : selectedItems()) {
        if (item->type() == ComponentItem::Type)
            out.append(static_cast<ComponentItem*>(item));
    }
    return out;
}

void SchematicScene::alignSelection(AlignMode mode)
{
    QVector<ComponentItem*> items = selectedComponents();
    if (items.size() < 2)
        return;

    // 共同外框：按符号本体算，标号文字的长短不参与
    QRectF bounds = items.first()->symbolRect();
    for (ComponentItem* item : items)
        bounds = bounds.united(item->symbolRect());

    for (ComponentItem* item : items) {
        const QRectF rect = item->symbolRect();
        QPointF delta;
        switch (mode) {
        case AlignMode::Left:    delta.setX(bounds.left() - rect.left()); break;
        case AlignMode::CenterH: delta.setX(bounds.center().x() - rect.center().x()); break;
        case AlignMode::Right:   delta.setX(bounds.right() - rect.right()); break;
        case AlignMode::Top:     delta.setY(bounds.top() - rect.top()); break;
        case AlignMode::CenterV: delta.setY(bounds.center().y() - rect.center().y()); break;
        case AlignMode::Bottom:  delta.setY(bounds.bottom() - rect.bottom()); break;
        }
        if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y()))
            continue;
        // setPos 会被 itemChange 吸到 10 px 网格；导线由 movedBy → followComponent 带走
        item->setPos(item->pos() + delta);
    }
    commitEdit();
}

void SchematicScene::distributeSelection(Qt::Orientation orientation)
{
    QVector<ComponentItem*> items = selectedComponents();
    if (items.size() < 3)
        return;

    const bool horizontal = orientation == Qt::Horizontal;
    std::sort(items.begin(), items.end(), [horizontal](ComponentItem* a, ComponentItem* b) {
        const QPointF ca = a->symbolRect().center();
        const QPointF cb = b->symbolRect().center();
        return horizontal ? ca.x() < cb.x() : ca.y() < cb.y();
    });

    const QRectF firstRect = items.first()->symbolRect();
    const QRectF lastRect = items.last()->symbolRect();
    const double firstCenter = horizontal ? firstRect.center().x() : firstRect.center().y();
    const double lastCenter = horizontal ? lastRect.center().x() : lastRect.center().y();
    const double step = (lastCenter - firstCenter) / double(items.size() - 1);
    if (qFuzzyIsNull(step))
        return; // 全都叠在一起，没有可分布的间距

    for (int i = 1; i < items.size() - 1; ++i) {
        ComponentItem* item = items.at(i);
        const QRectF rect = item->symbolRect();
        const double center = horizontal ? rect.center().x() : rect.center().y();
        const double target = firstCenter + step * i;
        QPointF delta = horizontal ? QPointF(target - center, 0.0) : QPointF(0.0, target - center);
        if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y()))
            continue;
        item->setPos(item->pos() + delta);
    }
    commitEdit();
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
            // 让粘贴内容的中心落在鼠标处，单个元件就是元件本身对准光标。
            // 偏移量必须是 10 px 的整数倍：元件位置会被 snap 到网格，而导线的点
            // 只是整体平移；两者步长不一致时，粘出来的线就会歪掉、和引脚对不上。
            const QPointF target = snap(m_cursorScenePos);
            offset = snap(target - bounds.center());
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
        // 整体平移一个 10 px 整数倍的偏移：与原图完全一致，不会把悬空的线头
        // 拽到别的引脚上，也不会让线变歪
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

void SchematicScene::snapWiresToGrid()
{
    if (wireItems().isEmpty())
        return;

    int changed = 0;
    const QList<WireItem*> wires = wireItems();
    QVector<QVector<QPointF>> repaired;
    for (WireItem* item : wires) {
        QVector<QPointF> points;
        for (const QPointF& p : item->scenePoints()) {
            const QPointF snapped = snap(p);
            if (QLineF(p, snapped).length() > 0.01)
                ++changed;
            if (points.isEmpty() || points.last() != snapped)
                points.append(snapped);
        }
        repaired.append(points);
    }
    if (changed == 0)
        return;

    // 重建导线（顺便去掉重合点）
    for (WireItem* item : wires)
        removeItem(item), delete item;
    for (const QVector<QPointF>& points : repaired) {
        if (points.size() >= 2)
            addWireRaw(points);
    }
    refresh();
    emit circuitChanged();
    commitEdit();
}

bool SchematicScene::hasCrossingAt(const QPointF& pos) const
{
    const QPointF point = snap(pos);
    int passing = 0;   // 有导线从中间穿过
    int touching = 0;  // 有导线的端点/顶点正好在这里
    for (WireItem* wire : wireItems()) {
        const QVector<QPointF> pts = wire->scenePoints();
        for (int i = 0; i < pts.size(); ++i) {
            if (QLineF(pts.at(i), point).length() <= 0.5) {
                ++touching;
                break;
            }
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
            if (QLineF(a + ab * t, point).length() > 0.5)
                continue;
            ++passing;
            break;
        }
    }
    // 两条线在这里碰上（一条穿过 + 另一条穿过或端点）才值得打交点；
    // 两条线只是共享端点时本来就连着，不算交叉
    return passing >= 1 && (passing + touching) >= 2;
}

bool SchematicScene::connectWiresAt(const QPointF& pos)
{
    const QPointF point = snap(pos);
    QVector<WireItem*> wires = wireItems();

    // 先把「穿过该点」的导线切开，让两条线都有这个顶点
    bool changed = false;
    for (WireItem* wire : wires) {
        if (splitWireAt(wire, point))
            changed = true;
    }
    if (!changed)
        return false;

    refresh();
    emit circuitChanged();
    commitEdit();
    return true;
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
    const auto liesOn = [](const QVector<QPointF>& pts, const QPointF& point) {
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
    const auto passesThrough = [](const QVector<QPointF>& pts, const QPointF& point) {
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

        // 这个点如果已经是「交点」（有线从中间穿过、或有第三条线/引脚搭上来），
        // 就不能合并：合掉中间的顶点，交点会消失、电气上也就断了。
        // 只有单纯的两段相接（拐角 / 直通续画）才合并成一条线。
        // 这个点上有什么：
        //   endsAtJoint    —— 导线在这点收尾（端点）
        //   passingAtJoint —— 导线从这段中间直穿而过（连顶点都没有）
        int endsAtJoint = 0;
        int passingAtJoint = 0;
        QVector<WireItem*> interiorWires; // 这点是它们的中间顶点
        for (WireItem* other : wireItems()) {
            if (!other->scene())
                continue;
            const QVector<QPointF> pts = other->scenePoints();
            if (pts.size() < 2)
                continue;
            if (QLineF(pts.first(), joint).length() <= 0.5
                || QLineF(pts.last(), joint).length() <= 0.5) {
                ++endsAtJoint;
                continue;
            }
            bool interior = false;
            for (int i = 1; i < pts.size() - 1; ++i) {
                if (QLineF(pts.at(i), joint).length() <= 0.5) {
                    interior = true;
                    break;
                }
            }
            if (interior) {
                interiorWires.append(other);
            } else if (liesOn(pts, joint) && passesThrough(pts, joint)) {
                ++passingAtJoint; // 从这段中间直穿而过（连顶点都没有）
            }
        }

        // 线头搭在另一条直穿的线上（十字交叉）→ 这是交点，两边的线头不能并成一条，
        // 否则中间的顶点没了，交点也就消失了
        if (endsAtJoint >= 1 && passingAtJoint >= 1)
            continue;
        // 三条及以上的线头汇聚在一个点 → 也是交点
        if (endsAtJoint >= 3)
            continue;

        // 其余情况（拐角、直通续画、支路接到某根线的中间）可以合并：
        // 先把「中间顶点」处切开，让合并循环能看到端点，再把共线的两段并回去
        for (WireItem* candidate : interiorWires)
            splitWireAt(candidate, joint);

        // 3) 在这个点上反复合并「共线」的一对，直到没有可并的
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
    const double step = Grid::size();
    return QPointF(std::round(point.x() / step) * step, std::round(point.y() / step) * step);
}

namespace {
/// 收集落在这些引脚上的导线顶点（被选中、会整组平移的导线跳过）
QVector<QPointF> verticesAtPins(const QVector<QPointF>& pins,
                                const QVector<WireItem*>& wires)
{
    QVector<QPointF> out;
    for (WireItem* wire : wires) {
        if (wire->isSelected())
            continue;   // 整组平移的导线不用再补
        for (const QPointF& point : wire->scenePoints()) {
            for (const QPointF& pin : pins) {
                if (QLineF(point, pin).length() <= 0.5) {
                    out.append(point);
                    break;
                }
            }
        }
    }
    return out;
}
} // namespace

void SchematicScene::beginComponentDrag()
{
    // 拖动开始：把「现在正好接着某个引脚」的导线顶点记下来。
    // 之后整次拖动只动这些点 —— 否则每步都重新判断，元件扫过一根线时
    // 它的顶点会被临时认领，看起来就像线被吸过来了。
    QVector<QPointF> pins;
    for (ComponentItem* item : componentItems()) {
        if (!item->isSelected())
            continue;
        const QVector<QPointF> positions = item->pinScenePositions();
        for (const QPointF& pin : positions)
            pins.append(pin);
    }
    m_dragAnchors = verticesAtPins(pins, wireItems());
    m_dragActive = true;
}

void SchematicScene::endComponentDrag()
{
    m_dragAnchors.clear();
    m_dragActive = false;
}

void SchematicScene::moveAttachedWireVertices(QVector<QPointF>& anchors, const QPointF& delta)
{
    if (anchors.isEmpty())
        return;
    for (WireItem* wire : wireItems()) {
        // 被一起选中的导线由 QGraphicsItem 自己整组平移，
        // 这里再补一次增量会走双倍距离
        if (wire->isSelected())
            continue;
        QVector<QPointF> points = wire->scenePoints();
        bool changed = false;
        for (QPointF& point : points) {
            for (QPointF& anchor : anchors) {
                if (QLineF(point, anchor).length() <= 0.5) {
                    point += delta;
                    anchor += delta;   // 跟着走，下一步还对得上
                    changed = true;
                    break;
                }
            }
        }
        if (changed) {
            wire->setScenePoints(points);
            // 端点被带走后可能拉出斜段：立刻整理成横平竖直
            wire->orthogonalize();
        }
    }
}

void SchematicScene::followComponent(ComponentItem* item, const QPointF& delta)
{
    if (!item || (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y())))
        return;

    if (m_dragActive) {
        // 用户拖动：只动按下时锁定的那些顶点
        moveAttachedWireVertices(m_dragAnchors, delta);
    } else {
        // 程序化的位移（对齐 / 分布、属性面板、撤销后的重定位）：
        // 按「移动前的几何」临时判断一次，行为与早先一致
        QVector<QPointF> pins = item->pinScenePositions();
        for (QPointF& pin : pins)
            pin -= delta;
        QVector<QPointF> anchors = verticesAtPins(pins, wireItems());
        moveAttachedWireVertices(anchors, delta);
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

    QHash<QPoint, int> incident; // 有多少条线段在这点结束（真正的分支）
    QHash<QPoint, int> passing;  // 有多少条线段从这点中间穿过
    QHash<QPoint, int> pins;     // 这点上有几个引脚

    QVector<QLineF> segments;
    for (WireItem* wire : wireItems()) {
        const QVector<QPointF> pts = wire->scenePoints();
        for (int i = 1; i < pts.size(); ++i)
            segments.append(QLineF(pts.at(i - 1), pts.at(i)));
        for (const QPointF& p : pts)
            incident[gridKey(p)] += 1;
    }

    for (ComponentItem* item : componentItems())
        for (const QPointF& pin : item->pinScenePositions())
            pins[gridKey(pin)] += 1;

    QSet<QPoint> keys;
    for (auto it = incident.constBegin(); it != incident.constEnd(); ++it)
        keys.insert(it.key());
    for (auto it = pins.constBegin(); it != pins.constEnd(); ++it)
        keys.insert(it.key());

    for (const QLineF& seg : segments) {
        for (const QPoint& key : keys) {
            const QPointF point(key.x() * Grid::size(), key.y() * Grid::size());
            if (strictlyInsideSegment(seg, point))
                passing[key] += 1;
        }
    }

    for (const QPoint& key : keys) {
        const int branches = incident.value(key);
        const int through = passing.value(key);
        const int pinCount = pins.value(key);
        // 三条及以上的分支 → 交点；一条线段从中间穿过、又有线头/引脚搭上来 → 交点；
        // 两条线只是十字交叉（各自都是直穿而过）则不是交点，电气上也不相连
        const bool dot = branches >= 3 || (branches >= 1 && through >= 1)
            || (pinCount >= 1 && branches >= 2);
        if (dot)
            m_junctions.append(QPointF(key.x() * Grid::size(), key.y() * Grid::size()));
    }
}

void SchematicScene::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->save();
    painter->fillRect(rect, Theme::canvas());

    if (m_showGrid && !rect.isEmpty()) {
        const qreal scale = painter->worldTransform().m11();
        const qreal grid = Grid::size();
        // 屏幕上每格不足 4 px 时按 5 格画一次：1 px 的网格按原样画会糊成一片，
        // 也白白多画几百条线
        const qreal step = scale * grid < 4.0 ? grid * 5.0 : grid;

        QPen minorPen(Theme::gridMinor(), 0.0);
        QPen majorPen(Theme::gridMajor(), 0.0);

        const qreal startX = std::floor(rect.left() / step) * step;
        const qreal startY = std::floor(rect.top() / step) * step;

        for (qreal x = startX; x <= rect.right(); x += step) {
            const bool major = std::abs(std::fmod(std::abs(x), grid * 5.0)) < 0.01;
            painter->setPen(major ? majorPen : minorPen);
            painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        }
        for (qreal y = startY; y <= rect.bottom(); y += step) {
            const bool major = std::abs(std::fmod(std::abs(y), grid * 5.0)) < 0.01;
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
    painter->setBrush(Theme::junction());
    for (const QPointF& p : m_junctions)
        painter->drawEllipse(p, 3.4, 3.4);
    painter->restore();
}

} // namespace csm
