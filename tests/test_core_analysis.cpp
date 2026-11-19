#include "core/Circuit.h"
#include "core/Units.h"
#include "core/UpdateChecker.h"
#include "csm_version.h"
#include "sim/Simulator.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <cmath>

using namespace csm;

namespace {

// 本文件自用的构造辅助。为了每个测试文件彻底独立（删掉别的文件也不
// 影响自己），各留一份，不再共用头文件。

Component makeComp(ComponentType type, const QPointF& pos, double value, int rotation = 0)
{
    Component c;
    c.type = type;
    c.pos = pos;
    c.value = value;
    c.rotation = rotation;
    return c;
}

void addComponent(Circuit& circuit, Component c)
{
    c.id = circuit.components.size() + 1;
    if (c.refdes.isEmpty())
        c.refdes = circuit.nextRefdes(c.type);
    circuit.components.append(c);
}

Circuit loopCircuit(ComponentType typeA, double valueA, ComponentType typeB, double valueB,
                    double sourceValue)
{
    Circuit circuit;

    Component v = makeComp(ComponentType::VoltageSource, QPointF(100, 300), 0.0, 90);
    v.function = SourceFunction::Constant;
    v.dcValue = sourceValue;
    addComponent(circuit, v);

    addComponent(circuit, makeComp(typeA, QPointF(200, 200), valueA));
    addComponent(circuit, makeComp(typeB, QPointF(300, 300), valueB, 90));

    addComponent(circuit, makeComp(ComponentType::Ground, QPointF(200, 400), 0.0));

    Wire w1;
    w1.points = {QPointF(100, 270), QPointF(100, 200), QPointF(170, 200)};
    Wire w2;
    w2.points = {QPointF(230, 200), QPointF(300, 200), QPointF(300, 270)};
    Wire w3;
    w3.points = {QPointF(300, 330), QPointF(300, 400), QPointF(100, 400), QPointF(100, 330)};
    circuit.wires = {w1, w2, w3};
    return circuit;
}

Circuit opAmpInverting(double inputVolts, double gain = 1e5)
{
    Circuit circuit;

    Component v = makeComp(ComponentType::VoltageSource, QPointF(100, 380), 0.0, 90);
    v.function = SourceFunction::Constant;
    v.dcValue = inputVolts;
    addComponent(circuit, v);

    addComponent(circuit, makeComp(ComponentType::Resistor, QPointF(180, 320), 1000.0));
    addComponent(circuit, makeComp(ComponentType::Resistor, QPointF(300, 420), 10000.0));
    addComponent(circuit, makeComp(ComponentType::OpAmp, QPointF(300, 300), gain));
    addComponent(circuit, makeComp(ComponentType::Ground, QPointF(100, 410), 0.0));
    addComponent(circuit, makeComp(ComponentType::Ground, QPointF(220, 240), 0.0));

    circuit.wires.append(Wire{{QPointF(100, 350), QPointF(100, 320), QPointF(150, 320)}});
    circuit.wires.append(Wire{{QPointF(210, 320), QPointF(270, 320)}});
    circuit.wires.append(Wire{{QPointF(270, 320), QPointF(270, 420)}});
    circuit.wires.append(Wire{{QPointF(330, 420), QPointF(330, 300)}});
    circuit.wires.append(Wire{{QPointF(270, 280), QPointF(270, 240), QPointF(220, 240)}});
    return circuit;
}

bool near(double a, double b, double tol)
{
    return std::abs(a - b) <= tol;
}

} // namespace

class CoreAnalysisTest : public QObject {
    Q_OBJECT

private slots:
    void suggestedStepUsesPulseWidthNotEdge();
    void sineAmplitudeIsRms();
    void suggestedStepAvoidsAliasing();
    void liveTransientMatchesBatch();
    void voltageDividerDc();
    void rcCharging();
    void opAmpInvertingDc();
    void opAmpInvertingAc();
    void rcLowPassAc();
    void probeMapsToNodeWithoutAffectingResult();
    void currentProbeMeasuresBranch();
    void allPinsLandOnGrid();
    void groundRequired();
    void meterServesAsReferenceWithoutGround();
    void anyGroundKindCanServeAsReference();
    void logicGatesFollowTruthTable();
    void logicGateChainSettles();
};

