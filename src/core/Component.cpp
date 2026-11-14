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
    if (type == ComponentType::Ground)
        return {QPointF(0.0, 0.0)};
    return {QPointF(-30.0, 0.0), QPointF(30.0, 0.0)};
}

int pinCount(ComponentType type)
{
    return type == ComponentType::Ground ? 1 : 2;
}

QString typeName(ComponentType type)
{
    switch (type) {
    case ComponentType::Resistor:      return QStringLiteral("电阻");
    case ComponentType::Capacitor:     return QStringLiteral("电容");
    case ComponentType::Inductor:      return QStringLiteral("电感");
    case ComponentType::Diode:         return QStringLiteral("二极管");
    case ComponentType::Switch:        return QStringLiteral("开关");
    case ComponentType::VoltageSource: return QStringLiteral("电压源");
    case ComponentType::CurrentSource: return QStringLiteral("电流源");
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
    case ComponentType::Switch:        return QStringLiteral("S");
    case ComponentType::VoltageSource: return QStringLiteral("V");
    case ComponentType::CurrentSource: return QStringLiteral("I");
    case ComponentType::Ground:        return QStringLiteral("GND");
    }
    return QString();
}

QString refPrefix(ComponentType type)
{
    if (type == ComponentType::Ground)
        return QStringLiteral("GND");
    return typeSymbol(type);
}

bool isTwoTerminal(ComponentType type)
{
    return type != ComponentType::Ground;
}

bool isSource(ComponentType type)
{
    return type == ComponentType::VoltageSource || type == ComponentType::CurrentSource;
}

bool supportsRotation(ComponentType type)
{
    return type != ComponentType::Ground;
}

QPointF Component::toScene(const QPointF& local) const
{
    const double rad = qDegreesToRadians(double(rotation));
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    return pos + QPointF(local.x() * c - local.y() * s, local.x() * s + local.y() * c);
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
    case ComponentType::Switch:        return QStringLiteral("Ω");
    case ComponentType::Ground:        return QString();
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

    case ComponentType::Switch:
        return closed ? QStringLiteral("闭合") : QStringLiteral("断开");

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

    case ComponentType::Ground:
        return QString();
    }
    return QString();
}

} // namespace csm
