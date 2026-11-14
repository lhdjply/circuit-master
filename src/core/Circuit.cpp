#include "core/Circuit.h"

#include "core/Units.h"

#include <QJsonArray>
#include <QLineF>
#include <QSet>
#include <QStringList>
#include <QtMath>

#include <cmath>

namespace csm {

namespace {

QString typeToTag(ComponentType type)
{
    switch (type) {
    case ComponentType::Resistor:      return QStringLiteral("R");
    case ComponentType::Capacitor:     return QStringLiteral("C");
    case ComponentType::Inductor:      return QStringLiteral("L");
    case ComponentType::Diode:         return QStringLiteral("D");
    case ComponentType::OpAmp:         return QStringLiteral("U");
    case ComponentType::Switch:        return QStringLiteral("SW");
    case ComponentType::VoltageSource: return QStringLiteral("V");
    case ComponentType::CurrentSource: return QStringLiteral("I");
    case ComponentType::Ground:        return QStringLiteral("GND");
    }
    return QStringLiteral("R");
}

ComponentType tagToType(const QString& tag)
{
    if (tag == QLatin1String("C")) return ComponentType::Capacitor;
    if (tag == QLatin1String("L")) return ComponentType::Inductor;
    if (tag == QLatin1String("D")) return ComponentType::Diode;
    if (tag == QLatin1String("U")) return ComponentType::OpAmp;
    if (tag == QLatin1String("SW")) return ComponentType::Switch;
    if (tag == QLatin1String("V")) return ComponentType::VoltageSource;
    if (tag == QLatin1String("I")) return ComponentType::CurrentSource;
    if (tag == QLatin1String("GND")) return ComponentType::Ground;
    return ComponentType::Resistor;
}

QString functionToTag(SourceFunction f)
{
    switch (f) {
    case SourceFunction::Constant: return QStringLiteral("DC");
    case SourceFunction::Sine:     return QStringLiteral("SIN");
    case SourceFunction::Pulse:    return QStringLiteral("PULSE");
    }
    return QStringLiteral("DC");
}

SourceFunction tagToFunction(const QString& s)
{
    if (s == QLatin1String("SIN")) return SourceFunction::Sine;
    if (s == QLatin1String("PULSE")) return SourceFunction::Pulse;
    return SourceFunction::Constant;
}

} // namespace

void Circuit::clear()
{
    components.clear();
    wires.clear();
}

const Component* Circuit::find(int id) const
{
    for (const Component& c : components) {
        if (c.id == id)
            return &c;
    }
    return nullptr;
}

Component* Circuit::find(int id)
{
    for (Component& c : components) {
        if (c.id == id)
            return &c;
    }
    return nullptr;
}

QString Circuit::nextRefdes(ComponentType type) const
{
    const QString prefix = refPrefix(type);
    if (type == ComponentType::Ground)
        return QStringLiteral("GND");

    QSet<int> used;
    for (const Component& c : components) {
        if (!c.refdes.startsWith(prefix))
            continue;
        bool ok = false;
        const int n = c.refdes.mid(prefix.size()).toInt(&ok);
        if (ok)
            used.insert(n);
    }
    int n = 1;
    while (used.contains(n))
        ++n;
    return prefix + QString::number(n);
}

Netlist Circuit::buildNetlist() const
{
    return Netlist::build(*this);
}

QString Circuit::toSpiceNetlist() const
{
    const Netlist net = buildNetlist();
    if (!net.valid)
        return QStringLiteral("* ") + net.error + QLatin1Char('\n');

    QStringList lines;
    lines << QStringLiteral("* 电路仿真大师 (Circuit Master) 导出的 SPICE 网表");
    lines << QStringLiteral("* 元件: %1  节点: %2").arg(components.size()).arg(net.nodeCount);
    lines << QString();

    bool hasDiode = false;
    for (const Component& comp : components) {
        if (comp.type == ComponentType::Ground)
            continue;
        const QVector<int> nodes = net.compPins.value(comp.id);
        if (nodes.size() < 2)
            continue;

        // 运放导出为 SPICE 的 E 元件（VCVS），输出参考地
        if (comp.type == ComponentType::OpAmp) {
            lines << QStringLiteral("* %1: 三端理想运放，开环增益 %2，输出电阻 50Ω")
                         .arg(comp.refdes, spiceNumber(comp.value));
            lines << QStringLiteral("E%1 %2 0 %3 %4 %5")
                         .arg(comp.refdes)
                         .arg(nodes.at(2))
                         .arg(nodes.at(0))
                         .arg(nodes.at(1))
                         .arg(spiceNumber(comp.value));
            continue;
        }

        QString name = comp.refdes;
        if (comp.type == ComponentType::Switch)
            name = QStringLiteral("R") + comp.refdes;

        QString line;
        if (comp.type == ComponentType::Diode) {
            hasDiode = true;
            line = QStringLiteral("%1 %2 %3 %4")
                       .arg(name)
                       .arg(nodes.at(0))
                       .arg(nodes.at(1))
                       .arg(comp.spiceValueSpec());
        } else {
            line = QStringLiteral("%1 %2 %3 %4")
                       .arg(name)
                       .arg(nodes.at(0))
                       .arg(nodes.at(1))
                       .arg(comp.spiceValueSpec());
        }
        lines << line;
    }

    if (hasDiode) {
        lines << QString();
        lines << QStringLiteral("* 1N4148 小信号二极管模型");
        lines << QStringLiteral(".model D1N4148 D(IS=2.52E-9 N=1.752 RS=0.568)");
    }
    lines << QStringLiteral(".op");
    lines << QStringLiteral(".end");
    lines << QString();

    return lines.join(QLatin1Char('\n'));
}

QJsonObject Circuit::toJson() const
{
    QJsonArray compArray;
    for (const Component& c : components) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), c.id);
        o.insert(QStringLiteral("type"), typeToTag(c.type));
        o.insert(QStringLiteral("x"), c.pos.x());
        o.insert(QStringLiteral("y"), c.pos.y());
        o.insert(QStringLiteral("rotation"), c.rotation);
        o.insert(QStringLiteral("refdes"), c.refdes);
        o.insert(QStringLiteral("value"), c.value);
        o.insert(QStringLiteral("function"), functionToTag(c.function));
        o.insert(QStringLiteral("dcValue"), c.dcValue);
        o.insert(QStringLiteral("amplitude"), c.amplitude);
        o.insert(QStringLiteral("offset"), c.offset);
        o.insert(QStringLiteral("frequency"), c.frequency);
        o.insert(QStringLiteral("phaseDeg"), c.phaseDeg);
        o.insert(QStringLiteral("delayTime"), c.delayTime);
        o.insert(QStringLiteral("damping"), c.damping);
        o.insert(QStringLiteral("pulseV1"), c.pulseV1);
        o.insert(QStringLiteral("pulseV2"), c.pulseV2);
        o.insert(QStringLiteral("pulseRise"), c.pulseRise);
        o.insert(QStringLiteral("pulseFall"), c.pulseFall);
        o.insert(QStringLiteral("pulseWidth"), c.pulseWidth);
        o.insert(QStringLiteral("pulsePeriod"), c.pulsePeriod);
        o.insert(QStringLiteral("closed"), c.closed);
        compArray.append(o);
    }

    QJsonArray wireArray;
    for (const Wire& w : wires) {
        QJsonArray pointArray;
        for (const QPointF& p : w.points) {
            QJsonArray pair;
            pair.append(p.x());
            pair.append(p.y());
            pointArray.append(pair);
        }
        wireArray.append(pointArray);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("circuit-master"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("components"), compArray);
    root.insert(QStringLiteral("wires"), wireArray);
    return root;
}

