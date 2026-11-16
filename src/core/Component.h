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
    OpAmp,
    Switch,
    VoltageSource,
    CurrentSource,
    Probe,
    CurrentProbe,
    Ground,
};

enum class SourceFunction {
    Constant,
    Sine,
    Pulse,
};

/// 元件在“局部坐标”下的引脚位置（未旋转、未平移）。
/// 两端元件在 x 轴 ±30 处；运放为三引脚（同相、反相、输出）；接地只有一个原点引脚。
QVector<QPointF> pinOffsets(ComponentType type);
int pinCount(ComponentType type);

QString typeName(ComponentType type);      // 中文显示名
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

    // 独立源参数
    SourceFunction function = SourceFunction::Constant;
    double dcValue = 0.0;
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

    bool closed = true; // 开关

    QPointF toScene(const QPointF& local) const;
    QPointF pinPosition(int index) const;
    QVector<QPointF> pinPositions() const;

    /// 独立源在 t 时刻的输出值；非独立源返回 0。
    double sourceValueAt(double t) const;

    QString displayValue() const;   // 画在原理图上的数值文本
    QString displayName() const;    // 例如 "R1"
    QString unitName() const;
    QString spiceValueSpec() const; // SPICE 网表中的数值部分，如 "10k" 或 "SIN(0 5 1k)"
};

} // namespace csm
