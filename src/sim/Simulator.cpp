#include "sim/Simulator.h"

#include "core/Units.h"
#include "sim/Matrix.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPair>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace csm {

namespace {

constexpr double kBoltzmann = 1.380649e-23;
constexpr double kElectron = 1.602176634e-19;
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxPoints = 200000;

using Cx = std::complex<double>;

// ---------------------------------------------------------------- 器件模型
struct DiodeModel {
    double is = 2.52e-9; // 1N4148
    double emission = 1.752; // 发射系数 n
    double vt = 0.025865;   // 热电压

    double nvt() const { return emission * vt; }
    double vcrit() const { return nvt() * std::log(nvt() / (std::sqrt(2.0) * is)); }

    double current(double v) const
    {
        const double vv = std::max(-5.0, std::min(v, 0.9));
        return is * (std::exp(vv / nvt()) - 1.0);
    }

    double conductance(double v) const
    {
        const double vv = std::max(-5.0, std::min(v, 0.9));
        return is * std::exp(vv / nvt()) / nvt();
    }
};

struct ElementState {
    QHash<int, double> capV;    // 上一步电容电压
    QHash<int, double> indI;    // 上一步电感电流
    QHash<int, double> diodeV;  // 上一次牛顿迭代的二极管电压
};

struct Problem {
    const Circuit* circuit = nullptr;
    const Netlist* net = nullptr;
    int nNodes = 0;
    int nBranches = 0;
    QHash<int, int> branchOf;
    QVector<QPair<int, int>> branchNodes;
    QVector<const Component*> elements; // 除接地符号外的所有元件

    int nodeOf(int compId, int pin) const
    {
        const int n = net ? net->nodeOf(compId, pin) : -1;
        return n < 0 ? 0 : n;
    }

