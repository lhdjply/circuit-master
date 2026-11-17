#include "core/Netlist.h"

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
        net.valid = false;
        net.error = QStringLiteral("电路中没有主接地符号（GND）。请放置 GND 并连接到电路，"
                                   "否则无法确定参考节点（AGND/DGND/PGND/EARTH 不算参考地）。");
        return net;
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

    // 6. 简单检查：悬空引脚 / 自短路
    QHash<int, int> pinCountByNode;
    for (const Component& comp : circuit.components) {
        const QVector<int> nodes = net.compPins.value(comp.id);
        for (int node : nodes)
            pinCountByNode[node] += 1;
    }
    for (const Component& comp : circuit.components) {
        const QVector<int> nodes = net.compPins.value(comp.id);
        if (comp.type == ComponentType::Ground)
            continue;
        for (int i = 0; i < nodes.size(); ++i) {
            if (nodes.at(i) != 0 && pinCountByNode.value(nodes.at(i)) <= 1) {
                net.warnings << QStringLiteral("%1 的引脚 %2 悬空，未与任何元件相连。")
                                    .arg(comp.refdes)
                                    .arg(i + 1);
            }
        }
        if (nodes.size() == 2 && nodes.at(0) == nodes.at(1)) {
            net.warnings << QStringLiteral("%1 的两个引脚被短接在一起。").arg(comp.refdes);
        }
    }

    net.valid = true;
    return net;
}

} // namespace csm
