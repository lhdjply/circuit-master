#pragma once

#include <QString>

namespace csm {

enum class UnitKind {
    None,
    Ohm,
    Farad,
    Henry,
    Volt,
    Ampere,
    Hertz,
    Second,
};

QString unitSymbol(UnitKind kind);

/// 解析工程记数法文本，如 "10k"、"4.7u"、"1e-3"、"2.2M"、"100nF"。
/// 失败时返回 0 并（若提供）把 ok 置为 false。
double parseEng(const QString& text, bool* ok = nullptr);

/// 解析带单位/前缀的数值，如 "10kΩ"、"4.7µF"、"5V"、"1kHz"。
/// 未写单位时按 expected 的单位处理；单位不匹配时返回 false 并给出原因。
double parseQuantity(const QString& text, UnitKind expected, bool* ok = nullptr,
                     QString* error = nullptr);

/// 以工程记数法格式化，如 4700 -> "4.7 kΩ"。
QString formatEng(double value, const QString& unit = QString(), int precision = 3);

/// 不带单位的纯数字（用于 SPICE 网表），保证可被 SPICE 解析。
QString spiceNumber(double value);

} // namespace csm
