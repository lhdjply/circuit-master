#pragma once

#include "core/Circuit.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace csm {

enum class AnalysisKind {
    OperatingPoint,
    Transient,
    AC,
};

enum class DisplayMode {
    Plot,
    Scope,
};

struct SimulationOptions {
    AnalysisKind kind = AnalysisKind::Transient;
    DisplayMode display = DisplayMode::Plot;
    double tStop = 0.01;
    double tStep = 1e-5;
    double fStart = 10.0;
    double fStop = 1e6;
    int acPoints = 150;
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

    double time() const;
    double step() const;
    int nodeCount() const;
    const QStringList& signalNames() const;
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
