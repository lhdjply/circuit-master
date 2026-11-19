#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

namespace csm {

enum class ComponentType {
    Resistor,
    Capacitor,
    Inductor,
    Diode,
    Led,
    Npn,
    Pnp,
    Nmos,
    Pmos,
    Igbt,
    Optocoupler,
    Tl431,
    Transformer,
    Ob2362,      ///< OB2362 电流模式 PWM 控制器（反激常用）
    OpAmp,
    Switch,
    PushButton,   ///< 按钮开关：仿真时按住才动作，松开回到静止状态
    VoltageSource,
    CurrentSource,
    Probe,
    CurrentProbe,
    Multimeter,   ///< 万用表：两引脚仪器，按档位测电压 / 电流 / 电阻
    NetLabel,     ///< 网络标签：单引脚，同名标签在网表里并成一个网络
    LogicGate,    ///< 逻辑门：与 / 或 / 非 / 异或 / 同或，数字电平进出
    Ground,
};

/// 万用表的档位
enum class MultimeterMode {
    Voltage,     ///< 直流电压档：并联，10 MΩ 内阻，读两点电压
    Current,     ///< 直流电流档：串入电路，读流过自身的电流
    Resistance,  ///< 电阻档：注入 1 A 测试电流，读 V / I
    AcVoltage,   ///< 交流电压档：隔直后按一个 50 Hz 周期算有效值
    AcCurrent,   ///< 交流电流档：隔直后按一个 50 Hz 周期算有效值
};

/// 接地的种类：不同符号代表不同的参考网络，在单点处连到一起
enum class GroundKind {
    Gnd,     ///< 主参考地（节点 0）
    Agnd,    ///< 模拟地
    Dgnd,    ///< 数字地
    Pgnd,    ///< 功率地
    Earth,   ///< 保护地 / 大地
};

/// 逻辑门的种类
enum class GateKind {
    And,   ///< 与门
    Or,    ///< 或门
    Not,   ///< 非门（一个输入）
    Xor,   ///< 异或门
    Xnor,  ///< 同或门
};

/// 变压器的绕组形式
enum class TransformerKind {
    Single,        ///< 一个初级 + 一个次级（4 引脚）
    PrimaryAuxSecondary, ///< 初级 + 辅助绕组 + 次级（6 引脚）
};

enum class SourceFunction {
    Constant,
    Sine,
    Pulse,
};

/// 元件在“局部坐标”下的引脚位置（未旋转、未平移）。
/// 两端元件在 x 轴 ±30 处；运放为三引脚（同相、反相、输出）；接地只有一个原点引脚。
/// 变压器这类「同一类型、不同引脚数」的元件请用下面按实例取的重载。
QVector<QPointF> pinOffsets(ComponentType type);
int pinCount(ComponentType type);
QVector<QPointF> pinOffsets(const struct Component& comp);
int pinCount(const struct Component& comp);

QString typeName(ComponentType type);      // 中文显示名
/// 接地种类的显示名（GND / AGND / DGND / PGND / EARTH）
QString groundKindName(GroundKind kind);
QString gateKindName(GateKind kind);
/// 逻辑门的输入引脚数（非门 1 个，其余 2 个）
int gateInputCount(GateKind kind);
/// 万用表档位的显示名（电压档 / 电流档 / 电阻档）
QString multimeterModeName(MultimeterMode mode);
/// 万用表档位画在符号里的字母（V / A / Ω / V~ / A~）
QString multimeterModeLetter(MultimeterMode mode);
/// 万用表读数用的基本单位（V / A / Ω）
QString multimeterModeUnit(MultimeterMode mode);
/// 是否是需要按周期算有效值的交流档
bool isAcMeterMode(MultimeterMode mode);
QString typeSymbol(ComponentType type);    // R / C / L / D / S / V / I / GND
QString refPrefix(ComponentType type);     // 参考编号前缀
bool isTwoTerminal(ComponentType type);
bool isSource(ComponentType type);
bool supportsRotation(ComponentType type);

/// P 型器件（PNP / PMOS）：电压与电流方向与 N 型相反
bool isPType(ComponentType type);
/// 三端半导体器件（三极管 / MOS / IGBT）
bool isThreeTerminalDevice(ComponentType type);

/// 单个元件实例。数值一律使用 SI 基本单位存储（Ω、F、H、V、A）。
struct Component {
    int id = 0;
    ComponentType type = ComponentType::Resistor;
    QPointF pos;
    int rotation = 0; // 0 / 90 / 180 / 270
    bool mirrorX = false; // 水平镜像（屏幕左右翻转）
    bool mirrorY = false; // 垂直镜像（屏幕上下翻转）

