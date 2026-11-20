#include "core/Netlist.h"
#include "core/Language.h"

#include "core/Circuit.h"

#include <QPoint>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace csm {

namespace {

/// 并查集，键为网格化后的整数坐标点。
class DisjointSet {
public:
    int add(const QPoint& p)
    {
        const auto it = m_index.constFind(p);
        if (it != m_index.constEnd())
            return it.value();
        const int id = int(m_parent.size());
        m_index.insert(p, id);
        m_parent.append(id);
        m_rank.append(0);
        return id;
    }

    int find(int x)
    {
        while (m_parent[x] != x) {
            m_parent[x] = m_parent[m_parent[x]];
            x = m_parent[x];
        }
        return x;
    }

    void unite(int a, int b)
    {
        a = find(a);
        b = find(b);
        if (a == b)
            return;
        if (m_rank[a] < m_rank[b])
            std::swap(a, b);
        m_parent[b] = a;
        if (m_rank[a] == m_rank[b])
            ++m_rank[a];
    }

private:
    QHash<QPoint, int> m_index;
    QVector<int> m_parent;
    QVector<int> m_rank;
};

QPoint toKey(const QPointF& p, double grid)
{
    return QPoint(int(std::lround(p.x() / grid)), int(std::lround(p.y() / grid)));
}

/// 判断网格点 p 是否落在段 ab 上（含端点）。所有坐标为整数，叉积精确。
bool pointOnSegment(const QPoint& p, const QPoint& a, const QPoint& b)
{
    const long long cross = 1LL * (b.x() - a.x()) * (p.y() - a.y())
        - 1LL * (b.y() - a.y()) * (p.x() - a.x());
    if (cross != 0)
        return false;
    const long long dot = 1LL * (p.x() - a.x()) * (b.x() - a.x())
        + 1LL * (p.y() - a.y()) * (b.y() - a.y());
    if (dot < 0)
        return false;
    const long long len2 = 1LL * (b.x() - a.x()) * (b.x() - a.x())
        + 1LL * (b.y() - a.y()) * (b.y() - a.y());
    return dot <= len2;
}

} // namespace