Circuit Circuit::fromJson(const QJsonObject& obj, QString* error)
{
    Circuit circuit;
    if (obj.value(QStringLiteral("format")).toString() != QLatin1String("circuit-master")) {
        if (error)
            *error = QStringLiteral("不是电路仿真大师的电路文件。");
        return circuit;
    }

    const QJsonArray compArray = obj.value(QStringLiteral("components")).toArray();
    for (const QJsonValue& v : compArray) {
        const QJsonObject o = v.toObject();
        Component c;
        c.id = o.value(QStringLiteral("id")).toInt();
        c.type = tagToType(o.value(QStringLiteral("type")).toString());
        c.pos = QPointF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
        c.rotation = o.value(QStringLiteral("rotation")).toInt();
        c.refdes = o.value(QStringLiteral("refdes")).toString();
        c.value = o.value(QStringLiteral("value")).toDouble();
        c.function = tagToFunction(o.value(QStringLiteral("function")).toString());
        c.dcValue = o.value(QStringLiteral("dcValue")).toDouble();
        c.amplitude = o.value(QStringLiteral("amplitude")).toDouble(1.0);
        c.offset = o.value(QStringLiteral("offset")).toDouble();
        c.frequency = o.value(QStringLiteral("frequency")).toDouble(1000.0);
        c.phaseDeg = o.value(QStringLiteral("phaseDeg")).toDouble();
        c.delayTime = o.value(QStringLiteral("delayTime")).toDouble();
        c.damping = o.value(QStringLiteral("damping")).toDouble();
        c.pulseV1 = o.value(QStringLiteral("pulseV1")).toDouble();
        c.pulseV2 = o.value(QStringLiteral("pulseV2")).toDouble(5.0);
        c.pulseRise = o.value(QStringLiteral("pulseRise")).toDouble(1e-6);
        c.pulseFall = o.value(QStringLiteral("pulseFall")).toDouble(1e-6);
        c.pulseWidth = o.value(QStringLiteral("pulseWidth")).toDouble(5e-4);
        c.pulsePeriod = o.value(QStringLiteral("pulsePeriod")).toDouble(1e-3);
        c.closed = o.value(QStringLiteral("closed")).toBool(true);
        if (c.id <= 0)
            continue;
        circuit.components.append(c);
    }

    const QJsonArray wireArray = obj.value(QStringLiteral("wires")).toArray();
    for (const QJsonValue& v : wireArray) {
        Wire w;
        for (const QJsonValue& pv : v.toArray()) {
            const QJsonArray pair = pv.toArray();
            if (pair.size() == 2)
                w.points.append(QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
        }
        if (w.points.size() >= 2)
            circuit.wires.append(w);
    }

    return circuit;
}

QPointF Circuit::snapPoint(const QPointF& p, double grid)
{
    return QPointF(std::round(p.x() / grid) * grid, std::round(p.y() / grid) * grid);
}

QVector<QPointF> Circuit::normalizeWire(const QVector<QPointF>& points)
{
    QVector<QPointF> out;
    for (const QPointF& p : points) {
        if (!out.isEmpty() && QLineF(out.last(), p).length() < 0.5)
            continue;
        out.append(p);
    }
    // 去掉共线的中间点
    if (out.size() >= 3) {
        QVector<QPointF> reduced;
        reduced.append(out.first());
        for (int i = 1; i < out.size() - 1; ++i) {
            const QPointF& a = reduced.last();
            const QPointF& b = out.at(i);
            const QPointF& c = out.at(i + 1);
            const double cross = (b.x() - a.x()) * (c.y() - b.y())
                - (b.y() - a.y()) * (c.x() - b.x());
            if (std::abs(cross) < 1e-6)
                continue;
            reduced.append(b);
        }
        reduced.append(out.last());
        out = reduced;
    }
    return out;
}

} // namespace csm