    double voltage(const std::vector<double>& x, int node) const
    {
        return node <= 0 ? 0.0 : x[size_t(node - 1)];
    }
};

// ---------------------------------------------------------------- 装配工具
template <typename T>
void addG(Matrix<T>& a, int n1, int n2, T g)
{
    const int i1 = n1 - 1;
    const int i2 = n2 - 1;
    if (i1 >= 0)
        a(i1, i1) += g;
    if (i2 >= 0)
        a(i2, i2) += g;
    if (i1 >= 0 && i2 >= 0) {
        a(i1, i2) -= g;
        a(i2, i1) -= g;
    }
}

/// 注入电流源：电流自 n1 经电源流向 n2（离开 n1）。
template <typename T>
void addI(std::vector<T>& b, int n1, int n2, T current)
{
    if (n1 > 0)
        b[size_t(n1 - 1)] -= current;
    if (n2 > 0)
        b[size_t(n2 - 1)] += current;
}

/// 支路元件（电压源 / 电感）：增加一个支路电流未知量，并写入 v1 - v2 - R*i = V。
template <typename T>
void addBranch(Matrix<T>& a, std::vector<T>& b, const Problem& p, int compId, T r, T v)
{
    const int br = p.branchOf.value(compId, -1);
    if (br < 0)
        return;
    const int n1 = p.branchNodes.at(br).first;
    const int n2 = p.branchNodes.at(br).second;
    const int ib = p.nNodes + br;

    if (n1 > 0) {
        a(n1 - 1, ib) += T(1);
        a(ib, n1 - 1) += T(1);
    }
    if (n2 > 0) {
        a(n2 - 1, ib) -= T(1);
        a(ib, n2 - 1) -= T(1);
    }
    a(ib, ib) -= r;
    b[size_t(ib)] = v;
}

double pnjlim(double vnew, double vold, double vt, double vcrit)
{
    if (!std::isfinite(vnew))
        return vold;
    if (vnew > vcrit && std::abs(vnew - vold) > 2.0 * vt) {
        if (vold > 0.0) {
            const double arg = 1.0 + (vnew - vold) / vt;
            vnew = arg > 0.0 ? vold + vt * std::log(arg) : vcrit;
        } else {
            vnew = vt * std::log(std::max(vnew / vt, 1e-12));
        }
    }
    return std::max(-5.0, std::min(vnew, 1.0));
}

Problem buildProblem(const Circuit& circuit, const Netlist& net)
{
    Problem p;
    p.circuit = &circuit;
    p.net = &net;
    p.nNodes = net.nodeCount;

    for (const Component& comp : circuit.components) {
        if (comp.type == ComponentType::Ground || comp.type == ComponentType::Probe)
            continue;
        p.elements.append(&comp);
        const bool needsBranch = comp.type == ComponentType::VoltageSource
            || comp.type == ComponentType::Inductor || comp.type == ComponentType::OpAmp
            || comp.type == ComponentType::CurrentProbe;
        if (needsBranch) {
            int n1 = -1;
            int n2 = 0;
            if (comp.type == ComponentType::OpAmp) {
                n1 = net.nodeOf(comp.id, 2); // 输出端
                n2 = 0;                      // 参考地
            } else {
                n1 = net.nodeOf(comp.id, 0);
                n2 = net.nodeOf(comp.id, 1);
            }
            if (n1 < 0 || n2 < 0)
                continue;
            p.branchOf.insert(comp.id, p.nBranches);
            p.branchNodes.append(qMakePair(n1, n2));
            ++p.nBranches;
        }
    }
    return p;
}

// ---------------------------------------------------------------- 直流 / 瞬态装配
void assembleReal(const Problem& p, const ElementState& st, bool transient, double h, double t,
                  Matrix<double>& a, std::vector<double>& b, const DiodeModel& dm)
{
    for (const Component* cp : p.elements) {
        const Component& comp = *cp;
        const int n1 = p.nodeOf(comp.id, 0);
        const int n2 = p.nodeOf(comp.id, 1);

        switch (comp.type) {
        case ComponentType::Resistor: {
            const double r = std::max(comp.value, 1e-9);
            addG(a, n1, n2, 1.0 / r);
            break;
        }
        case ComponentType::Capacitor: {
            if (!transient)
                break; // 直流下电容开路
            const double c = std::max(comp.value, 1e-15);
            const double g = c / h;
            addG(a, n1, n2, g);
            // 后向欧拉伴随电流源：等效为自 n2 向 n1 注入 g*v(t-h)
            addI(b, n1, n2, -g * st.capV.value(comp.id, 0.0));
            break;
        }
        case ComponentType::Inductor: {
            const double l = std::max(comp.value, 1e-12);
            const double r = transient ? l / h : 0.0;
            const double v = transient ? -r * st.indI.value(comp.id, 0.0) : 0.0;
            addBranch(a, b, p, comp.id, r, v);
            break;
        }
        case ComponentType::VoltageSource:
            addBranch(a, b, p, comp.id, 0.0, comp.sourceValueAt(t));
            break;

        case ComponentType::CurrentProbe:
            addBranch(a, b, p, comp.id, 0.0, 0.0);
            break;

        case ComponentType::CurrentSource:
            addI(b, n1, n2, comp.sourceValueAt(t));
            break;

        case ComponentType::Diode: {
            const double vd = st.diodeV.value(comp.id, 0.0);
            const double g = dm.conductance(vd);
            const double id = dm.current(vd);
            addG(a, n1, n2, g);
            addI(b, n1, n2, id - g * vd);
            break;
        }
        case ComponentType::OpAmp: {
            // 三端理想运放：v_out - Rout·i_out = A·(v+ - v-)
            const int br = p.branchOf.value(comp.id, -1);
            if (br < 0)
                break;
            const double gain = comp.value > 1.0 ? comp.value : 1e5;
            addBranch(a, b, p, comp.id, 50.0, 0.0);
            const int ib = p.nNodes + br;
            const int inP = p.nodeOf(comp.id, 0);
            const int inM = p.nodeOf(comp.id, 1);
            if (inP > 0)
                a(ib, inP - 1) -= gain;
            if (inM > 0)
                a(ib, inM - 1) += gain;
            break;
        }
        case ComponentType::Switch: {
            const double r = comp.closed ? 1e-3 : 1e9;
            addG(a, n1, n2, 1.0 / r);
            break;
        }
        case ComponentType::Probe:
        case ComponentType::Ground:
            break;
        }
    }

    // gmin：每个节点对地加极小电导，避免纯浮空节点导致矩阵奇异
    for (int i = 1; i <= p.nNodes; ++i)
        a(i - 1, i - 1) += 1e-12;
}

// ---------------------------------------------------------------- 牛顿迭代
bool newtonSolve(const Problem& p, ElementState& st, bool transient, double h, double t,
                 std::vector<double>& x, const DiodeModel& dm, QString* error)
{
    const int n = p.nNodes + p.nBranches;
    Matrix<double> a(n);
    std::vector<double> b(size_t(n), 0.0);
    std::vector<double> xnew;
    if (int(x.size()) != n)
        x.assign(size_t(n), 0.0);

    const double reltol = 1e-6;
    const double abstol = 1e-9;

    for (int iter = 0; iter < 150; ++iter) {
        a.zero();
        std::fill(b.begin(), b.end(), 0.0);
        assembleReal(p, st, transient, h, t, a, b, dm);

        QString solveError;
        if (!a.solve(b, xnew, &solveError)) {
            if (error)
                *error = solveError
                    + QStringLiteral("。请检查是否存在电压源短路回路、理想电压源并联，"
                                     "或未连接的悬空节点。");
            return false;
        }

        double maxAbs = 0.0;
        double maxDelta = 0.0;
        for (int i = 0; i < n; ++i) {
            if (!std::isfinite(xnew[size_t(i)])) {
                if (error)
                    *error = QStringLiteral("求解结果出现非有限数值，电路可能发散。");
                return false;
            }
            if (i < p.nNodes) {
                maxAbs = std::max(maxAbs, std::abs(xnew[size_t(i)]));
                maxDelta = std::max(maxDelta, std::abs(xnew[size_t(i)] - x[size_t(i)]));
            }
        }
        x = xnew;

        // 二极管电压限幅更新
        bool hasDiode = false;
        double diodeDelta = 0.0;
        for (const Component* cp : p.elements) {
            if (cp->type != ComponentType::Diode)
                continue;
            hasDiode = true;
            const int n1 = p.nodeOf(cp->id, 0);
            const int n2 = p.nodeOf(cp->id, 1);
            const double vNew = p.voltage(x, n1) - p.voltage(x, n2);
            const double vOld = st.diodeV.value(cp->id, 0.0);
            const double vLim = pnjlim(vNew, vOld, dm.nvt(), dm.vcrit());
            diodeDelta = std::max(diodeDelta, std::abs(vLim - vOld));
            st.diodeV.insert(cp->id, vLim);
        }

        const bool nodeConverged = maxDelta <= reltol * (1.0 + maxAbs) + abstol;
        const bool diodeConverged = !hasDiode || diodeDelta <= 1e-7;
        if (nodeConverged && diodeConverged && iter >= 1) {
            if (maxAbs > 1e9) {
                if (error)
                    *error = QStringLiteral("节点电压超出合理范围（>1 GV），请检查电路连接。");
                return false;
            }
            return true;
        }
    }

    if (error)
        *error = QStringLiteral("牛顿迭代 150 次仍未收敛，请检查二极管/开关所在回路。");
    return false;
}

// ---------------------------------------------------------------- 结果采样
void sample(const Problem& p, const std::vector<double>& x, const ElementState& st,
            bool transient, double h, double t, const DiodeModel& dm, QVector<double>& out)
{
    out.clear();
    out.reserve(p.nNodes + p.elements.size());

    for (int i = 1; i <= p.nNodes; ++i)
        out.append(p.voltage(x, i));

    for (const Component* cp : p.elements) {
        const Component& comp = *cp;
        const int n1 = p.nodeOf(comp.id, 0);
        const int n2 = p.nodeOf(comp.id, 1);
        const double v = p.voltage(x, n1) - p.voltage(x, n2);
        double current = 0.0;

        switch (comp.type) {
        case ComponentType::Resistor:
            current = v / std::max(comp.value, 1e-9);
            break;
        case ComponentType::Capacitor:
            if (transient)
                current = (std::max(comp.value, 1e-15) / h)
                    * (v - st.capV.value(comp.id, 0.0));
            break;
        case ComponentType::Inductor:
        case ComponentType::VoltageSource:
        case ComponentType::OpAmp:
        case ComponentType::CurrentProbe: {
            const int br = p.branchOf.value(comp.id, -1);
            if (br >= 0)
                current = x[size_t(p.nNodes + br)];
            break;
        }
        case ComponentType::CurrentSource:
            current = comp.sourceValueAt(t);
            break;
        case ComponentType::Diode:
            current = dm.current(v);
            break;
        case ComponentType::Switch:
            current = v / (comp.closed ? 1e-3 : 1e9);
            break;
        case ComponentType::Probe:
        case ComponentType::Ground:
            break;
        }
        out.append(current);
    }
}

QVector<Signal> makeSignals(const Problem& p)
{
    QVector<Signal> list;
    list.reserve(p.nNodes + p.elements.size());
    for (int i = 1; i <= p.nNodes; ++i)
        list.append(Signal{QStringLiteral("V(%1)").arg(i), QStringLiteral("V"), {}, {}});
    for (const Component* cp : p.elements)
        list.append(Signal{QStringLiteral("I(%1)").arg(cp->refdes), QStringLiteral("A"), {}, {}});
    return list;
}

void updateState(const Problem& p, ElementState& st, const std::vector<double>& x)
{
    for (const Component* cp : p.elements) {
        const int n1 = p.nodeOf(cp->id, 0);
        const int n2 = p.nodeOf(cp->id, 1);
        const double v = p.voltage(x, n1) - p.voltage(x, n2);
        if (cp->type == ComponentType::Capacitor) {
            st.capV.insert(cp->id, v);
        } else if (cp->type == ComponentType::Inductor) {
            const int br = p.branchOf.value(cp->id, -1);
            if (br >= 0)
                st.indI.insert(cp->id, x[size_t(p.nNodes + br)]);
        }
    }
}

QString nodeLabel(const Circuit& circuit, const Netlist& net, int node)
{
    QStringList pins;
    for (auto it = net.compPins.constBegin(); it != net.compPins.constEnd(); ++it) {
        const QVector<int>& nodes = it.value();
        const Component* comp = circuit.find(it.key());
        const QString name = comp ? comp->refdes : QString::number(it.key());
        for (int i = 0; i < nodes.size(); ++i) {
            if (nodes.at(i) == node && pins.size() < 3)
                pins << QStringLiteral("%1.%2").arg(name).arg(i + 1);
        }
    }
    if (pins.isEmpty())
        return QStringLiteral("节点 %1").arg(node);
    return QStringLiteral("节点 %1 (%2)").arg(node).arg(pins.join(QStringLiteral(", ")));
}

QString buildReport(const Circuit& circuit, const Problem& p, const Netlist& net,
                    const std::vector<double>& x, const ElementState& st, const DiodeModel& dm,
                    double t)
{
    QStringList lines;
    lines << QStringLiteral("直流工作点分析结果");
    lines << QStringLiteral("————————————————————————————");
    lines << QStringLiteral("节点电压：");
    for (int i = 1; i <= p.nNodes; ++i) {
        lines << QStringLiteral("  %1 = %2")
                     .arg(nodeLabel(circuit, net, i).leftJustified(28, QLatin1Char(' ')),
                          formatEng(p.voltage(x, i), QStringLiteral("V"), 5));
    }

    lines << QString();
    lines << QStringLiteral("元件电流：");
    QVector<double> currents;
    sample(p, x, st, false, 0.0, t, dm, currents);
    int idx = p.nNodes;
    for (const Component* cp : p.elements) {
        if (idx >= currents.size())
            break;
        lines << QStringLiteral("  %1 = %2")
                     .arg(cp->refdes.leftJustified(8, QLatin1Char(' ')),
                          formatEng(currents.at(idx), QStringLiteral("A"), 4));
        ++idx;
    }
    return lines.join(QLatin1Char('\n'));
}

// ---------------------------------------------------------------- 交流装配
void assembleAc(const Problem& p, const ElementState& st, double omega, Matrix<Cx>& a,
                std::vector<Cx>& b, const DiodeModel& dm)
{
    for (const Component* cp : p.elements) {
        const Component& comp = *cp;
        const int n1 = p.nodeOf(comp.id, 0);
        const int n2 = p.nodeOf(comp.id, 1);

        switch (comp.type) {
        case ComponentType::Resistor:
            addG(a, n1, n2, Cx(1.0 / std::max(comp.value, 1e-9), 0.0));
            break;
        case ComponentType::Capacitor:
            addG(a, n1, n2, Cx(0.0, omega * std::max(comp.value, 1e-15)));
            break;
        case ComponentType::Inductor:
            addBranch(a, b, p, comp.id, Cx(0.0, omega * std::max(comp.value, 1e-12)), Cx(0.0, 0.0));
            break;
        case ComponentType::VoltageSource:
            addBranch(a, b, p, comp.id, Cx(0.0, 0.0), Cx(1.0, 0.0));
            break;
        case ComponentType::CurrentProbe:
            addBranch(a, b, p, comp.id, Cx(0.0, 0.0), Cx(0.0, 0.0));
            break;
        case ComponentType::CurrentSource:
            addI(b, n1, n2, Cx(1.0, 0.0));
            break;
        case ComponentType::Diode:
            addG(a, n1, n2, Cx(dm.conductance(st.diodeV.value(comp.id, 0.6)), 0.0));
            break;
        case ComponentType::OpAmp: {
            const int br = p.branchOf.value(comp.id, -1);
            if (br < 0)
                break;
            const double gain = comp.value > 1.0 ? comp.value : 1e5;
            addBranch(a, b, p, comp.id, Cx(50.0, 0.0), Cx(0.0, 0.0));
            const int ib = p.nNodes + br;
            const int inP = p.nodeOf(comp.id, 0);
            const int inM = p.nodeOf(comp.id, 1);
            if (inP > 0)
                a(ib, inP - 1) -= Cx(gain, 0.0);
            if (inM > 0)
                a(ib, inM - 1) += Cx(gain, 0.0);
            break;
        }
        case ComponentType::Switch:
            addG(a, n1, n2, Cx(1.0 / (comp.closed ? 1e-3 : 1e9), 0.0));
            break;
        case ComponentType::Probe:
        case ComponentType::Ground:
            break;
        }
    }
    for (int i = 1; i <= p.nNodes; ++i)
        a(i - 1, i - 1) += Cx(1e-12, 0.0);
}

} // namespace

