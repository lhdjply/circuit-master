#include "core/Component.h"

#include "core/Units.h"

#include <QStringList>
#include <QtMath>

#include <cmath>

namespace csm {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

QVector<QPointF> pinOffsets(ComponentType type)
{
    if (type == ComponentType::Ground || type == ComponentType::Probe)
        return {QPointF(0.0, 0.0)};
    if (type == ComponentType::OpAmp)
        return {QPointF(-30.0, -20.0), QPointF(-30.0, 20.0), QPointF(30.0, 0.0)};
    // TL431：0 = 参考端 REF（左侧），1 = 阳极（下），2 = 阴极（上）
    if (type == ComponentType::Tl431)
        return {QPointF(-30.0, 0.0), QPointF(0.0, 30.0), QPointF(0.0, -30.0)};
    // 光耦：0 = 输入 LED 阳极，1 = 阴极，2 = 输出三极管集电极，3 = 发射极
    if (type == ComponentType::Optocoupler)
        return {QPointF(-30.0, -10.0), QPointF(-30.0, 10.0), QPointF(30.0, -10.0),
                QPointF(30.0, 10.0)};
    // 三极管 / MOSFET / IGBT：0 = 集电极(漏极)，1 = 基极(栅极)，2 = 发射极(源极)
    if (isThreeTerminalDevice(type))
        return {QPointF(30.0, -30.0), QPointF(-30.0, 0.0), QPointF(30.0, 30.0)};
    return {QPointF(-30.0, 0.0), QPointF(30.0, 0.0)};
}

int pinCount(ComponentType type)
{
    switch (type) {
    case ComponentType::Ground: return 1;
    case ComponentType::Probe:  return 1;
    case ComponentType::OpAmp:  return 3;
    case ComponentType::Npn:
    case ComponentType::Pnp:
    case ComponentType::Nmos:
    case ComponentType::Pmos:
    case ComponentType::Igbt:   return 3;
    case ComponentType::Optocoupler: return 4;
    case ComponentType::Tl431:  return 3;
    default:                    return 2;
    }
}

QString typeName(ComponentType type)
{
    switch (type) {
    case ComponentType::Resistor:      return QStringLiteral("电阻");
    case ComponentType::Capacitor:     return QStringLiteral("电容");
    case ComponentType::Inductor:      return QStringLiteral("电感");
    case ComponentType::Diode:         return QStringLiteral("二极管");
    case ComponentType::OpAmp:         return QStringLiteral("运算放大器");
    case ComponentType::Led:           return QStringLiteral("发光二极管");
    case ComponentType::Npn:           return QStringLiteral("NPN 三极管");
    case ComponentType::Pnp:           return QStringLiteral("PNP 三极管");
    case ComponentType::Nmos:          return QStringLiteral("N 沟道 MOS");
    case ComponentType::Pmos:          return QStringLiteral("P 沟道 MOS");
    case ComponentType::Igbt:          return QStringLiteral("IGBT");
    case ComponentType::Optocoupler:   return QStringLiteral("光耦");
    case ComponentType::Tl431:         return QStringLiteral("TL431 基准");
    case ComponentType::Switch:        return QStringLiteral("开关");
    case ComponentType::VoltageSource: return QStringLiteral("电压源");
    case ComponentType::CurrentSource: return QStringLiteral("电流源");
    case ComponentType::Probe:         return QStringLiteral("电压测试点");
    case ComponentType::CurrentProbe:  return QStringLiteral("电流测试点");
    case ComponentType::Ground:        return QStringLiteral("接地");
    }
    return QString();
}

QString typeSymbol(ComponentType type)
{
    switch (type) {
    case ComponentType::Resistor:      return QStringLiteral("R");
    case ComponentType::Capacitor:     return QStringLiteral("C");
    case ComponentType::Inductor:      return QStringLiteral("L");
    case ComponentType::Diode:         return QStringLiteral("D");
    case ComponentType::OpAmp:         return QStringLiteral("U");
    case ComponentType::Led:           return QStringLiteral("D");
    case ComponentType::Npn:           return QStringLiteral("Q");
    case ComponentType::Pnp:           return QStringLiteral("Q");
    case ComponentType::Nmos:          return QStringLiteral("M");
    case ComponentType::Pmos:          return QStringLiteral("M");
    case ComponentType::Igbt:          return QStringLiteral("Z");
    case ComponentType::Optocoupler:   return QStringLiteral("OC");
    case ComponentType::Tl431:         return QStringLiteral("VR");
    case ComponentType::Switch:        return QStringLiteral("S");
    case ComponentType::VoltageSource: return QStringLiteral("V");
    case ComponentType::CurrentSource: return QStringLiteral("I");
    case ComponentType::Probe:         return QStringLiteral("TP");
    case ComponentType::CurrentProbe:  return QStringLiteral("IP");
    case ComponentType::Ground:        return QStringLiteral("GND");
    }
    return QString();
}

QString refPrefix(ComponentType type)
{
    if (type == ComponentType::Ground)
        return QStringLiteral("GND");
    if (type == ComponentType::Probe)
        return QStringLiteral("TP");
    if (type == ComponentType::CurrentProbe)
        return QStringLiteral("IP");
    return typeSymbol(type);
}

bool isTwoTerminal(ComponentType type)
{
    return type != ComponentType::Ground && type != ComponentType::OpAmp
        && type != ComponentType::Probe && !isThreeTerminalDevice(type);
}

bool isThreeTerminalDevice(ComponentType type)
{
    switch (type) {
    case ComponentType::Npn:
    case ComponentType::Pnp:
    case ComponentType::Nmos:
    case ComponentType::Pmos:
    case ComponentType::Igbt:
        return true;
    default:
        return false;
    }
}

bool isPType(ComponentType type)
{
    return type == ComponentType::Pnp || type == ComponentType::Pmos;
}

bool isSource(ComponentType type)
{
    return type == ComponentType::VoltageSource || type == ComponentType::CurrentSource;
}

bool supportsRotation(ComponentType type)
{
    // 所有元件都能转：接地符号转 90° 后箭头朝侧/朝上，引脚在原点，
    // 电气连接不受影响，只是画法不同
    Q_UNUSED(type);
    return true;
}

QPointF Component::toScene(const QPointF& local) const
{
    const double rad = qDegreesToRadians(double(rotation));
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    const QPointF rotated(local.x() * c - local.y() * s, local.x() * s + local.y() * c);
    // 镜像在屏幕坐标系中进行（先旋转再左右/上下翻转）
    return pos + QPointF(rotated.x() * (mirrorX ? -1.0 : 1.0),
                         rotated.y() * (mirrorY ? -1.0 : 1.0));
}

QPointF Component::pinPosition(int index) const
{
    const QVector<QPointF> offsets = pinOffsets(type);
    if (index < 0 || index >= offsets.size())
        return pos;
    return toScene(offsets.at(index));
}

QVector<QPointF> Component::pinPositions() const
{
    QVector<QPointF> out;
    const QVector<QPointF> offsets = pinOffsets(type);
    out.reserve(offsets.size());
    for (const QPointF& p : offsets)
        out.append(toScene(p));
    return out;
}

double Component::sourceValueAt(double t) const
{
    if (!isSource(type))
        return 0.0;

    switch (function) {
    case SourceFunction::Constant:
        return dcValue;

    case SourceFunction::Sine: {
        const double local = t - delayTime;
        if (local < 0.0)
            return offset + amplitude * std::sin(qDegreesToRadians(phaseDeg));
        const double angle = 2.0 * kPi * frequency * local + qDegreesToRadians(phaseDeg);
        return offset + amplitude * std::exp(-damping * local) * std::sin(angle);
    }

    case SourceFunction::Pulse: {
        double local = t - delayTime;
        if (local < 0.0)
            return pulseV1;
        const double period = pulsePeriod > 0.0 ? pulsePeriod : 1.0;
        local = std::fmod(local, period);
        const double rise = std::max(pulseRise, 1e-12);
        const double fall = std::max(pulseFall, 1e-12);
        const double width = std::max(pulseWidth, 0.0);
        if (local < rise)
            return pulseV1 + (pulseV2 - pulseV1) * (local / rise);
        if (local < rise + width)
            return pulseV2;
        if (local < rise + width + fall)
            return pulseV2 + (pulseV1 - pulseV2) * ((local - rise - width) / fall);
        return pulseV1;
    }
    }
    return dcValue;
}

QString Component::unitName() const
{
    switch (type) {
    case ComponentType::Resistor:      return QStringLiteral("Ω");
    case ComponentType::Capacitor:     return QStringLiteral("F");
    case ComponentType::Inductor:      return QStringLiteral("H");
    case ComponentType::VoltageSource: return QStringLiteral("V");
    case ComponentType::CurrentSource: return QStringLiteral("A");
    case ComponentType::Diode:         return QStringLiteral("A");
    case ComponentType::OpAmp:         return QString();
    case ComponentType::Led:           return QStringLiteral("V");
    case ComponentType::Npn:
    case ComponentType::Pnp:           return QStringLiteral("A/A");
    case ComponentType::Nmos:
    case ComponentType::Pmos:
    case ComponentType::Igbt:
    case ComponentType::Optocoupler:
    case ComponentType::Tl431:         return QStringLiteral("V");
    case ComponentType::Switch:        return QStringLiteral("Ω");
    case ComponentType::Probe:
    case ComponentType::Ground:        return QString();
    case ComponentType::CurrentProbe:  return QStringLiteral("A");
    }
    return QString();
}

QString Component::displayValue() const
{
    switch (type) {
    case ComponentType::Resistor:
    case ComponentType::Capacitor:
    case ComponentType::Inductor:
        return formatEng(value, unitName());

    case ComponentType::VoltageSource:
    case ComponentType::CurrentSource: {
        const QString unit = unitName();
        switch (function) {
        case SourceFunction::Constant:
            return formatEng(dcValue, unit);
        case SourceFunction::Sine:
            return QStringLiteral("SIN ") + formatEng(amplitude, unit) + QStringLiteral(" / ")
                + formatEng(frequency, QStringLiteral("Hz"));
        case SourceFunction::Pulse:
            return QStringLiteral("PULSE ") + formatEng(pulseV1, QString()) + QStringLiteral("→")
                + formatEng(pulseV2, unit);
        }
        return formatEng(dcValue, unit);
    }

    case ComponentType::Diode:
        return QStringLiteral("1N4148");

    case ComponentType::OpAmp:
        return QStringLiteral("A = ") + formatEng(value, QString(), 3);

    case ComponentType::Led:
        return QStringLiteral("Vf = ") + formatEng(vf, QStringLiteral("V"), 3);

    case ComponentType::Npn:
    case ComponentType::Pnp:
        return QStringLiteral("β = ") + QString::number(beta, 'g', 4);

    case ComponentType::Nmos:
    case ComponentType::Pmos:
        return QStringLiteral("Vth = ") + formatEng(vth, QStringLiteral("V"), 3);

    case ComponentType::Igbt:
        return QStringLiteral("Vth = ") + formatEng(vth, QStringLiteral("V"), 3);

    case ComponentType::Optocoupler:
        return QStringLiteral("CTR = ") + QString::number(ctr, 'g', 3);

    case ComponentType::Tl431:
        return QStringLiteral("Vref = ") + formatEng(value, QStringLiteral("V"), 3);

    case ComponentType::Switch:
        return closed ? QStringLiteral("闭合") : QStringLiteral("断开");

    case ComponentType::Probe:
    case ComponentType::CurrentProbe:
        return QString();

    case ComponentType::Ground:
        return QStringLiteral("0");
    }
    return QString();
}

QString Component::displayName() const
{
    return refdes;
}

QString Component::spiceValueSpec() const
{
    switch (type) {
    case ComponentType::Resistor:
    case ComponentType::Capacitor:
    case ComponentType::Inductor:
        return spiceNumber(value);

    case ComponentType::Switch:
        // SPICE 中用一个小电阻/大电阻近似开关
        return spiceNumber(closed ? 1e-3 : 1e9);

    case ComponentType::Diode:
        return QStringLiteral("D1N4148");

    case ComponentType::OpAmp:
        return spiceNumber(value);

    case ComponentType::Npn:
    case ComponentType::Pnp:
    case ComponentType::Nmos:
    case ComponentType::Pmos:
    case ComponentType::Igbt:
    case ComponentType::Optocoupler:
    case ComponentType::Tl431:
        return QStringLiteral("MODEL"); // 由 Circuit::toSpiceNetlist 负责生成整行

    case ComponentType::VoltageSource:
    case ComponentType::CurrentSource: {
        switch (function) {
        case SourceFunction::Constant:
            return QStringLiteral("DC ") + spiceNumber(dcValue);
        case SourceFunction::Sine:
            return QStringLiteral("SIN(%1 %2 %3 %4 %5)")
                .arg(spiceNumber(offset), spiceNumber(amplitude), spiceNumber(frequency),
                     spiceNumber(delayTime), spiceNumber(damping));
        case SourceFunction::Pulse: {
            QString spec = QStringLiteral("PULSE(%1 %2 %3 %4 %5 %6 %7)");
            spec = spec.arg(spiceNumber(pulseV1), spiceNumber(pulseV2), spiceNumber(delayTime),
                            spiceNumber(pulseRise), spiceNumber(pulseFall), spiceNumber(pulseWidth));
            spec = spec.arg(spiceNumber(pulsePeriod));
            return spec;
        }
        }
        return QStringLiteral("DC 0");
    }

    case ComponentType::Probe:
    case ComponentType::CurrentProbe:
    case ComponentType::Ground:
        return QString();
    }
    return QString();
}

} // namespace csm
