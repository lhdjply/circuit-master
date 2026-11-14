#include "core/Units.h"

#include <QRegularExpression>
#include <cmath>

namespace csm {

namespace {

constexpr double kPi = 3.14159265358979323846;

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
        if (suffix.startsWith(QLatin1String(p.text))) {
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
