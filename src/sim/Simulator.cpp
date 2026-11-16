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
    double vmax = 0.9;      // 指数项的电压上限（LED 要放宽）

    double nvt() const { return emission * vt; }
    double vcrit() const { return nvt() * std::log(nvt() / (std::sqrt(2.0) * is)); }

    double current(double v) const
    {
        const double vv = std::max(-5.0, std::min(v, vmax));
        return is * (std::exp(vv / nvt()) - 1.0);
    }

    double conductance(double v) const
    {
        const double vv = std::max(-5.0, std::min(v, vmax));
        return is * std::exp(vv / nvt()) / nvt();
    }
};

/// 发光二极管模型：由「额定电流下的正向压降」反推饱和电流。
/// 取 n = 2（LED 的典型发射系数），20 mA 时压降正好等于 Vf。
constexpr double kLedRatedCurrent = 0.02;
constexpr double kLedEmission = 2.0;

DiodeModel ledModel(double vf, double vt)
{
    DiodeModel dm;
    dm.emission = kLedEmission;
    dm.vt = vt;
    dm.vmax = std::max(4.0, vf + 1.5); // 给指数项留够余量
    const double nvt = dm.nvt();
    const double target = std::max(vf, 0.8);
    dm.is = kLedRatedCurrent / (std::exp(target / nvt) - 1.0);
    return dm;
}

/// 三极管 / MOSFET / IGBT 的工作点：电流 + 线性化后的跨导
struct BjtOp {
    double ic = 0.0;   // 集电极电流（C→E）
    double ib = 0.0;   // 基极电流（B→E）
    double if0 = 0.0;  // 正偏结电流
    double ir0 = 0.0;  // 反偏结电流
    double gf = 0.0;   // dIf/dVbe
    double gr = 0.0;   // dIr/dVbc
    double alphaF = 0.99;
    double alphaR = 0.5;
};

struct MosOp {
    double id = 0.0;  // 漏极电流（D→S）
    double gm = 0.0;  // dId/dVgs
    double gds = 0.0; // dId/dVds
};

