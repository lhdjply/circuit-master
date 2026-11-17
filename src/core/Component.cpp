#include "core/Component.h"

#include "core/Units.h"

#include <QStringList>
#include <QtMath>

#include <cmath>

namespace csm {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

QVector<QPointF> pinOffsets(const Component& comp)
{
    // 变压器按绕组形式给不同的引脚布局：三绕组时左边初级 + 辅助、右边次级
    if (comp.type == ComponentType::Transformer) {
        if (comp.transformerKind == TransformerKind::PrimaryAuxSecondary) {
            // 引脚坐标必须是 10 px 的整数倍，否则放在网格上时端点落在网格之间，
            // 导线画不上去（之前用了 ±15，就是这个问题）
            return {QPointF(-30.0, -50.0), QPointF(-30.0, -20.0),   // 初级（1、2）
                    QPointF(-30.0, 20.0), QPointF(-30.0, 50.0),     // 辅助（4、5）
                    QPointF(30.0, -50.0), QPointF(30.0, 50.0)};     // 次级（8、6）
        }
        return {QPointF(-30.0, -10.0), QPointF(-30.0, 10.0), QPointF(30.0, -10.0),
                QPointF(30.0, 10.0)};
    }
    return pinOffsets(comp.type);
}

int pinCount(const Component& comp)
{
    return pinOffsets(comp).size();
}

QVector<QPointF> pinOffsets(ComponentType type)
{
    if (type == ComponentType::Ground || type == ComponentType::Probe)
        return {QPointF(0.0, 0.0)};
    if (type == ComponentType::OpAmp)
        return {QPointF(-30.0, -20.0), QPointF(-30.0, 20.0), QPointF(30.0, 0.0)};
    // 变压器：0/1 = 初级 +/−（左），2/3 = 次级 +/−（右）
    if (type == ComponentType::Transformer)
        return {QPointF(-30.0, -10.0), QPointF(-30.0, 10.0), QPointF(30.0, -10.0),
                QPointF(30.0, 10.0)};
    // OB2362（SOT23-6）：0 = GND、1 = FB、2 = PRT、3 = CS、4 = VCC、5 = GATE
    if (type == ComponentType::Ob2362)
        return {QPointF(0.0, 30.0), QPointF(-30.0, 0.0), QPointF(-30.0, 20.0),
                QPointF(30.0, 10.0), QPointF(0.0, -30.0), QPointF(30.0, -10.0)};
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
    case ComponentType::Transformer: return 4;
    case ComponentType::Ob2362:  return 6;
    default:                    return 2;
    }
}