void CoreAnalysisTest::suggestedStepUsesPulseWidthNotEdge()
{
    // 步长不该被「边沿有多陡」绑架。
    // 以前按 1/(edge·4) 算：1 µs 的沿要求 200 ns 步长，而电路时间常数
    // 常在毫秒量级，根本响应不了这么陡的沿 —— 5 s 的仿真要 2500 万步，
    // 实时示波器 1 秒只能推进 0.1 s 仿真时间，卡得没法看。
    Circuit c = loopCircuit(ComponentType::Resistor, 1000.0,
                            ComponentType::Resistor, 1000.0, 0.0);
    Component& v = c.components[0];
    v.function = SourceFunction::Pulse;
    v.pulsePeriod = 1.0;   // 1 Hz
    v.pulseWidth = 0.5;    // 半周期
    v.pulseRise = 1e-6;    // 1 µs 边沿
    v.pulseFall = 1e-6;

    // 由周期决定：1/(1 Hz × 20) = 50 ms
    const double wide = c.suggestedMaxStep();
    QVERIFY2(near(wide, 0.05, 1e-9), qPrintable(QString::number(wide)));
    QVERIFY2(wide > 1e-3,
             qPrintable(QStringLiteral("不该被 1 µs 的沿压到微秒级：%1 s").arg(wide)));

    // 但窄脉冲仍然要细步长：脉宽 1 µs → 1/4 脉宽 = 250 ns
    v.pulsePeriod = 1e-3;
    v.pulseWidth = 1e-6;
    const double narrow = c.suggestedMaxStep();
    QVERIFY2(near(narrow, 2.5e-7, 1e-12), qPrintable(QString::number(narrow)));

    // 边沿本身完全不影响结果：只改上升/下降沿，建议步长不变
    v.pulseRise = 1e-9;
    v.pulseFall = 1e-9;
    QCOMPARE(c.suggestedMaxStep(), narrow);
}

