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
    QString toSpiceNetlist() const;

    QJsonObject toJson() const;
    static Circuit fromJson(const QJsonObject& obj, QString* error = nullptr);

    static QPointF snapPoint(const QPointF& p, double grid = kGrid);
    static QVector<QPointF> normalizeWire(const QVector<QPointF>& points);
};

} // namespace csm