QString groundKindName(GroundKind kind)
{
    switch (kind) {
    case GroundKind::Gnd:   return QStringLiteral("GND");
    case GroundKind::Agnd:  return QStringLiteral("AGND");
    case GroundKind::Dgnd:  return QStringLiteral("DGND");
    case GroundKind::Pgnd:  return QStringLiteral("PGND");
    case GroundKind::Earth: return QStringLiteral("EARTH");
    }
    return QStringLiteral("GND");
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
    case ComponentType::Transformer:   return QStringLiteral("变压器");
    case ComponentType::Ob2362:        return QStringLiteral("OB2362 控制器");

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
    case ComponentType::Transformer:   return QStringLiteral("T");
    case ComponentType::Ob2362:        return QStringLiteral("U");

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

QPointF Component::pinPositionRaw(int index) const
{
    const QVector<QPointF> offsets = pinOffsets(*this);
    if (index < 0 || index >= offsets.size())
        return pos;
    const QPointF local = offsets.at(index);
    const double rad = qDegreesToRadians(double(rotation));
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    const QPointF rotated(local.x() * c - local.y() * s, local.x() * s + local.y() * c);
    return pos + QPointF(rotated.x() * (mirrorX ? -1.0 : 1.0),
                         rotated.y() * (mirrorY ? -1.0 : 1.0));
}

QPointF Component::toScene(const QPointF& local) const
{
    const double rad = qDegreesToRadians(double(rotation));
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    const QPointF rotated(local.x() * c - local.y() * s, local.x() * s + local.y() * c);
    // 镜像在屏幕坐标系中进行（先旋转再左右/上下翻转）
    const QPointF scene = pos + QPointF(rotated.x() * (mirrorX ? -1.0 : 1.0),
                                        rotated.y() * (mirrorY ? -1.0 : 1.0));
    // 引脚吸附到 10 px 网格：斜放（15°/45°/60°）时旋转出来的坐标是小数，
    // 而导线端点都在网格上，不取整就会差几个像素、看起来「线画不直」甚至连不上
    return QPointF(std::round(scene.x() / 10.0) * 10.0, std::round(scene.y() / 10.0) * 10.0);
}

QPointF Component::pinPosition(int index) const
{
    const QVector<QPointF> offsets = pinOffsets(*this);
    if (index < 0 || index >= offsets.size())
        return pos;
    return toScene(offsets.at(index));
}

QVector<QPointF> Component::pinPositions() const
{
    QVector<QPointF> out;
    const QVector<QPointF> offsets = pinOffsets(*this);
    out.reserve(offsets.size());
    for (const QPointF& p : offsets)
        out.append(toScene(p));
    return out;
}

double Component::peakAmplitude() const
{
    return amplitude * std::sqrt(2.0);
}

double Component::sourceValueAt(double t) const
{
    if (!isSource(type))
        return 0.0;

    switch (function) {
    case SourceFunction::Constant:
        return dcValue;

    case SourceFunction::Sine: {
        // amplitude 存的是有效值，输出时换算成峰值
        const double peak = peakAmplitude();
        const double local = t - delayTime;
        if (local < 0.0)
            return offset + peak * std::sin(qDegreesToRadians(phaseDeg));
        const double angle = 2.0 * kPi * frequency * local + qDegreesToRadians(phaseDeg);
        return offset + peak * std::exp(-damping * local) * std::sin(angle);
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
    case ComponentType::Tl431:
    case ComponentType::Ob2362:
    case ComponentType::Transformer:   return QStringLiteral("V");
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

    case ComponentType::Ob2362:
        return formatEng(frequency, QStringLiteral("Hz"), 3);

    case ComponentType::Transformer:
        // 匝数比用普通数字，别写成工程记数（0.5 不该显示成 500 m）
        if (transformerKind == TransformerKind::PrimaryAuxSecondary)
            return QStringLiteral("1:%1:%2")
                .arg(QString::number(auxRatio, 'g', 3), QString::number(value, 'g', 3));
        return QStringLiteral("1:") + QString::number(value, 'g', 3);

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

    case ComponentType::Led:
        // 与 Diode 一样是器件行，模型卡由 toSpiceNetlist 生成 .model DLED
        return QStringLiteral("DLED");

    case ComponentType::OpAmp:
        return spiceNumber(value);

    case ComponentType::Npn:
    case ComponentType::Pnp:
    case ComponentType::Nmos:
    case ComponentType::Pmos:
    case ComponentType::Igbt:
    case ComponentType::Optocoupler:
    case ComponentType::Tl431:
    case ComponentType::Transformer:
        return QStringLiteral("MODEL"); // 由 Circuit::toSpiceNetlist 负责生成整行

    case ComponentType::VoltageSource:
    case ComponentType::CurrentSource: {
        switch (function) {
        case SourceFunction::Constant:
            return QStringLiteral("DC ") + spiceNumber(dcValue);
        case SourceFunction::Sine:
            return QStringLiteral("SIN(%1 %2 %3 %4 %5)")
                .arg(spiceNumber(offset), spiceNumber(peakAmplitude()), spiceNumber(frequency),
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

    case ComponentType::Ob2362:
        // SPICE 里没有对应模型，toSpiceNetlist 只留一行注释
        return QString();

    case ComponentType::Probe:
    case ComponentType::CurrentProbe:
    case ComponentType::Ground:
        return QString();
    }
    return QString();
}

} // namespace csm