void CoreAnalysisTest::sineAmplitudeIsRms()
{
    // 正弦源的幅值填的是有效值：220 V 有效值 → 峰值 311 V
    Circuit c = loopCircuit(ComponentType::Resistor, 1000.0,
                            ComponentType::Resistor, 1000.0, 0.0);
    Component& v = c.components[0];
    v.function = SourceFunction::Sine;
    v.amplitude = 220.0;
    v.frequency = 50.0;
    QVERIFY2(near(v.peakAmplitude(), 311.13, 0.05),
             qPrintable(QString::number(v.peakAmplitude())));

    const double at90 = v.sourceValueAt(0.005); // 5 ms = 90°
    QVERIFY2(near(at90, 311.13, 0.1), qPrintable(QString::number(at90)));

    // 存盘后再读回来还是有效值
    QString error;
    const QJsonObject json = c.toJson();
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 2);
    const Circuit back = Circuit::fromJson(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(near(back.components.at(0).amplitude, 220.0, 1e-9));

    // 版本 1 的老文件存的是峰值 311，读入时换算成 219.9 V 有效值
    QJsonObject legacy = json;
    legacy.insert(QStringLiteral("version"), 1);
    QJsonArray comps = legacy.value(QStringLiteral("components")).toArray();
    QJsonObject first = comps.at(0).toObject();
    first.insert(QStringLiteral("amplitude"), 311.0);
    comps.replace(0, first);
    legacy.insert(QStringLiteral("components"), comps);
    const Circuit old = Circuit::fromJson(legacy, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(near(old.components.at(0).amplitude, 219.91, 0.05),
             qPrintable(QString::number(old.components.at(0).amplitude)));
}

void CoreAnalysisTest::suggestedStepAvoidsAliasing()
{
    Circuit c = loopCircuit(ComponentType::Resistor, 1000.0,
                            ComponentType::Resistor, 1000.0, 0.0);
    Component& v = c.components[0];
    v.function = SourceFunction::Sine;
    v.amplitude = 220.0 / std::sqrt(2.0); // 峰值 220.0 V
    v.frequency = 50.0; // 周期 20 ms

    // 每个周期 20 点 → 1 ms
    QVERIFY2(near(c.suggestedMaxStep(), 1e-3, 1e-9),
             qPrintable(QString::number(c.suggestedMaxStep())));

    auto peakWith = [&c](double step) {
        LiveTransient live;
        QString error;
        if (!live.start(c, step, &error))
            return -1.0;
        const QStringList names = live.signalNames();
        const int index = names.indexOf(QStringLiteral("V(2)"));
        if (index < 0)
            return -1.0;
        double peak = 0.0;
        const int steps = int(0.25 / step);
        for (int i = 0; i < steps; ++i) {
            if (!live.advance(1, &error))
                return -1.0;
            peak = std::max(peak, std::abs(live.values().at(index)));
        }
        return peak;
    };

    // 建议步长下应看到完整正弦峰值（约 110 V）
    const double good = peakWith(c.suggestedMaxStep());
    QVERIFY2(near(good, 110.0, 5.0), qPrintable(QString::number(good)));

    // 步长等于信号周期时完全混叠，输出退化为 0
    const double aliased = peakWith(0.02);
    QVERIFY2(aliased < 1.0, qPrintable(QString::number(aliased)));
}

void CoreAnalysisTest::liveTransientMatchesBatch()
{
    Circuit c = loopCircuit(ComponentType::Resistor, 1000.0,
                            ComponentType::Capacitor, 1e-6, 0.0);
    Component& v = c.components[0];
    v.function = SourceFunction::Pulse;
    v.pulseV1 = 0.0;
    v.pulseV2 = 5.0;
    v.pulseRise = 1e-9;
    v.pulseFall = 1e-9;
    v.pulseWidth = 0.004;
    v.pulsePeriod = 0.01;

    LiveTransient live;
    QString error;
    QVERIFY2(live.start(c, 1e-5, &error), qPrintable(error));
    QVERIFY2(live.advance(500, &error), qPrintable(error));
    QCOMPARE(live.time(), 5e-3);
    const QVector<double> values = live.values();
    QVERIFY(values.size() >= 2);
    const double liveCap = values.at(1); // V(2)

    SimulationOptions opt;
    opt.kind = AnalysisKind::Transient;
    opt.tStop = 5e-3;
    opt.tStep = 1e-5;
    const SimulationResult r = Simulator::run(c, opt);
    QVERIFY2(r.ok, qPrintable(r.error));
    const Signal* s = r.signal(QStringLiteral("V(2)"));
    QVERIFY(s);
    QVERIFY2(near(liveCap, s->y.last(), 1e-9),
             qPrintable(QString::number(liveCap - s->y.last())));
}

void CoreAnalysisTest::voltageDividerDc()
{
    Circuit c = loopCircuit(ComponentType::Resistor, 1000.0,
                            ComponentType::Resistor, 2000.0, 10.0);

    SimulationOptions op;
    op.kind = AnalysisKind::OperatingPoint;
    const SimulationResult rop = Simulator::run(c, op);
    QVERIFY2(rop.ok, qPrintable(rop.error));
    QVERIFY(!rop.report.isEmpty());

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 1e-3;
    tr.tStep = 1e-4;
    const SimulationResult rt = Simulator::run(c, tr);
    QVERIFY2(rt.ok, qPrintable(rt.error));
    const Signal* s = rt.signal(QStringLiteral("V(2)"));
    QVERIFY(s);
    QVERIFY(!s->y.isEmpty());
    QVERIFY2(near(s->y.first(), 6.6667, 1e-3), qPrintable(QString::number(s->y.first())));
    QVERIFY2(near(s->y.first(), 10.0 * 2000.0 / 3000.0, 1e-3), "分压公式");
}

void CoreAnalysisTest::rcCharging()
{
    Circuit c = loopCircuit(ComponentType::Resistor, 1000.0,
                            ComponentType::Capacitor, 1e-6, 0.0);

    Component& v = c.components[0];
    v.function = SourceFunction::Pulse;
    v.pulseV1 = 0.0;
    v.pulseV2 = 5.0;
    v.pulseRise = 1e-9;
    v.pulseFall = 1e-9;
    v.pulseWidth = 0.009;
    v.pulsePeriod = 0.02;

    SimulationOptions opt;
    opt.kind = AnalysisKind::Transient;
    opt.tStop = 0.005;
    opt.tStep = 1e-5;
    const SimulationResult r = Simulator::run(c, opt);
    QVERIFY2(r.ok, qPrintable(r.error));

    const Signal* s = r.signal(QStringLiteral("V(2)"));
    QVERIFY(s);
    QCOMPARE(s->y.size(), 501);
    const int idx = 100; // t = 1 ms, 理论值 3.1606 V
    QVERIFY2(near(s->y.at(idx), 3.16, 0.15), qPrintable(QString::number(s->y.at(idx))));
    QVERIFY2(s->y.last() > 4.8, qPrintable(QString::number(s->y.last())));
}

void CoreAnalysisTest::opAmpInvertingDc()
{
    Circuit c = opAmpInverting(1.0);
    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QCOMPARE(net.nodeCount, 3);

    const Component* op = nullptr;
    for (const Component& comp : c.components) {
        if (comp.refdes == QLatin1String("U1"))
            op = &comp;
    }
    QVERIFY(op);
    QCOMPARE(net.compPins.value(op->id).size(), 3);

    const int outNode = net.nodeOf(op->id, 2);
    const int inNode = net.nodeOf(op->id, 1);

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 1e-4;
    tr.tStep = 1e-5;
    const SimulationResult r = Simulator::run(c, tr);
    QVERIFY2(r.ok, qPrintable(r.error));

    const Signal* out = r.signal(QStringLiteral("V(%1)").arg(outNode));
    const Signal* in = r.signal(QStringLiteral("V(%1)").arg(inNode));
    QVERIFY(out);
    QVERIFY(in);
    // 增益 10 倍反相，虚地约 0 V
    QVERIFY2(near(out->y.first(), -10.0, 0.05),
             qPrintable(QString::number(out->y.first())));
    QVERIFY2(std::abs(in->y.first()) < 2e-3,
             qPrintable(QString::number(in->y.first())));

    QString error;
    const Circuit loaded = Circuit::fromJson(c.toJson(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(loaded.find(op->id));
    QCOMPARE(loaded.find(op->id)->type, ComponentType::OpAmp);
}

void CoreAnalysisTest::opAmpInvertingAc()
{
    Circuit c = opAmpInverting(1.0);
    const Netlist net = c.buildNetlist();
    const int outNode = net.nodeOf(c.components.at(3).id, 2);

    SimulationOptions opt;
    opt.kind = AnalysisKind::AC;
    opt.fStart = 1000.0;
    opt.fStop = 1000.0001;
    opt.acPoints = 2;
    const SimulationResult r = Simulator::run(c, opt);
    QVERIFY2(r.ok, qPrintable(r.error));

    const Signal* out = r.signal(QStringLiteral("V(%1)").arg(outNode));
    QVERIFY(out);
    QVERIFY(!out->y.isEmpty());
    // 反相 10 倍 → 20 dB
    QVERIFY2(near(out->y.first(), 20.0, 0.1), qPrintable(QString::number(out->y.first())));
}

void CoreAnalysisTest::rcLowPassAc()
{
    Circuit c = loopCircuit(ComponentType::Resistor, 1000.0,
                            ComponentType::Capacitor, 100e-9, 0.0);

    const double fc = 1.0 / (2.0 * 3.14159265358979 * 1000.0 * 100e-9);
    SimulationOptions opt;
    opt.kind = AnalysisKind::AC;
    opt.fStart = fc;
    opt.fStop = fc * 1.0001;
    opt.acPoints = 2;
    const SimulationResult r = Simulator::run(c, opt);
    QVERIFY2(r.ok, qPrintable(r.error));

    const Signal* s = r.signal(QStringLiteral("V(2)"));
    QVERIFY(s);
    QVERIFY(!s->y.isEmpty());
    QVERIFY2(near(s->y.first(), -3.01, 0.3), qPrintable(QString::number(s->y.first())));
}

void CoreAnalysisTest::probeMapsToNodeWithoutAffectingResult()
{
    Circuit c = opAmpInverting(1.0);
    Component tp1;
    tp1.type = ComponentType::Probe;
    tp1.pos = QPointF(130, 320);
    tp1.refdes = QStringLiteral("TP1");
    addComponent(c, tp1);
    Component tp2 = tp1;
    tp2.pos = QPointF(240, 320);
    tp2.refdes = QStringLiteral("TP2");
    addComponent(c, tp2);
    Component tp3 = tp1;
    tp3.pos = QPointF(330, 360);
    tp3.refdes = QStringLiteral("TP3");
    addComponent(c, tp3);

    // addComponent 按值接收，需要回读实际分配的 id
    tp1.id = c.components.at(c.components.size() - 3).id;
    tp2.id = c.components.at(c.components.size() - 2).id;
    tp3.id = c.components.at(c.components.size() - 1).id;

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QCOMPARE(net.nodeCount, 3);

    const int node1 = net.nodeOf(tp1.id, 0);
    const int node2 = net.nodeOf(tp2.id, 0);
    const int node3 = net.nodeOf(tp3.id, 0);
    QVERIFY(node1 > 0);
    QCOMPARE(node2, node1 + 1);
    QCOMPARE(node3, node2 + 1);

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 1e-4;
    tr.tStep = 1e-5;
    const SimulationResult withProbes = Simulator::run(c, tr);
    QVERIFY2(withProbes.ok, qPrintable(withProbes.error));

    Circuit plain = opAmpInverting(1.0);
    const SimulationResult withoutProbes = Simulator::run(plain, tr);
    QVERIFY(withoutProbes.ok);

    const int outNode = net.nodeOf(c.components.at(3).id, 2);
    const Signal* a = withProbes.signal(QStringLiteral("V(%1)").arg(outNode));
    const Signal* b = withoutProbes.signal(QStringLiteral("V(%1)").arg(outNode));
    QVERIFY(a && b);
    QCOMPARE(a->y.size(), b->y.size());
    QVERIFY(near(a->y.last(), b->y.last(), 1e-12));

    Circuit loaded = Circuit::fromJson(c.toJson());
    QVERIFY(loaded.find(tp1.id));
    QCOMPARE(loaded.find(tp1.id)->type, ComponentType::Probe);
}

void CoreAnalysisTest::currentProbeMeasuresBranch()
{
    Circuit c = opAmpInverting(1.0);

    // 把反馈支路的直连导线改成经电流测试点串入
    QVector<Wire> kept;
    for (const Wire& wire : c.wires) {
        if (wire.points.size() == 2 && wire.points.first() == QPointF(270, 320)
            && wire.points.last() == QPointF(270, 420))
            continue;
        kept.append(wire);
    }
    c.wires = kept;
    c.wires.append(Wire{{QPointF(270, 320), QPointF(270, 340)}});
    c.wires.append(Wire{{QPointF(270, 400), QPointF(270, 420)}});

    Component ip;
    ip.type = ComponentType::CurrentProbe;
    ip.pos = QPointF(270, 370);
    ip.rotation = 90;
    ip.refdes = QStringLiteral("IP1");
    addComponent(c, ip);
    ip.id = c.components.last().id;

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QCOMPARE(net.nodeCount, 4);
    QCOMPARE(net.compPins.value(ip.id).size(), 2);

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 0.25e-3;
    tr.tStep = 5e-6;
    const SimulationResult r = Simulator::run(c, tr);
    QVERIFY2(r.ok, qPrintable(r.error));
    const Signal* current = r.signal(QStringLiteral("I(IP1)"));
    QVERIFY(current);
    // 1 V 输入、10 倍反相 → 反馈电流 1 mA
    QVERIFY2(near(current->y.last(), 1e-3, 1e-5),
             qPrintable(QString::number(current->y.last())));

    SimulationOptions ac;
    ac.kind = AnalysisKind::AC;
    ac.fStart = 1000.0;
    ac.fStop = 1000.0001;
    ac.acPoints = 2;
    const SimulationResult ra = Simulator::run(c, ac);
    QVERIFY2(ra.ok, qPrintable(ra.error));
    const Signal* acCurrent = ra.signal(QStringLiteral("I(IP1)"));
    QVERIFY(acCurrent);
    QVERIFY2(near(acCurrent->y.first(), -60.0, 0.5),
             qPrintable(QString::number(acCurrent->y.first())));

    const Circuit loaded = Circuit::fromJson(c.toJson());
    QVERIFY(loaded.find(ip.id));
    QCOMPARE(loaded.find(ip.id)->type, ComponentType::CurrentProbe);
}

void CoreAnalysisTest::allPinsLandOnGrid()
{
    // 引脚坐标必须是 10 px 整数倍：否则元件放在网格上时端点落在网格之间，
    // 导线（端点都在网格上）画不上去，看起来就是「线画不了」
    const QVector<ComponentType> types = {
        ComponentType::Resistor,    ComponentType::Capacitor,  ComponentType::Inductor,
        ComponentType::Diode,       ComponentType::Led,        ComponentType::Npn,
        ComponentType::Pnp,         ComponentType::Nmos,       ComponentType::Pmos,
        ComponentType::Igbt,        ComponentType::Optocoupler,
        ComponentType::Tl431,       ComponentType::Transformer,
        ComponentType::Ob2362,      ComponentType::OpAmp,      ComponentType::Switch,
        ComponentType::PushButton,
        ComponentType::VoltageSource, ComponentType::CurrentSource,
        ComponentType::Probe,       ComponentType::CurrentProbe,
        ComponentType::Ground,
    };
    for (ComponentType type : types) {
        for (int kind = 0; kind <= 1; ++kind) {
            Component comp;
            comp.type = type;
            comp.pos = QPointF(100, 100); // 网格上
            if (type == ComponentType::Transformer)
                comp.transformerKind = kind == 0 ? TransformerKind::Single
                                                 : TransformerKind::PrimaryAuxSecondary;
            for (int i = 0; i < comp.pinPositions().size(); ++i) {
                const QPointF pin = comp.pinPosition(i);
                const bool onGrid = std::abs(pin.x() - std::round(pin.x() / 10.0) * 10.0) < 1e-6
                    && std::abs(pin.y() - std::round(pin.y() / 10.0) * 10.0) < 1e-6;
                QVERIFY2(onGrid, qPrintable(QStringLiteral("%1 kind=%2 引脚 %3 在 (%4,%5) 不在网格上")
                                                .arg(typeName(type))
                                                .arg(kind)
                                                .arg(i)
                                                .arg(pin.x())
                                                .arg(pin.y())));
            }
        }
    }
}


/// 搭一个「两个理想源驱动一个逻辑门，输出接 1 kΩ 到地」的电路，
/// 返回输出节点在仿真结束时的电压（失败返回 -1）。
/// 源用 rotation 90：0 号引脚（+）在上，1 号引脚（−）在下。
double gateOutput(GateKind kind, double va, double vb)
{
    Circuit c;

    Component gate;
    gate.id = 1;
    gate.type = ComponentType::LogicGate;
    gate.pos = QPointF(400, 300);
    gate.refdes = QStringLiteral("U1");
    gate.gateKind = kind;
    gate.logicHigh = 5.0;
    c.components.append(gate);

    Component src1;
    src1.id = 2;
    src1.type = ComponentType::VoltageSource;
    src1.pos = QPointF(200, 290);
    src1.rotation = 90;
    src1.refdes = QStringLiteral("V1");
    src1.dcValue = va;
    c.components.append(src1);

    Component src2;
    src2.id = 3;
    src2.type = ComponentType::VoltageSource;
    src2.pos = QPointF(200, 420);
    src2.rotation = 90;
    src2.refdes = QStringLiteral("V2");
    src2.dcValue = vb;
    c.components.append(src2);

    Component load;
    load.id = 4;
    load.type = ComponentType::Resistor;
    load.pos = QPointF(600, 300);
    load.refdes = QStringLiteral("R1");
    load.value = 1000.0;
    c.components.append(load);

    Component gnd;
    gnd.id = 5;
    gnd.type = ComponentType::Ground;
    gnd.pos = QPointF(100, 480);
    gnd.refdes = QStringLiteral("GND1");
    c.components.append(gnd);

    // 门引脚：0 = 输入 A，1 = 输入 B（非门没有它），最后一个 = 输出
    const QVector<QPointF> pins = gate.pinPositions();
    const int outPin = int(pins.size()) - 1;

    c.wires.append(Wire{{QPointF(200, 260), QPointF(200, 240), QPointF(340, 240),
                         QPointF(340, 290), pins.at(0)}});
    if (outPin == 2)
        c.wires.append(Wire{{QPointF(200, 390), QPointF(200, 370), QPointF(320, 370),
                             QPointF(320, 310), pins.at(1)}});
    c.wires.append(Wire{{QPointF(200, 320), QPointF(100, 320), QPointF(100, 480)}});
    c.wires.append(Wire{{QPointF(200, 450), QPointF(100, 450)}});
    c.wires.append(Wire{{pins.last(), QPointF(570, 300)}});
    c.wires.append(Wire{{QPointF(630, 300), QPointF(630, 480), QPointF(100, 480)}});

    SimulationOptions options;
    options.kind = AnalysisKind::Transient;
    options.tStop = 3e-3;
    options.tStep = 1e-3;
    const SimulationResult result = Simulator::run(c, options);
    if (!result.ok)
        return -1.0;

    const Netlist net = c.buildNetlist();
    const Signal* out = result.signal(QStringLiteral("V(%1)").arg(net.nodeOf(gate.id, outPin)));
    if (!out || out->y.isEmpty())
        return -1.0;
    return out->y.last();
}

void CoreAnalysisTest::logicGateChainSettles()
{
    // 两级串联（与门 → 非门）：门的状态要能在同一次求解里传下去，
    // 不能靠「上一步的输入」慢慢爬 —— 否则直流工作点会停在中间态
    Circuit c;

    Component andGate;
    andGate.id = 1;
    andGate.type = ComponentType::LogicGate;
    andGate.pos = QPointF(400, 300);
    andGate.refdes = QStringLiteral("U1");
    andGate.gateKind = GateKind::And;
    andGate.logicHigh = 5.0;
    c.components.append(andGate);

    Component notGate;
    notGate.id = 2;
    notGate.type = ComponentType::LogicGate;
    notGate.pos = QPointF(560, 300);
    notGate.refdes = QStringLiteral("U2");
    notGate.gateKind = GateKind::Not;
    notGate.logicHigh = 5.0;
    c.components.append(notGate);

    Component src;
    src.id = 3;
    src.type = ComponentType::VoltageSource;
    src.pos = QPointF(200, 290);
    src.rotation = 90;
    src.refdes = QStringLiteral("V1");
    src.dcValue = 5.0;
    c.components.append(src);

    Component load;
    load.id = 4;
    load.type = ComponentType::Resistor;
    load.pos = QPointF(700, 300);
    load.refdes = QStringLiteral("R1");
    load.value = 1000.0;
    c.components.append(load);

    Component gnd;
    gnd.id = 5;
    gnd.type = ComponentType::Ground;
    gnd.pos = QPointF(100, 480);
    gnd.refdes = QStringLiteral("GND1");
    c.components.append(gnd);

    const QPointF andIn0 = andGate.pinPositions().at(0);
    const QPointF andIn1 = andGate.pinPositions().at(1);
    const QPointF andOut = andGate.pinPositions().at(2);
    const QPointF notIn = notGate.pinPositions().at(0);
    const QPointF notOut = notGate.pinPositions().at(1);

    // 源的 + 极同时接到与门的两个输入上
    c.wires.append(Wire{{QPointF(200, 260), QPointF(200, 240), QPointF(340, 240),
                         QPointF(340, 310), andIn1}});
    c.wires.append(Wire{{QPointF(340, 290), andIn0}});
    c.wires.append(Wire{{andOut, notIn}});
    c.wires.append(Wire{{notOut, QPointF(670, 300)}});
    c.wires.append(Wire{{QPointF(200, 320), QPointF(100, 320), QPointF(100, 480)}});
    c.wires.append(Wire{{QPointF(730, 300), QPointF(730, 480), QPointF(100, 480)}});

    SimulationOptions options;
    options.kind = AnalysisKind::Transient;
    options.tStop = 3e-3;
    options.tStep = 1e-3;
    const SimulationResult result = Simulator::run(c, options);
    QVERIFY2(result.ok, qPrintable(result.error));

    const Netlist net = c.buildNetlist();
    const Signal* andSignal =
        result.signal(QStringLiteral("V(%1)").arg(net.nodeOf(andGate.id, 2)));
    const Signal* notSignal =
        result.signal(QStringLiteral("V(%1)").arg(net.nodeOf(notGate.id, 1)));
    QVERIFY(andSignal);
    QVERIFY(notSignal);
    // 与门两个输入都是 5 V → 输出 5 V；非门再反相 → 0 V
    QVERIFY2(andSignal->y.last() > 4.9, qPrintable(QString::number(andSignal->y.last())));
    QVERIFY2(notSignal->y.last() < 0.01, qPrintable(QString::number(notSignal->y.last())));
}

void CoreAnalysisTest::logicGatesFollowTruthTable()
{
    // 五个门逐一核对真值表。输出按「理想电平 + 100 Ω 内阻」等效，
    // 接 1 kΩ 到地时仍是干净的电平（高 5 V / 低 0 V）
    auto high = [](double v) { return v > 2.5; };
    auto low = [](double v) { return v >= 0.0 && v < 0.2; };

    // 与门：只有两个输入都高才输出高
    const double andOut = gateOutput(GateKind::And, 5.0, 5.0);
    QVERIFY2(high(andOut), qPrintable(QString::number(andOut)));
    QVERIFY(low(gateOutput(GateKind::And, 5.0, 0.0)));
    QVERIFY(low(gateOutput(GateKind::And, 0.0, 5.0)));
    QVERIFY(low(gateOutput(GateKind::And, 0.0, 0.0)));

    // 或门：有一个高就高
    QVERIFY(high(gateOutput(GateKind::Or, 5.0, 0.0)));
    QVERIFY(high(gateOutput(GateKind::Or, 0.0, 5.0)));
    QVERIFY(high(gateOutput(GateKind::Or, 5.0, 5.0)));
    QVERIFY(low(gateOutput(GateKind::Or, 0.0, 0.0)));

    // 非门：一个输入，反相
    QVERIFY(low(gateOutput(GateKind::Not, 5.0, 0.0)));
    QVERIFY(high(gateOutput(GateKind::Not, 0.0, 0.0)));

    // 异或门：两个不一样才高
    QVERIFY(high(gateOutput(GateKind::Xor, 5.0, 0.0)));
    QVERIFY(high(gateOutput(GateKind::Xor, 0.0, 5.0)));
    QVERIFY(low(gateOutput(GateKind::Xor, 5.0, 5.0)));
    QVERIFY(low(gateOutput(GateKind::Xor, 0.0, 0.0)));

    // 同或门：两个一样才高
    QVERIFY(high(gateOutput(GateKind::Xnor, 5.0, 5.0)));
    QVERIFY(high(gateOutput(GateKind::Xnor, 0.0, 0.0)));
    QVERIFY(low(gateOutput(GateKind::Xnor, 5.0, 0.0)));
    QVERIFY(low(gateOutput(GateKind::Xnor, 0.0, 5.0)));
}

void CoreAnalysisTest::anyGroundKindCanServeAsReference()
{
    // 只画了 AGND、没画 GND 的电路也要能仿真：参考点取哪一个只影响读数基准，
    // 不影响电路本身的方程。「五种地相互隔离」说的是它们各自成网，不是
    // 「必须有 GND」。
    Circuit c;

    Component source;
    source.id = 1;
    source.type = ComponentType::VoltageSource;
    source.pos = QPointF(300, 200);
    source.refdes = QStringLiteral("V1");
    source.dcValue = 10.0;
    c.components.append(source);

    // 源的两个引脚正好落在两个元件的位置上：不需要画线也算连接
    Component probe;
    probe.id = 2;
    probe.type = ComponentType::Probe;
    probe.pos = QPointF(270, 200); // 落在 V1 的 0 号引脚上
    probe.refdes = QStringLiteral("TP1");
    probe.groundKind = GroundKind::Agnd; // 以 AGND 为参考
    c.components.append(probe);

    Component agnd;
    agnd.id = 3;
    agnd.type = ComponentType::Ground;
    agnd.pos = QPointF(330, 200); // 落在 V1 的 1 号引脚上
    agnd.refdes = QStringLiteral("GND1");
    agnd.groundKind = GroundKind::Agnd; // 只画了 AGND，没有 GND
    c.components.append(agnd);

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    // 参考节点是 AGND 那个根
    QCOMPARE(net.nodeOf(agnd.id, 0), 0);
    QVERIFY(net.nodeOf(source.id, 0) > 0);
    // 应当说清楚参考点是怎么来的
    bool told = false;
    for (const QString& warning : net.warnings) {
        if (warning.contains(QStringLiteral("AGND")) && warning.contains(QStringLiteral("参考节点")))
            told = true;
    }
    QVERIFY2(told, "应当提示已把 AGND 当作参考节点");

    SimulationOptions options;
    options.kind = AnalysisKind::Transient;
    options.tStop = 1e-3;
    options.tStep = 1e-4;
    const SimulationResult result = Simulator::run(c, options);
    QVERIFY2(result.ok, qPrintable(result.error));
    // AGND 是节点 0，源的正端就是 10 V
    const Signal* reading = result.signal(QStringLiteral("V(%1)").arg(net.nodeOf(source.id, 0)));
    QVERIFY(reading);
    QVERIFY2(near(reading->y.last(), 10.0, 1e-6),
             qPrintable(QString::number(reading->y.last())));

    // 同时画了 GND 和 AGND 时，AGND 仍然是独立网络（原有行为不变）
    Circuit mixed;
    Component g;
    g.id = 1;
    g.type = ComponentType::Ground;
    g.pos = QPointF(0, 0);
    g.refdes = QStringLiteral("GND1");
    mixed.components.append(g);
    Component a;
    a.id = 2;
    a.type = ComponentType::Ground;
    a.pos = QPointF(200, 0);
    a.refdes = QStringLiteral("GND2");
    a.groundKind = GroundKind::Agnd;
    mixed.components.append(a);
    const Netlist mixedNet = mixed.buildNetlist();
    QVERIFY(mixedNet.valid);
    QCOMPARE(mixedNet.nodeOf(g.id, 0), 0);
    QVERIFY(mixedNet.nodeOf(a.id, 0) > 0); // AGND 不是 0
}

void CoreAnalysisTest::meterServesAsReferenceWithoutGround()
{
    // 没有 GND，但有万用表：拿它的负端（1 号引脚，真表上的 COM）当参考节点，
    // 仿真照常能跑 —— 万用表本来就是两脚表，读数只看两端之差。
    Circuit c;

    Component source;
    source.id = 1;
    source.type = ComponentType::VoltageSource;
    source.pos = QPointF(100, 300);
    source.rotation = 90;
    source.refdes = QStringLiteral("V1");
    source.dcValue = 10.0;
    c.components.append(source);

    Component resistor;
    resistor.id = 2;
    resistor.type = ComponentType::Resistor;
    resistor.pos = QPointF(300, 200);
    resistor.refdes = QStringLiteral("R1");
    resistor.value = 1000.0;
    c.components.append(resistor);

    Component meter;
    meter.id = 3;
    meter.type = ComponentType::Multimeter;
    meter.pos = QPointF(300, 340);
    meter.refdes = QStringLiteral("XMM1");
    meter.multimeterMode = MultimeterMode::Voltage;
    c.components.append(meter);

    // V1+ → R1 左 → 万用表+；R1 右 → 万用表− → V1−
    c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(270, 200)}});
    c.wires.append(Wire{{QPointF(270, 200), QPointF(270, 340)}});
    c.wires.append(Wire{{QPointF(330, 200), QPointF(420, 200), QPointF(420, 400),
                         QPointF(100, 400), QPointF(100, 330)}});
    c.wires.append(Wire{{QPointF(330, 340), QPointF(330, 400)}});

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    // 得说清楚参考节点是怎么来的，不能悄悄换掉
    bool told = false;
    for (const QString& warning : net.warnings) {
        if (warning.contains(QStringLiteral("XMM1")) && warning.contains(QStringLiteral("参考节点")))
            told = true;
    }
    QVERIFY2(told, "应当提示已把万用表负端当作参考节点");

    // 万用表负端就是节点 0，正端读到 10 V
    QCOMPARE(net.nodeOf(meter.id, 1), 0);
    QVERIFY(net.nodeOf(meter.id, 0) > 0);

    SimulationOptions options;
    options.kind = AnalysisKind::Transient;
    options.tStop = 1e-3;
    options.tStep = 1e-4;
    const SimulationResult result = Simulator::run(c, options);
    QVERIFY2(result.ok, qPrintable(result.error));
    const Signal* reading = result.signal(QStringLiteral("XMM1"));
    QVERIFY(reading);
    QVERIFY2(near(reading->y.last(), 10.0, 1e-6),
             qPrintable(QString::number(reading->y.last())));

    // 既没有 GND 也没有万用表时，仍然要报错（groundRequired 覆盖的另一半）
    Circuit bare;
    addComponent(bare, makeComp(ComponentType::Resistor, QPointF(0, 0), 100.0));
    addComponent(bare, makeComp(ComponentType::VoltageSource, QPointF(-60, 0), 5.0));
    QVERIFY(!bare.buildNetlist().valid);
}

void CoreAnalysisTest::groundRequired()
{
    Circuit c;
    addComponent(c, makeComp(ComponentType::Resistor, QPointF(0, 0), 100.0));
    addComponent(c, makeComp(ComponentType::VoltageSource, QPointF(-60, 0), 5.0));
    const Netlist net = c.buildNetlist();
    QVERIFY(!net.valid);
    QVERIFY(!net.error.isEmpty());
}

QTEST_GUILESS_MAIN(CoreAnalysisTest)

#include "test_core_analysis.moc"
