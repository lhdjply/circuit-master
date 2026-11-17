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
    case ComponentType::Led:           return QStringLiteral("LED");
    case ComponentType::Optocoupler:   return QStringLiteral("OPTO");
    case ComponentType::Tl431:         return QStringLiteral("TL431");
    case ComponentType::Transformer:   return QStringLiteral("TRANS");
    case ComponentType::Ob2362:        return QStringLiteral("OB2362");
    case ComponentType::Npn:           return QStringLiteral("Q");
    case ComponentType::Pnp:           return QStringLiteral("QP");
    case ComponentType::Nmos:          return QStringLiteral("M");
    case ComponentType::Pmos:          return QStringLiteral("MP");
    case ComponentType::Igbt:          return QStringLiteral("Z");
    case ComponentType::Switch:        return QStringLiteral("SW");
    case ComponentType::VoltageSource: return QStringLiteral("V");
    case ComponentType::CurrentSource: return QStringLiteral("I");
    case ComponentType::Probe:         return QStringLiteral("TP");
    case ComponentType::CurrentProbe:  return QStringLiteral("IP");
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
    if (tag == QLatin1String("LED")) return ComponentType::Led;
    if (tag == QLatin1String("OPTO")) return ComponentType::Optocoupler;
    if (tag == QLatin1String("TL431")) return ComponentType::Tl431;
    if (tag == QLatin1String("TRANS")) return ComponentType::Transformer;
    if (tag == QLatin1String("OB2362")) return ComponentType::Ob2362;
    if (tag == QLatin1String("Q")) return ComponentType::Npn;
    if (tag == QLatin1String("QP")) return ComponentType::Pnp;
    if (tag == QLatin1String("MP")) return ComponentType::Pmos;
    if (tag == QLatin1String("M")) return ComponentType::Nmos;
    if (tag == QLatin1String("Z")) return ComponentType::Igbt;
    if (tag == QLatin1String("SW")) return ComponentType::Switch;
    if (tag == QLatin1String("V")) return ComponentType::VoltageSource;
    if (tag == QLatin1String("I")) return ComponentType::CurrentSource;
    if (tag == QLatin1String("TP")) return ComponentType::Probe;
    if (tag == QLatin1String("IP")) return ComponentType::CurrentProbe;
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
    bool hasZener = false;
    double zenerV = 2.5;
    bool hasLed = false;
    double ledVf = 2.0;
    bool hasBjt = false;
    bool hasPnp = false;
    bool hasMos = false;
    bool hasPmos = false;
    double bjtBeta = 100.0;
    double pnpBeta = 100.0;
    double pmosVth = 2.0;
    double pmosK = 2e-3;
    double mosVth = 2.0;
    double mosK = 2e-3;

    // 同类器件共用一个 model 卡：取第一个的参数，导出前先扫一遍
    for (const Component& comp : components) {
        if (comp.type == ComponentType::Npn)
            bjtBeta = comp.beta;
        else if (comp.type == ComponentType::Pnp)
            pnpBeta = comp.beta;
        else if (comp.type == ComponentType::Pmos) {
            pmosVth = comp.vth;
            pmosK = comp.k;
        }
        else if (comp.type == ComponentType::Nmos || comp.type == ComponentType::Igbt) {
            mosVth = comp.vth;
            mosK = comp.k;
        }
    }
    for (const Component& comp : components) {
        if (comp.type == ComponentType::Ground || comp.type == ComponentType::Probe)
            continue;
        const QVector<int> nodes = net.compPins.value(comp.id);
        if (nodes.size() < 2)
            continue;

        // 电流测试点导出为 0V 电压源（理想的串入式电流表）
        if (comp.type == ComponentType::CurrentProbe) {
            lines << QStringLiteral("V%1 %2 %3 0")
                         .arg(comp.refdes)
                         .arg(nodes.at(0))
                         .arg(nodes.at(1));
            continue;
        }

        // 三极管 / MOSFET / IGBT 按 SPICE 标准器件行导出，并附 .model 卡
        if (comp.type == ComponentType::Led) {
            hasLed = true;
            ledVf = comp.vf;
            lines << QStringLiteral("%1 %2 %3 DLED").arg(comp.refdes).arg(nodes.at(0)).arg(nodes.at(1));
            continue;
        }
        if (comp.type == ComponentType::Ob2362) {
            // SPICE 里没有对应模型：按「GATE 由外部 PWM 驱动」处理，这里只留注释
            lines << QStringLiteral("* %1: OB2362 控制器（VDD/GND/FB/CS/GATE），"
                                    "SPICE 导出时未建模")
                         .arg(comp.refdes);
            continue;
        }
        if (comp.type == ComponentType::Transformer) {
            // SPICE 里变压器用两个电感加 K 卡（耦合系数 1）
            const double n = comp.value > 0.01 ? comp.value : 1.0;
            const double l1 = std::max(comp.lpri, 1e-9);
            lines << QStringLiteral("* %1: 变压器 1:%2").arg(comp.refdes).arg(spiceNumber(n));
            lines << QStringLiteral("L%1a %2 %3 %4").arg(comp.refdes)
                         .arg(nodes.at(0)).arg(nodes.at(1)).arg(spiceNumber(l1));
            lines << QStringLiteral("L%1b %2 %3 %4").arg(comp.refdes)
                         .arg(nodes.at(2)).arg(nodes.at(3)).arg(spiceNumber(l1 * n * n));
            lines << QStringLiteral("K%1 L%1a L%1b 1.0").arg(comp.refdes);
            continue;
        }
        if (comp.type == ComponentType::Tl431) {
            // SPICE 没有 TL431 的通用模型：按「阴极-阳极之间一只 Vref 稳压管」近似，
            // 适用于 REF 直接接阴极的常见用法；带分压电阻的接法需要用户自行展开
            hasZener = true;
            zenerV = comp.value;
            lines << QStringLiteral("* %1: TL431 近似（REF 接阴极时等效于 %2 V 稳压管）")
                         .arg(comp.refdes, spiceNumber(comp.value));
            lines << QStringLiteral("%1 %2 %3 DZENER").arg(comp.refdes)
                         .arg(nodes.at(2)) // 阴极
                         .arg(nodes.at(1)); // 阳极
            continue;
        }
        if (comp.type == ComponentType::Optocoupler) {
            // SPICE 没有通用光耦模型：输入侧用二极管，输出侧用 B 源按 CTR 跟随 LED 电流
            const QString sense = QStringLiteral("Vsen_%1").arg(comp.refdes);
            lines << QStringLiteral("* %1: 光耦近似（CTR=%2）")
                         .arg(comp.refdes, spiceNumber(comp.ctr));
            lines << QStringLiteral("%1 %2 %3 0").arg(sense).arg(nodes.at(0)).arg(nodes.at(0));
            lines << QStringLiteral("Dled_%1 %2 %3 DLED").arg(comp.refdes).arg(nodes.at(0)).arg(nodes.at(1));
            lines << QStringLiteral("Bout_%1 %2 %3 I=%4*I(%5)")
                         .arg(comp.refdes)
                         .arg(nodes.at(2))
                         .arg(nodes.at(3))
                         .arg(spiceNumber(comp.ctr), sense);
            hasLed = true;
            ledVf = comp.vf;
            continue;
        }
        if (comp.type == ComponentType::Npn) {
            hasBjt = true;
            lines << QStringLiteral("%1 %2 %3 %4 QNPN")
                         .arg(comp.refdes)
                         .arg(nodes.at(0))
                         .arg(nodes.at(1))
                         .arg(nodes.at(2));
            continue;
        }
        if (comp.type == ComponentType::Pnp) {
            hasPnp = true;
            lines << QStringLiteral("%1 %2 %3 %4 QPNP")
                         .arg(comp.refdes)
                         .arg(nodes.at(0))
                         .arg(nodes.at(1))
                         .arg(nodes.at(2));
            continue;
        }
        if (comp.type == ComponentType::Pmos) {
            hasPmos = true;
            lines << QStringLiteral("%1 %2 %3 %4 PMOS")
                         .arg(comp.refdes)
                         .arg(nodes.at(0))
                         .arg(nodes.at(1))
                         .arg(nodes.at(2));
            continue;
        }
        if (comp.type == ComponentType::Nmos) {
            hasMos = true;
            lines << QStringLiteral("%1 %2 %3 %4 NMOS")
                         .arg(comp.refdes)
                         .arg(nodes.at(0))
                         .arg(nodes.at(1))
                         .arg(nodes.at(2));
            continue;
        }
        if (comp.type == ComponentType::Igbt) {
            // SPICE 没有通用 IGBT 模型，这里用一个 N 沟道 MOSFET 近似
            hasMos = true;
            lines << QStringLiteral("* %1: IGBT，用 N 沟道 MOSFET 近似（Vth=%2, K=%3, Vf=%4）")
                         .arg(comp.refdes, spiceNumber(comp.vth), spiceNumber(comp.k),
                              spiceNumber(comp.vf));
            lines << QStringLiteral("%1 %2 %3 %4 NMOS").arg(comp.refdes)
                         .arg(nodes.at(0))
                         .arg(nodes.at(1))
                         .arg(nodes.at(2));
            continue;
        }

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
    if (hasZener) {
        lines << QStringLiteral(".model DZENER D(IS=1E-14 N=1.5 BV=%1 IBV=1E-3)")
                     .arg(spiceNumber(zenerV));
    }
    if (hasLed) {
        // 由正向压降反推饱和电流：20 mA 时压降正好是 Vf，n 取 2（LED 的典型值）
        const double nvt = 2.0 * 0.025865;
        const double is = 0.02 / (std::exp(ledVf / nvt) - 1.0);
        lines << QStringLiteral(".model DLED D(IS=%1 N=2)").arg(spiceNumber(is));
    }
    if (hasBjt) {
        lines << QStringLiteral(".model QNPN NPN(IS=1E-14 BF=%1)").arg(spiceNumber(bjtBeta));
    }
    if (hasMos) {
        lines << QStringLiteral(".model NMOS NMOS(VTO=%1 KP=%2)")
                     .arg(spiceNumber(mosVth), spiceNumber(mosK));
    }
    if (hasPnp) {
        lines << QStringLiteral(".model QPNP PNP(IS=1E-14 BF=%1)").arg(spiceNumber(pnpBeta));
    }
    if (hasPmos) {
        // SPICE 的 PMOS 模型用负的开启电压
        lines << QStringLiteral(".model PMOS PMOS(VTO=%1 KP=%2)")
                     .arg(spiceNumber(-std::abs(pmosVth)), spiceNumber(pmosK));
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
        o.insert(QStringLiteral("mirrorX"), c.mirrorX);
        o.insert(QStringLiteral("mirrorY"), c.mirrorY);
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
        o.insert(QStringLiteral("polarized"), c.polarized);
        o.insert(QStringLiteral("beta"), c.beta);
        o.insert(QStringLiteral("vth"), c.vth);
        o.insert(QStringLiteral("k"), c.k);
        o.insert(QStringLiteral("vf"), c.vf);
        o.insert(QStringLiteral("ctr"), c.ctr);
        o.insert(QStringLiteral("vceSat"), c.vceSat);
        o.insert(QStringLiteral("lpri"), c.lpri);
        o.insert(QStringLiteral("secondaryInverted"), c.secondaryInverted);
        o.insert(QStringLiteral("auxRatio"), c.auxRatio);
        o.insert(QStringLiteral("groundKind"), int(c.groundKind));
        o.insert(QStringLiteral("transformerKind"),
                 c.transformerKind == TransformerKind::PrimaryAuxSecondary
                     ? QStringLiteral("primary-aux-secondary")
                     : QStringLiteral("single"));
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
        c.mirrorX = o.value(QStringLiteral("mirrorX")).toBool(false);
        c.mirrorY = o.value(QStringLiteral("mirrorY")).toBool(false);
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
        c.polarized = o.value(QStringLiteral("polarized")).toBool(false);
        c.beta = o.value(QStringLiteral("beta")).toDouble(100.0);
        c.vth = o.value(QStringLiteral("vth")).toDouble(2.0);
        c.k = o.value(QStringLiteral("k")).toDouble(2e-3);
        c.vf = o.value(QStringLiteral("vf")).toDouble(0.7);
        c.ctr = o.value(QStringLiteral("ctr")).toDouble(3.0);
        c.vceSat = o.value(QStringLiteral("vceSat")).toDouble(0.2);
        c.lpri = o.value(QStringLiteral("lpri")).toDouble(1.0);
        c.secondaryInverted = o.value(QStringLiteral("secondaryInverted")).toBool(false);
        c.auxRatio = o.value(QStringLiteral("auxRatio")).toDouble(1.0);
        c.groundKind = static_cast<GroundKind>(o.value(QStringLiteral("groundKind")).toInt(0));
        c.transformerKind =
            o.value(QStringLiteral("transformerKind")).toString() == QLatin1String("primary-aux-secondary")
                ? TransformerKind::PrimaryAuxSecondary
                : TransformerKind::Single;
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

double Circuit::suggestedMaxStep() const
{
    double fmax = 0.0;
    for (const Component& comp : components) {
        if (!isSource(comp.type))
            continue;
        if (comp.function == SourceFunction::Sine && comp.frequency > 0.0) {
            fmax = std::max(fmax, comp.frequency);
        } else if (comp.function == SourceFunction::Pulse) {
            if (comp.pulsePeriod > 0.0)
                fmax = std::max(fmax, 1.0 / comp.pulsePeriod);
            const double edge = std::max(comp.pulseRise, comp.pulseFall);
            if (edge > 0.0)
                fmax = std::max(fmax, 1.0 / (edge * 4.0));
        }
    }
    if (fmax <= 0.0)
        return 0.0;

    // 每个周期至少 20 个采样点
    return 1.0 / (fmax * 20.0);
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
