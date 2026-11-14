#pragma once

#include "core/Circuit.h"

#include <QString>
#include <QStringList>
#include <QVector>

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

/// 基于改进节点法（MNA）的电路求解器。
/// 支持：直流工作点（含二极管牛顿迭代）、瞬态（后向欧拉）、交流小信号扫频。
class Simulator {
public:
    static SimulationResult run(const Circuit& circuit, const SimulationOptions& options);
};

} // namespace csm
