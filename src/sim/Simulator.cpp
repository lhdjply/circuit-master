#include "sim/Simulator.h"
#include "core/Language.h"

#include "core/Units.h"
#include "sim/Matrix.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <complex>
#include <vector>

namespace csm {

namespace {

constexpr double kBoltzmann = 1.380649e-23;
constexpr double kElectron = 1.602176634e-19;
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxPoints = 200000;

/// 逻辑门输出级的内阻：输出按「理想电平 + 串一个电阻」等效
constexpr double kGateRout = 100.0;
/// 逻辑门的输入回差（相对输出高电平）：高于 0.55·VOH 判 1，低于 0.45·VOH 判 0。
/// 回差是必需的 —— 没有它，输入恰好压在阈值上时牛顿迭代会在两个状态之间来回跳
constexpr double kGateHysteresis = 0.05;

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
    QHash<int, double> indI;    // 上一步电感电流（变压器：初级）
    QHash<int, QVector<double>> windingI;  // 变压器：各绕组上一步电流
    QHash<int, double> diodeV;  // 上一次牛顿迭代的二极管电压
    QHash<int, double> bjtVbe;  // 三极管：上一次迭代的 vbe
    QHash<int, double> bjtVbc;  // 三极管：上一次迭代的 vbc
    QHash<int, double> mosVgs;  // MOSFET / IGBT：上一次迭代的 vgs(或 vge)
    QHash<int, double> mosVds;  // MOSFET / IGBT：上一次迭代的 vds(或 vce)
    QHash<int, double> optoVce; // 光耦输出侧：上一次迭代的 Vce
    QHash<int, double> tlVref;  // TL431：上一次迭代的参考端电压（相对阳极）
    QHash<int, double> obFb;     // OB2362：上一步的 FB 电压
    QHash<int, double> obCs;     // OB2362：上一步的 CS 电压
    QHash<int, double> obVcc;    // OB2362：上一步的 VCC 电压
    QHash<int, double> obUvloOn; // OB2362：UVLO 锁存状态（0 关 / 1 开）
    /// 逻辑门：上一轮迭代决定的输出电平（0 或 logicHigh）。
    /// 输入按阈值 + 回差判定，输出用戴维南等效（源内阻 kGateRout）打进矩阵。
    QHash<int, double> gateLevel;

    /// 万用表交流档的滑动窗口：存「正好一个周期」的采样，算交流有效值。
    /// 窗口样本数 = round(period / (decim·h))，所以窗口跨度正好是一个整周期，
    /// 正弦的有效值就是 A/√2，不会有「窗口差一点点」带来的偏差。
    /// 采样过密（h 很小、周期很长）时按 decim 抽稀，保证样本数有上界。
    struct AcWindow {
        QVector<double> samples;
        int index = 0;   // 下一个写入位置
        int filled = 0;  // 已填样本数
        int decim = 1;   // 每 decim 个采样存一个
        int skip = 0;    // 抽稀计数器
    };
    QHash<int, AcWindow> meterAc;
    QHash<int, double> meterAcRms; // 最近一次算出的交流有效值

