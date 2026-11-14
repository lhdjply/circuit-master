#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace csm {

class Circuit;

/// 由原理图几何连接关系提取出的网表：每个元件的每个引脚对应一个节点编号。
/// 节点 0 为地（GND），其余节点从 1 开始编号。
struct Netlist {
    int nodeCount = 0;
    QHash<int, QVector<int>> compPins; // 元件 id -> 各引脚节点号
    QVector<QString> nodeNames;        // nodeNames[0] == "0"
    QStringList warnings;
    bool valid = false;
    QString error;

    int nodeOf(int compId, int pin) const
    {
        const auto it = compPins.constFind(compId);
        if (it == compPins.constEnd() || pin < 0 || pin >= it->size())
            return -1;
        return it->at(pin);
    }

    static Netlist build(const Circuit& circuit);
};

} // namespace csm