Netlist Netlist::build(const Circuit& circuit)
{
    Netlist net;
    net.nodeNames.append(QStringLiteral("0"));

    const double grid = Circuit::kGrid;
    DisjointSet dsu;

    // 1. 登记所有引脚位置
    for (const Component& comp : circuit.components) {
        for (const QPointF& pin : comp.pinPositions())
            dsu.add(toKey(pin, grid));
    }

    // 2. 导线连通其两个端点
    for (const Wire& wire : circuit.wires) {
        for (int i = 1; i < wire.points.size(); ++i) {
            dsu.unite(dsu.add(toKey(wire.points.at(i - 1), grid)),
                      dsu.add(toKey(wire.points.at(i), grid)));
        }
    }

    // 3. 引脚落在导线中间（T 型连接）也算连接
    for (const Component& comp : circuit.components) {
        for (const QPointF& pinF : comp.pinPositions()) {
            const QPoint pin = toKey(pinF, grid);
            const int pinRoot = dsu.add(pin);
            for (const Wire& wire : circuit.wires) {
                for (int i = 1; i < wire.points.size(); ++i) {
                    const QPoint a = toKey(wire.points.at(i - 1), grid);
                    const QPoint b = toKey(wire.points.at(i), grid);
                    if (pointOnSegment(pin, a, b))
                        dsu.unite(pinRoot, dsu.add(a));
                }
            }
        }
    }

    // 3b. 导线顶点落在另一条导线段内部（T 型接线）也要连通
    for (const Wire& wire : circuit.wires) {
        for (const QPointF& vertexF : wire.points) {
            const QPoint vertex = toKey(vertexF, grid);
            const int vertexRoot = dsu.add(vertex);
            for (const Wire& other : circuit.wires) {
                for (int i = 1; i < other.points.size(); ++i) {
                    const QPoint a = toKey(other.points.at(i - 1), grid);
                    const QPoint b = toKey(other.points.at(i), grid);
                    if (vertex == a || vertex == b)
                        continue;
                    if (pointOnSegment(vertex, a, b))
                        dsu.unite(vertexRoot, dsu.add(a));
                }
            }
        }
    }

    // 3c. 同名网络标签把各自的网络并起来（不用画线也能连）
    {
        QHash<QString, int> labelRoots;
        for (const Component& comp : circuit.components) {
            if (comp.type != ComponentType::NetLabel)
                continue;
            const QString name = comp.netLabel.trimmed();
            if (name.isEmpty())
                continue;
            const int root = dsu.find(dsu.add(toKey(comp.pinPosition(0), grid)));
            const auto it = labelRoots.constFind(name);
            if (it == labelRoots.constEnd())
                labelRoots.insert(name, root);
            else
                dsu.unite(it.value(), root);
        }
    }

    // 4. 找地节点：同一种接地符号算同一个网络（GND → 节点 0，其余各自一个节点）
    bool hasGround = false;
    QVector<int> groundRoots;
    QHash<int, QVector<int>> kindRoots; // GroundKind → 该种类的所有根
    for (const Component& comp : circuit.components) {
        if (comp.type != ComponentType::Ground)
            continue;
        const int root = dsu.find(dsu.add(toKey(comp.pinPosition(0), grid)));
        QVector<int>& roots = kindRoots[int(comp.groundKind)];
        if (!roots.contains(root))
            roots.append(root);
        if (comp.groundKind == GroundKind::Gnd) {
            hasGround = true;
            if (!groundRoots.contains(root))
                groundRoots.append(root);
        }
    }
    if (!hasGround) {
        // 没有主接地（GND），但画了别的种类的地（AGND / DGND / PGND / EARTH）：
        // 拿第一个当地参考节点。「五种地相互隔离」说的是它们各自成网，
        // 不是「必须有 GND 才能仿真」—— 参考点取哪一个只影响读数基准，
        // 不影响电路本身的方程。
        const Component* groundReference = nullptr;
        for (const Component& comp : circuit.components) {
            if (comp.type == ComponentType::Ground) {
                groundReference = &comp;
                break;
            }
        }
        if (groundReference) {
            const int root = dsu.find(dsu.add(toKey(groundReference->pinPosition(0), grid)));
            if (!groundRoots.contains(root))
                groundRoots.append(root);
            net.warnings << CSM_TR("No GND in the circuit; %1 is used as the reference node (0 V).")
                                .arg(groundKindName(groundReference->groundKind));
            hasGround = true;
        }
    }

    if (!hasGround) {
        // 连地都没画，但有万用表：拿它的负端（1 号引脚，真表上的 COM）当参考节点。
        // 万用表本来就是两脚表，读数只看两端之差；把 COM 定成 0 V 之后，
        // 其余节点的电压都相对它 —— 和真表接在电路里的情形一致。
        const Component* reference = nullptr;
        for (const Component& comp : circuit.components) {
            if (comp.type == ComponentType::Multimeter) {
                reference = &comp;
                break;
            }
        }
        if (reference) {
            const int root = dsu.find(dsu.add(toKey(reference->pinPosition(1), grid)));
            if (!groundRoots.contains(root))
                groundRoots.append(root);
            net.warnings << CSM_TR("No GND in the circuit; the negative terminal of multimeter %1 is used as the reference (0 V).")
                                .arg(reference->refdes);
        } else {
            net.valid = false;
            net.error = CSM_TR(
                "The circuit has no main ground symbol (GND). Place a GND and connect it, "
                "otherwise the reference node is undefined (AGND/DGND/PGND/EARTH do not count; "
                "a multimeter's negative terminal works as a reference too).");
            return net;
        }
    }

    // 5. 分配节点号
    QHash<int, int> rootToNode;
    for (int root : groundRoots)
        rootToNode.insert(root, 0);

    int nextNode = 1;
    // 非主地（AGND/DGND/PGND/EARTH）各自合成一个节点，供测试点引用
    QList<int> kinds = kindRoots.keys();
    std::sort(kinds.begin(), kinds.end());
    for (int kind : kinds) {
        if (kind == int(GroundKind::Gnd))
            continue;
        int node = 0;
        for (int root : kindRoots.value(kind)) {
            if (rootToNode.contains(root))
                continue; // 已经当参考节点（0 V）了，别再分配新号
            if (node == 0) {
                node = nextNode++;
                rootToNode.insert(root, node);
            } else {
                rootToNode.insert(root, node);
            }
        }
    }
    for (const Component& comp : circuit.components) {
        QVector<int> nodes;
        const QVector<QPointF> pins = comp.pinPositions();
        nodes.reserve(pins.size());
        for (const QPointF& pinF : pins) {
            const int root = dsu.find(dsu.add(toKey(pinF, grid)));
            int node;
            const auto it = rootToNode.constFind(root);
            if (it != rootToNode.constEnd()) {
                node = it.value();
            } else {
                node = nextNode++;
                rootToNode.insert(root, node);
            }
            nodes.append(node);
        }
        net.compPins.insert(comp.id, nodes);
    }

    net.nodeCount = nextNode - 1;
    net.nodeNames.reserve(net.nodeCount + 1);
    for (int i = 1; i <= net.nodeCount; ++i)
        net.nodeNames.append(QString::number(i));
    // 挂了网络标签的节点用标签名代替数字，报告里更好读
    for (const Component& comp : circuit.components) {
        if (comp.type != ComponentType::NetLabel)
            continue;
        const QString name = comp.netLabel.trimmed();
        if (name.isEmpty())
            continue;
        const int node = net.compPins.value(comp.id).value(0);
        if (node > 0 && node < net.nodeNames.size())
            net.nodeNames[node] = name;
    }

    // 6. 简单检查：悬空引脚 / 自短路
    QHash<int, int> pinCountByNode;
    for (const Component& comp : circuit.components) {
        const QVector<int> nodes = net.compPins.value(comp.id);
        for (int node : nodes)
            pinCountByNode[node] += 1;
    }
    for (const Component& comp : circuit.components) {
        const QVector<int> nodes = net.compPins.value(comp.id);
        if (comp.type == ComponentType::Ground || comp.type == ComponentType::NetLabel)
            continue;
        for (int i = 0; i < nodes.size(); ++i) {
            if (nodes.at(i) != 0 && pinCountByNode.value(nodes.at(i)) <= 1) {
                net.warnings << CSM_TR("Pin %2 of %1 is floating, not connected to anything.")
                                    .arg(comp.refdes)
                                    .arg(i + 1);
            }
        }
        if (nodes.size() == 2 && nodes.at(0) == nodes.at(1)) {
            net.warnings << CSM_TR("Both pins of %1 are shorted together.").arg(comp.refdes);
        }
    }

    net.valid = true;
    return net;
}

} // namespace csm
