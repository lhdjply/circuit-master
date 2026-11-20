#pragma once

#include "core/Component.h"
#include "core/Netlist.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace csm {

struct Wire {
    QVector<QPointF> points;
};

/// 一份完整的电路文档：元件 + 导线。负责编号、网表生成与序列化。
class Circuit {
public:
    static constexpr double kGrid = 10.0;

    QVector<Component> components;
    QVector<Wire> wires;

    void clear();
    const Component* find(int id) const;
    Component* find(int id);
    QString nextRefdes(ComponentType type) const;

    Netlist buildNetlist() const;

    QJsonObject toJson() const;
    static Circuit fromJson(const QJsonObject& obj, QString* error = nullptr);

    /// 依据电源频率/脉冲周期建议的最大步长，用于避免采样混叠。
    /// 没有动态电源时返回 0。
    double suggestedMaxStep() const;

    /// grid = 0 表示用当前设置里的网格间距（见 core/Grid.h）
    static QPointF snapPoint(const QPointF& p, double grid = 0.0);
    static QVector<QPointF> normalizeWire(const QVector<QPointF>& points);
};

} // namespace csm
