#include "core/Component.h"
#include "core/Language.h"

#include "core/Units.h"

#include <QtMath>

#include <cmath>

namespace csm {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

QVector<QPointF> pinOffsets(const Component& comp)
{
    // 运放：打开「电源引脚」后多两个引脚（3 = V+、4 = V−），坐标必须是 10 的
    // 整数倍，否则导线端点落在网格之间画不上去
    if (comp.type == ComponentType::OpAmp && comp.opampPowerPins) {
        return {QPointF(-30.0, -20.0), QPointF(-30.0, 20.0), QPointF(30.0, 0.0),
                QPointF(0.0, -30.0), QPointF(0.0, 30.0)};
    }
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
    // 逻辑门：0..n-1 是输入（左侧上下排开），最后一个引脚是输出（右侧）
    if (comp.type == ComponentType::LogicGate) {
        QVector<QPointF> pins;
        const int inputs = gateInputCount(comp.gateKind);
        if (inputs == 1) {
            pins.append(QPointF(-30.0, 0.0));
        } else {
            pins.append(QPointF(-30.0, -10.0));
            pins.append(QPointF(-30.0, 10.0));
        }
        pins.append(QPointF(30.0, 0.0));
        return pins;
    }
    return pinOffsets(comp.type);
}

int pinCount(const Component& comp)
{
    return pinOffsets(comp).size();
}

QVector<QPointF> pinOffsets(ComponentType type)
{
    if (type == ComponentType::Ground || isVoltageTap(type)
        || type == ComponentType::NetLabel)
        return {QPointF(0.0, 0.0)};
    // 运放：默认三端（−IN 上、+IN 下、输出右）。打开电源引脚后多出 V+（上）与 V−（下）
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

bool isVoltageTap(ComponentType type)
{
    return type == ComponentType::Probe || type == ComponentType::MeasureProbe;
}

int pinCount(ComponentType type)
{
    switch (type) {
    case ComponentType::Ground: return 1;
    case ComponentType::Probe:
    case ComponentType::MeasureProbe: return 1;
    case ComponentType::NetLabel: return 1;
    case ComponentType::Multimeter: return 2;
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

QString gateKindName(GateKind kind)
{
    switch (kind) {
    case GateKind::And:  return CSM_TR("AND gate");
    case GateKind::Or:   return CSM_TR("OR gate");
    case GateKind::Not:  return CSM_TR("NOT gate");
    case GateKind::Xor:  return CSM_TR("XOR gate");
    case GateKind::Xnor: return CSM_TR("XNOR gate");
    }
    return CSM_TR("AND gate");
}

int gateInputCount(GateKind kind)
{
    return kind == GateKind::Not ? 1 : 2;
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
    case ComponentType::Resistor:      return CSM_TR("Resistor");
    case ComponentType::Capacitor:     return CSM_TR("Capacitor");
    case ComponentType::Inductor:      return CSM_TR("Inductor");
    case ComponentType::Diode:         return CSM_TR("Diode");
    case ComponentType::Zener:         return CSM_TR("Zener diode");
    case ComponentType::OpAmp:         return CSM_TR("Op-amp");
    case ComponentType::Led:           return CSM_TR("LED");
    case ComponentType::Npn:           return CSM_TR("NPN transistor");
    case ComponentType::Pnp:           return CSM_TR("PNP transistor");
    case ComponentType::Nmos:          return CSM_TR("N-channel MOSFET");
    case ComponentType::Pmos:          return CSM_TR("P-channel MOSFET");
    case ComponentType::Igbt:          return QStringLiteral("IGBT");
    case ComponentType::Optocoupler:   return CSM_TR("Optocoupler");
    case ComponentType::Tl431:         return CSM_TR("TL431 reference");
    case ComponentType::Transformer:   return CSM_TR("Transformer");
    case ComponentType::Ob2362:        return CSM_TR("OB2362 controller");

    case ComponentType::Switch:        return CSM_TR("Switch");
    case ComponentType::PushButton:    return CSM_TR("Push button");
    case ComponentType::VoltageSource: return CSM_TR("Voltage source");
    case ComponentType::CurrentSource: return CSM_TR("Current source");
    case ComponentType::SignalGenerator: return CSM_TR("Signal generator");
    case ComponentType::Probe:         return CSM_TR("Voltage probe");
    case ComponentType::CurrentProbe:  return CSM_TR("Current probe");
    case ComponentType::MeasureProbe:  return CSM_TR("Measurement probe");
    case ComponentType::Multimeter:    return CSM_TR("Multimeter");
    case ComponentType::NetLabel:      return CSM_TR("Net label");
    case ComponentType::LogicGate:     return CSM_TR("Logic gate");
    case ComponentType::Ground:        return CSM_TR("Ground");
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
    case ComponentType::Zener:         return QStringLiteral("DZ");
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
    case ComponentType::PushButton:    return QStringLiteral("SB");
    case ComponentType::VoltageSource: return QStringLiteral("V");
    case ComponentType::CurrentSource: return QStringLiteral("I");
    case ComponentType::SignalGenerator: return QStringLiteral("FG");
    case ComponentType::Probe:         return QStringLiteral("TP");
    case ComponentType::CurrentProbe:  return QStringLiteral("IP");
    case ComponentType::MeasureProbe:  return QStringLiteral("MP");
    case ComponentType::Multimeter:    return QStringLiteral("XMM");
    case ComponentType::NetLabel:      return QStringLiteral("NET");
    case ComponentType::LogicGate:     return QStringLiteral("U");
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
    if (type == ComponentType::SignalGenerator)
        return QStringLiteral("FG");
    if (type == ComponentType::MeasureProbe)
        return QStringLiteral("MP");
    if (type == ComponentType::CurrentProbe)
        return QStringLiteral("IP");
    if (type == ComponentType::NetLabel)
        return QStringLiteral("L");
    return typeSymbol(type);
}

bool isTwoTerminal(ComponentType type)
{
    return type != ComponentType::Ground && type != ComponentType::OpAmp
        && type != ComponentType::Probe && type != ComponentType::MeasureProbe
        && !isThreeTerminalDevice(type);
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
    return type == ComponentType::VoltageSource || type == ComponentType::CurrentSource
           || type == ComponentType::SignalGenerator;
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

QString waveformName(Waveform waveform)
{
    switch (waveform) {
    case Waveform::Sine:     return CSM_TR("Sine");
    case Waveform::Square:   return CSM_TR("Square");
    case Waveform::Triangle: return CSM_TR("Triangle");
    case Waveform::Sawtooth: return CSM_TR("Sawtooth");
    }
    return {};
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

double Component::signalValueAt(double t) const
{
    // 峰值幅值直接取 amplitude（与 SIN 的「存有效值」不同：函数发生器习惯按峰值给）
    const double peak = amplitude;
    const double period = frequency > 0.0 ? 1.0 / frequency : 1.0;
    double local = std::fmod(std::max(t - delayTime, 0.0), period);
    if (local < 0.0)
        local += period;
    // 0..1 的相位，含用户设定的相位偏移（单位：度）
    double frac = period > 0.0 ? local / period : 0.0;
    frac += phaseDeg / 360.0;
    frac -= std::floor(frac);   // 归一到 [0,1)

    switch (waveform) {
    case Waveform::Sine:
        return offset + peak * std::sin(2.0 * kPi * frac);
    case Waveform::Square: {
        const double high = std::clamp(duty, 0.0, 1.0);
        return offset + ((frac < high) ? peak : -peak);
    }
    case Waveform::Triangle:
        // 0 → +peak → 0 → −peak → 0
        if (frac < 0.25)
            return offset + peak * (4.0 * frac);
        if (frac < 0.75)
            return offset + peak * (2.0 - 4.0 * frac);
        return offset + peak * (4.0 * frac - 4.0);
    case Waveform::Sawtooth:
        // −peak 线性升到 +peak，然后跳回
        return offset + peak * (2.0 * frac - 1.0);
    }
    return offset;
}

QString Component::unitName() const
{
    switch (type) {
    case ComponentType::Resistor:      return QStringLiteral("Ω");
    case ComponentType::Capacitor:     return QStringLiteral("F");
    case ComponentType::Inductor:      return QStringLiteral("H");
    case ComponentType::VoltageSource: return QStringLiteral("V");
    case ComponentType::CurrentSource: return QStringLiteral("A");
    case ComponentType::SignalGenerator: return QStringLiteral("V");
    case ComponentType::Diode:         return QStringLiteral("A");
    case ComponentType::Zener:         return QStringLiteral("V");
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
    case ComponentType::Switch:
    case ComponentType::PushButton:    return QStringLiteral("Ω");
    case ComponentType::Probe:
    case ComponentType::Ground:        return QString();
    case ComponentType::CurrentProbe:  return QStringLiteral("A");
    case ComponentType::MeasureProbe:  return QString(); // 读数来自仿真，没有可设的数值
    case ComponentType::Multimeter:    return QString(); // 万用表没有可设的数值
    case ComponentType::NetLabel:      return QString(); // 网络标签只有名字
    case ComponentType::LogicGate:     return QStringLiteral("V");
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
        // 压降是可调的，画布上直接显示这个值（和 LED 一样带 Vf 前缀）
        return QStringLiteral("Vf = ") + formatEng(vf, QStringLiteral("V"), 3);

    case ComponentType::Zener:
        return formatEng(value, QStringLiteral("V"), 3);

    case ComponentType::OpAmp:
        return QStringLiteral("A = ") + formatEng(value, QString(), 3);

    case ComponentType::LogicGate:
        return gateKindName(gateKind) + QStringLiteral("  ")
            + formatEng(logicHigh, QStringLiteral("V"), 2);

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

    case ComponentType::SignalGenerator:
        // 例如 "方波 1 kHz / 2 Vpp"：波形 + 频率 + 峰峰值（含占空比时补上）
        return QStringLiteral("%1 %2 / %3")
            .arg(waveformName(waveform), formatEng(frequency, QStringLiteral("Hz"), 3),
                 formatEng(2.0 * amplitude, QStringLiteral("Vpp"), 3));

    case ComponentType::Switch:
        return closed ? CSM_TR("Closed") : CSM_TR("Open");

    case ComponentType::PushButton:
        // 按钮的静止状态才是它的「身份」，按下只是瞬时的
        return normallyClosed ? CSM_TR("Normally closed") : CSM_TR("Normally open");

    case ComponentType::Probe:
    case ComponentType::CurrentProbe:
    case ComponentType::MeasureProbe:
        return QString();

    case ComponentType::Multimeter:
        return multimeterModeLetter(multimeterMode);

    case ComponentType::NetLabel:
        return netLabel;

    case ComponentType::Ground:
        return QStringLiteral("0");
    }
    return QString();
}

QString Component::displayName() const
{
    // 网络标签的名字就是它的「编号」，不另画 refdes
    if (type == ComponentType::NetLabel)
        return netLabel;
    return refdes;
}

QString multimeterModeName(MultimeterMode mode)
{
    switch (mode) {
    case MultimeterMode::Voltage:    return CSM_TR("DC volts");
    case MultimeterMode::Current:    return CSM_TR("DC amps");
    case MultimeterMode::Resistance: return CSM_TR("Resistance");
    case MultimeterMode::AcVoltage:  return CSM_TR("AC volts");
    case MultimeterMode::AcCurrent:  return CSM_TR("AC amps");
    }
    return QString();
}

QString multimeterModeLetter(MultimeterMode mode)
{
    switch (mode) {
    case MultimeterMode::Voltage:    return QStringLiteral("V");
    case MultimeterMode::Current:    return QStringLiteral("A");
    case MultimeterMode::Resistance: return QStringLiteral("Ω");
    case MultimeterMode::AcVoltage:  return QStringLiteral("V~");
    case MultimeterMode::AcCurrent:  return QStringLiteral("A~");
    }
    return QString();
}

QString multimeterModeUnit(MultimeterMode mode)
{
    switch (mode) {
    case MultimeterMode::Voltage:
    case MultimeterMode::AcVoltage:  return QStringLiteral("V");
    case MultimeterMode::Current:
    case MultimeterMode::AcCurrent:  return QStringLiteral("A");
    case MultimeterMode::Resistance: return QStringLiteral("Ω");
    }
    return QString();
}

bool isAcMeterMode(MultimeterMode mode)
{
    return mode == MultimeterMode::AcVoltage || mode == MultimeterMode::AcCurrent;
}

} // namespace csm