    QString refdes;   // 例如 R1
    double value = 0.0;

    // 电容：有极性（电解/钽电容）时符号带「+」，负极接在引脚 1 一侧
    bool polarized = false;

    // 三端半导体器件的模型参数（不使用 value）
    double beta = 100.0;  // NPN 直流电流放大系数 hFE
    double vth = 2.0;     // MOSFET / IGBT 开启电压 (V)
    double k = 2e-3;      // MOSFET / IGBT 跨导参数 (A/V²)
    double vf = 0.7;      // IGBT 输出侧导通压降 (V) / 光耦输入侧 LED 的正向压降
    /// 光耦电流传输比：Ic = CTR · If。
    /// 默认按 LTV-817S-TA1-C（CTR 档位 C，200~400%，典型 300%）
    double ctr = 3.0;
    /// 光耦输出侧饱和压降：Vce 低于此值时输出按电阻性收窄（LTV-817 max 0.2 V）
    double vceSat = 0.2;
    /// 变压器初级电感（H）；次级电感 = lpri · n²，耦合系数按理想 1 处理
    double lpri = 1.0;
    /// 接地符号的种类（Ground）；测试点的参考地也用它（Probe）
    GroundKind groundKind = GroundKind::Gnd;
    /// 网络标签的名字（NetLabel）：同名标签在网表里并成一个网络
    QString netLabel;
    /// 逻辑门的种类（LogicGate）
    GateKind gateKind = GateKind::And;
    /// 逻辑门输出的高电平（V）；低电平固定 0 V，输入阈值取它的一半
    double logicHigh = 3.3;
    /// 标号 / 数值标签相对默认位置的偏移（画布上拖动标签时写入）
    QPointF refLabelOffset;
    QPointF valueLabelOffset;
    /// 万用表的档位（Multimeter）
    MultimeterMode multimeterMode = MultimeterMode::Voltage;
    /// 交流档算有效值用的频率（Hz）：测量窗口取一个周期。
    /// 默认 50 Hz（市电）；被测信号不是 50 Hz 时必须跟着改，
    /// 否则窗口里装不下一个完整周期，读数会随时间乱跳。
    double acFrequency = 50.0;
    /// 变压器的绕组形式（单绕组 / 初级 + 辅助 + 次级）
    TransformerKind transformerKind = TransformerKind::Single;
    /// 次级同名端画在下方（反相）：耦合系数取负，次级电压反 180°
    bool secondaryInverted = false;
    /// 辅助绕组的匝数比（相对初级）；三绕组变压器才有意义
    double auxRatio = 1.0;

    // 独立源参数
    SourceFunction function = SourceFunction::Constant;
    double dcValue = 0.0;
    /// 正弦源的有效值（RMS）。峰值 = 有效值 × √2
    double amplitude = 1.0;
    double offset = 0.0;
    double frequency = 1000.0; // Hz
    double phaseDeg = 0.0;
    double delayTime = 0.0;
    double damping = 0.0;      // 1/s，SIN 的衰减因子
    double pulseV1 = 0.0;
    double pulseV2 = 5.0;
    double pulseRise = 1e-6;
    double pulseFall = 1e-6;
    double pulseWidth = 5e-4;
    double pulsePeriod = 1e-3;

    bool closed = true; // 开关 / 按钮（按钮：按住时与静止状态相反）
    /// 按钮开关的静止状态：false = 常开（按下才接通），true = 常闭（按下才断开）
    bool normallyClosed = false;

    QPointF toScene(const QPointF& local) const;
    QPointF pinPosition(int index) const;
    /// 未吸附到网格的引脚位置（画符号时补引线用）
    QPointF pinPositionRaw(int index) const;
    QVector<QPointF> pinPositions() const;

    /// 独立源在 t 时刻的输出值；非独立源返回 0。
    double sourceValueAt(double t) const;
    /// 正弦源的峰值（有效值 × √2）
    double peakAmplitude() const;

    QString displayValue() const;   // 画在原理图上的数值文本
    QString displayName() const;    // 例如 "R1"
    QString unitName() const;

};

} // namespace csm