    /// 把一次采样喂进交流窗口，返回整窗的交流有效值（窗口没满时返回 NaN）
    double feedAcWindow(int id, double value, double h, double period)
    {
        constexpr int kMaxSamples = 20000;
        AcWindow& w = meterAc[id];

        if (w.samples.isEmpty()) {
            // 首次调用：按 period/h 定窗口大小，样本过多就抽稀
            const double raw = std::max(period / std::max(h, 1e-15), 2.0);
            w.decim = std::max(1, int(std::ceil(raw / kMaxSamples)));
            // 用 fill 而不是 assign：QList::assign 要 Qt 6.6，Debian 12 上是 6.4
            w.samples.fill(0.0, std::max(2, int(std::lround(period / (w.decim * h)))));
            w.index = 0;
            w.filled = 0;
            w.skip = 0;
        }

        if (++w.skip < w.decim)
            return std::numeric_limits<double>::quiet_NaN();
        w.skip = 0;

        w.samples[w.index] = value;
        w.index = (w.index + 1) % w.samples.size();
        if (w.filled < w.samples.size())
            ++w.filled;

        // 还没攒够一个整周期：返回 NaN，界面显示一排横杠。
        // 真实万用表在低频档也要等几个周期才稳住，这里不假装已经量到了。
        if (w.filled < w.samples.size())
            return std::numeric_limits<double>::quiet_NaN();

        double total = 0.0, totalSq = 0.0;
        for (double v : w.samples) {
            total += v;
            totalSq += v * v;
        }
        const double n = double(w.samples.size());
        const double mean = total / n;
        return std::sqrt(std::max(totalSq / n - mean * mean, 0.0));
    }
};

/// 变压器的绕组：每个绕组是一对引脚（按局部引脚编号）
QVector<QPair<int, int>> transformerWindings(const Component& comp)
{
    if (comp.transformerKind == TransformerKind::PrimaryAuxSecondary) {
        return {{0, 1}, {2, 3}, {4, 5}}; // 初级1、初级2、次级
    }
    return {{0, 1}, {2, 3}}; // 初级、次级
}

/// 各绕组的同名端极性：+1 同名端在上，−1 同名端在下（次级反相）
QVector<double> transformerPolarities(const Component& comp)
{
    const double secondary = comp.secondaryInverted ? -1.0 : 1.0;
    if (comp.transformerKind == TransformerKind::PrimaryAuxSecondary)
        return {1.0, 1.0, secondary};
    return {1.0, secondary};
}

/// 各绕组的电感：L_i = L1 · (N_i/N1)²，初级 N1、辅助绕组 N2、次级 N3
QVector<double> transformerInductances(const Component& comp)
{
    const double n = comp.value > 0.01 ? comp.value : 1.0;
    const double aux = comp.auxRatio > 0.01 ? comp.auxRatio : 1.0;
    const double l1 = std::max(comp.lpri, 1e-9);
    if (comp.transformerKind == TransformerKind::PrimaryAuxSecondary)
        return {l1, l1 * aux * aux, l1 * n * n};
    return {l1, l1 * n * n};
}

struct Problem {
    const Circuit* circuit = nullptr;
    const Netlist* net = nullptr;
    int nNodes = 0;
    int nBranches = 0;
    QHash<int, int> branchOf;
    QHash<int, QVector<int>> windingBranches;  // 变压器：各绕组的支路编号
    QHash<int, int> referenceNode;             // GroundKind → 该地网络的节点号
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
/// 极性电容的反向耐压：超过这个反向电压就当作击穿（界面放爆炸特效）
constexpr double kCapReverseLimit = -1.0;

/// 记录极性电容的端电压（引脚 0 为「+」）：只保留最负的一次
void trackReverseCaps(const Problem& p, const std::vector<double>& x,
                      QHash<int, double>& worst)
{
    for (const Component* comp : p.elements) {
        if (comp->type != ComponentType::Capacitor || !comp->polarized)
            continue;
        const double v = p.voltage(x, p.nodeOf(comp->id, 0)) - p.voltage(x, p.nodeOf(comp->id, 1));
        if (!worst.contains(comp->id) || v < worst.value(comp->id))
            worst.insert(comp->id, v); // 反接时为负，越负越严重
    }
}

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
        if (comp.type == ComponentType::Ground) {
            // 记录每种地的节点号，供测试点选参考地；接地符号本身也要参与装配，
            // 非主地在单点处串一个隔离电阻连到主地
            p.referenceNode.insert(int(comp.groundKind), net.nodeOf(comp.id, 0));
            p.elements.append(&comp);
            continue;
        }
        if (isVoltageTap(comp.type)) {
            p.elements.append(&comp);
            continue;
        }
        p.elements.append(&comp);
        if (comp.type == ComponentType::Transformer) {
            // 每个绕组一个支路未知量
            QVector<int> branches;
            bool ok = true;
            for (const auto& pair : transformerWindings(comp)) {
                const int a = net.nodeOf(comp.id, pair.first);
                const int b = net.nodeOf(comp.id, pair.second);
                if (a < 0 || b < 0) {
                    ok = false;
                    break;
                }
                branches.append(p.nBranches);
                p.branchNodes.append(qMakePair(a, b));
                ++p.nBranches;
            }
            if (ok) {
                p.windingBranches.insert(comp.id, branches);
                // branchOf 只记一条：初级绕组。装配走上面的 windingBranches，
                // 但「读这个元件的电流」全走 branchOf（报告、elementCurrents、
                // 波形），不登记的话 I(T1) 永远是 0。
                if (!branches.isEmpty())
                    p.branchOf.insert(comp.id, branches.first());
            }
            continue;
        }
        const bool needsBranch = comp.type == ComponentType::VoltageSource
            || comp.type == ComponentType::SignalGenerator
            || comp.type == ComponentType::Inductor || comp.type == ComponentType::OpAmp
            || comp.type == ComponentType::CurrentProbe
            || (comp.type == ComponentType::Multimeter
                && (comp.multimeterMode == MultimeterMode::Current
                    || comp.multimeterMode == MultimeterMode::AcCurrent))
            || comp.type == ComponentType::Ob2362; // GATE 输出当作电压源
        if (needsBranch) {
            int n1 = -1;
            int n2 = 0;
            if (comp.type == ComponentType::OpAmp) {
                n1 = net.nodeOf(comp.id, 2); // 输出端
                n2 = 0;                      // 参考地
            } else if (comp.type == ComponentType::Ob2362) {
                n1 = net.nodeOf(comp.id, 5); // GATE
                n2 = 0;                      // 以主地为参考
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

int currentSlotCount(const Problem& p, const Component& comp)
{
    if (comp.type != ComponentType::Transformer)
        return 1;
    // 变压器没登记上绕组时也占一路，保证采样向量与信号表的布局一致
    const QVector<int> branches = p.windingBranches.value(comp.id);
    return branches.isEmpty() ? 1 : branches.size();
}

QString currentLabel(const Component& comp, int slot)
{
    if (slot == 0)
        return comp.refdes;
    return QStringLiteral("%1.%2").arg(comp.refdes).arg(slot + 1);
}

QString currentSignalName(const Component& comp, int slot)
{
    return QStringLiteral("I(%1)").arg(currentLabel(comp, slot));
}

/// 逻辑门的输出电平：按回差判定每个输入是高还是低，再按门类型算布尔函数。
/// 返回 0 或 comp.logicHigh。
double gateLevelFor(const Problem& p, const Component& comp, const std::vector<double>& x,
                    const ElementState& st)
{
    const double vhigh = comp.logicHigh > 0.0 ? comp.logicHigh : 3.3;
    const double vUp = (0.5 + kGateHysteresis) * vhigh;   // 判 1 的阈值
    const double vDown = (0.5 - kGateHysteresis) * vhigh; // 判 0 的阈值
    const bool wasHigh = st.gateLevel.value(comp.id, 0.0) > 0.5 * vhigh;

    auto isHigh = [&](int pin) {
        const double v = p.voltage(x, p.nodeOf(comp.id, pin));
        // 当前是高就按低阈值判（保持），当前是低就按高阈值判（翻转）
        return v >= (wasHigh ? vDown : vUp);
    };

    const int inputs = gateInputCount(comp.gateKind);
    const bool in0 = isHigh(0);
    const bool in1 = inputs > 1 ? isHigh(1) : false;

    bool on = false;
    switch (comp.gateKind) {
    case GateKind::And:  on = in0 && in1; break;
    case GateKind::Or:   on = in0 || in1; break;
    case GateKind::Not:  on = !in0; break;
    case GateKind::Xor:  on = in0 != in1; break;
    case GateKind::Xnor: on = in0 == in1; break;
    }
    return on ? vhigh : 0.0;
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
        case ComponentType::LogicGate: {
            // 输出端：戴维南等效「电平源 + 100 Ω 内阻」，输入引脚是高阻（不贡献矩阵）
            const int outPin = gateInputCount(comp.gateKind);
            const int nout = p.nodeOf(comp.id, outPin);
            const double g = 1.0 / kGateRout;
            const double level = st.gateLevel.value(comp.id, 0.0);
            addG(a, nout, 0, g);
            addI(b, nout, 0, -g * level);
            break;
        }
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
        case ComponentType::Transformer: {
            // 耦合电感模型（耦合系数取 1）：每个绕组一条支路，任意两个绕组之间
            //   M_ij = √(L_i·L_j)
            // 后向欧拉离散后，第 i 条支路方程是
            //   V(a_i) − V(b_i) − Σ_j (L_ij/h)·i_j = −Σ_j (L_ij/h)·i_j^prev
            // 直流稳态下所有绕组都是短路，次级输出 0 V —— 和真实变压器一致。
            const QVector<int> branches = p.windingBranches.value(comp.id);
            const QVector<double> inds = transformerInductances(comp);
            if (branches.size() != inds.size())
                break;
            const QVector<double> prev = st.windingI.value(comp.id);
            const QVector<double> polarities = transformerPolarities(comp);
            const int count = inds.size();

            // 绕组电阻：理想电感在直流下是短路，直接并在理想电压源上会让矩阵奇异
            for (int i = 0; i < count; ++i) {
                const int row = p.nNodes + branches.at(i);
                const int a1 = p.branchNodes.at(branches.at(i)).first;
                const int a2 = p.branchNodes.at(branches.at(i)).second;

                // KCL：本绕组电流流进 a1、流出 a2
                if (a1 > 0)
                    a(a1 - 1, row) += 1.0;
                if (a2 > 0)
                    a(a2 - 1, row) -= 1.0;

                // 支路方程：V(a1) − V(a2) − Σ_j (L_ij/h)·i_j = −Σ_j (L_ij/h)·i_j^prev
                if (a1 > 0)
                    a(row, a1 - 1) += 1.0;
                if (a2 > 0)
                    a(row, a2 - 1) -= 1.0;

                double rhs = 0.0;
                for (int j = 0; j < count; ++j) {
                    const double mij = polarities.at(i) * polarities.at(j)
                        * std::sqrt(inds.at(i) * inds.at(j));
                    const double coef = transient ? mij / h : 0.0;
                    a(row, p.nNodes + branches.at(j)) -= coef;
                    if (transient)
                        rhs -= coef * prev.value(j, 0.0);
                }
                const double rs = 0.1 * inds.at(i) / inds.first();
                a(row, row) -= rs;
                b[size_t(row)] = rhs;
            }
            break;
        }

        case ComponentType::VoltageSource:
            addBranch(a, b, p, comp.id, 0.0, comp.sourceValueAt(t));
            break;

        case ComponentType::SignalGenerator:
            // 和电压源一样是理想电压支路，只是波形由 waveform 决定
            addBranch(a, b, p, comp.id, 0.0, comp.signalValueAt(t));
            break;

        case ComponentType::CurrentProbe:
            addBranch(a, b, p, comp.id, 0.0, 0.0);
            break;

        case ComponentType::Multimeter:
            // 直流/交流电压档：10 MΩ 内阻并联（真实 DMM 的量级，对电路的扰动可忽略）
            // 直流/交流电流档：串入一条 0 V 支路（安培计），电流从支路未知量里读
            // 电阻档：注入 1 A 测试电流（从引脚 0 流向外部、经被测网络回到引脚 1），
            //         读 V(0) − V(1) 即 R（Ω）；1 A 是为了让常见阻值直接读出欧姆数
            //
            // 交流档不做真正的隔直电容：仿真里串一个 20 ms 级的电容会拖慢收敛，
            // 隔直放到读数环节做（扣掉窗口均值），效果与真表一致。
            switch (comp.multimeterMode) {
            case MultimeterMode::Voltage:
            case MultimeterMode::AcVoltage:
                addG(a, n1, n2, 1.0 / 10e6);
                break;
            case MultimeterMode::Current:
            case MultimeterMode::AcCurrent:
                addBranch(a, b, p, comp.id, 0.0, 0.0);
                break;
            case MultimeterMode::Resistance:
                addI(b, n2, n1, 1.0);
                break;
            }
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
        case ComponentType::Tl431: {
            // 引脚：0 = REF，1 = 阳极 A，2 = 阴极 K
            const int nr = n1;
            const int na = n2;
            const int nk = p.nodeOf(comp.id, 2);
            const double vref0 = comp.value > 0.5 ? comp.value : 2.495;

            // 参考端电压（相对阳极）驱动一只「分流管」：Vref 高于基准时阴极开始吸电流，
            // 指数特性让电流随过驱动电压快速上升，宏观上就是 Vka 被稳在设定值附近
            const double vref = st.tlVref.value(comp.id, 0.0);
            // 分流管：Vref 超过基准后开始吸电流，跨导取 100 S。
            // 指数特性在这里不可用 —— 参考端与阴极短接时增益过高，迭代会发散
            constexpr double kTlGain = 100.0;
            const double overdrive = std::max(vref - vref0, 0.0);
            const double ik = kTlGain * overdrive;
            const double g = vref > vref0 ? kTlGain : 0.0;

            // 分流电流 K→A，受 (REF, A) 控制。
            // 线性化在「当前工作点 vref」处展开：Ik ≈ g·vref + (Ik0 − g·vref_current)，
            // 用阈值 vref0 当展开点会多算一截电流，迭代永远收敛不了
            addVccs(a, nk, na, nr, na, g);
            addI(b, nk, na, ik - g * vref);

            // 阳极→阴极的正向通路（相当于内部那只反偏保护二极管），
            // 只在 V(A) > V(K) 时导通，正常稳压时几乎不流电流
            const double vd = st.diodeV.value(comp.id, 0.0);
            const double gd = dm.conductance(vd);
            const double id = dm.current(vd);
            addG(a, na, nk, gd);
            addI(b, na, nk, id - gd * vd);
            break;
        }

        case ComponentType::Optocoupler: {
            // 引脚：0/1 = 输入 LED 的阳极/阴极，2/3 = 输出三极管的集电极/发射极
            const int na = n1;
            const int nk = n2;
            const int nc = p.nodeOf(comp.id, 2);
            const int ne = p.nodeOf(comp.id, 3);
            const DiodeModel model = ledModel(comp.vf, dm.vt);
            const double vd = st.diodeV.value(comp.id, 0.0);
            const double g = model.conductance(vd);
            const double if0 = model.current(vd);
            const double ctr = std::max(comp.ctr, 0.0);
            const double vceSat = std::max(comp.vceSat, 1e-3);

            // 输入 LED
            addG(a, na, nk, g);
            addI(b, na, nk, if0 - g * vd);

            // 输出侧 Ic = CTR·If·f(Vce)：Vce 低于饱和压降时 f 线性收窄到 0，
            // 这样光敏管不会把发射极顶得比集电极还高（否则高 CTR 会算出 10 V）
            const double vce = st.optoVce.value(comp.id, 0.0);
            // 输出特性：Ic = CTR·If · x/(1+x)，x = Vce/VceSat。
            // 单调、处处可导：Vce=0 时输出为 0，Vce 远大于 VceSat 时接近 CTR·If，
            // 于是光敏管在饱和区表现为有限电阻，发射极不会跑到集电极之上。
            // （恒流源会算出 10 V；分段折线在边界来回跳，牛顿迭代不收敛）
            const double vcePos = std::max(vce, 0.0); // 模型在 Vce<0 时按 0 处理
            const double x = vcePos / vceSat;
            const double f = x / (1.0 + x);
            // 导数在 Vce=0 处取右侧值（不是 0）：否则从 0 出发的那一步没有电导，
            // 迭代会在「全电流」和「零电流」之间来回跳
            const double df = 1.0 / ((1.0 + x) * (1.0 + x) * vceSat);
            const double ieqIf = if0 - g * vd;

            addVccs(a, nc, ne, na, nk, ctr * f * g);
            addG(a, nc, ne, ctr * if0 * df + 1e-9);
            // 常数项也用钳到 0 的 Vce：否则 Vce 一旦为负，这一项会变成几安培的注入，
            // 迭代直接发散（CTR 较大时尤其明显）
            addI(b, nc, ne, ctr * f * ieqIf - ctr * if0 * df * vcePos);
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

        case ComponentType::Ob2362: {
            // 行为级模型（按 OB2362A 数据手册）：
            //   · 内部 65 kHz 振荡器，GATE 每周期按 FB 决定的占空比拉高到约 11 V
            //   · 峰值电流模式：CS 达到限流阈值就关断
            //   · FB 脚：内部 30 kΩ 输入阻抗 + 约 150 µA 偏置电流，开路时约 4.5 V
            //     （对应手册的开环 OLP 阈值 4.4 V），这样没有光耦拉低时能满占空比起机
            //   · VCC 脚：UVLO 17 V 开 / 7.3 V 关（带回差）、工作电流 2.5 mA、
            //     28 V 过压钳位
            const int br = p.branchOf.value(comp.id, -1);
            if (br < 0)
                break;

            const int nfb = p.nodeOf(comp.id, 1);
            const int ncs = p.nodeOf(comp.id, 3);
            const int nvcc = p.nodeOf(comp.id, 4);

            const double vfb = st.obFb.value(comp.id, 0.0);
            const double vcs = st.obCs.value(comp.id, 0.0);
            const double vcc = st.obVcc.value(comp.id, 0.0);
            const bool uvloOn = st.obUvloOn.value(comp.id, 0.0) > 0.5;

            // FB 脚：30 kΩ 输入阻抗 + 150 µA 偏置
            addG(a, nfb, 0, 1.0 / 30000.0);
            addI(b, 0, nfb, 150e-6);

            // VCC 脚：工作电流 + 28 V 钳位
            addI(b, nvcc, 0, uvloOn ? 2.5e-3 : 0.0);
            if (vcc > 28.0) {
                // I(V) = g·(V − 28) 线性化：I ≈ g·V − g·28
                const double gClamp = 5.0;
                addG(a, nvcc, 0, gClamp);
                addI(b, nvcc, 0, -gClamp * 28.0);
            }

            // GATE：FB 决定占空比，CS 达阈值则提前关断
            const double fsw = comp.frequency > 1.0 ? comp.frequency : 65000.0;
            const double period = 1.0 / fsw;
            const double phase = period > 0.0 ? std::fmod(t, period) / period : 0.0;
            const double dutyMax = std::clamp((vfb - 0.8) / 2.0, 0.0, 0.8);
            const double vlimit = comp.value > 0.01 ? comp.value : 0.72;
            const bool cycleOn = phase < dutyMax && vcs < vlimit;
            const double gate = (uvloOn && cycleOn) ? 11.0 : 0.0;
            addBranch(a, b, p, comp.id, 0.0, gate);
            (void)ncs;
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
        case ComponentType::Switch:
        case ComponentType::PushButton: {
            const double r = comp.closed ? 1e-3 : 1e9;
            addG(a, n1, n2, 1.0 / r);
            break;
        }
        case ComponentType::Ground: {
            // 各种地之间默认是**隔离**的：GND 是参考节点，AGND/DGND/PGND/EARTH
            // 各自成网，只通过 1 MΩ 漏电阻接到主地（数值上保证矩阵不奇异，
            // 物理上相当于断路：300 V 下漏电也只有 0.3 mA）。
            // 需要把它们连起来时，直接在画布上拉一根线即可。
            if (comp.groundKind != GroundKind::Gnd)
                addG(a, n1, 0, 1e-6);
            break;
        }
        case ComponentType::MeasureProbe:
        case ComponentType::Probe:
        case ComponentType::NetLabel:
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
                *error = solveError + CSM_TR(
                    ". Check for shorted voltage sources or parallel ideal sources, "
                    "or for floating nodes that are not connected.");
            return false;
        }

        double maxAbs = 0.0;
        double maxDelta = 0.0;
        for (int i = 0; i < n; ++i) {
            if (!std::isfinite(xnew[size_t(i)])) {
                if (error)
                    *error = CSM_TR("The solution contains non-finite values; the circuit may be diverging.");
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
            if (cp->type != ComponentType::Diode && cp->type != ComponentType::Led
                && cp->type != ComponentType::Optocoupler)
                continue;
            hasDiode = true;
            const int n1 = p.nodeOf(cp->id, 0);
            const int n2 = p.nodeOf(cp->id, 1);
            const double vNew = p.voltage(x, n1) - p.voltage(x, n2);
            const double vOld = st.diodeV.value(cp->id, 0.0);
            // LED / 光耦输入侧都用 LED 模型限幅：拿普通二极管的 0.9 V 上限去卡，
            // 会和 stamp 里用的模型不一致，迭代收敛到错误工作点（表现为电流天文数字）
            const DiodeModel model = (cp->type == ComponentType::Led
                                      || cp->type == ComponentType::Optocoupler)
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

        // TL431：记录参考端电压（相对阳极）与阴极-阳极电压
        for (const Component* cp : p.elements) {
            if (cp->type != ComponentType::Tl431)
                continue;
            hasDiode = true;
            const int nr = p.nodeOf(cp->id, 0);
            const int na = p.nodeOf(cp->id, 1);
            const int nk = p.nodeOf(cp->id, 2);
            const double vrefOld = st.tlVref.value(cp->id, 0.0);
            const double vrefNew = p.voltage(x, nr) - p.voltage(x, na);
            // 参考端每步最多变化 0.1 V：指数分流管的跨导很高，放开会来回振荡
            const double vref = vrefOld + std::max(-0.1, std::min(vrefNew - vrefOld, 0.1));
            diodeDelta = std::max(diodeDelta, std::abs(vref - vrefOld));
            st.tlVref.insert(cp->id, vref);

            const double vkaNew = p.voltage(x, na) - p.voltage(x, nk); // 阳极相对阴极
            const double vkaOld = st.diodeV.value(cp->id, 0.0);
            const double vka = pnjlim(vkaNew, vkaOld, dm.nvt(), dm.vcrit(), 1.0);
            diodeDelta = std::max(diodeDelta, std::abs(vka - vkaOld));
            st.diodeV.insert(cp->id, vka);
        }

        // 光耦：记录输出侧 Vce，供饱和判断用
        // 每步最多变化 0.1 V —— 饱和区边界是不连续点，放开步长会来回振荡
        for (const Component* cp : p.elements) {
            if (cp->type != ComponentType::Optocoupler)
                continue;
            hasDiode = true;
            const int nc = p.nodeOf(cp->id, 2);
            const int ne = p.nodeOf(cp->id, 3);
            // 每步最多变化 0.5 V：恒流源跨到饱和区时增益很大，放开会来回振荡；
            // 同时把变化量计入收敛判据，避免状态还没走到就被判为收敛
            const double vceOld = st.optoVce.value(cp->id, 0.0);
            const double vceNew = p.voltage(x, nc) - p.voltage(x, ne);
            diodeDelta = std::max(diodeDelta, std::abs(vceNew - vceOld));
            st.optoVce.insert(cp->id, vceNew);
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
            // 步长按电压量级放大：高压开关电路里漏源电压一步要跳几百伏，
            // 固定 0.5 V 的限幅会让迭代爬几百次都到不了解（150 次上限直接失败）
            const double vgsStep = std::max(0.5, 0.25 * std::abs(vgsOld));
            const double vdsStep = std::max(0.5, 0.25 * std::abs(vdsOld));
            const double vgs = vgsOld + std::max(-vgsStep, std::min(vgsNew - vgsOld, vgsStep));
            const double vds = vdsOld + std::max(-vdsStep, std::min(vdsNew - vdsOld, vdsStep));
            diodeDelta = std::max({diodeDelta, std::abs(vgs - vgsOld), std::abs(vds - vdsOld)});
            st.mosVgs.insert(cp->id, vgs);
            st.mosVds.insert(cp->id, vds);
        }

        // 逻辑门：按当前解重新判定每个门的输出电平。数字状态必须进迭代，
        // 否则直流工作点会停在「所有门都是低电平」上。
        double gateDelta = 0.0;
        for (const Component* cp : p.elements) {
            if (cp->type != ComponentType::LogicGate)
                continue;
            const double oldLevel = st.gateLevel.value(cp->id, 0.0);
            const double level = gateLevelFor(p, *cp, x, st);
            gateDelta = std::max(gateDelta, std::abs(level - oldLevel));
            st.gateLevel.insert(cp->id, level);
        }

        const bool nodeConverged = maxDelta <= reltol * (1.0 + maxAbs) + abstol;
        const bool diodeConverged = !hasDiode || diodeDelta <= 1e-7;
        // 门电平只要还在翻转就说明逻辑还没稳定，接着迭代
        const bool gateConverged = gateDelta <= 1e-9;
        if (nodeConverged && diodeConverged && gateConverged && iter >= 1) {
            if (maxAbs > 1e9) {
                if (error)
                    *error = CSM_TR("Node voltages exceed a sane range (>1 GV); check the circuit wiring.");
                return false;
            }
            return true;
        }
    }

    if (error)
        *error = CSM_TR("Newton iteration did not converge in 150 steps; check the loops with diodes or switches.");
    return false;
}

// ---------------------------------------------------------------- 结果采样
void sample(const Problem& p, const std::vector<double>& x, const ElementState& st,
            bool transient, double h, double t, const DiodeModel& dm, QVector<double>& out)
{
    out.clear();
    out.reserve(p.nNodes + p.elements.size() + 2);

    for (int i = 1; i <= p.nNodes; ++i)
        out.append(p.voltage(x, i));

    for (const Component* cp : p.elements) {
        const Component& comp = *cp;

        // 变压器一个绕组一路电流：初级是 I(T1)，其余 I(T1.2) / I(T1.3)。
        // 每条绕组支路本身就是一个未知量，直接取即可。
        if (comp.type == ComponentType::Transformer) {
            const QVector<int> branches = p.windingBranches.value(comp.id);
            if (branches.isEmpty()) {
                out.append(0.0);   // 没登记上也要占一路，保持布局一致
                continue;
            }
            for (int br : branches) {
                const size_t idx = size_t(p.nNodes + br);
                out.append(idx < x.size() ? x[idx] : 0.0);
            }
            continue;
        }

        const int n1 = p.nodeOf(comp.id, 0);
        const int n2 = p.nodeOf(comp.id, 1);
        const double v = p.voltage(x, n1) - p.voltage(x, n2);
        double current = 0.0;

        switch (comp.type) {
        case ComponentType::LogicGate: {
            // 输出端灌进负载的电流（正号表示往外送）
            const int outPin = gateInputCount(comp.gateKind);
            const int nout = p.nodeOf(comp.id, outPin);
            const double level = st.gateLevel.value(comp.id, 0.0);
            current = (level - p.voltage(x, nout)) / kGateRout;
            break;
        }
        case ComponentType::Resistor:
            current = v / std::max(comp.value, 1e-9);
            break;
        case ComponentType::Capacitor:
            if (transient)
                current = (std::max(comp.value, 1e-15) / h)
                    * (v - st.capV.value(comp.id, 0.0));
            break;
        case ComponentType::Ground:
        case ComponentType::NetLabel:
            // 接地符号与网络标签本身没有电流可采
            break;
        case ComponentType::Inductor:
        case ComponentType::Transformer:
        case ComponentType::Ob2362:
        case ComponentType::SignalGenerator:
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
        case ComponentType::Tl431: {
            // 报告阴极吸入的电流
            const double vref0 = comp.value > 0.5 ? comp.value : 2.495;
            const double vref = st.tlVref.value(comp.id, 0.0);
            constexpr double kTlGain = 100.0;
            current = kTlGain * std::max(vref - vref0, 0.0);
            break;
        }

        case ComponentType::Optocoupler: {
            const DiodeModel model = (comp.type == ComponentType::Led
                                      || comp.type == ComponentType::Optocoupler)
                ? ledModel(comp.vf, dm.vt)
                : dm;
            current = model.current(v);
            break;
        }
        case ComponentType::Switch:
        case ComponentType::PushButton:
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
        case ComponentType::MeasureProbe:
        case ComponentType::Probe:
            break;
        case ComponentType::Multimeter:
            // 读数走专属的 XMM 信号（与 makeSignals 末尾的顺序一致），这里不产生元件电流
            break;
        }
        out.append(current);
    }

    // 与 makeSignals 的顺序保持一致：带参考地的测试点补一条差分值
    for (const Component* cp : p.elements) {
        if (!isVoltageTap(cp->type) || cp->groundKind == GroundKind::Gnd)
            continue;
        const int a = p.nodeOf(cp->id, 0);
        const int b = p.referenceNode.value(int(cp->groundKind), 0);
        if (a > 0)
            out.append(p.voltage(x, a) - p.voltage(x, b));
    }

    // 与 makeSignals 的顺序保持一致：万用表读数
    for (const Component* cp : p.elements) {
        if (cp->type != ComponentType::Multimeter)
            continue;
        const int a = p.nodeOf(cp->id, 0);
        const int b = p.nodeOf(cp->id, 1);
        const bool currentMode = cp->multimeterMode == MultimeterMode::Current
            || cp->multimeterMode == MultimeterMode::AcCurrent;

        double instant = 0.0;
        if (currentMode) {
            const int br = p.branchOf.value(cp->id, -1);
            instant = br >= 0 ? x[size_t(p.nNodes + br)] : 0.0;
        } else {
            // 电压档：V；电阻档：V / 1 A = R（Ω）
            instant = p.voltage(x, a) - p.voltage(x, b);
        }

        if (isAcMeterMode(cp->multimeterMode)) {
            // 交流档：取滑动窗口里算好的有效值（由 updateState 在每步之后喂进去）
            const auto it = st.meterAcRms.constFind(cp->id);
            out.append(it == st.meterAcRms.constEnd() ? 0.0 : it.value());
        } else {
            out.append(instant);
        }
    }
}

QVector<Signal> makeSignals(const Problem& p)
{
    QVector<Signal> list;
    list.reserve(p.nNodes + p.elements.size());
    for (int i = 1; i <= p.nNodes; ++i)
        list.append(Signal{QStringLiteral("V(%1)").arg(i), QStringLiteral("V"), {}, {}});
    for (const Component* cp : p.elements) {
        const int count = currentSlotCount(p, *cp);   // slots 是 Qt 的关键字宏，别用它当变量名
        for (int slot = 0; slot < count; ++slot)
            list.append(Signal{currentSignalName(*cp, slot), QStringLiteral("A"), {}, {}});
    }

    // 测试点可以指定参考地：非 GND 时补一条 V(a,b) 的差分信号
    for (const Component* cp : p.elements) {
        if (!isVoltageTap(cp->type) || cp->groundKind == GroundKind::Gnd)
            continue;
        const int a = p.nodeOf(cp->id, 0);
        const int b = p.referenceNode.value(int(cp->groundKind), 0);
        if (a > 0)
            list.append(Signal{QStringLiteral("V(%1,%2)").arg(a).arg(b), QStringLiteral("V"),
                               {}, {}});
    }

    // 万用表：每个档位一条专属信号，读数含义随档位变化（V / A / Ω）
    for (const Component* cp : p.elements) {
        if (cp->type != ComponentType::Multimeter)
            continue;
        list.append(Signal{cp->refdes, multimeterModeUnit(cp->multimeterMode), {}, {}});
    }
    return list;
}

/// 把当前时刻的万用表读数喂进交流档的滑动窗口（瞬态每步调用一次）。
/// 直流工作点不调用 —— 没有时间轴，交流有效值就是 0。
void updateAcMeters(const Problem& p, ElementState& st, const std::vector<double>& x,
                    double h, double period = 0.02)
{
    for (const Component* cp : p.elements) {
        if (cp->type != ComponentType::Multimeter || !isAcMeterMode(cp->multimeterMode))
            continue;
        double instant = 0.0;
        if (cp->multimeterMode == MultimeterMode::AcCurrent) {
            const int br = p.branchOf.value(cp->id, -1);
            if (br >= 0)
                instant = x[size_t(p.nNodes + br)];
        } else {
            const int a = p.nodeOf(cp->id, 0);
            const int b = p.nodeOf(cp->id, 1);
            instant = p.voltage(x, a) - p.voltage(x, b);
        }
        st.meterAcRms.insert(cp->id, st.feedAcWindow(cp->id, instant, h, period));
    }
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
        } else if (cp->type == ComponentType::Ob2362) {
            st.obFb.insert(cp->id, p.voltage(x, p.nodeOf(cp->id, 1)));  // FB
            st.obCs.insert(cp->id, p.voltage(x, p.nodeOf(cp->id, 3)));  // CS
            const double vcc = p.voltage(x, p.nodeOf(cp->id, 4));
            st.obVcc.insert(cp->id, vcc);
            // UVLO 带回差：17 V 起机、7.3 V 关断
            double on = st.obUvloOn.value(cp->id, 0.0);
            if (on < 0.5 && vcc > 17.0)
                on = 1.0;
            else if (on > 0.5 && vcc < 7.3)
                on = 0.0;
            st.obUvloOn.insert(cp->id, on);
        } else if (cp->type == ComponentType::Transformer) {
            QVector<double> currents;
            for (int br : p.windingBranches.value(cp->id))
                currents.append(x[size_t(p.nNodes + br)]);
            st.windingI.insert(cp->id, currents);
        }
    }
}

QString nodeLabel(const Circuit& circuit, const Netlist& net, int node)
{
    // 分两趟收集：网络标签（网络的「名字」）优先，再补引脚。
    // compPins 是 QHash，迭代顺序每个进程都不一样；只有一趟遍历时，
    // 三个引脚名额可能被器件占满，标签名就时有时无 —— 报告也跟着飘。
    QStringList names;
    QStringList pins;
    QSet<QString> seenNames;
    for (auto it = net.compPins.constBegin(); it != net.compPins.constEnd(); ++it) {
        const Component* comp = circuit.find(it.key());
        if (!comp || comp->type != ComponentType::NetLabel)
            continue;
        const QString name = comp->displayName();
        if (it.value().value(0) == node && !seenNames.contains(name)) {
            seenNames.insert(name);
            names << name;
        }
    }
    for (auto it = net.compPins.constBegin(); it != net.compPins.constEnd(); ++it) {
        const Component* comp = circuit.find(it.key());
        if (!comp || comp->type == ComponentType::NetLabel)
            continue;
        const QVector<int>& nodes = it.value();
        for (int i = 0; i < nodes.size(); ++i) {
            if (nodes.at(i) == node)
                pins << QStringLiteral("%1.%2").arg(comp->refdes).arg(i + 1);
        }
    }

    QStringList parts = names;
    for (const QString& pin : pins) {
        if (parts.size() >= 3)
            break;
        parts << pin;
    }
    if (parts.isEmpty())
        return CSM_TR("Node %1").arg(node);
    return CSM_TR("Node %1 (%2)").arg(node).arg(parts.join(QStringLiteral(", ")));
}

QString buildReport(const Circuit& circuit, const Problem& p, const Netlist& net,
                    const std::vector<double>& x, const ElementState& st, const DiodeModel& dm,
                    double t)
{
    QStringList lines;
    lines << CSM_TR("DC operating point result");
    lines << QStringLiteral("————————————————————————————");
    lines << CSM_TR("Node voltages:");
    for (int i = 1; i <= p.nNodes; ++i) {
        lines << QStringLiteral("  %1 = %2")
                     .arg(nodeLabel(circuit, net, i).leftJustified(28, QLatin1Char(' ')),
                          formatEng(p.voltage(x, i), QStringLiteral("V"), 5));
    }

    lines << QString();
    lines << CSM_TR("Element currents:");
    QVector<double> currents;
    sample(p, x, st, false, 0.0, t, dm, currents);
    int idx = p.nNodes;
    for (const Component* cp : p.elements) {
        const int count = currentSlotCount(p, *cp);
        if (idx + count > currents.size())
            break;
        // 网络标签不是器件，不列电流
        if (cp->type != ComponentType::NetLabel) {
            // 变压器一个绕组一行：T1 / T1.2 / T1.3
            for (int slot = 0; slot < count; ++slot) {
                lines << QStringLiteral("  %1 = %2")
                             .arg(currentLabel(*cp, slot).leftJustified(8, QLatin1Char(' ')),
                                  formatEng(currents.at(idx + slot), QStringLiteral("A"), 4));
            }
        }
        idx += count;
    }

    // 万用表读数：排在采样向量的最后一段（节点电压 + 元件电流 + 测试点差分之后），
    // 含义随档位变化（V / A / Ω）
    // 万用表读数在采样向量里排在最后：节点电压 + 元件电流 + 带参考地的测试点差分之后
    int meterBase = p.nNodes;
    for (const Component* cp : p.elements) {
        meterBase += currentSlotCount(p, *cp);
        if (isVoltageTap(cp->type) && cp->groundKind != GroundKind::Gnd)
            ++meterBase;
    }
    QVector<QPair<QString, QString>> meters; // (名字, 单位)
    for (const Component* cp : p.elements) {
        if (cp->type != ComponentType::Multimeter)
            continue;
        meters.append(qMakePair(cp->refdes, multimeterModeUnit(cp->multimeterMode)));
    }
    if (!meters.isEmpty()) {
        lines << QString();
        lines << CSM_TR("Multimeter readings:");
        for (int k = 0; k < meters.size(); ++k) {
            const double value = meterBase + k < currents.size()
                ? currents.at(meterBase + k) : 0.0;
            lines << QStringLiteral("  %1 = %2")
                         .arg(meters.at(k).first.leftJustified(8, QLatin1Char(' ')),
                              formatEng(value, meters.at(k).second, 4));
        }
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
        case ComponentType::LogicGate: {
            // 小信号下门的输出电平是固定的（对交流就是地），只有内阻在矩阵里
            const int outPin = gateInputCount(comp.gateKind);
            const int nout = p.nodeOf(comp.id, outPin);
            addG(a, nout, 0, Cx(1.0 / kGateRout, 0.0));
            break;
        }
        case ComponentType::Resistor:
            addG(a, n1, n2, Cx(1.0 / std::max(comp.value, 1e-9), 0.0));
            break;
        case ComponentType::Capacitor:
            addG(a, n1, n2, Cx(0.0, omega * std::max(comp.value, 1e-15)));
            break;
        case ComponentType::Inductor:
            addBranch(a, b, p, comp.id, Cx(0.0, omega * std::max(comp.value, 1e-12)), Cx(0.0, 0.0));
            break;
        case ComponentType::Transformer: {
            const QVector<int> branches = p.windingBranches.value(comp.id);
            const QVector<double> inds = transformerInductances(comp);
            if (branches.size() != inds.size())
                break;
            const QVector<double> polarities = transformerPolarities(comp);
            const int count = inds.size();
            for (int i = 0; i < count; ++i) {
                const int row = p.nNodes + branches.at(i);
                const int a1 = p.branchNodes.at(branches.at(i)).first;
                const int a2 = p.branchNodes.at(branches.at(i)).second;
                if (a1 > 0) a(a1 - 1, row) += Cx(1.0, 0.0);
                if (a2 > 0) a(a2 - 1, row) -= Cx(1.0, 0.0);
                if (a1 > 0) a(row, a1 - 1) += Cx(1.0, 0.0);
                if (a2 > 0) a(row, a2 - 1) -= Cx(1.0, 0.0);
                for (int j = 0; j < count; ++j) {
                    const double mij = polarities.at(i) * polarities.at(j)
                        * std::sqrt(inds.at(i) * inds.at(j));
                    a(row, p.nNodes + branches.at(j)) -= Cx(0.0, omega * mij);
                }
                a(row, row) -= Cx(0.1 * inds.at(i) / inds.first(), 0.0);
            }
            break;
        }
        case ComponentType::Ob2362:
            addBranch(a, b, p, comp.id, Cx(0.0, 0.0), Cx(0.0, 0.0));
            break;
        case ComponentType::VoltageSource:
        case ComponentType::SignalGenerator:
            addBranch(a, b, p, comp.id, Cx(0.0, 0.0), Cx(1.0, 0.0));
            break;
        case ComponentType::CurrentProbe:
            addBranch(a, b, p, comp.id, Cx(0.0, 0.0), Cx(0.0, 0.0));
            break;
        case ComponentType::Multimeter:
            // 电压档：10 MΩ 并联；电流档：0 V 支路；电阻档：1 A 测试电流源
            // （交流下源幅度按 1 处理，V/I 的比值即阻抗，dB 读数是 20·log10|Z|）
            switch (comp.multimeterMode) {
            case MultimeterMode::Voltage:
            case MultimeterMode::AcVoltage:
                addG(a, n1, n2, Cx(1.0 / 10e6, 0.0));
                break;
            case MultimeterMode::Current:
            case MultimeterMode::AcCurrent:
                addBranch(a, b, p, comp.id, Cx(0.0, 0.0), Cx(0.0, 0.0));
                break;
            case MultimeterMode::Resistance:
                addI(b, n2, n1, Cx(1.0, 0.0));
                break;
            }
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

        case ComponentType::Tl431: {
            const int nr = n1;
            const int na = n2;
            const int nk = p.nodeOf(comp.id, 2);
            const double vref0 = comp.value > 0.5 ? comp.value : 2.495;
            const double vref = st.tlVref.value(comp.id, 0.0);
            constexpr double kTlGain = 100.0;
            const double g = vref > vref0 ? kTlGain : 0.0;
            addVccs(a, nk, na, nr, na, Cx(g, 0.0));
            addG(a, na, nk, Cx(dm.conductance(st.diodeV.value(comp.id, 0.0)), 0.0));
            break;
        }

        case ComponentType::Optocoupler: {
            const DiodeModel model = ledModel(comp.vf, dm.vt);
            const double vd = st.diodeV.value(comp.id, 0.6);
            const double g = model.conductance(vd);
            const double if0 = model.current(vd);
            const int nc = p.nodeOf(comp.id, 2);
            const int ne = p.nodeOf(comp.id, 3);
            const double ctr = std::max(comp.ctr, 0.0);
            const double vceSat = std::max(comp.vceSat, 1e-3);
            // 饱和收窄必须与瞬态/工作点用同一个函数：Ic = CTR·If·x/(1+x)，
            // x = Vce/VceSat。原来这里用的是 1−exp(−x)，在 x≈1…10 的区间与小信号
            // 增益都差 10–30%（反激环路里就是几十个百分点的幅值偏差），
            // 两边各算一套等于同一个器件有两个模型。
            const double vce = st.optoVce.value(comp.id, 0.0);
            const double vcePos = std::max(vce, 0.0);
            const double x = vcePos / vceSat;
            const double fSmall = x / (1.0 + x);
            const double df = 1.0 / ((1.0 + x) * (1.0 + x) * vceSat);

            addG(a, n1, n2, Cx(g, 0.0));
            addVccs(a, nc, ne, n1, n2, Cx(ctr * fSmall * g, 0.0));
            addG(a, nc, ne, Cx(ctr * if0 * df + 1e-9, 0.0));
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
        case ComponentType::PushButton:
            addG(a, n1, n2, Cx(1.0 / (comp.closed ? 1e-3 : 1e9), 0.0));
            break;
        case ComponentType::Ground: {
            // 非主地只通过 1 MΩ 漏电阻接主地（相当于隔离）
            if (comp.groundKind != GroundKind::Gnd)
                addG(a, n1, 0, Cx(1e-6, 0.0));
            break;
        }
        case ComponentType::MeasureProbe:
        case ComponentType::Probe:
        case ComponentType::NetLabel:
            break;
        }
    }
    for (int i = 1; i <= p.nNodes; ++i)
        a(i - 1, i - 1) += Cx(1e-12, 0.0);
}

} // namespace

// ---------------------------------------------------------------- 增量瞬态
struct LiveTransient::Impl {
    QHash<int, double> worstReverse; // 极性电容见过的最严重反向电压
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
            *error = CSM_TR("The circuit has no solvable nodes.");
        return false;
    }

    d.diode = DiodeModel();
    d.diode.vt = kBoltzmann * (273.15 + 27.0) / kElectron;
    d.problem = buildProblem(d.circuit, d.net);
    d.x.assign(size_t(d.problem.nNodes + d.problem.nBranches), 0.0);
    d.state = ElementState();
    d.worstReverse.clear();
    d.t = 0.0;

    if (!newtonSolve(d.problem, d.state, false, 0.0, 0.0, d.x, d.diode, error))
        return false;
    updateState(d.problem, d.state, d.x);

    d.names.clear();
    for (int i = 1; i <= d.problem.nNodes; ++i)
        d.names << QStringLiteral("V(%1)").arg(i);
    for (const Component* comp : d.problem.elements) {
        const int count = currentSlotCount(d.problem, *comp);
        for (int slot = 0; slot < count; ++slot)
            d.names << currentSignalName(*comp, slot);
    }
    // 带参考地的测试点：与 sample()/makeSignals() 一致，补一条差分信号
    for (const Component* comp : d.problem.elements) {
        if (!isVoltageTap(comp->type) || comp->groundKind == GroundKind::Gnd)
            continue;
        const int a = d.problem.nodeOf(comp->id, 0);
        const int b = d.problem.referenceNode.value(int(comp->groundKind), 0);
        if (a > 0)
            d.names << QStringLiteral("V(%1,%2)").arg(a).arg(b);
    }
    // 万用表：与 makeSignals 的末尾顺序一致（名字就是参考编号，单位随档位）
    for (const Component* comp : d.problem.elements) {
        if (comp->type == ComponentType::Multimeter)
            d.names << comp->refdes;
    }

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
            *error = CSM_TR("The circuit has no solvable nodes.");
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
    trackReverseCaps(d.problem, d.x, d.worstReverse);

    d.names.clear();
    for (int i = 1; i <= d.problem.nNodes; ++i)
        d.names << QStringLiteral("V(%1)").arg(i);
    for (const Component* comp : d.problem.elements) {
        const int count = currentSlotCount(d.problem, *comp);
        for (int slot = 0; slot < count; ++slot)
            d.names << currentSignalName(*comp, slot);
    }
    // 带参考地的测试点：与 sample()/makeSignals() 一致，补一条差分信号
    for (const Component* comp : d.problem.elements) {
        if (!isVoltageTap(comp->type) || comp->groundKind == GroundKind::Gnd)
            continue;
        const int a = d.problem.nodeOf(comp->id, 0);
        const int b = d.problem.referenceNode.value(int(comp->groundKind), 0);
        if (a > 0)
            d.names << QStringLiteral("V(%1,%2)").arg(a).arg(b);
    }
    // 万用表：与 makeSignals 的末尾顺序一致（名字就是参考编号，单位随档位）
    for (const Component* comp : d.problem.elements) {
        if (comp->type == ComponentType::Multimeter)
            d.names << comp->refdes;
    }
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
        updateAcMeters(d.problem, d.state, d.x, d.h);
        trackReverseCaps(d.problem, d.x, d.worstReverse);
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

QHash<int, double> LiveTransient::reverseCapacitors() const
{
    QHash<int, double> result;
    const Impl& d = *m_impl;
    for (auto it = d.worstReverse.constBegin(); it != d.worstReverse.constEnd(); ++it) {
        if (it.value() < kCapReverseLimit)
            result.insert(it.key(), it.value());
    }
    return result;
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
        result.error = CSM_TR("The circuit has no solvable nodes. Place parts and wire them together.");
        return result;
    }

    const Problem p = buildProblem(circuit, net);
    DiodeModel dm;
    dm.vt = kBoltzmann * (273.15 + 27.0) / kElectron;

    for (int i = 1; i <= net.nodeCount; ++i)
        result.nodeLabels << nodeLabel(circuit, net, i);

    result.log << CSM_TR("Nodes %1, branch unknowns %2, components %3")
                      .arg(net.nodeCount)
                      .arg(p.nBranches)
                      .arg(p.elements.size());

    ElementState st;
    std::vector<double> x(size_t(p.nNodes + p.nBranches), 0.0);
    QString error;

    // 统一先求直流工作点，作为瞬态初值与交流线性化点
    if (!newtonSolve(p, st, false, 0.0, 0.0, x, dm, &error)) {
        result.error = CSM_TR("DC operating point failed: ") + error;
        return result;
    }

    if (options.kind == AnalysisKind::OperatingPoint) {
        result.ok = true;
        QHash<int, double> worst;
        trackReverseCaps(p, x, worst);
        for (auto it = worst.constBegin(); it != worst.constEnd(); ++it) {
            if (it.value() < kCapReverseLimit)
                result.blownCapacitors.insert(it.key(), it.value());
        }

        QVector<double> currents;
        sample(p, x, st, false, 0.0, 0.0, dm, currents);
        int idx = p.nNodes;
        for (const Component* cp : p.elements) {
            const int count = currentSlotCount(p, *cp);
            for (int slot = 0; slot < count; ++slot) {
                if (idx + slot < currents.size())
                    result.elementCurrents.insert(currentSignalName(*cp, slot),
                                                  currents.at(idx + slot));
            }
            idx += count;
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
        // 只提示、不擅自改步长：改步长会改变结果（老工程与测试都按给定步长算）
        const double suggested = circuit.suggestedMaxStep();
        if (suggested > 0.0 && h > suggested) {
            result.log << CSM_TR("Tip: the max step %1 is coarser than the switching dynamics (suggest ≤ %2), "
                                 "the waveforms will be distorted and the output may not resolve.")
                              .arg(formatEng(h, QStringLiteral("s"), 3),
                                   formatEng(suggested, QStringLiteral("s"), 3));
        }

        double steps = std::ceil(tStop / h);
        if (steps > kMaxPoints) {
            h = tStop / kMaxPoints;
            steps = kMaxPoints;
            result.log << CSM_TR("Too many steps; adjusted to %1 steps (step %2).")
                              .arg(kMaxPoints)
                              .arg(formatEng(h, QStringLiteral("s"), 3));
            if (suggested > 0.0 && h > suggested) {
                result.log << CSM_TR("Note: limited by the point cap, the step is still coarser than the "
                                     "circuit dynamics, so switching waveforms will be distorted. "
                                     "Consider a stop time below %1.")
                                  .arg(formatEng(suggested * kMaxPoints, QStringLiteral("s"), 2));
            }
        }
        const int stepCount = std::max(1, int(steps));

        result.waves = makeSignals(p);
        for (Signal& s : result.waves)
            s.x.reserve(stepCount + 1), s.y.reserve(stepCount + 1);

        QVector<double> values;
        sample(p, x, st, false, h, 0.0, dm, values);

        QHash<int, double> worstReverse;

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
                result.error = CSM_TR("Transient failed at t = %1: %2")
                                   .arg(formatEng(t, QStringLiteral("s"), 4), error);
                result.waves.clear();
                return result;
            }
            updateAcMeters(p, st, x, h);
            sample(p, x, st, true, h, t, dm, values);
            appendPoint(t);
            updateState(p, st, x);
            trackReverseCaps(p, x, worstReverse);
        }

        // 反向电压超过耐压的极性电容：记录下来，界面据此放爆炸特效
        for (auto it = worstReverse.constBegin(); it != worstReverse.constEnd(); ++it) {
            if (it.value() < kCapReverseLimit)
                result.blownCapacitors.insert(it.key(), it.value());
        }

        result.ok = true;
        // 交流万用表要攒满一个整周期才有读数：停止时间比一个周期还短时
        // 读数会一直显示横杠，这里明确告诉用户该把停止时间放长到多少
        for (const Component& comp : circuit.components) {
            if (comp.type != ComponentType::Multimeter || !isAcMeterMode(comp.multimeterMode))
                continue;
            const double freq = comp.acFrequency > 0.1 ? comp.acFrequency : 50.0;
            const double period = 1.0 / freq;
            if (tStop < period) {
                result.log << CSM_TR("Tip: %1 is an AC range and needs one full period (%2 Hz → %3) "
                                     "before it reads; the current stop time %4 is too short, "
                                     "readings will show dashes.")
                                  .arg(comp.refdes)
                                  .arg(freq, 0, 'g', 4)
                                  .arg(formatEng(period, QStringLiteral("s"), 3),
                                       formatEng(tStop, QStringLiteral("s"), 3));
            }
        }

        result.log << CSM_TR("Transient analysis done: %1 steps, step %2, ended at %3")
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

    // 万用表：与 makeSignals 末尾的顺序一致，信号排在电流测试点之后
    QVector<int> meterBranches;
    QVector<const Component*> meterComps;
    int acMeterBase = -1;
    for (const Component& comp : circuit.components) {
        if (comp.type != ComponentType::Multimeter)
            continue;
        Signal s;
        s.name = comp.refdes;
        s.unit = QStringLiteral("dB");
        s.x.reserve(points);
        s.y.reserve(points);
        if (acMeterBase < 0)
            acMeterBase = result.waves.size();
        result.waves.append(s);
        meterComps.append(&comp);
        meterBranches.append(p.branchOf.value(comp.id, -1));
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
            result.error = CSM_TR("AC analysis failed at %1: %2")
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

        // 万用表：电压/电阻档输出跨端电压幅值（电阻档即 20·log10|Z|），
        // 电流档输出支路电流幅值
        for (int k = 0; k < meterBranches.size(); ++k) {
            const Component& comp = *meterComps.at(k);
            double mag = 0.0;
            if (comp.multimeterMode == MultimeterMode::Current) {
                const int br = meterBranches.at(k);
                mag = br >= 0 ? std::abs(xc[size_t(p.nNodes + br)]) : 0.0;
            } else {
                const int na = p.nodeOf(comp.id, 0);
                const int nb = p.nodeOf(comp.id, 1);
                const Cx va = na > 0 ? xc[size_t(na - 1)] : Cx(0.0, 0.0);
                const Cx vb = nb > 0 ? xc[size_t(nb - 1)] : Cx(0.0, 0.0);
                mag = std::abs(va - vb);
            }
            const double db = 20.0 * std::log10(std::max(mag, 1e-30));
            result.waves[acMeterBase + k].x.append(f);
            result.waves[acMeterBase + k].y.append(db);
        }
    }

    result.ok = true;
    result.log << CSM_TR("AC analysis done: %1 points (%2 ~ %3)")
                      .arg(points)
                      .arg(formatEng(fStart, QStringLiteral("Hz"), 3),
                           formatEng(fStop, QStringLiteral("Hz"), 3));
    result.elapsedMs = double(timer.nsecsElapsed()) / 1e6;
    return result;
}

} // namespace csm