struct ElementState {
    QHash<int, double> capV;    // 上一步电容电压
    QHash<int, double> indI;    // 上一步电感电流
    QHash<int, double> diodeV;  // 上一次牛顿迭代的二极管电压
    QHash<int, double> bjtVbe;  // 三极管：上一次迭代的 vbe
    QHash<int, double> bjtVbc;  // 三极管：上一次迭代的 vbc
    QHash<int, double> mosVgs;  // MOSFET / IGBT：上一次迭代的 vgs(或 vge)
    QHash<int, double> mosVds;  // MOSFET / IGBT：上一次迭代的 vds(或 vce)
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

/// 受控电流源：电流 g·(VctlP − VctlN) 从 outP 流向 outN。
template <typename T>
void addVccs(Matrix<T>& a, int outP, int outN, int ctlP, int ctlN, T g)
{
    const int op = outP - 1;
    const int on = outN - 1;
    const int cp = ctlP - 1;
    const int cn = ctlN - 1;
    if (op >= 0 && cp >= 0)
        a(op, cp) += g;
    if (op >= 0 && cn >= 0)
        a(op, cn) -= g;
    if (on >= 0 && cp >= 0)
        a(on, cp) -= g;
    if (on >= 0 && cn >= 0)
        a(on, cn) += g;
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

constexpr double kBjtIs = 1e-14;   // 三极管饱和电流
constexpr double kMosGmin = 1e-9;  // MOSFET 输出电导下限，避免矩阵奇异

/// NPN 的 Ebers-Moll 方程与线性化系数
BjtOp bjtOperating(double vbe, double vbc, double beta, double vt)
{
    BjtOp op;
    const double safeBeta = std::max(beta, 1.0);
    op.alphaF = safeBeta / (1.0 + safeBeta);
    op.alphaR = 0.5;

    // 结电压限幅：和二极管一样，避免 exp 溢出
    const double vbeL = std::max(-5.0, std::min(vbe, 0.9));
    const double vbcL = std::max(-5.0, std::min(vbc, 0.9));

    op.if0 = kBjtIs * (std::exp(vbeL / vt) - 1.0);
    op.ir0 = kBjtIs * (std::exp(vbcL / vt) - 1.0);
    op.gf = kBjtIs * std::exp(vbeL / vt) / vt;
    op.gr = kBjtIs * std::exp(vbcL / vt) / vt;

    op.ic = op.alphaF * op.if0 - op.ir0;
    op.ib = (1.0 - op.alphaF) * op.if0 + (1.0 - op.alphaR) * op.ir0;
    return op;
}

/// 增强型 N 沟道 MOSFET 的 level-1（平方律）模型
MosOp mosOperating(double vgs, double vds, double vth, double k)
{
    MosOp op;
    const double vov = vgs - vth; // 过驱动电压

    if (vov <= 0.0) {
        // 截止区：只留一点点漏电导，帮牛顿迭代收敛
        op.id = 0.0;
        op.gm = 0.0;
        op.gds = kMosGmin;
        return op;
    }

    if (vds < vov) {
        // 线性区
        op.id = k * (vov * vds - 0.5 * vds * vds);
        op.gm = k * vds;
        op.gds = k * (vov - vds);
    } else {
        // 饱和区
        op.id = 0.5 * k * vov * vov;
        op.gm = k * vov;
        op.gds = 0.0;
    }
    op.gds += kMosGmin;
    return op;
}

/// 结电压限幅。vmax 是最终允许的上限：硅二极管约 0.9 V，
/// LED 的正向压降能到 2~3 V，必须放宽，否则状态永远上不去、迭代不收敛。
double pnjlim(double vnew, double vold, double vt, double vcrit, double vmax = 1.0)
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
    return std::max(-5.0, std::min(vnew, vmax));
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

        case ComponentType::Diode:
        case ComponentType::Led: {
            // LED 与普通二极管方程相同，只是模型参数按 Vf 反推
            const DiodeModel model = comp.type == ComponentType::Led
                ? ledModel(comp.vf, dm.vt)
                : dm;
            const double vd = st.diodeV.value(comp.id, 0.0);
            const double g = model.conductance(vd);
            const double id = model.current(vd);
            addG(a, n1, n2, g);
            addI(b, n1, n2, id - g * vd);
            break;
        }
        case ComponentType::Npn:
        case ComponentType::Pnp: {
            // 引脚：0 = 集电极 C，1 = 基极 B，2 = 发射极 E
            // PNP 与 NPN 是镜像：结电压取反后方程相同，端电流再乘回符号
            const double sign = isPType(comp.type) ? -1.0 : 1.0;
            const int nc = n1;
            const int nb = n2;
            const int ne = p.nodeOf(comp.id, 2);
            const double vbe = st.bjtVbe.value(comp.id, 0.0); // 已按 sign 归一化
            const double vbc = st.bjtVbc.value(comp.id, 0.0);
            const BjtOp op = bjtOperating(vbe, vbc, comp.beta, dm.vt);

            // 集电极电流 C→E = alphaF·If(vbe) − Ir(vbc)
            addVccs(a, nc, ne, nb, ne, op.alphaF * op.gf);
            addVccs(a, nc, ne, nb, nc, -op.gr);
            addI(b, nc, ne, sign * (op.alphaF * (op.if0 - op.gf * vbe)
                                    - (op.ir0 - op.gr * vbc)));

            // 基极电流 B→E = (1−alphaF)·If(vbe) + (1−alphaR)·Ir(vbc)
            addVccs(a, nb, ne, nb, ne, (1.0 - op.alphaF) * op.gf);
            addVccs(a, nb, ne, nb, nc, (1.0 - op.alphaR) * op.gr);
            addI(b, nb, ne, sign * ((1.0 - op.alphaF) * (op.if0 - op.gf * vbe)
                                    + (1.0 - op.alphaR) * (op.ir0 - op.gr * vbc)));
            break;
        }

        case ComponentType::Nmos:
        case ComponentType::Pmos: {
            // 引脚：0 = 漏极 D，1 = 栅极 G，2 = 源极 S（栅极电流为 0）
            const int nd = n1;
            const int ng = n2;
            const int ns = p.nodeOf(comp.id, 2);
            const double vgs = st.mosVgs.value(comp.id, 0.0);
            const double vds = st.mosVds.value(comp.id, 0.0);
            const MosOp op = mosOperating(vgs, vds, comp.vth, comp.k);

            const double sign = isPType(comp.type) ? -1.0 : 1.0;
            addVccs(a, nd, ns, ng, ns, op.gm);  // 控制 vgs = Vg − Vs
            addVccs(a, nd, ns, nd, ns, op.gds); // 控制 vds = Vd − Vs
            addI(b, nd, ns, sign * (op.id - op.gm * vgs - op.gds * vds));
            break;
        }

        case ComponentType::Igbt: {
            // IGBT 用「栅极像 MOSFET、输出侧带 Vf 压降」的简化模型
            const int nc = n1;
            const int ng = n2;
            const int ne = p.nodeOf(comp.id, 2);
            const double vge = st.mosVgs.value(comp.id, 0.0);
            const double vce = st.mosVds.value(comp.id, 0.0);
            const double vf = std::max(comp.vf, 0.0);
            const MosOp op = mosOperating(vge, vce - vf, comp.vth, comp.k);

            addVccs(a, nc, ne, ng, ne, op.gm);
            addVccs(a, nc, ne, nc, ne, op.gds);
            addI(b, nc, ne, op.id - op.gm * vge - op.gds * (vce - vf));
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
            if (cp->type != ComponentType::Diode && cp->type != ComponentType::Led)
                continue;
            hasDiode = true;
            const int n1 = p.nodeOf(cp->id, 0);
            const int n2 = p.nodeOf(cp->id, 1);
            const double vNew = p.voltage(x, n1) - p.voltage(x, n2);
            const double vOld = st.diodeV.value(cp->id, 0.0);
            const DiodeModel model = cp->type == ComponentType::Led
                ? ledModel(cp->vf, dm.vt)
                : dm;
            const double vLim = pnjlim(vNew, vOld, model.nvt(), model.vcrit(), model.vmax);
            diodeDelta = std::max(diodeDelta, std::abs(vLim - vOld));
            st.diodeV.insert(cp->id, vLim);
        }

        // 三极管：两个 PN 结同样用 pnjlim 限幅，避免指数项把迭代踢飞。
        // PNP 的结电压先取反再限幅，存储的始终是“归一化后”的正向结电压。
        for (const Component* cp : p.elements) {
            if (cp->type != ComponentType::Npn && cp->type != ComponentType::Pnp)
                continue;
            const double sign = isPType(cp->type) ? -1.0 : 1.0;
            hasDiode = true;
            const int nc = p.nodeOf(cp->id, 0);
            const int nb = p.nodeOf(cp->id, 1);
            const int ne = p.nodeOf(cp->id, 2);
            const double vb = p.voltage(x, nb);
            const double vbeOld = st.bjtVbe.value(cp->id, 0.0);
            const double vbcOld = st.bjtVbc.value(cp->id, 0.0);
            const double vbe = pnjlim(sign * (vb - p.voltage(x, ne)), vbeOld, dm.vt, 0.8);
            const double vbc = pnjlim(sign * (vb - p.voltage(x, nc)), vbcOld, dm.vt, 0.8);
            diodeDelta = std::max({diodeDelta, std::abs(vbe - vbeOld), std::abs(vbc - vbcOld)});
            st.bjtVbe.insert(cp->id, vbe);
            st.bjtVbc.insert(cp->id, vbc);
        }

        // MOSFET / IGBT：栅源、漏源电压每步最多变化 0.5 V，给平方律模型做阻尼
        for (const Component* cp : p.elements) {
            if (cp->type != ComponentType::Nmos && cp->type != ComponentType::Pmos
                && cp->type != ComponentType::Igbt)
                continue;
            const double sign = isPType(cp->type) ? -1.0 : 1.0;
            hasDiode = true;
            const int n1 = p.nodeOf(cp->id, 0);
            const int ng = p.nodeOf(cp->id, 1);
            const int n2 = p.nodeOf(cp->id, 2);
            const double vgsOld = st.mosVgs.value(cp->id, 0.0);
            const double vdsOld = st.mosVds.value(cp->id, 0.0);
            const double vgsNew = sign * (p.voltage(x, ng) - p.voltage(x, n2));
            const double vdsNew = sign * (p.voltage(x, n1) - p.voltage(x, n2));
            const double vgs = vgsOld + std::max(-0.5, std::min(vgsNew - vgsOld, 0.5));
            const double vds = vdsOld + std::max(-0.5, std::min(vdsNew - vdsOld, 0.5));
            diodeDelta = std::max({diodeDelta, std::abs(vgs - vgsOld), std::abs(vds - vdsOld)});
            st.mosVgs.insert(cp->id, vgs);
            st.mosVds.insert(cp->id, vds);
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
        case ComponentType::Led: {
            const DiodeModel model = comp.type == ComponentType::Led
                ? ledModel(comp.vf, dm.vt)
                : dm;
            current = model.current(v);
            break;
        }
        case ComponentType::Switch:
            current = v / (comp.closed ? 1e-3 : 1e9);
            break;
        case ComponentType::Npn:
        case ComponentType::Pnp: {
            const double vbe = st.bjtVbe.value(comp.id, 0.0);
            const double vbc = st.bjtVbc.value(comp.id, 0.0);
            const double sign = isPType(comp.type) ? -1.0 : 1.0;
            current = sign * bjtOperating(vbe, vbc, comp.beta, dm.vt).ic;
            break;
        }
        case ComponentType::Nmos:
        case ComponentType::Pmos: {
            const double vgs = st.mosVgs.value(comp.id, 0.0);
            const double vds = st.mosVds.value(comp.id, 0.0);
            const double sign = isPType(comp.type) ? -1.0 : 1.0;
            current = sign * mosOperating(vgs, vds, comp.vth, comp.k).id;
            break;
        }
        case ComponentType::Igbt: {
            const double vge = st.mosVgs.value(comp.id, 0.0);
            const double vce = st.mosVds.value(comp.id, 0.0);
            const double vf = std::max(comp.vf, 0.0);
            current = mosOperating(vge, vce - vf, comp.vth, comp.k).id;
            break;
        }
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
        case ComponentType::Led: {
            const DiodeModel model = comp.type == ComponentType::Led
                ? ledModel(comp.vf, dm.vt)
                : dm;
            addG(a, n1, n2, Cx(model.conductance(st.diodeV.value(comp.id, 0.6)), 0.0));
            break;
        }
            break;
        case ComponentType::Npn:
        case ComponentType::Pnp: {
            const int nc = n1;
            const int nb = n2;
            const int ne = p.nodeOf(comp.id, 2);
            const double vbe = st.bjtVbe.value(comp.id, 0.0);
            const double vbc = st.bjtVbc.value(comp.id, 0.0);
            const BjtOp op = bjtOperating(vbe, vbc, comp.beta, dm.vt);

            // 交流分析只保留工作点处的小信号跨导
            addVccs(a, nc, ne, nb, ne, Cx(op.alphaF * op.gf, 0.0));
            addVccs(a, nc, ne, nb, nc, Cx(-op.gr, 0.0));
            addVccs(a, nb, ne, nb, ne, Cx((1.0 - op.alphaF) * op.gf, 0.0));
            addVccs(a, nb, ne, nb, nc, Cx((1.0 - op.alphaR) * op.gr, 0.0));
            break;
        }

        case ComponentType::Nmos:
        case ComponentType::Pmos: {
            const int nd = n1;
            const int ng = n2;
            const int ns = p.nodeOf(comp.id, 2);
            const double vgs = st.mosVgs.value(comp.id, 0.0);
            const double vds = st.mosVds.value(comp.id, 0.0);
            const MosOp op = mosOperating(vgs, vds, comp.vth, comp.k);

            addVccs(a, nd, ns, ng, ns, Cx(op.gm, 0.0));
            addVccs(a, nd, ns, nd, ns, Cx(op.gds, 0.0));
            break;
        }

        case ComponentType::Igbt: {
            const int nc = n1;
            const int ng = n2;
            const int ne = p.nodeOf(comp.id, 2);
            const double vge = st.mosVgs.value(comp.id, 0.0);
            const double vce = st.mosVds.value(comp.id, 0.0);
            const double vf = std::max(comp.vf, 0.0);
            const MosOp op = mosOperating(vge, vce - vf, comp.vth, comp.k);

            addVccs(a, nc, ne, ng, ne, Cx(op.gm, 0.0));
            addVccs(a, nc, ne, nc, ne, Cx(op.gds, 0.0));
            break;
        }

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

// ---------------------------------------------------------------- 增量瞬态
struct LiveTransient::Impl {
    Circuit circuit;
    Netlist net;
    Problem problem;
    DiodeModel diode;
    ElementState state;
    std::vector<double> x;
    QStringList names;
    double h = 1e-5;
    double t = 0.0;
    bool started = false;
};

LiveTransient::LiveTransient()
    : m_impl(new Impl)
{
}

LiveTransient::~LiveTransient() = default;

bool LiveTransient::start(const Circuit& circuit, double step, QString* error)
{
    Impl& d = *m_impl;
    d.circuit = circuit;
    d.h = step > 0.0 ? step : 1e-5;
    d.net = circuit.buildNetlist();
    if (!d.net.valid) {
        if (error)
            *error = d.net.error;
        return false;
    }
    if (d.net.nodeCount == 0) {
        if (error)
            *error = QStringLiteral("电路中没有可求解的节点。");
        return false;
    }

    d.diode = DiodeModel();
    d.diode.vt = kBoltzmann * (273.15 + 27.0) / kElectron;
    d.problem = buildProblem(d.circuit, d.net);
    d.x.assign(size_t(d.problem.nNodes + d.problem.nBranches), 0.0);
    d.state = ElementState();
    d.t = 0.0;

    if (!newtonSolve(d.problem, d.state, false, 0.0, 0.0, d.x, d.diode, error))
        return false;
    updateState(d.problem, d.state, d.x);

    d.names.clear();
    for (int i = 1; i <= d.problem.nNodes; ++i)
        d.names << QStringLiteral("V(%1)").arg(i);
    for (const Component* comp : d.problem.elements)
        d.names << QStringLiteral("I(%1)").arg(comp->refdes);

    d.started = true;
    return true;
}

bool LiveTransient::updateCircuit(const Circuit& circuit, QString* error)
{
    Impl& d = *m_impl;
    if (!d.started)
        return start(circuit, d.h, error);

    Netlist net = circuit.buildNetlist();
    if (!net.valid) {
        if (error)
            *error = net.error;
        return false;
    }
    if (net.nodeCount == 0) {
        if (error)
            *error = QStringLiteral("电路中没有可求解的节点。");
        return false;
    }

    // 拓扑是否一致：节点数相同，且每个元件的引脚落点都没变
    const bool sameTopology = net.nodeCount == d.net.nodeCount && net.compPins == d.net.compPins;

    const double t = d.t; // 时间轴继续走，不清零
    ElementState oldState = d.state;
    std::vector<double> oldX = d.x;

    d.circuit = circuit;
    d.net = net;
    d.problem = buildProblem(d.circuit, d.net);

    const int size = d.problem.nNodes + d.problem.nBranches;
    if (sameTopology && int(oldX.size()) == size) {
        d.x = oldX;         // 保持上一时刻的解，迭代收敛更快
        d.state = oldState; // 电容电压、电感电流继续用
    } else {
        d.state = ElementState();
        d.x.assign(size_t(size), 0.0);
    }
    d.t = t;

    // 在当前时刻重解一次工作点，让新的拓扑/数值立即生效
    if (!newtonSolve(d.problem, d.state, false, 0.0, d.t, d.x, d.diode, error))
        return false;
    updateState(d.problem, d.state, d.x);

    d.names.clear();
    for (int i = 1; i <= d.problem.nNodes; ++i)
        d.names << QStringLiteral("V(%1)").arg(i);
    for (const Component* comp : d.problem.elements)
        d.names << QStringLiteral("I(%1)").arg(comp->refdes);
    return true;
}

bool LiveTransient::advance(int steps, QString* error)
{
    Impl& d = *m_impl;
    if (!d.started)
        return false;

    for (int i = 0; i < steps; ++i) {
        d.t += d.h;
        if (!newtonSolve(d.problem, d.state, true, d.h, d.t, d.x, d.diode, error))
            return false;
        updateState(d.problem, d.state, d.x);
    }
    return true;
}

double LiveTransient::time() const
{
    return m_impl->t;
}

double LiveTransient::step() const
{
    return m_impl->h;
}

int LiveTransient::nodeCount() const
{
    return m_impl->problem.nNodes;
}

const QStringList& LiveTransient::signalNames() const
{
    return m_impl->names;
}

QVector<double> LiveTransient::values() const
{
    QVector<double> out;
    sample(m_impl->problem, m_impl->x, m_impl->state, true, m_impl->h, m_impl->t, m_impl->diode,
           out);
    return out;
}

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
        QVector<double> currents;
        sample(p, x, st, false, 0.0, 0.0, dm, currents);
        for (int i = 0; i < p.elements.size(); ++i) {
            const int idx = p.nNodes + i;
            if (idx < currents.size())
                result.elementCurrents.insert(
                    QStringLiteral("I(%1)").arg(p.elements.at(i)->refdes), currents.at(idx));
        }
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
