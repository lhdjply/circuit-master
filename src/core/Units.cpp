#include "core/Units.h"

#include <QRegularExpression>
#include <cmath>

namespace csm {

namespace {

struct PrefixEntry {
    const char* text;
    double factor;
};

// 长前缀必须排在短前缀之前（"meg" 先于 "m"）。
const PrefixEntry kPrefixes[] = {
    {"meg", 1e6},
    {"MEG", 1e6},
    {"Meg", 1e6},
    {"p", 1e-12},
    {"n", 1e-9},
    {"u", 1e-6},
    {"µ", 1e-6},
    {"μ", 1e-6},
    {"m", 1e-3},
    {"k", 1e3},
    {"K", 1e3},
    {"M", 1e6},
    {"G", 1e9},
    {"g", 1e9},
    {"T", 1e12},
};

QString prefixForExponent(int exponent)
{
    switch (exponent) {
    case -12: return QStringLiteral("p");
    case -9:  return QStringLiteral("n");
    case -6:  return QStringLiteral("µ");
    case -3:  return QStringLiteral("m");
    case 0:   return QString();
    case 3:   return QStringLiteral("k");
    case 6:   return QStringLiteral("M");
    case 9:   return QStringLiteral("G");
    default:  return QString();
    }
}

} // namespace

double parseEng(const QString& text, bool* ok)
{
    if (ok)
        *ok = false;

    QString s = text.trimmed();
    s.remove(QLatin1Char(' '));
    if (s.isEmpty())
        return 0.0;

    // 允许用户输入 "1,000" 这类写法
    s.replace(QLatin1Char(','), QLatin1Char('.'));

    static const QRegularExpression re(
        QStringLiteral("^([-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][-+]?\\d+)?)(.*)$"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch())
        return 0.0;

    bool numOk = false;
    const double value = m.captured(1).toDouble(&numOk);
    if (!numOk)
        return 0.0;

    QString suffix = m.captured(2);
    double factor = 1.0;
    for (const auto& p : kPrefixes) {
        if (suffix.startsWith(QString::fromUtf8(p.text))) {
            factor = p.factor;
            suffix.remove(0, int(qstrlen(p.text)));
            break;
        }
    }

    // 剩余部分只允许是单位字母（Ω/F/H/V/A/ohm 等），否则视为非法
    static const QRegularExpression unitRe(QStringLiteral("^[A-Za-zΩµμ]*$"));
    if (!suffix.isEmpty() && !unitRe.match(suffix).hasMatch())
        return 0.0;

    if (ok)
        *ok = true;
    return value * factor;
}

QString unitSymbol(UnitKind kind)
{
    switch (kind) {
    case UnitKind::Ohm:    return QStringLiteral("Ω");
    case UnitKind::Farad:  return QStringLiteral("F");
    case UnitKind::Henry:  return QStringLiteral("H");
    case UnitKind::Volt:   return QStringLiteral("V");
    case UnitKind::Ampere: return QStringLiteral("A");
    case UnitKind::Hertz:  return QStringLiteral("Hz");
    case UnitKind::Second: return QStringLiteral("s");
    case UnitKind::None:   return QString();
    }
    return QString();
}

namespace {

UnitKind unitFromSymbol(const QString& symbol)
{
    if (symbol.isEmpty())
        return UnitKind::None;
    if (symbol == QStringLiteral("Ω") || symbol.compare(QStringLiteral("ohm"), Qt::CaseInsensitive) == 0)
        return UnitKind::Ohm;
    if (symbol.compare(QLatin1String("F"), Qt::CaseInsensitive) == 0)
        return UnitKind::Farad;
    if (symbol.compare(QLatin1String("H"), Qt::CaseInsensitive) == 0)
        return UnitKind::Henry;
    if (symbol.compare(QLatin1String("V"), Qt::CaseInsensitive) == 0)
        return UnitKind::Volt;
    if (symbol.compare(QLatin1String("A"), Qt::CaseInsensitive) == 0)
        return UnitKind::Ampere;
    if (symbol.compare(QLatin1String("Hz"), Qt::CaseInsensitive) == 0)
        return UnitKind::Hertz;
    if (symbol.compare(QLatin1String("s"), Qt::CaseInsensitive) == 0
        || symbol.compare(QLatin1String("sec"), Qt::CaseInsensitive) == 0)
        return UnitKind::Second;
    return UnitKind::None;
}

} // namespace

double parseQuantity(const QString& text, UnitKind expected, bool* ok, QString* error)
{
    if (ok)
        *ok = false;
    if (error)
        error->clear();

    QString s = text.trimmed();
    s.remove(QLatin1Char(' '));
    s.replace(QLatin1Char(','), QLatin1Char('.'));

    if (s.isEmpty()) {
        if (error)
            *error = QStringLiteral("请输入数值");
        return 0.0;
    }

    static const QRegularExpression re(
        QStringLiteral("^([-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][-+]?\\d+)?)(.*)$"));
    const QRegularExpressionMatch match = re.match(s);
    if (!match.hasMatch()) {
        if (error)
            *error = QStringLiteral("无法识别的数值：%1").arg(text);
        return 0.0;
    }

    bool numberOk = false;
    const double number = match.captured(1).toDouble(&numberOk);
    if (!numberOk) {
        if (error)
            *error = QStringLiteral("无法识别的数值：%1").arg(text);
        return 0.0;
    }

    QString rest = match.captured(2);
    double factor = 1.0;
    for (const auto& prefix : kPrefixes) {
        if (rest.startsWith(QString::fromUtf8(prefix.text))) {
            factor = prefix.factor;
            rest.remove(0, int(qstrlen(prefix.text)));
            break;
        }
    }

    UnitKind kind = unitFromSymbol(rest);
    if (!rest.isEmpty() && kind == UnitKind::None) {
        if (error)
            *error = QStringLiteral("无法识别的单位：%1").arg(rest);
        return 0.0;
    }
    if (expected == UnitKind::None && !rest.isEmpty()) {
        if (error)
            *error = QStringLiteral("该参数没有单位，请直接输入数字");
        return 0.0;
    }
    if (expected != UnitKind::None && !rest.isEmpty() && kind != expected) {
        if (error)
            *error = QStringLiteral("单位不匹配，应为 %1").arg(unitSymbol(expected));
        return 0.0;
    }

    if (ok)
        *ok = true;
    return number * factor;
}

QString formatEng(double value, const QString& unit, int precision)
{
    if (!std::isfinite(value))
        return QStringLiteral("—");

    if (value == 0.0) {
        if (unit.isEmpty())
            return QStringLiteral("0");
        return QStringLiteral("0 ") + unit;
    }

    const double magnitude = std::abs(value);
    int exponent = int(std::floor(std::log10(magnitude) / 3.0)) * 3;
    if (exponent < -12)
        exponent = -12;
    if (exponent > 9)
        exponent = 9;

    double mantissa = value / std::pow(10.0, exponent);
    if (std::abs(mantissa) >= 999.5 && exponent < 9) {
        mantissa /= 1000.0;
        exponent += 3;
    }

    // 用定点格式并按有效位数四舍五入，避免 "100" 被格式化成 "1e+02"
    const double absMantissa = std::abs(mantissa);
    const int digits = absMantissa > 0.0 ? int(std::floor(std::log10(absMantissa))) + 1 : 1;
    const int decimals = std::max(0, precision - digits);
    QString number = QString::number(mantissa, 'f', decimals);
    if (number.contains(QLatin1Char('.'))) {
        while (number.endsWith(QLatin1Char('0')))
            number.chop(1);
        if (number.endsWith(QLatin1Char('.')))
            number.chop(1);
    }
    if (number == QLatin1String("-0"))
        number = QStringLiteral("0");

    QString out = number;
    const QString prefix = prefixForExponent(exponent);
    if (!prefix.isEmpty()) {
        out += QLatin1Char(' ');
        out += prefix;
    }
    if (!unit.isEmpty()) {
        // 前缀与单位连写（4.7 kΩ），无前缀时用空格分隔（5 V）
        out += prefix.isEmpty() ? QStringLiteral(" ") + unit : unit;
    }
    return out;
}

QString spiceNumber(double value)
{
    if (!std::isfinite(value))
        return QStringLiteral("0");
    return QString::number(value, 'g', 12);
}

} // namespace csm
