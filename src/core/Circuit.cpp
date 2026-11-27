#include "core/Circuit.h"

#include "core/Grid.h"
#include "core/Language.h"

#include "core/Units.h"

#include <QJsonArray>
#include <QLineF>
#include <QSet>
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
    case ComponentType::PushButton:    return QStringLiteral("PB");
    case ComponentType::VoltageSource: return QStringLiteral("V");
    case ComponentType::CurrentSource: return QStringLiteral("I");
    case ComponentType::SignalGenerator: return QStringLiteral("SG");
    case ComponentType::Probe:         return QStringLiteral("TP");
    case ComponentType::MeasureProbe:  return QStringLiteral("MPR");
    case ComponentType::Multimeter:    return QStringLiteral("XMM");
    case ComponentType::NetLabel:      return QStringLiteral("NET");
    case ComponentType::LogicGate:     return QStringLiteral("GATE");
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
    if (tag == QLatin1String("PB")) return ComponentType::PushButton;
    if (tag == QLatin1String("V")) return ComponentType::VoltageSource;
    if (tag == QLatin1String("I")) return ComponentType::CurrentSource;
    if (tag == QLatin1String("SG")) return ComponentType::SignalGenerator;
    if (tag == QLatin1String("TP")) return ComponentType::Probe;
    if (tag == QLatin1String("MPR")) return ComponentType::MeasureProbe;
    if (tag == QLatin1String("XMM")) return ComponentType::Multimeter;
    if (tag == QLatin1String("NET")) return ComponentType::NetLabel;
    if (tag == QLatin1String("GATE")) return ComponentType::LogicGate;
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

QString waveformToTag(Waveform waveform)
{
    switch (waveform) {
    case Waveform::Sine:     return QStringLiteral("sine");
    case Waveform::Square:   return QStringLiteral("square");
    case Waveform::Triangle: return QStringLiteral("triangle");
    case Waveform::Sawtooth: return QStringLiteral("sawtooth");
    }
    return QStringLiteral("sine");
}

Waveform tagToWaveform(const QString& tag)
{
    if (tag == QLatin1String("square"))   return Waveform::Square;
    if (tag == QLatin1String("triangle")) return Waveform::Triangle;
    if (tag == QLatin1String("sawtooth")) return Waveform::Sawtooth;
    return Waveform::Sine;
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
        o.insert(QStringLiteral("waveform"), waveformToTag(c.waveform));
        o.insert(QStringLiteral("duty"), c.duty);
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
        o.insert(QStringLiteral("multimeterMode"), int(c.multimeterMode));
        o.insert(QStringLiteral("normallyClosed"), c.normallyClosed);
        o.insert(QStringLiteral("gateKind"), int(c.gateKind));
        o.insert(QStringLiteral("logicHigh"), c.logicHigh);
        if (!c.netLabel.isEmpty())
            o.insert(QStringLiteral("netLabel"), c.netLabel);
        if (!c.refLabelOffset.isNull()) {
            o.insert(QStringLiteral("refLabelDx"), c.refLabelOffset.x());
            o.insert(QStringLiteral("refLabelDy"), c.refLabelOffset.y());
        }
        if (!c.valueLabelOffset.isNull()) {
            o.insert(QStringLiteral("valueLabelDx"), c.valueLabelOffset.x());
            o.insert(QStringLiteral("valueLabelDy"), c.valueLabelOffset.y());
        }
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
    // 版本 2 起 amplitude 存有效值（RMS）；版本 1 存的是峰值
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("components"), compArray);
    root.insert(QStringLiteral("wires"), wireArray);
    return root;
}

Circuit Circuit::fromJson(const QJsonObject& obj, QString* error)
{
    Circuit circuit;
    if (obj.value(QStringLiteral("format")).toString() != QLatin1String("circuit-master")) {
        if (error)
            *error = CSM_TR("Not a Circuit Master circuit file.");
        return circuit;
    }

    // 版本 2 起正弦源的 amplitude 是有效值；老文件存的是峰值，读入时换算
    const int formatVersion = obj.value(QStringLiteral("version")).toInt(1);
    const bool legacyPeakAmplitude = formatVersion < 2;

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
        if (legacyPeakAmplitude && c.function == SourceFunction::Sine)
            c.amplitude /= std::sqrt(2.0);
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
        c.waveform = tagToWaveform(o.value(QStringLiteral("waveform")).toString());
        c.duty = o.value(QStringLiteral("duty")).toDouble(0.5);
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
        c.multimeterMode = static_cast<MultimeterMode>(
            o.value(QStringLiteral("multimeterMode")).toInt(0));
        c.netLabel = o.value(QStringLiteral("netLabel")).toString();
        c.normallyClosed = o.value(QStringLiteral("normallyClosed")).toBool(false);
        c.gateKind = static_cast<GateKind>(o.value(QStringLiteral("gateKind")).toInt(0));
        c.logicHigh = o.value(QStringLiteral("logicHigh")).toDouble(3.3);
        c.refLabelOffset = QPointF(o.value(QStringLiteral("refLabelDx")).toDouble(),
                                   o.value(QStringLiteral("refLabelDy")).toDouble());
        c.valueLabelOffset = QPointF(o.value(QStringLiteral("valueLabelDx")).toDouble(),
                                     o.value(QStringLiteral("valueLabelDy")).toDouble());
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
    double widthStep = 0.0; // 由脉宽决定的上限：窄脉冲不能被整个跨过去
    for (const Component& comp : components) {
        // 开关控制器内部有几十 kHz 的振荡器：步长必须比它的周期细得多，
        // 否则每个开关周期都被跨过去，波形直接算成直流
        if (comp.type == ComponentType::Ob2362) {
            if (comp.frequency > 0.0)
                fmax = std::max(fmax, comp.frequency);
            continue;
        }
        if (!isSource(comp.type))
            continue;
        if (comp.function == SourceFunction::Sine && comp.frequency > 0.0) {
            fmax = std::max(fmax, comp.frequency);
        } else if (comp.function == SourceFunction::Pulse) {
            if (comp.pulsePeriod > 0.0)
                fmax = std::max(fmax, 1.0 / comp.pulsePeriod);
            // 高电平期间至少 4 个采样点，否则窄脉冲会被跨过去
            const double width = comp.pulseWidth > 0.0 ? comp.pulseWidth
                                                       : comp.pulsePeriod * 0.5;
            if (width > 0.0) {
                const double w = width / 4.0;
                widthStep = widthStep > 0.0 ? std::min(widthStep, w) : w;
            }
        }
    }

    // 注意：这里**不用**上升/下降沿来约束步长。以前按 1/(edge·4) 算，
    // 1 µs 的沿就要求 200 ns 步长 —— 而电路的时间常数往往在毫秒量级，
    // 根本响应不了这么陡的沿，等于是白算（5 s 的仿真要 2500 万步）。
    // 实测那个 RC 电路：25 µs 与 200 ns 的结果完全一致，1 ms 也只差 0.18%。
    // 真正要防的是「窄脉冲被整个跨过去」，所以用脉宽而不是沿。
    double step = 0.0;
    if (fmax > 0.0)
        step = 1.0 / (fmax * 20.0); // 每个周期至少 20 个采样点
    if (widthStep > 0.0)
        step = step > 0.0 ? std::min(step, widthStep) : widthStep;
    return step;
}

QPointF Circuit::snapPoint(const QPointF& p, double grid)
{
    const double step = grid > 0.0 ? grid : csm::Grid::size();
    return QPointF(std::round(p.x() / step) * step, std::round(p.y() / step) * step);
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
