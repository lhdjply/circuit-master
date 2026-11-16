#pragma once

#include "core/Circuit.h"

#include <QString>
#include <QStringList>
#include <QHash>
#include <QVector>

#include <memory>

namespace csm {

enum class AnalysisKind {
    OperatingPoint,
    Transient,
    AC,
};

struct SimulationOptions {
    AnalysisKind kind = AnalysisKind::Transient;
    double tStop = 0.01;
    double tStep = 1e-5;
    double fStart = 10.0;
    double fStop = 1e6;
    int acPoints = 150;
    // 实时示波器的刻度（随工程保存，下次打开照旧）
    double scopeTimeDiv = 5e-4;  // 时间/格，默认 500 µs/div
    double scopeVoltDiv = 5.0;   // 电压/格，默认 5 V
    double scopeAmpDiv = 1e-3;   // 电流/格，默认 1 mA
};

struct Signal {
    QString name;
    QString unit;
    QVector<double> x;
    QVector<double> y;
};

struct SimulationResult {
    bool ok = false;
    QString error;
    AnalysisKind kind = AnalysisKind::Transient;
    QVector<Signal> waves;
    QStringList log;
    QString report;      // 工作点文字报告
    QStringList nodeLabels; // 节点 1..N 的连接说明，用于界面提示
    QVector<int> probeWaves; // 测试点对应的波形下标（已排在 waves 前部）
    /// 最后一个采样点上各元件的电流（键为 "I(编号)"），界面用它点亮 LED 等
    QHash<QString, double> elementCurrents;
    /// 反接击穿的极性电容：元件 id → 最严重的反向电压（负值，单位 V）
    QHash<int, double> blownCapacitors;
    double elapsedMs = 0.0;

    const Signal* signal(const QString& name) const
    {
        for (const Signal& s : waves) {
            if (s.name == name)
                return &s;
        }
        return nullptr;
    }
};

/// 增量瞬态求解器：用于示波器的连续仿真，每次推进若干步。
class LiveTransient {
public:
    LiveTransient();
    ~LiveTransient();
    LiveTransient(const LiveTransient&) = delete;
    LiveTransient& operator=(const LiveTransient&) = delete;

    bool start(const Circuit& circuit, double step, QString* error);
    bool advance(int steps, QString* error);

    /// 用新电路继续仿真（不清空时间轴）。
    /// 拓扑没变（只改了开关状态、元件数值等）时保留电容电压、电感电流，
    /// 时间 t 继续往前走；拓扑变了则重置状态变量，但 t 仍然接着走。
    /// 返回 false 表示新电路无法求解，调用方应保留原状态。
    bool updateCircuit(const Circuit& circuit, QString* error);

    double time() const;
    double step() const;
    int nodeCount() const;
    const QStringList& signalNames() const;
    /// 反接击穿的极性电容：元件 id → 最严重的反向电压（负值）
    QHash<int, double> reverseCapacitors() const;
    QVector<double> values() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// 基于改进节点法（MNA）的电路求解器。
/// 支持：直流工作点（含二极管牛顿迭代）、瞬态（后向欧拉）、交流小信号扫频。
class Simulator {
public:
    static SimulationResult run(const Circuit& circuit, const SimulationOptions& options);
};

} // namespace csm
