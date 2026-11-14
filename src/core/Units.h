#pragma once

#include <QString>

namespace csm {

/// 解析工程记数法文本，如 "10k"、"4.7u"、"1e-3"、"2.2M"、"100nF"。
/// 失败时返回 0 并（若提供）把 ok 置为 false。
double parseEng(const QString& text, bool* ok = nullptr);

/// 以工程记数法格式化，如 4700 -> "4.7 kΩ"。
QString formatEng(double value, const QString& unit = QString(), int precision = 3);

/// 不带单位的纯数字（用于 SPICE 网表），保证可被 SPICE 解析。
QString spiceNumber(double value);

} // namespace csm
