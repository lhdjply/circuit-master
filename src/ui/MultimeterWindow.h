#pragma once

#include <QColor>
#include <QWidget>

class QComboBox;
class QLabel;

namespace csm {

enum class MultimeterMode;
enum class MultimeterDigits {
    Half3, // 3½ 位：满量程 1999
    Half4, // 4½ 位：满量程 19999
    Half5, // 5½ 位：满量程 199999
    Half6, // 6½ 位：满量程 1999999
};

/// 七段码「8」字显示：深色底、亮段数字，仿真时实时刷新读数。
/// 无数据（NaN）时画一排「-」。位数（3½ ~ 6½）可切换。
class SevenSegmentDisplay : public QWidget
{
    Q_OBJECT
public:
    explicit SevenSegmentDisplay(QWidget* parent = nullptr);

    void setValue(double value);           // NaN 表示无数据（画横杠）
    void setDigits(MultimeterDigits digits);
    void setUnit(const QString& unit);     // 基本单位：V / A / Ω
    /// 是否为负号预留一格（直流档打开）：负数画「−」，正数留空。
    /// 留空而不是不占位，是为了让位数在正负之间切换时不左右跳。
    /// 交流档与电阻档关掉 —— 读数永远非负，不需要这一格。
    void setSignSlot(bool enabled);
    /// 当前显示的「前缀 + 单位」，例如 "mV"、"V"、"kΩ"（测试与调试用）
    QString displayedUnit() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Reading {
        bool valid = false;
        bool negative = false;
        QString digitsText; // 只含 0-9 与 '.'
        QString prefix;     // 工程前缀（m / k / M …）
    };
    Reading formatReading(double value) const;
    /// 按当前读数更新量程（带迟滞）
    void updateRange(double value);
    /// 工程指数对应的前缀文本
    static QString prefixForExp(int exp);

    double m_value = std::numeric_limits<double>::quiet_NaN();
    MultimeterDigits m_digits = MultimeterDigits::Half4;
    QString m_unit;      // 基本单位（V / A / Ω）
    bool m_signSlot = false;
    /// 当前量程的工程指数（-12 p、-9 n、-6 µ、-3 m、0、3 k、6 M、9 G）。
    /// 换挡带迟滞：读数在 1 V 附近来回抖时不会在「999.9 mV」和「1.000 V」
    /// 之间反复横跳。
    int m_rangeExp = 0;
    bool m_hasRange = false;
};

/// 点击原理图上的万用表时弹出的读数窗口：
/// 表头是参考编号与档位，中间是七段码读数，右上角可选位数（3½ ~ 6½）。
/// MainWindow 在实时仿真的每个时钟、静态仿真完成后调用 setValue()。
class MultimeterWindow : public QWidget
{
    Q_OBJECT
public:
    MultimeterWindow(const QString& refdes, MultimeterMode mode, QWidget* parent = nullptr);

    void setValue(double value);
    QString refdes() const { return m_refdes; }

signals:
    void closed(MultimeterWindow* window);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QString m_refdes;
    MultimeterMode m_mode;
    double m_lastValue = std::numeric_limits<double>::quiet_NaN();
    SevenSegmentDisplay* m_display = nullptr;
};

} // namespace csm