SimulationResult Simulator::run(const Circuit& circuit, const SimulationOptions& options)
{
    QElapsedTimer timer;
    timer.start();

    SimulationResult result;
    result.kind = options.kind;

    const Netlist net = circuit.buildNetlist();
    result.log = net.warnings;
    if (!net.valid) {
        result.error = net.error;
        return result;
    }
    if (net.nodeCount == 0) {
        result.error = QStringLiteral("电路中没有可求解的节点。请放置元件并用导线连接。");
        return result;
    }

    const Problem p = buildProblem(circuit, net);
    DiodeModel dm;
    dm.vt = kBoltzmann * (273.15 + 27.0) / kElectron;

    for (int i = 1; i <= net.nodeCount; ++i)
        result.nodeLabels << nodeLabel(circuit, net, i);

    result.log << QStringLiteral("节点数 %1，支路未知量 %2，元件 %3")
                      .arg(net.nodeCount)
                      .arg(p.nBranches)
                      .arg(p.elements.size());

    ElementState st;
    std::vector<double> x(size_t(p.nNodes + p.nBranches), 0.0);
    QString error;

    // 统一先求直流工作点，作为瞬态初值与交流线性化点
    if (!newtonSolve(p, st, false, 0.0, 0.0, x, dm, &error)) {
        result.error = QStringLiteral("直流工作点求解失败：") + error;
        return result;
    }

    if (options.kind == AnalysisKind::OperatingPoint) {
        result.ok = true;
        result.report = buildReport(circuit, p, net, x, st, dm, 0.0);
        result.elapsedMs = double(timer.nsecsElapsed()) / 1e6;
        return result;
    }

    // 用工作点初始化动态元件状态（电容电压、电感电流）
    updateState(p, st, x);

    if (options.kind == AnalysisKind::Transient) {
        double tStop = options.tStop > 0.0 ? options.tStop : 0.01;
        double h = options.tStep > 0.0 ? options.tStep : tStop / 1000.0;
        double steps = std::ceil(tStop / h);
        if (steps > kMaxPoints) {
            h = tStop / kMaxPoints;
            steps = kMaxPoints;
            result.log << QStringLiteral("步数过多，已自动调整为 %1 步。").arg(kMaxPoints);
        }
        const int stepCount = std::max(1, int(steps));

        result.waves = makeSignals(p);
        for (Signal& s : result.waves)
            s.x.reserve(stepCount + 1), s.y.reserve(stepCount + 1);

        QVector<double> values;
        sample(p, x, st, false, h, 0.0, dm, values);

        auto appendPoint = [&](double time) {
            for (int i = 0; i < result.waves.size(); ++i) {
                result.waves[i].x.append(time);
                result.waves[i].y.append(values.at(i));
            }
        };
        appendPoint(0.0);

        for (int k = 1; k <= stepCount; ++k) {
            const double t = h * k;
            if (!newtonSolve(p, st, true, h, t, x, dm, &error)) {
                result.error = QStringLiteral("瞬态求解在 t = %1 时失败：%2")
                                   .arg(formatEng(t, QStringLiteral("s"), 4), error);
                result.waves.clear();
                return result;
            }
            sample(p, x, st, true, h, t, dm, values);
            appendPoint(t);
            updateState(p, st, x);
        }

        result.ok = true;
        result.log << QStringLiteral("瞬态分析完成：%1 步，步长 %2，终止于 %3")
                          .arg(stepCount)
                          .arg(formatEng(h, QStringLiteral("s"), 3),
                               formatEng(tStop, QStringLiteral("s"), 3));
        result.elapsedMs = double(timer.nsecsElapsed()) / 1e6;
        return result;
    }

    // ------------------------------------------------------------ 交流扫频
    double fStart = options.fStart > 0.0 ? options.fStart : 10.0;
    double fStop = options.fStop > fStart ? options.fStop : fStart * 100.0;
    const int points = std::max(2, options.acPoints);

    result.waves.reserve(p.nNodes);
    for (int i = 1; i <= p.nNodes; ++i) {
        Signal s;
        s.name = QStringLiteral("V(%1)").arg(i);
        s.unit = QStringLiteral("dB");
        s.x.reserve(points);
        s.y.reserve(points);
        result.waves.append(s);
    }

    // 电流测试点：交流下输出支路电流幅值（dB）
    QVector<int> currentProbeBranches;
    for (const Component& comp : circuit.components) {
        if (comp.type != ComponentType::CurrentProbe)
            continue;
        const int br = p.branchOf.value(comp.id, -1);
        if (br < 0)
            continue;
        Signal s;
        s.name = QStringLiteral("I(%1)").arg(comp.refdes);
        s.unit = QStringLiteral("dB");
        s.x.reserve(points);
        s.y.reserve(points);
        result.waves.append(s);
        currentProbeBranches.append(br);
    }

    const int n = p.nNodes + p.nBranches;
    Matrix<Cx> a(n);
    std::vector<Cx> b(size_t(n), Cx(0.0, 0.0));
    std::vector<Cx> xc;

    for (int i = 0; i < points; ++i) {
        const double ratio = points > 1 ? double(i) / double(points - 1) : 0.0;
        const double f = fStart * std::pow(fStop / fStart, ratio);
        const double omega = 2.0 * kPi * f;

        a.zero();
        std::fill(b.begin(), b.end(), Cx(0.0, 0.0));
        assembleAc(p, st, omega, a, b, dm);
        if (!a.solve(b, xc, &error)) {
            result.error = QStringLiteral("交流分析在 %1 处失败：%2")
                               .arg(formatEng(f, QStringLiteral("Hz"), 4), error);
            result.waves.clear();
            return result;
        }

        for (int node = 1; node <= p.nNodes; ++node) {
            const double mag = std::abs(xc[size_t(node - 1)]);
            const double db = 20.0 * std::log10(std::max(mag, 1e-30));
            result.waves[node - 1].x.append(f);
            result.waves[node - 1].y.append(db);
        }

        for (int k = 0; k < currentProbeBranches.size(); ++k) {
            const double mag = std::abs(xc[size_t(p.nNodes + currentProbeBranches.at(k))]);
            const double db = 20.0 * std::log10(std::max(mag, 1e-30));
            result.waves[p.nNodes + k].x.append(f);
            result.waves[p.nNodes + k].y.append(db);
        }
    }

    result.ok = true;
    result.log << QStringLiteral("交流分析完成：%1 个频点（%2 ~ %3）")
                      .arg(points)
                      .arg(formatEng(fStart, QStringLiteral("Hz"), 3),
                           formatEng(fStop, QStringLiteral("Hz"), 3));
    result.elapsedMs = double(timer.nsecsElapsed()) / 1e6;
    return result;
}

} // namespace csm
