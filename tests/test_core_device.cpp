#include "core/Circuit.h"

#include "core/Language.h"
#include "core/Units.h"
#include "core/UpdateChecker.h"
#include "csm_version.h"
#include "sim/Simulator.h"

#include <QJsonArray>
#include <QJsonObject>
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

bool near(double a, double b, double tol)
{
    return std::abs(a - b) <= tol;
}

} // namespace

class CoreDeviceTest : public QObject {
    Q_OBJECT

private slots:
    void bipolarTransistorAmplifies();
    void polarizedCapacitorReverseDetected();
    void ledForwardVoltage();
    void pnpTransistorMirrorsNpn();
    void transformerStepsVoltageByTurnsRatio();
    void primaryAuxTransformerWindings();
    void transformerThreeWindingsHaveOwnRatios();
    void transformerPolarityFlipsPhase();
    void transformerCurrentIsReported();
    void liveScopeSignalsLineUpWithValues();
    void ob2362DrivesGateByFeedback();
    void ob2362HasBiasClampAndUvlo();
    void switchingControllerSuggestsFineStep();
    void tl431RegulatesToReference();
    void optocouplerCouplesLight();
    void mosfetSwitches();
    void pushButtonContactsFollowState();
    void pmosConductsWithNegativeGate();
    void igbtConductsAboveThreshold();
    void diodeRectifier();
    void diodeForwardDropIsAdjustable();
    void zenerClampsItsBreakdownVoltage();
    void zenerFallbackIsForwardDiode();
    void multimeterMeasuresVoltageCurrentResistance();
    void multimeterMeasuresAcRms();
    void acMeterHonoursConfiguredFrequency();
    void groundKindsAreSeparateNets();
    void signalGeneratorPlaysEveryWaveform();
    void signalGeneratorDrivesCircuitAndRoundTrips();
};

void CoreDeviceTest::bipolarTransistorAmplifies()
{
    // 恒流源向基极注入 10 µA，beta = 100 → 集电极电流 1 mA
    Circuit c;

    Component v;
    v.id = 1;
    v.type = ComponentType::VoltageSource;
    v.pos = QPointF(100, 300);
    v.rotation = 90;
    v.refdes = QStringLiteral("V1");
    v.function = SourceFunction::Constant;
    v.dcValue = 12.0;
    c.components.append(v);

    Component rc;
    rc.id = 2;
    rc.type = ComponentType::Resistor;
    rc.pos = QPointF(200, 200);
    rc.refdes = QStringLiteral("RC");
    rc.value = 1000.0;
    c.components.append(rc);

    Component q;
    q.id = 3;
    q.type = ComponentType::Npn;
    q.pos = QPointF(400, 300);
    q.refdes = QStringLiteral("Q1");
    q.beta = 100.0;
    c.components.append(q);

    Component ib;
    ib.id = 4;
    ib.type = ComponentType::CurrentSource;
    ib.pos = QPointF(300, 400);
    ib.refdes = QStringLiteral("IB");
    ib.function = SourceFunction::Constant;
    ib.dcValue = 10e-6;
    c.components.append(ib);

    Component g1;
    g1.id = 5;
    g1.type = ComponentType::Ground;
    g1.pos = QPointF(100, 400);
    g1.refdes = QStringLiteral("GND");
    c.components.append(g1);

    Component g2 = g1;
    g2.id = 6;
    g2.pos = QPointF(430, 400);
    c.components.append(g2);

    Component g3 = g1;
    g3.id = 7;
    g3.pos = QPointF(270, 430);
    c.components.append(g3);

    c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(170, 200)}});
    c.wires.append(Wire{{QPointF(230, 200), QPointF(430, 200), QPointF(430, 270)}});
    c.wires.append(Wire{{QPointF(430, 330), QPointF(430, 400)}});
    c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
    c.wires.append(Wire{{QPointF(270, 400), QPointF(270, 430)}});
    c.wires.append(Wire{{QPointF(330, 400), QPointF(370, 400), QPointF(370, 300)}});

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QVERIFY2(net.warnings.isEmpty(), qPrintable(net.warnings.join(QStringLiteral("; "))));

    SimulationOptions opt;
    opt.kind = AnalysisKind::OperatingPoint;
    const SimulationResult r = Simulator::run(c, opt);
    QVERIFY2(r.ok, qPrintable(r.error));

    // 用瞬态取一处稳态读数，便于断言
    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 1e-4;
    tr.tStep = 1e-5;
    const SimulationResult rt = Simulator::run(c, tr);
    QVERIFY2(rt.ok, qPrintable(rt.error));

    const int collector = net.nodeOf(q.id, 0);
    const int base = net.nodeOf(q.id, 1);
    const Signal* vc = rt.signal(QStringLiteral("V(%1)").arg(collector));
    const Signal* vb = rt.signal(QStringLiteral("V(%1)").arg(base));
    QVERIFY(vc && vb);

    // 集电极电压 = 12 − Ic·1kΩ；Ic = beta·Ib = 1 mA → 11 V
    QVERIFY2(near(vc->y.last(), 11.0, 0.2), qPrintable(QString::number(vc->y.last())));
    // 基极-发射极正向导通，约 0.6~0.75 V
    QVERIFY2(vb->y.last() > 0.5 && vb->y.last() < 0.85,
             qPrintable(QString::number(vb->y.last())));
}


void CoreDeviceTest::acMeterHonoursConfiguredFrequency()
{
    // 交流档的测量窗口 = 1 / 频率。频率必须按这只表自己填的算：
    // 以前窗口写死 0.02 s（50 Hz），而且属性面板的输入框根本没写回元件 ——
    // 填 400 Hz 也没用，读数会跟着窗口乱跳。
    auto build = [](double signalHz, double meterHz) {
        Circuit c;
        Component v;
        v.id = 1;
        v.type = ComponentType::VoltageSource;
        v.pos = QPointF(100, 350);
        v.rotation = 90;
        v.refdes = QStringLiteral("V1");
        v.function = SourceFunction::Sine;
        v.amplitude = 220.0;
        v.frequency = signalHz;
        c.components.append(v);

        Component mm;
        mm.id = 2;
        mm.type = ComponentType::Multimeter;
        mm.pos = QPointF(250, 350);
        mm.rotation = 90;
        mm.refdes = QStringLiteral("XMM1");
        mm.multimeterMode = MultimeterMode::AcVoltage;
        mm.acFrequency = meterHz;
        c.components.append(mm);

        Component r;
        r.id = 3;
        r.type = ComponentType::Resistor;
        r.pos = QPointF(350, 200);
        r.refdes = QStringLiteral("R1");
        r.value = 1000.0;
        c.components.append(r);

        Component g;
        g.id = 4;
        g.type = ComponentType::Ground;
        g.pos = QPointF(100, 500);
        g.refdes = QStringLiteral("GND");
        c.components.append(g);

        c.wires.append(Wire{{QPointF(100, 320), QPointF(100, 200), QPointF(320, 200)}});
        c.wires.append(Wire{{QPointF(380, 200), QPointF(400, 200), QPointF(400, 320)}});
        c.wires.append(Wire{{QPointF(400, 380), QPointF(400, 500), QPointF(100, 500)}});
        c.wires.append(Wire{{QPointF(100, 380), QPointF(100, 500)}});
        c.wires.append(Wire{{QPointF(250, 200), QPointF(250, 320)}});
        c.wires.append(Wire{{QPointF(250, 380), QPointF(250, 500)}});
        return c;
    };

    const auto readBatch = [&build](double signalHz, double meterHz, double tStop) {
        const Circuit c = build(signalHz, meterHz);
        SimulationOptions opt;
        opt.kind = AnalysisKind::Transient;
        opt.tStop = tStop;
        opt.tStep = 1.0 / (meterHz * 200.0);
        const SimulationResult r = Simulator::run(c, opt);
        if (!r.ok)
            return -1.0;
        const Signal* sig = r.signal(QStringLiteral("XMM1"));
        return sig ? sig->y.last() : -1.0;
    };

    // ① 窗口长度就是 1/频率：300 Hz 的窗口 3.3 ms，跑到 5 ms 早该有读数
    const double at5ms300 = readBatch(300.0, 300.0, 0.005);
    QVERIFY2(std::isfinite(at5ms300),
             qPrintable(QStringLiteral("300 Hz 窗口 5 ms 时应当已有读数，实际 %1")
                            .arg(at5ms300)));
    // 同一时刻把表填成 50 Hz（窗口 20 ms）还攒不满 —— 读数必须是 NaN（界面显示横杠）。
    // 这一条正是「频率没生效」的照妖镜：以前两条路径都按 50 Hz 取窗口。
    const double at5ms50 = readBatch(300.0, 50.0, 0.005);
    QVERIFY2(std::isnan(at5ms50),
             qPrintable(QStringLiteral("50 Hz 窗口 5 ms 时不该有读数，实际 %1")
                            .arg(at5ms50)));

    // ② 220 V 有效值、400 Hz：表也填 400 Hz 时读数应当准
    const double at400 = readBatch(400.0, 400.0, 0.05);
    QVERIFY2(near(at400, 220.0, 2.0),
             qPrintable(QStringLiteral("400 Hz 时读 %1 V").arg(at400)));

    // ③ 实时仿真走的必须是同一个窗口（以前那条路径把周期写死成 0.02 s）
    const auto liveReading = [&build](double meterHz, double seconds) {
        const Circuit c = build(400.0, meterHz);
        LiveTransient live;
        QString error;
        const double h = 1.0 / (meterHz * 200.0);
        if (!live.start(c, h, &error))
            return -1.0;
        const int index = live.signalNames().indexOf(QStringLiteral("XMM1"));
        if (index < 0)
            return -1.0;
        if (!live.advance(int(std::lround(seconds / h)), &error))
            return -1.0;
        return live.values().at(index);
    };
    const double live400 = liveReading(400.0, 0.05);
    QVERIFY2(near(live400, 220.0, 2.0),
             qPrintable(QStringLiteral("实时仿真 400 Hz 读 %1 V").arg(live400)));
    const double live5ms300 = liveReading(300.0, 0.005);
    QVERIFY2(std::isfinite(live5ms300),
             qPrintable(QStringLiteral("实时仿真 300 Hz 窗口 5 ms 时应当已有读数，实际 %1")
                            .arg(live5ms300)));

    // 频率范围 20~500 Hz：越界按边界算，没填按 50 Hz
    Component meter;
    meter.type = ComponentType::Multimeter;
    meter.multimeterMode = MultimeterMode::AcVoltage;
    meter.acFrequency = 0.0;
    QCOMPARE(meter.acMeterFrequency(), 50.0);
    meter.acFrequency = 5.0;
    QCOMPARE(meter.acMeterFrequency(), Component::kAcFrequencyMin);
    meter.acFrequency = 1000.0;
    QCOMPARE(meter.acMeterFrequency(), Component::kAcFrequencyMax);
    meter.acFrequency = 300.0;
    QCOMPARE(meter.acMeterFrequency(), 300.0);

    // 存盘往返：这个值以前写出去却从来不读回来，存一次就退回 50 Hz
    meter.acFrequency = 400.0;
    Circuit saved;
    saved.components.append(meter);
    const Circuit back = Circuit::fromJson(saved.toJson());
    QCOMPARE(back.components.first().acFrequency, 400.0);

    // 读盘时越界值也要钳住
    QJsonObject raw = saved.toJson();
    QJsonArray comps = raw.value(QStringLiteral("components")).toArray();
    QJsonObject first = comps.at(0).toObject();
    first.insert(QStringLiteral("acFrequency"), 5000.0);
    comps.replace(0, first);
    raw.insert(QStringLiteral("components"), comps);
    QCOMPARE(Circuit::fromJson(raw).components.first().acFrequency,
             Component::kAcFrequencyMax);
}

void CoreDeviceTest::polarizedCapacitorReverseDetected()
{
    // 5 V 电源反接在极性电容上：正极接 0 V、负极接 5 V，反向电压 5 V
    auto build = [](bool reverse) {
        Circuit c;
        Component v;
        v.id = 1;
        v.type = ComponentType::VoltageSource;
        v.pos = QPointF(100, 300);
        v.rotation = 90;
        v.refdes = QStringLiteral("V1");
        v.function = SourceFunction::Constant;
        v.dcValue = 5.0;
        c.components.append(v);

        Component cap;
        cap.id = 2;
        cap.type = ComponentType::Capacitor;
        cap.pos = QPointF(300, 300);
        cap.rotation = 90;
        cap.refdes = QStringLiteral("C1");
        cap.value = 100e-6;
        cap.polarized = true;
        c.components.append(cap);

        Component g;
        g.id = 3;
        g.type = ComponentType::Ground;
        g.pos = QPointF(100, 400);
        g.refdes = QStringLiteral("GND");
        c.components.append(g);
        Component g2 = g;
        g2.id = 4;
        g2.pos = QPointF(300, 400);
        c.components.append(g2);

        // 电容竖直放置（rot 90）：引脚 0 在上、引脚 1 在下
        // 反接：5 V 接到引脚 1，引脚 0 接地
        if (reverse) {
            // 5 V → 右侧绕过去接引脚 1（负极）；引脚 0（正极）接地
            c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(380, 200),
                                 QPointF(380, 330), QPointF(300, 330)}});
            c.wires.append(Wire{{QPointF(300, 270), QPointF(300, 100), QPointF(60, 100),
                                 QPointF(60, 400)}});
            c.wires.append(Wire{{QPointF(60, 400), QPointF(300, 400)}});
            c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
        } else {
            c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(300, 200),
                                 QPointF(300, 270)}}); // 5 V 接引脚 0（正极）
            c.wires.append(Wire{{QPointF(300, 330), QPointF(300, 400)}});
            c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
        }
        return c;
    };

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 1e-3;
    tr.tStep = 1e-5;

    // 正接：不该击穿
    const SimulationResult ok = Simulator::run(build(false), tr);
    QVERIFY2(ok.ok, qPrintable(ok.error));
    QVERIFY2(ok.blownCapacitors.isEmpty(), "正接不应报击穿");

    // 反接：应当报击穿，并给出反向电压
    const SimulationResult bad = Simulator::run(build(true), tr);
    QVERIFY2(bad.ok, qPrintable(bad.error));
    QVERIFY2(bad.blownCapacitors.contains(2), "反接应当被检测到");
    // 记录的是电容端电压（引脚 0 相对引脚 1），反接时为负
    QVERIFY2(bad.blownCapacitors.value(2) < -1.0,
             qPrintable(QString::number(bad.blownCapacitors.value(2))));

    // 非极性电容反接不算击穿
    Circuit plain = build(true);
    for (Component& comp : plain.components) {
        if (comp.type == ComponentType::Capacitor)
            comp.polarized = false;
    }
    const SimulationResult none = Simulator::run(plain, tr);
    QVERIFY2(none.ok, qPrintable(none.error));
    QVERIFY2(none.blownCapacitors.isEmpty(), "无极性电容不应报击穿");
}

void CoreDeviceTest::ledForwardVoltage()
{
    // 5 V 电源经 1 kΩ 点亮 LED（Vf = 2 V）：
    // 电流约 (5 − 2)/1k = 3 mA，LED 两端应接近 2 V
    Circuit c;

    Component v;
    v.id = 1;
    v.type = ComponentType::VoltageSource;
    v.pos = QPointF(100, 300);
    v.rotation = 90;
    v.refdes = QStringLiteral("V1");
    v.function = SourceFunction::Constant;
    v.dcValue = 5.0;
    c.components.append(v);

    Component r;
    r.id = 2;
    r.type = ComponentType::Resistor;
    r.pos = QPointF(250, 200);
    r.refdes = QStringLiteral("R1");
    r.value = 1000.0;
    c.components.append(r);

    Component led;
    led.id = 3;
    led.type = ComponentType::Led;
    led.pos = QPointF(400, 300);
    led.rotation = 90;
    led.refdes = QStringLiteral("D1");
    led.vf = 2.0;
    c.components.append(led);

    Component g;
    g.id = 4;
    g.type = ComponentType::Ground;
    g.pos = QPointF(100, 400);
    g.refdes = QStringLiteral("GND");
    c.components.append(g);
    Component g2 = g;
    g2.id = 5;
    g2.pos = QPointF(400, 400);
    c.components.append(g2);

    // LED 竖直放置（rot 90）：引脚 0（阳极）在上、引脚 1（阴极）在下接地
    c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(220, 200)}});
    c.wires.append(Wire{{QPointF(280, 200), QPointF(400, 200), QPointF(400, 270)}});
    c.wires.append(Wire{{QPointF(400, 330), QPointF(400, 400)}});
    c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QVERIFY2(net.warnings.isEmpty(), qPrintable(net.warnings.join(QStringLiteral("; "))));

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 1e-5;
    tr.tStep = 1e-6;
    const SimulationResult r2 = Simulator::run(c, tr);
    QVERIFY2(r2.ok, qPrintable(r2.error));

    const int top = net.nodeOf(led.id, 0); // 阳极
    const Signal* vled = r2.signal(QStringLiteral("V(%1)").arg(top));
    QVERIFY(vled);
    // 正向压降应当落在 1.7~2.4 V（模型按 20 mA 时 2.0 V 标定）
    QVERIFY2(vled->y.last() > 1.7 && vled->y.last() < 2.4,
             qPrintable(QString::number(vled->y.last())));

    // 串联电阻上的压降约为 5 − Vf，电流在 1.5~3.5 mA
    const double current = (5.0 - vled->y.last()) / 1000.0;
    QVERIFY2(current > 1.5e-3 && current < 3.5e-3, qPrintable(QString::number(current)));

    // 界面靠这个电流点亮 LED，直流工作点也要带上元件电流
    SimulationOptions op;
    op.kind = AnalysisKind::OperatingPoint;
    const SimulationResult dc = Simulator::run(c, op);
    QVERIFY(dc.ok);
    const double idc = dc.elementCurrents.value(QStringLiteral("I(D1)"), 0.0);
    QVERIFY2(idc > 1.5e-3 && idc < 3.5e-3, qPrintable(QString::number(idc)));
}

void CoreDeviceTest::pnpTransistorMirrorsNpn()
{
    // PNP：发射极接 +12 V，恒流源从基极抽出 10 µA，集电极经 1 kΩ 到地
    Circuit c;

    Component v;
    v.id = 1;
    v.type = ComponentType::VoltageSource;
    v.pos = QPointF(100, 300);
    v.rotation = 90;
    v.refdes = QStringLiteral("V1");
    v.function = SourceFunction::Constant;
    v.dcValue = 12.0;
    c.components.append(v);

    Component rc;
    rc.id = 2;
    rc.type = ComponentType::Resistor;
    rc.pos = QPointF(250, 150);
    rc.refdes = QStringLiteral("RC");
    rc.value = 1000.0;
    c.components.append(rc);

    Component q;
    q.id = 3;
    q.type = ComponentType::Pnp;
    q.pos = QPointF(400, 300);
    q.refdes = QStringLiteral("Q1");
    q.beta = 100.0;
    c.components.append(q);

    Component ib;
    ib.id = 4;
    ib.type = ComponentType::CurrentSource;
    ib.pos = QPointF(300, 400);
    ib.refdes = QStringLiteral("IB");
    ib.function = SourceFunction::Constant;
    ib.dcValue = -10e-6; // 负号 = 从基极抽出 10 µA
    c.components.append(ib);

    Component g;
    g.id = 5;
    g.type = ComponentType::Ground;
    g.pos = QPointF(100, 400);
    g.refdes = QStringLiteral("GND");
    c.components.append(g);
    for (const QPointF& pos : {QPointF(220, 400), QPointF(270, 430)}) {
        Component gg = g;
        gg.id = 6 + static_cast<int>(c.components.size());
        gg.pos = pos;
        c.components.append(gg);
    }

    // 发射极 → 上方电源轨 → V1 正端
    c.wires.append(Wire{{QPointF(430, 330), QPointF(500, 330), QPointF(500, 100),
                         QPointF(100, 100), QPointF(100, 270)}});
    c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
    // 集电极 → 上方 → RC → 地
    c.wires.append(Wire{{QPointF(430, 270), QPointF(430, 150), QPointF(280, 150)}});
    c.wires.append(Wire{{QPointF(220, 150), QPointF(220, 400)}});
    // 基极 → 恒流源 → 地
    c.wires.append(Wire{{QPointF(370, 300), QPointF(330, 300), QPointF(330, 400)}});
    c.wires.append(Wire{{QPointF(270, 400), QPointF(270, 430)}});

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QVERIFY2(net.warnings.isEmpty(), qPrintable(net.warnings.join(QStringLiteral("; "))));

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 1e-4;
    tr.tStep = 1e-5;
    const SimulationResult r = Simulator::run(c, tr);
    QVERIFY2(r.ok, qPrintable(r.error));

    const int collector = net.nodeOf(q.id, 0);
    const Signal* vc = r.signal(QStringLiteral("V(%1)").arg(collector));
    QVERIFY(vc);
    // 集电极电流 1 mA 流过 1 kΩ → 约 1 V
    QVERIFY2(vc->y.last() > 0.6 && vc->y.last() < 1.6,
             qPrintable(QString::number(vc->y.last())));
}

void CoreDeviceTest::transformerStepsVoltageByTurnsRatio()
{
    // 1 kHz、10 V 正弦驱动初级，次级接 1 kΩ；匝数比 1:n → 次级约为 n·10 V
    auto build = [](double ratio) {
        Circuit c;
        Component v;
        v.id = 1;
        v.type = ComponentType::VoltageSource;
        v.pos = QPointF(100, 300);
        v.rotation = 90;
        v.refdes = QStringLiteral("V1");
        v.function = SourceFunction::Sine;
        v.amplitude = 10.0 / std::sqrt(2.0); // 峰值 10.0 V
        v.frequency = 1000.0;
        c.components.append(v);

        Component t;
        t.id = 2;
        t.type = ComponentType::Transformer;
        t.pos = QPointF(300, 300);
        t.refdes = QStringLiteral("T1");
        t.value = ratio;
        t.lpri = 1.0;
        c.components.append(t);

        Component r;
        r.id = 3;
        r.type = ComponentType::Resistor;
        r.pos = QPointF(450, 300);
        r.rotation = 90;
        r.refdes = QStringLiteral("RL");
        r.value = 1000.0;
        c.components.append(r);

        Component g;
        g.id = 4;
        g.type = ComponentType::Ground;
        g.pos = QPointF(100, 400);
        g.refdes = QStringLiteral("GND");
        c.components.append(g);
        for (const QPointF& pos : {QPointF(270, 400), QPointF(330, 400), QPointF(450, 400)}) {
            Component gg = g;
            gg.id = 5 + static_cast<int>(c.components.size());
            gg.pos = pos;
            c.components.append(gg);
        }

        // 初级：V+ → T1 初级+；初级− → 地；V− → 地
        c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(270, 200),
                             QPointF(270, 290)}});
        c.wires.append(Wire{{QPointF(270, 310), QPointF(270, 400)}});
        c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
        // 次级：次级+ → RL → 地；次级− → 地
        c.wires.append(Wire{{QPointF(330, 290), QPointF(450, 290), QPointF(450, 270)}});
        c.wires.append(Wire{{QPointF(450, 330), QPointF(450, 400)}});
        c.wires.append(Wire{{QPointF(330, 310), QPointF(330, 400)}});
        return c;
    };

    auto run = [](const Circuit& c) {
        const Netlist net = c.buildNetlist();
        SimulationOptions tr;
        tr.kind = AnalysisKind::Transient;
        tr.tStop = 0.004;
        tr.tStep = 1e-5;
        const SimulationResult r = Simulator::run(c, tr);
        return qMakePair(net, r);
    };

    const QPair<Netlist, SimulationResult> up = run(build(2.0));
    QVERIFY2(up.second.ok, qPrintable(up.second.error));
    auto peak = [&up](int node) {
        const Signal* s = up.second.signal(QStringLiteral("V(%1)").arg(node));
        if (!s)
            return -1.0;
        double m = 0.0;
        for (double y : s->y)
            m = std::max(m, std::abs(y));
        return m;
    };
    const double primary = peak(up.first.nodeOf(2, 0));
    const double secondary = peak(up.first.nodeOf(2, 2));
    QVERIFY2(primary > 9.5 && primary < 10.5, qPrintable(QString::number(primary)));
    QVERIFY2(secondary > 18.5 && secondary < 21.5,
             qPrintable(QStringLiteral("1:2 次级 %1 V").arg(secondary)));

    // 降压：1:0.5 → 次级约 5 V
    const QPair<Netlist, SimulationResult> down = run(build(0.5));
    QVERIFY2(down.second.ok, qPrintable(down.second.error));
    const Signal* s = down.second.signal(
        QStringLiteral("V(%1)").arg(down.first.nodeOf(2, 2)));
    QVERIFY(s);
    double m = 0.0;
    for (double y : s->y)
        m = std::max(m, std::abs(y));
    QVERIFY2(m > 4.3 && m < 5.6, qPrintable(QStringLiteral("1:0.5 次级 %1 V").arg(m)));
}


void CoreDeviceTest::transformerCurrentIsReported()
{
    // 变压器的装配走 windingBranches（每个绕组一条支路），但「读这个元件的电流」
    // 走 branchOf。漏了登记的话 I(T1) 恒为 0 —— 工作点报告、elementCurrents、
    // 波形全错，而电路本身算得没错。多绕组还要各占一路：I(T1) / I(T1.2) / I(T1.3)。
    auto build = [](SourceFunction fn) {
        Circuit c;
        Component v;
        v.id = 1;
        v.type = ComponentType::VoltageSource;
        v.pos = QPointF(100, 300);
        v.rotation = 90;
        v.refdes = QStringLiteral("V1");
        v.function = fn;
        v.dcValue = 12.0;
        v.amplitude = 10.0 / std::sqrt(2.0);
        v.frequency = 1000.0;
        c.components.append(v);

        Component t;
        t.id = 2;
        t.type = ComponentType::Transformer;
        t.pos = QPointF(300, 300);
        t.refdes = QStringLiteral("T1");
        t.value = 1.0;
        t.lpri = 1.0;
        c.components.append(t);

        Component r;
        r.id = 3;
        r.type = ComponentType::Resistor;
        r.pos = QPointF(450, 300);
        r.rotation = 90;
        r.refdes = QStringLiteral("RL");
        r.value = 1000.0;
        c.components.append(r);

        Component g;
        g.id = 4;
        g.type = ComponentType::Ground;
        g.pos = QPointF(100, 400);
        g.refdes = QStringLiteral("GND");
        c.components.append(g);
        int id = 5;
        for (const QPointF& pos : {QPointF(270, 400), QPointF(330, 400), QPointF(450, 400)}) {
            Component gg = g;
            gg.id = id++;
            gg.pos = pos;
            c.components.append(gg);
        }
        // 初级：V+ → T1 初级+；初级− → 地；V− → 地
        c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(270, 200),
                             QPointF(270, 290)}});
        c.wires.append(Wire{{QPointF(270, 310), QPointF(270, 400)}});
        c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
        // 次级：次级+ → RL → 地；次级− → 地
        c.wires.append(Wire{{QPointF(330, 290), QPointF(450, 290), QPointF(450, 270)}});
        c.wires.append(Wire{{QPointF(450, 330), QPointF(450, 400)}});
        c.wires.append(Wire{{QPointF(330, 310), QPointF(330, 400)}});
        return c;
    };

    // 工作点：初级与电源串联，电流必须相等；两路都得出现在 elementCurrents 里
    SimulationOptions op;
    op.kind = AnalysisKind::OperatingPoint;
    const SimulationResult dc = Simulator::run(build(SourceFunction::Constant), op);
    QVERIFY2(dc.ok, qPrintable(dc.error));

    QVERIFY2(dc.elementCurrents.contains(QStringLiteral("I(T1)")), "报告里没有 I(T1)");
    QVERIFY2(dc.elementCurrents.contains(QStringLiteral("I(T1.2)")), "报告里没有 I(T1.2)");
    const double primary = dc.elementCurrents.value(QStringLiteral("I(T1)"));
    const double source = dc.elementCurrents.value(QStringLiteral("I(V1)"));
    QVERIFY2(std::abs(primary) > 1.0,
             qPrintable(QStringLiteral("I(T1) 不该是 0：%1").arg(primary)));
    QVERIFY2(std::abs(std::abs(primary) - std::abs(source)) < 0.5,
             qPrintable(QStringLiteral("I(T1)=%1 与 I(V1)=%2 应当相等").arg(primary).arg(source)));

    bool primaryLine = false;
    bool secondLine = false;
    for (const QString& line : dc.report.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("T1 "))) {
            primaryLine = true;
            QVERIFY2(!line.contains(QStringLiteral("= 0 A")), qPrintable(line));
        }
        if (trimmed.startsWith(QStringLiteral("T1.2")))
            secondLine = true;
    }
    QVERIFY2(primaryLine, "工作点报告里没有 T1 这一行");
    QVERIFY2(secondLine, "工作点报告里没有 T1.2 这一行");

    // 瞬态：两个绕组都该有实际电流（直流下次级没有感应电动势，所以要用正弦）
    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 0.004;
    tr.tStep = 1e-5;
    const SimulationResult ac = Simulator::run(build(SourceFunction::Sine), tr);
    QVERIFY2(ac.ok, qPrintable(ac.error));
    auto peak = [&ac](const QString& name) {
        const Signal* sig = ac.signal(name);
        if (!sig)
            return -1.0;
        double m = 0.0;
        for (double y : sig->y)
            m = std::max(m, std::abs(y));
        return m;
    };
    const double iPrimary = peak(QStringLiteral("I(T1)"));
    const double iSecondary = peak(QStringLiteral("I(T1.2)"));
    // 10 V 峰值 / 1 kΩ 负载 → 次级约 10 mA；初级还要加上励磁电流
    QVERIFY2(iPrimary > 1e-3, qPrintable(QStringLiteral("I(T1) 峰值 %1").arg(iPrimary)));
    QVERIFY2(iSecondary > 5e-3 && iSecondary < 15e-3,
             qPrintable(QStringLiteral("I(T1.2) 峰值 %1").arg(iSecondary)));

    // 三绕组：每个绕组各占一路信号，名字依次是 I(T1) / I(T1.2) / I(T1.3)
    Circuit three = build(SourceFunction::Sine);
    for (Component& comp : three.components) {
        if (comp.type == ComponentType::Transformer)
            comp.transformerKind = TransformerKind::PrimaryAuxSecondary;
    }
    const SimulationResult tri = Simulator::run(three, tr);
    QVERIFY2(tri.ok, qPrintable(tri.error));
    for (const char* name : {"I(T1)", "I(T1.2)", "I(T1.3)"})
        QVERIFY2(tri.signal(QLatin1String(name)) != nullptr, name);
}


void CoreDeviceTest::liveScopeSignalsLineUpWithValues()
{
    // 动态示波器用 LiveTransient：signalNames() 与 values() 必须一一对应。
    // 变压器的采样按绕组展开后，名字表也要跟着展开 —— 少一条就会让后面的
    // 测试点差分信号整体错位，表现是示波器上某个测试点恒为 0 V。
    Circuit c;
    Component v;
    v.id = 1;
    v.type = ComponentType::VoltageSource;
    v.pos = QPointF(100, 300);
    v.rotation = 90;
    v.refdes = QStringLiteral("V1");
    v.function = SourceFunction::Sine;
    v.amplitude = 10.0 / std::sqrt(2.0);
    v.frequency = 1000.0;
    c.components.append(v);

    Component t;
    t.id = 2;
    t.type = ComponentType::Transformer;
    t.pos = QPointF(300, 300);
    t.refdes = QStringLiteral("T1");
    t.value = 1.0;
    t.lpri = 1.0;
    c.components.append(t);

    Component r;
    r.id = 3;
    r.type = ComponentType::Resistor;
    r.pos = QPointF(450, 300);
    r.rotation = 90;
    r.refdes = QStringLiteral("RL");
    r.value = 1000.0;
    c.components.append(r);

    Component g;
    g.id = 4;
    g.type = ComponentType::Ground;
    g.pos = QPointF(100, 400);
    g.refdes = QStringLiteral("GND");
    c.components.append(g);
    int id = 5;
    for (const QPointF& pos : {QPointF(270, 400), QPointF(330, 400), QPointF(450, 400)}) {
        Component gg = g;
        gg.id = id++;
        gg.pos = pos;
        c.components.append(gg);
    }
    c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(270, 200),
                         QPointF(270, 290)}});
    c.wires.append(Wire{{QPointF(270, 310), QPointF(270, 400)}});
    c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
    c.wires.append(Wire{{QPointF(330, 290), QPointF(450, 290), QPointF(450, 270)}});
    c.wires.append(Wire{{QPointF(450, 330), QPointF(450, 400)}});
    c.wires.append(Wire{{QPointF(330, 310), QPointF(330, 400)}});

    LiveTransient live;
    QString error;
    QVERIFY2(live.start(c, 1e-5, &error), qPrintable(error));
    QVERIFY2(live.advance(1, &error), qPrintable(error));

    const QStringList& names = live.signalNames();
    const QVector<double> values = live.values();
    QVERIFY2(!names.isEmpty(), "名字表是空的");
    QCOMPARE(names.size(), values.size());

    // 变压器的两个绕组各占一条名字
    QVERIFY2(names.contains(QStringLiteral("I(T1)")), "缺少 I(T1)");
    QVERIFY2(names.contains(QStringLiteral("I(T1.2)")), "缺少 I(T1.2)");
}

void CoreDeviceTest::primaryAuxTransformerWindings()
{
    // 初级 + 辅助绕组串联（等效 2N 匝）接 10 V，次级是 N 匝 → 约 5 V
    Circuit c;
    Component v;
    v.id = 1;
    v.type = ComponentType::VoltageSource;
    v.pos = QPointF(100, 300);
    v.rotation = 90;
    v.refdes = QStringLiteral("V1");
    v.function = SourceFunction::Sine;
    v.amplitude = 10.0 / std::sqrt(2.0); // 峰值 10.0 V
    v.frequency = 1000.0;
    c.components.append(v);

    Component t;
    t.id = 2;
    t.type = ComponentType::Transformer;
    t.pos = QPointF(300, 300);
    t.refdes = QStringLiteral("T1");
    t.value = 1.0;
    t.lpri = 1.0;
    t.transformerKind = TransformerKind::PrimaryAuxSecondary;
    c.components.append(t);

    Component r;
    r.id = 3;
    r.type = ComponentType::Resistor;
    r.pos = QPointF(450, 300);
    r.rotation = 90;
    r.refdes = QStringLiteral("RL");
    r.value = 1000.0;
    c.components.append(r);

    Component g;
    g.id = 4;
    g.type = ComponentType::Ground;
    g.pos = QPointF(100, 400);
    g.refdes = QStringLiteral("GND");
    c.components.append(g);
    for (const QPointF& pos : {QPointF(270, 400), QPointF(330, 400), QPointF(450, 400)}) {
        Component gg = g;
        gg.id = 5 + static_cast<int>(c.components.size());
        gg.pos = pos;
        c.components.append(gg);
    }

    // 三绕组引脚：0/1 = 初级（y −50/−20），2/3 = 辅助（20/50），4/5 = 次级（±50）
    // 初级与辅助绕组串联：V+ → 初级+，初级− → 辅助+，辅助− → 地
    c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(270, 200),
                         QPointF(270, 250)}});
    c.wires.append(Wire{{QPointF(270, 280), QPointF(270, 320)}});
    c.wires.append(Wire{{QPointF(270, 350), QPointF(270, 400)}});
    c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
    // 次级：+ → RL → 地；− → 地
    c.wires.append(Wire{{QPointF(330, 250), QPointF(450, 250), QPointF(450, 270)}});
    c.wires.append(Wire{{QPointF(450, 330), QPointF(450, 400)}});
    c.wires.append(Wire{{QPointF(330, 350), QPointF(330, 400)}});

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QCOMPARE(net.compPins.value(2).size(), 6);

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 0.004;
    tr.tStep = 1e-5;
    const SimulationResult result = Simulator::run(c, tr);
    QVERIFY2(result.ok, qPrintable(result.error));

    const Signal* s = result.signal(QStringLiteral("V(%1)").arg(net.nodeOf(2, 4)));
    QVERIFY(s);
    double peak = 0.0;
    for (double y : s->y)
        peak = std::max(peak, std::abs(y));
    QVERIFY2(peak > 4.3 && peak < 5.6,
             qPrintable(QStringLiteral("次级峰值 %1 V").arg(peak)));
}

void CoreDeviceTest::transformerThreeWindingsHaveOwnRatios()
{
    // 初级 10 V，辅助 1:1、次级 1:2 → 辅助约 10 V、次级约 20 V
    Circuit c;
    Component v;
    v.id = 1;
    v.type = ComponentType::VoltageSource;
    v.pos = QPointF(100, 300);
    v.rotation = 90;
    v.refdes = QStringLiteral("V1");
    v.function = SourceFunction::Sine;
    v.amplitude = 10.0 / std::sqrt(2.0); // 峰值 10.0 V
    v.frequency = 1000.0;
    c.components.append(v);

    Component t;
    t.id = 2;
    t.type = ComponentType::Transformer;
    t.pos = QPointF(300, 300);
    t.refdes = QStringLiteral("T1");
    t.value = 2.0;      // 次级 2:1
    t.auxRatio = 1.0;   // 辅助 1:1
    t.lpri = 1.0;
    t.transformerKind = TransformerKind::PrimaryAuxSecondary;
    c.components.append(t);

    Component ra;
    ra.id = 3;
    ra.type = ComponentType::Resistor;
    ra.pos = QPointF(430, 320);
    ra.refdes = QStringLiteral("RA");
    ra.value = 1000.0;
    c.components.append(ra);

    Component rs;
    rs.id = 4;
    rs.type = ComponentType::Resistor;
    rs.pos = QPointF(520, 250);
    rs.refdes = QStringLiteral("RS");
    rs.value = 1000.0;
    c.components.append(rs);

    Component g;
    g.id = 5;
    g.type = ComponentType::Ground;
    g.pos = QPointF(100, 400);
    g.refdes = QStringLiteral("GND");
    c.components.append(g);
    for (const QPointF& pos : {QPointF(240, 400), QPointF(270, 400), QPointF(330, 400),
                               QPointF(460, 400), QPointF(550, 400)}) {
        Component gg = g;
        gg.id = 6 + static_cast<int>(c.components.size());
        gg.pos = pos;
        c.components.append(gg);
    }

    // 初级：V+ → 初级+，初级− → 地
    c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(270, 200),
                         QPointF(270, 250)}});
    // 初级− 往左绕下去接地：直接走 x=270 会穿过辅助绕组的两个引脚
    c.wires.append(Wire{{QPointF(270, 280), QPointF(240, 280), QPointF(240, 400)}});
    c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
    // 辅助绕组：辅助+ → RA → 地，辅助− → 地
    c.wires.append(Wire{{QPointF(270, 320), QPointF(400, 320)}});
    c.wires.append(Wire{{QPointF(460, 320), QPointF(460, 400)}});
    c.wires.append(Wire{{QPointF(270, 350), QPointF(270, 400)}});
    // 次级：次级+ → RS → 地，次级− → 地
    c.wires.append(Wire{{QPointF(330, 250), QPointF(490, 250)}});
    c.wires.append(Wire{{QPointF(550, 250), QPointF(550, 400)}});
    c.wires.append(Wire{{QPointF(330, 350), QPointF(330, 400)}});

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 0.004;
    tr.tStep = 1e-5;
    const SimulationResult result = Simulator::run(c, tr);
    QVERIFY2(result.ok, qPrintable(result.error));

    auto peak = [&result](int node) {
        const Signal* s = result.signal(QStringLiteral("V(%1)").arg(node));
        if (!s)
            return -1.0;
        double m = 0.0;
        for (double y : s->y)
            m = std::max(m, std::abs(y));
        return m;
    };

    const double aux = peak(net.nodeOf(2, 2));
    const double sec = peak(net.nodeOf(2, 4));
    QVERIFY2(aux > 8.5 && aux < 11.5, qPrintable(QStringLiteral("辅助 %1 V").arg(aux)));
    QVERIFY2(sec > 17.0 && sec < 22.5, qPrintable(QStringLiteral("次级 %1 V").arg(sec)));
}

void CoreDeviceTest::transformerPolarityFlipsPhase()
{
    // 次级同名端反过来时，次级电压应当反相 180°
    auto build = [](bool inverted) {
        Circuit c;
        Component v;
        v.id = 1;
        v.type = ComponentType::VoltageSource;
        v.pos = QPointF(100, 300);
        v.rotation = 90;
        v.refdes = QStringLiteral("V1");
        v.function = SourceFunction::Sine;
        v.amplitude = 10.0 / std::sqrt(2.0); // 峰值 10.0 V
        v.frequency = 1000.0;
        c.components.append(v);

        Component t;
        t.id = 2;
        t.type = ComponentType::Transformer;
        t.pos = QPointF(300, 300);
        t.refdes = QStringLiteral("T1");
        t.value = 2.0;
        t.lpri = 1.0;
        t.secondaryInverted = inverted;
        c.components.append(t);

        Component r;
        r.id = 3;
        r.type = ComponentType::Resistor;
        r.pos = QPointF(450, 300);
        r.rotation = 90;
        r.refdes = QStringLiteral("RL");
        r.value = 1000.0;
        c.components.append(r);

        Component g;
        g.id = 4;
        g.type = ComponentType::Ground;
        g.pos = QPointF(100, 400);
        g.refdes = QStringLiteral("GND");
        c.components.append(g);
        for (const QPointF& pos : {QPointF(270, 400), QPointF(330, 400), QPointF(450, 400)}) {
            Component gg = g;
            gg.id = 5 + static_cast<int>(c.components.size());
            gg.pos = pos;
            c.components.append(gg);
        }

        c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(270, 200),
                             QPointF(270, 290)}});
        c.wires.append(Wire{{QPointF(270, 310), QPointF(270, 400)}});
        c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
        c.wires.append(Wire{{QPointF(330, 290), QPointF(450, 290), QPointF(450, 270)}});
        c.wires.append(Wire{{QPointF(450, 330), QPointF(450, 400)}});
        c.wires.append(Wire{{QPointF(330, 310), QPointF(330, 400)}});
        return c;
    };

    auto sample = [](const Circuit& c, int pin) {
        const Netlist net = c.buildNetlist();
        SimulationOptions tr;
        tr.kind = AnalysisKind::Transient;
        tr.tStop = 0.004;
        tr.tStep = 1e-5;
        const SimulationResult r = Simulator::run(c, tr);
        QVector<double> out;
        const Signal* s = r.signal(QStringLiteral("V(%1)").arg(net.nodeOf(2, pin)));
        return s ? s->y : out;
    };

    const QVector<double> normal = sample(build(false), 2);
    const QVector<double> inverted = sample(build(true), 2);
    QVERIFY(!normal.isEmpty() && normal.size() == inverted.size());

    // 找一个初级电压明显不为零的时刻，比较次级符号
    const Circuit probe = build(false);
    const Netlist net = probe.buildNetlist();
    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 0.004;
    tr.tStep = 1e-5;
    const SimulationResult r = Simulator::run(probe, tr);
    const Signal* primary = r.signal(QStringLiteral("V(%1)").arg(net.nodeOf(2, 0)));
    QVERIFY(primary);

    int checked = 0;
    for (int i = 0; i < normal.size() && checked < 20; ++i) {
        if (std::abs(primary->y.at(i)) < 2.0)
            continue;
        ++checked;
        const double same = normal.at(i) * primary->y.at(i);
        const double flip = inverted.at(i) * primary->y.at(i);
        QVERIFY2(same * flip < 0.0, "反相后次级电压应当与正接时反号");
    }
    QVERIFY2(checked > 0, "应当找到初级电压不为零的时刻");
}

void CoreDeviceTest::ob2362DrivesGateByFeedback()
{
    // OB2362 简化模型：FB 决定占空比，GATE 在每周期按占空比拉高，
    // CS 达到限流阈值就关断（逐周期限流）。VCC 要高于 UVLO 起机电压 17 V。
    Circuit c;
    Component vcc;
    vcc.id = 1;
    vcc.type = ComponentType::VoltageSource;
    vcc.pos = QPointF(300, 300);
    vcc.rotation = 90;
    vcc.refdes = QStringLiteral("V1");
    vcc.dcValue = 20.0;
    c.components.append(vcc);

    Component ctl;
    ctl.id = 2;
    ctl.type = ComponentType::Ob2362;
    ctl.pos = QPointF(500, 300);
    ctl.refdes = QStringLiteral("U1");
    ctl.frequency = 65000.0;
    ctl.value = 0.72;
    c.components.append(ctl);

    Component fb;
    fb.id = 3;
    fb.type = ComponentType::VoltageSource;
    fb.pos = QPointF(400, 360);
    fb.rotation = 90;
    fb.refdes = QStringLiteral("VFB");
    fb.dcValue = 3.0; // 使 CS 目标 ≈ (3−0.5)/3.5 ≈ 0.71 V
    c.components.append(fb);

    Component rcs;
    rcs.id = 4;
    rcs.type = ComponentType::Resistor;
    rcs.pos = QPointF(590, 310);
    rcs.refdes = QStringLiteral("RCS");
    rcs.value = 0.5;
    c.components.append(rcs);

    Component g;
    g.id = 5;
    g.type = ComponentType::Ground;
    g.pos = QPointF(300, 400);
    g.refdes = QStringLiteral("GND");
    c.components.append(g);
    for (const QPointF& pos : {QPointF(500, 400), QPointF(400, 420), QPointF(620, 400)}) {
        Component gg = g;
        gg.id = 6 + static_cast<int>(c.components.size());
        gg.pos = pos;
        c.components.append(gg);
    }

    // VCC 与 GND
    c.wires.append(Wire{{QPointF(300, 270), QPointF(300, 200), QPointF(500, 200),
                         QPointF(500, 270)}});
    c.wires.append(Wire{{QPointF(500, 330), QPointF(500, 400)}});
    c.wires.append(Wire{{QPointF(300, 330), QPointF(300, 400)}});
    // FB 接 3 V
    c.wires.append(Wire{{QPointF(470, 300), QPointF(400, 300), QPointF(400, 330)}});
    c.wires.append(Wire{{QPointF(400, 390), QPointF(400, 420)}});
    // CS 经 0.5 Ω 到地
    c.wires.append(Wire{{QPointF(530, 310), QPointF(560, 310)}});
    c.wires.append(Wire{{QPointF(620, 310), QPointF(620, 400)}});

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QCOMPARE(net.compPins.value(2).size(), 6);

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 5e-5;
    tr.tStep = 1e-7;
    const SimulationResult result = Simulator::run(c, tr);
    QVERIFY2(result.ok, qPrintable(result.error));

    const Signal* gate = result.signal(QStringLiteral("V(%1)").arg(net.nodeOf(2, 5)));
    QVERIFY(gate);
    double high = 0.0;
    double low = 1e9;
    for (double y : gate->y) {
        high = std::max(high, y);
        low = std::min(low, y);
    }
    // 每周期 GATE 拉到约 11 V，关断时回 0
    QVERIFY2(high > 9.0 && high < 12.5, qPrintable(QStringLiteral("GATE 高 %1 V").arg(high)));
    QVERIFY2(low < 0.5, qPrintable(QStringLiteral("GATE 低 %1 V").arg(low)));
}

void CoreDeviceTest::ob2362HasBiasClampAndUvlo()
{
    // FB 偏置：光耦不通时 FB 应当浮到约 4.5 V（开环 OLP 阈值），占空比拉满
    auto build = [](double vccSource) {
        Circuit c;
        Component vcc;
        vcc.id = 1;
        vcc.type = ComponentType::VoltageSource;
        vcc.pos = QPointF(300, 300);
        vcc.rotation = 90;
        vcc.refdes = QStringLiteral("V1");
        vcc.dcValue = vccSource;
        c.components.append(vcc);

        Component ctl;
        ctl.id = 2;
        ctl.type = ComponentType::Ob2362;
        ctl.pos = QPointF(500, 300);
        ctl.refdes = QStringLiteral("U1");
        ctl.frequency = 65000.0;
        ctl.value = 0.72;
        c.components.append(ctl);

        Component g;
        g.id = 3;
        g.type = ComponentType::Ground;
        g.pos = QPointF(300, 400);
        g.refdes = QStringLiteral("GND");
        c.components.append(g);
        for (const QPointF& pos : {QPointF(500, 400), QPointF(620, 400)}) {
            Component gg = g;
            gg.id = 4 + static_cast<int>(c.components.size());
            gg.pos = pos;
            c.components.append(gg);
        }
        // VCC 经 10 kΩ 接电源（模拟启动电阻）
        Component r;
        r.id = 9;
        r.type = ComponentType::Resistor;
        r.pos = QPointF(400, 270);
        r.refdes = QStringLiteral("R1");
        r.value = 500.0; // 20 V 经 500 Ω，扣掉 2.5 mA 工作电流后仍有 ~18.8 V
        c.components.append(r);

        // 电源 + → R1 → U1 的 VCC 脚（pin4 = 500,270）
        c.wires.append(Wire{{QPointF(300, 270), QPointF(370, 270)}});
        c.wires.append(Wire{{QPointF(430, 270), QPointF(500, 270)}});
        c.wires.append(Wire{{QPointF(300, 330), QPointF(300, 400)}});
        c.wires.append(Wire{{QPointF(500, 330), QPointF(500, 400)}});
        return c;
    };

    auto measure = [](const Circuit& c, QString* error) {
        const Netlist net = c.buildNetlist();
        SimulationOptions tr;
        tr.kind = AnalysisKind::Transient;
        tr.tStop = 2e-4;
        tr.tStep = 1e-6;
        const SimulationResult r = Simulator::run(c, tr);
        if (error)
            *error = r.error;
        return qMakePair(net, r);
    };

    // 1) 正常供电（高于 UVLO 17 V）：FB 浮到 ~4.5 V，占空比接近上限
    {
        QString error;
        const auto pair = measure(build(20.0), &error);
        QVERIFY2(pair.second.ok, qPrintable(error));
        const Signal* fb = pair.second.signal(
            QStringLiteral("V(%1)").arg(pair.first.nodeOf(2, 1)));
        QVERIFY(fb);
        QVERIFY2(fb->y.last() > 4.0 && fb->y.last() < 5.0,
                 qPrintable(QStringLiteral("FB = %1 V").arg(fb->y.last())));
        const Signal* gate = pair.second.signal(
            QStringLiteral("V(%1)").arg(pair.first.nodeOf(2, 5)));
        QVERIFY(gate);
        int high = 0;
        for (double y : gate->y)
            high += (y > 5.0) ? 1 : 0;
        const double duty = double(high) / std::max(1, int(gate->y.size()));
        QVERIFY2(duty > 0.5, qPrintable(QStringLiteral("占空比 %1").arg(duty)));
    }

    // 2) VCC 过压：28 V 钳位，不会跟着电源一路爬到 300 V
    {
        QString error;
        const auto pair = measure(build(300.0), &error);
        QVERIFY2(pair.second.ok, qPrintable(error));
        const Signal* vcc = pair.second.signal(
            QStringLiteral("V(%1)").arg(pair.first.nodeOf(2, 4)));
        QVERIFY(vcc);
        QVERIFY2(vcc->y.last() > 25.0 && vcc->y.last() < 31.0,
                 qPrintable(QStringLiteral("VCC = %1 V").arg(vcc->y.last())));
    }

    // 3) UVLO：VCC 只有 5 V 时 GATE 一直是低
    {
        QString error;
        const auto pair = measure(build(5.0), &error);
        QVERIFY2(pair.second.ok, qPrintable(error));
        const Signal* gate = pair.second.signal(
            QStringLiteral("V(%1)").arg(pair.first.nodeOf(2, 5)));
        QVERIFY(gate);
        for (double y : gate->y)
            QVERIFY2(y < 1.0, qPrintable(QStringLiteral("UVLO 期间 GATE = %1 V").arg(y)));
    }
}

void CoreDeviceTest::switchingControllerSuggestsFineStep()
{
    // 65 kHz 的开关控制器要求步长 ≤ 1/(f·20) ≈ 0.77 µs，
    // 否则每个开关周期都被跨过去、波形算成直流
    Circuit c;
    Component ctl;
    ctl.id = 1;
    ctl.type = ComponentType::Ob2362;
    ctl.pos = QPointF(100, 100);
    ctl.refdes = QStringLiteral("U1");
    ctl.frequency = 65000.0;
    c.components.append(ctl);
    const double suggested = c.suggestedMaxStep();
    QVERIFY2(suggested > 0.0 && suggested < 1e-6,
             qPrintable(QStringLiteral("suggested = %1 s").arg(suggested)));

    // 同时有 50 Hz 正弦时，仍以快的那个为准
    Component src;
    src.id = 2;
    src.type = ComponentType::VoltageSource;
    src.pos = QPointF(200, 100);
    src.refdes = QStringLiteral("V1");
    src.function = SourceFunction::Sine;
    src.frequency = 50.0;
    c.components.append(src);
    QCOMPARE(c.suggestedMaxStep(), suggested);
}

void CoreDeviceTest::tl431RegulatesToReference()
{
    // 5 V 经 1 kΩ 接阴极，REF 与阴极相连（2.5 V 基准的典型接法），阳极接地：
    // 稳压后阴极约 2.5 V，电阻上的电流约 (5 − 2.5)/1k = 2.5 mA
    auto build = [](bool refTied) {
        Circuit c;
        Component v;
        v.id = 1;
        v.type = ComponentType::VoltageSource;
        v.pos = QPointF(60, 300);
        v.rotation = 90;
        v.refdes = QStringLiteral("V1");
        v.function = SourceFunction::Constant;
        v.dcValue = 5.0;
        c.components.append(v);

        Component r;
        r.id = 2;
        r.type = ComponentType::Resistor;
        r.pos = QPointF(180, 200);
        r.refdes = QStringLiteral("R1");
        r.value = 1000.0;
        c.components.append(r);

        Component tl;
        tl.id = 3;
        tl.type = ComponentType::Tl431;
        tl.pos = QPointF(320, 300);
        tl.refdes = QStringLiteral("VR1");
        tl.value = 2.495;
        c.components.append(tl);

        Component g;
        g.id = 4;
        g.type = ComponentType::Ground;
        g.pos = QPointF(320, 400);
        g.refdes = QStringLiteral("GND");
        c.components.append(g);
        Component g2 = g;
        g2.id = 5;
        g2.pos = QPointF(60, 400);
        c.components.append(g2);

        // V1 → R1 → 阴极（上端，引脚 2）；阳极（下端，引脚 1）→ 地
        c.wires.append(Wire{{QPointF(60, 270), QPointF(60, 200), QPointF(150, 200)}});
        c.wires.append(Wire{{QPointF(210, 200), QPointF(320, 200), QPointF(320, 270)}});
        c.wires.append(Wire{{QPointF(320, 330), QPointF(320, 400)}});
        c.wires.append(Wire{{QPointF(60, 330), QPointF(60, 400)}});
        if (refTied) // REF（左侧引脚）绕上去接阴极
            c.wires.append(Wire{{QPointF(290, 300), QPointF(290, 270), QPointF(320, 270)}});
        return c;
    };

    auto cathode = [](const Circuit& c) {
        const Netlist net = c.buildNetlist();
        if (!net.valid)
            return -1.0;
        SimulationOptions tr;
        tr.kind = AnalysisKind::Transient;
        tr.tStop = 1e-4;
        tr.tStep = 1e-5;
        const SimulationResult r = Simulator::run(c, tr);
        if (!r.ok)
            return -1.0;
        const Signal* v = r.signal(QStringLiteral("V(%1)").arg(net.nodeOf(3, 2)));
        return v ? v->y.last() : -1.0;
    };

    // REF 接阴极：稳压到基准电压附近
    const double regulated = cathode(build(true));
    QVERIFY2(regulated > 2.2 && regulated < 2.8, qPrintable(QString::number(regulated)));

    // REF 悬空（这里接不到基准）：分流管不导通，阴极被拉到 5 V
    const double off = cathode(build(false));
    QVERIFY2(off > 4.5, qPrintable(QString::number(off)));
}

void CoreDeviceTest::optocouplerCouplesLight()
{
    // 输入侧：5 V 经 1 kΩ 驱动 LED（Vf≈1.2 V → If≈3.6 mA）
    // 输出侧：10 V 经 1 kΩ 接集电极，CTR=0.5 → Ic≈1.8 mA，Vce≈8.2 V
    auto build = [](double ctr) {
        Circuit c;

        Component vin;
        vin.id = 1;
        vin.type = ComponentType::VoltageSource;
        vin.pos = QPointF(60, 300);
        vin.rotation = 90;
        vin.refdes = QStringLiteral("VIN");
        vin.function = SourceFunction::Constant;
        vin.dcValue = 5.0;
        c.components.append(vin);

        Component rin;
        rin.id = 2;
        rin.type = ComponentType::Resistor;
        rin.pos = QPointF(160, 200);
        rin.refdes = QStringLiteral("RIN");
        rin.value = 1000.0;
        c.components.append(rin);

        Component opto;
        opto.id = 3;
        opto.type = ComponentType::Optocoupler;
        opto.pos = QPointF(300, 300);
        opto.refdes = QStringLiteral("U1");
        opto.vf = 1.2;
        opto.ctr = ctr;
        c.components.append(opto);

        Component vcc;
        vcc.id = 4;
        vcc.type = ComponentType::VoltageSource;
        vcc.pos = QPointF(460, 300);
        vcc.rotation = 90;
        vcc.refdes = QStringLiteral("VCC");
        vcc.function = SourceFunction::Constant;
        vcc.dcValue = 10.0;
        c.components.append(vcc);

        Component rc;
        rc.id = 5;
        rc.type = ComponentType::Resistor;
        rc.pos = QPointF(400, 200);
        rc.refdes = QStringLiteral("RC");
        rc.value = 1000.0;
        c.components.append(rc);

        Component g;
        g.id = 6;
        g.type = ComponentType::Ground;
        g.pos = QPointF(60, 400);
        g.refdes = QStringLiteral("GND");
        c.components.append(g);
        for (const QPointF& pos : {QPointF(150, 400), QPointF(270, 400),
                                   QPointF(330, 400), QPointF(460, 400)}) {
            Component gg = g;
            gg.id = 7 + static_cast<int>(c.components.size());
            gg.pos = pos;
            c.components.append(gg);
        }

        // 输入回路：VIN → RIN → 光耦阳极(引脚0)，阴极(引脚1) → 地
        c.wires.append(Wire{{QPointF(60, 270), QPointF(60, 200), QPointF(130, 200)}});
        c.wires.append(Wire{{QPointF(190, 200), QPointF(270, 200), QPointF(270, 290)}});
        c.wires.append(Wire{{QPointF(60, 330), QPointF(60, 400)}});
        c.wires.append(Wire{{QPointF(270, 310), QPointF(270, 400)}});

        // 输出回路：VCC → RC → 集电极(引脚2，坐标 330,290)，发射极(引脚3) → 地
        c.wires.append(Wire{{QPointF(460, 270), QPointF(460, 200), QPointF(430, 200)}});
        c.wires.append(Wire{{QPointF(370, 200), QPointF(330, 200), QPointF(330, 290)}});
        c.wires.append(Wire{{QPointF(460, 330), QPointF(460, 400)}});
        c.wires.append(Wire{{QPointF(330, 310), QPointF(330, 400)}});
        return c;
    };

    auto collectorVoltage = [](const Circuit& c, int compId) {
        const Netlist net = c.buildNetlist();
        if (!net.valid)
            return -1.0;
        SimulationOptions tr;
        tr.kind = AnalysisKind::Transient;
        tr.tStop = 1e-4;
        tr.tStep = 1e-5;
        const SimulationResult r = Simulator::run(c, tr);
        if (!r.ok)
            return -1.0;
        // 引脚 2 是输出侧集电极
        const Signal* v = r.signal(QStringLiteral("V(%1)").arg(net.nodeOf(compId, 2)));
        return v ? v->y.last() : -1.0;
    };

    // CTR=1：输入约 3.6 mA → 输出约 3.6 mA，Vce ≈ 10 − 3.6 = 6.4 V
    const double vce = collectorVoltage(build(1.0), 3);
    QVERIFY2(vce > 4.0 && vce < 8.5, qPrintable(QString::number(vce)));

    // CTR=0（不导通）：集电极被拉到 VCC
    const double off = collectorVoltage(build(0.0), 3);
    QVERIFY2(off > 9.5, qPrintable(QString::number(off)));

    // CTR 越大，输出电流越大、集电极电压越低（LTV-817 的 C 档是 300%）
    const double strong = collectorVoltage(build(3.0), 3);
    QVERIFY2(strong < vce, qPrintable(QString::number(strong)));

    // 高 CTR 时输出不能把发射极顶到集电极之上（饱和压降要起作用）
    {
        const Circuit sat = build(10.0); // CTR 远超需求，必然饱和
        const Netlist net = sat.buildNetlist();
        QVERIFY2(net.valid, qPrintable(net.error));
        SimulationOptions tr;
        tr.kind = AnalysisKind::Transient;
        tr.tStop = 1e-4;
        tr.tStep = 1e-5;
        const SimulationResult r = Simulator::run(sat, tr);
        QVERIFY2(r.ok, qPrintable(r.error));
        // 节点 0 是地，没有对应信号
        const auto nodeV = [&r, &net](int pin) {
            const int node = net.nodeOf(3, pin);
            if (node == 0)
                return 0.0;
            const Signal* s = r.signal(QStringLiteral("V(%1)").arg(node));
            return s ? s->y.last() : 0.0;
        };
        const double vce = nodeV(2) - nodeV(3);
        QVERIFY2(vce >= -0.01, qPrintable(QStringLiteral("Vce=%1").arg(vce)));
        QVERIFY2(vce <= 0.3, qPrintable(QStringLiteral("Vce=%1").arg(vce))); // 落在饱和区
    }

    // 元件电流必须是物理量级：限幅用错模型时这里会冒出天文数字
    const Circuit c = build(1.0);
    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    SimulationOptions op;
    op.kind = AnalysisKind::OperatingPoint;
    const SimulationResult dc = Simulator::run(c, op);
    QVERIFY2(dc.ok, qPrintable(dc.error));
    const double ifLed = dc.elementCurrents.value(QStringLiteral("I(U1)"), 0.0);
    QVERIFY2(ifLed > 1e-3 && ifLed < 20e-3, qPrintable(QString::number(ifLed)));
}

void CoreDeviceTest::pushButtonContactsFollowState()
{
    // 按钮开关和普通开关共用一套接触电阻模型：闭合 1 mΩ、断开 1 GΩ。
    // 区别只在「谁来决定 closed」—— 按钮由按住 / 松开决定，静止状态由
    // normallyClosed 决定。这里验证两种静止状态各自的导通情况。
    auto nodeVoltage = [this](bool normallyClosed, bool closed) {
        Circuit c;

        Component src;
        src.id = 1;
        src.type = ComponentType::VoltageSource;
        src.pos = QPointF(150, 300);
        src.rotation = 90;
        src.refdes = QStringLiteral("V1");
        src.dcValue = 5.0;
        c.components.append(src);

        Component button;
        button.id = 2;
        button.type = ComponentType::PushButton;
        button.pos = QPointF(300, 200);
        button.refdes = QStringLiteral("SB1");
        button.normallyClosed = normallyClosed;
        button.closed = closed;
        c.components.append(button);

        Component load;
        load.id = 3;
        load.type = ComponentType::Resistor;
        load.pos = QPointF(450, 200);
        load.refdes = QStringLiteral("R1");
        load.value = 1000.0;
        c.components.append(load);

        Component gnd;
        gnd.id = 4;
        gnd.type = ComponentType::Ground;
        gnd.pos = QPointF(150, 500);
        gnd.refdes = QStringLiteral("GND1");
        c.components.append(gnd);

        // V1+ → 按钮 → R1 → 地；测按钮与 R1 之间那一点
        c.wires.append(Wire{{QPointF(150, 270), QPointF(150, 200), QPointF(270, 200)}});
        c.wires.append(Wire{{QPointF(330, 200), QPointF(420, 200)}});
        c.wires.append(Wire{{QPointF(480, 200), QPointF(560, 200), QPointF(560, 480),
                             QPointF(150, 480), QPointF(150, 500)}});
        c.wires.append(Wire{{QPointF(150, 330), QPointF(150, 500)}});

        SimulationOptions options;
        options.kind = AnalysisKind::Transient;
        options.tStop = 1e-3;
        options.tStep = 1e-4;
        const SimulationResult result = Simulator::run(c, options);
        if (!result.ok)
            return -1.0;
        const Netlist net = c.buildNetlist();
        const Signal* signal =
            result.signal(QStringLiteral("V(%1)").arg(net.nodeOf(button.id, 1)));
        if (!signal || signal->y.isEmpty())
            return -1.0;
        return signal->y.last();
    };

    // 测的是「按钮与 R1 之间」那一点：按钮断开时没有电流，R1 上没有压降，
    // 这一点被 R1 拉到 0 V（5 V 全落在按钮上）；接通时等于电源电压。
    //
    // 常开：静止断开 → 0 V；按住接通 → 5 V
    QVERIFY2(near(nodeVoltage(false, false), 0.0, 1e-3),
             qPrintable(QString::number(nodeVoltage(false, false))));
    QVERIFY2(near(nodeVoltage(false, true), 5.0, 0.2),
             qPrintable(QString::number(nodeVoltage(false, true))));

    // 常闭：静止接通 → 5 V；按住断开 → 0 V
    QVERIFY2(near(nodeVoltage(true, true), 5.0, 0.2),
             qPrintable(QString::number(nodeVoltage(true, true))));
    QVERIFY2(near(nodeVoltage(true, false), 0.0, 1e-3),
             qPrintable(QString::number(nodeVoltage(true, false))));
}

void CoreDeviceTest::mosfetSwitches()
{
    // VDD 10 V、漏极电阻 1 kΩ、栅极 5 V；Vth 2 V、K 2 mA/V²
    Circuit c;

    Component vdd;
    vdd.id = 1;
    vdd.type = ComponentType::VoltageSource;
    vdd.pos = QPointF(100, 300);
    vdd.rotation = 90;
    vdd.refdes = QStringLiteral("VDD");
    vdd.function = SourceFunction::Constant;
    vdd.dcValue = 10.0;
    c.components.append(vdd);

    Component vg;
    vg.id = 2;
    vg.type = ComponentType::VoltageSource;
    vg.pos = QPointF(250, 300);
    vg.rotation = 90;
    vg.refdes = QStringLiteral("VG");
    vg.function = SourceFunction::Constant;
    vg.dcValue = 5.0;
    c.components.append(vg);

    Component rd;
    rd.id = 3;
    rd.type = ComponentType::Resistor;
    rd.pos = QPointF(400, 200);
    rd.refdes = QStringLiteral("RD");
    rd.value = 1000.0;
    c.components.append(rd);

    Component m;
    m.id = 4;
    m.type = ComponentType::Nmos;
    m.pos = QPointF(500, 300);
    m.refdes = QStringLiteral("M1");
    m.vth = 2.0;
    m.k = 2e-3;
    c.components.append(m);

    Component g1;
    g1.id = 5;
    g1.type = ComponentType::Ground;
    g1.pos = QPointF(100, 400);
    g1.refdes = QStringLiteral("GND");
    c.components.append(g1);
    for (const QPointF& pos : {QPointF(250, 400), QPointF(530, 400)}) {
        Component g = g1;
        g.id = 6 + static_cast<int>(c.components.size());
        g.pos = pos;
        c.components.append(g);
    }

    // VDD 顶 → 漏极电阻 → 漏极
    c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(370, 200)}});
    c.wires.append(Wire{{QPointF(430, 200), QPointF(530, 200), QPointF(530, 270)}});
    c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
    // 栅极：VG 上端直接拉到栅极引脚
    c.wires.append(Wire{{QPointF(250, 270), QPointF(470, 270), QPointF(470, 300)}});
    c.wires.append(Wire{{QPointF(250, 330), QPointF(250, 400)}});
    // 源极接地
    c.wires.append(Wire{{QPointF(530, 330), QPointF(530, 400)}});

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QVERIFY2(net.warnings.isEmpty(), qPrintable(net.warnings.join(QStringLiteral("; "))));

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 1e-5;
    tr.tStep = 1e-6;
    const SimulationResult r = Simulator::run(c, tr);
    QVERIFY2(r.ok, qPrintable(r.error));

    const int drain = net.nodeOf(m.id, 0);
    const Signal* vd = r.signal(QStringLiteral("V(%1)").arg(drain));
    QVERIFY(vd);
    // 饱和/线性边界上 Id ≈ 8 mA，漏极电压约 2 V
    QVERIFY2(vd->y.last() > 1.0 && vd->y.last() < 4.0,
             qPrintable(QString::number(vd->y.last())));
}

void CoreDeviceTest::pmosConductsWithNegativeGate()
{
    // PMOS：源极接 +10 V，栅极电压低于源极时导通，漏极经 1 kΩ 到地
    auto build = [](double gateVolts) {
        Circuit c;
        Component vdd;
        vdd.id = 1;
        vdd.type = ComponentType::VoltageSource;
        vdd.pos = QPointF(100, 300);
        vdd.rotation = 90;
        vdd.refdes = QStringLiteral("VDD");
        vdd.function = SourceFunction::Constant;
        vdd.dcValue = 10.0;
        c.components.append(vdd);

        Component vg;
        vg.id = 2;
        vg.type = ComponentType::VoltageSource;
        vg.pos = QPointF(250, 300);
        vg.rotation = 90;
        vg.refdes = QStringLiteral("VG");
        vg.function = SourceFunction::Constant;
        vg.dcValue = gateVolts;
        c.components.append(vg);

        Component rd;
        rd.id = 3;
        rd.type = ComponentType::Resistor;
        rd.pos = QPointF(400, 150);
        rd.refdes = QStringLiteral("RD");
        rd.value = 1000.0;
        c.components.append(rd);

        Component m;
        m.id = 4;
        m.type = ComponentType::Pmos;
        m.pos = QPointF(500, 300);
        m.refdes = QStringLiteral("M1");
        m.vth = 2.0;
        m.k = 2e-3;
        c.components.append(m);

        Component g;
        g.id = 5;
        g.type = ComponentType::Ground;
        g.pos = QPointF(100, 400);
        g.refdes = QStringLiteral("GND");
        c.components.append(g);
        for (const QPointF& pos : {QPointF(250, 400), QPointF(370, 400)}) {
            Component gg = g;
            gg.id = 6 + static_cast<int>(c.components.size());
            gg.pos = pos;
            c.components.append(gg);
        }

        // 电源轨：VDD 正端 → 绕过漏极 → 源极
        c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 100), QPointF(560, 100),
                             QPointF(560, 330), QPointF(530, 330)}});
        c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
        // 漏极 → RD → 地
        c.wires.append(Wire{{QPointF(530, 270), QPointF(530, 150), QPointF(430, 150)}});
        c.wires.append(Wire{{QPointF(370, 150), QPointF(370, 400)}});
        // 栅极 → VG → 地
        c.wires.append(Wire{{QPointF(470, 300), QPointF(470, 270), QPointF(250, 270)}});
        c.wires.append(Wire{{QPointF(250, 330), QPointF(250, 400)}});
        return c;
    };

    auto drainVoltage = [](const Circuit& c, int compId) {
        const Netlist net = c.buildNetlist();
        if (!net.valid)
            return -1.0;
        SimulationOptions tr;
        tr.kind = AnalysisKind::Transient;
        tr.tStop = 1e-5;
        tr.tStep = 1e-6;
        const SimulationResult r = Simulator::run(c, tr);
        if (!r.ok)
            return -1.0;
        const Signal* v = r.signal(QStringLiteral("V(%1)").arg(net.nodeOf(compId, 0)));
        return v ? v->y.last() : -1.0;
    };

    // 栅极 5 V（Vgs = −5 V，超过 |Vth| = 2 V）→ 导通，漏极被拉高
    const double on = drainVoltage(build(5.0), 4);
    // 栅极 10 V（Vgs = 0）→ 关断，漏极经 RD 落地
    const double off = drainVoltage(build(10.0), 4);
    QVERIFY2(on > 1.0 && on < 10.0, qPrintable(QString::number(on)));
    QVERIFY2(off < 1.0, qPrintable(QString::number(off)));
    QVERIFY2(on > off, "PMOS 应当在栅极电压低于源极时导通");
}

void CoreDeviceTest::igbtConductsAboveThreshold()
{
    auto build = [](double gateVolts) {
        Circuit c;
        Component vcc;
        vcc.id = 1;
        vcc.type = ComponentType::VoltageSource;
        vcc.pos = QPointF(100, 300);
        vcc.rotation = 90;
        vcc.refdes = QStringLiteral("VCC");
        vcc.function = SourceFunction::Constant;
        vcc.dcValue = 20.0;
        c.components.append(vcc);

        Component vg;
        vg.id = 2;
        vg.type = ComponentType::VoltageSource;
        vg.pos = QPointF(250, 300);
        vg.rotation = 90;
        vg.refdes = QStringLiteral("VG");
        vg.function = SourceFunction::Constant;
        vg.dcValue = gateVolts;
        c.components.append(vg);

        Component rc;
        rc.id = 3;
        rc.type = ComponentType::Resistor;
        rc.pos = QPointF(400, 200);
        rc.refdes = QStringLiteral("RC");
        rc.value = 10.0;
        c.components.append(rc);

        Component z;
        z.id = 4;
        z.type = ComponentType::Igbt;
        z.pos = QPointF(500, 300);
        z.refdes = QStringLiteral("Z1");
        z.vth = 5.0;
        z.k = 0.05;
        z.vf = 0.7;
        c.components.append(z);

        Component g;
        g.id = 5;
        g.type = ComponentType::Ground;
        g.pos = QPointF(100, 400);
        g.refdes = QStringLiteral("GND");
        c.components.append(g);
        for (const QPointF& pos : {QPointF(250, 400), QPointF(530, 400)}) {
            Component gg = g;
            gg.id = 6 + static_cast<int>(c.components.size());
            gg.pos = pos;
            c.components.append(gg);
        }

        c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(370, 200)}});
        c.wires.append(Wire{{QPointF(430, 200), QPointF(530, 200), QPointF(530, 270)}});
        c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});
        c.wires.append(Wire{{QPointF(250, 270), QPointF(470, 270), QPointF(470, 300)}});
        c.wires.append(Wire{{QPointF(250, 330), QPointF(250, 400)}});
        c.wires.append(Wire{{QPointF(530, 330), QPointF(530, 400)}});
        return c;
    };

    auto collectorVoltage = [](const Circuit& c, int compId) {
        const Netlist net = c.buildNetlist();
        if (!net.valid)
            return -1.0;
        SimulationOptions tr;
        tr.kind = AnalysisKind::Transient;
        tr.tStop = 1e-5;
        tr.tStep = 1e-6;
        const SimulationResult r = Simulator::run(c, tr);
        if (!r.ok)
            return -1.0;
        const Signal* v = r.signal(QStringLiteral("V(%1)").arg(net.nodeOf(compId, 0)));
        return v ? v->y.last() : -1.0;
    };

    // 栅极 10 V > Vth 5 V：导通，集电极被拉到较低电压
    const double on = collectorVoltage(build(10.0), 4);
    // 栅极 0 V：关断，集电极保持 20 V
    const double off = collectorVoltage(build(0.0), 4);
    QVERIFY2(on > 0.0 && on < 18.0, qPrintable(QString::number(on)));
    QVERIFY2(off > 19.0, qPrintable(QString::number(off)));
    QVERIFY2(on < off, "IGBT 应当在高栅压下导通");
}

void CoreDeviceTest::diodeRectifier()
{
    Circuit c = loopCircuit(ComponentType::Diode, 0.0,
                            ComponentType::Resistor, 1000.0, 0.0);
    Component& v = c.components[0];
    v.function = SourceFunction::Sine;
    v.amplitude = 5.0 / std::sqrt(2.0); // 峰值 5.0 V
    v.frequency = 50.0;
    v.offset = 0.0;

    SimulationOptions opt;
    opt.kind = AnalysisKind::Transient;
    opt.tStop = 0.04;
    opt.tStep = 2e-5;
    const SimulationResult r = Simulator::run(c, opt);
    QVERIFY2(r.ok, qPrintable(r.error));

    const Signal* s = r.signal(QStringLiteral("V(2)"));
    QVERIFY(s);
    double maxV = -1e9;
    double minV = 1e9;
    for (double y : s->y) {
        maxV = std::max(maxV, y);
        minV = std::min(minV, y);
    }
    // 正半周导通时输出约为输入减去二极管压降
    QVERIFY2(maxV > 3.8 && maxV < 5.05, qPrintable(QString::number(maxV)));
    QVERIFY2(minV > -0.2 && minV < 0.1, qPrintable(QString::number(minV)));
}


void CoreDeviceTest::diodeForwardDropIsAdjustable()
{
    // 半波整流：压降调大，输出峰值就该低一截
    const auto peak = [](double vf) {
        Circuit c = loopCircuit(ComponentType::Diode, 0.0,
                                ComponentType::Resistor, 1000.0, 0.0);
        Component& v = c.components[0];
        v.function = SourceFunction::Sine;
        v.amplitude = 5.0 / std::sqrt(2.0);   // 峰值 5 V
        v.frequency = 50.0;
        v.offset = 0.0;
        for (Component& comp : c.components)
            if (comp.type == ComponentType::Diode)
                comp.vf = vf;

        SimulationOptions opt;
        opt.kind = AnalysisKind::Transient;
        opt.tStop = 0.04;
        opt.tStep = 2e-5;
        const SimulationResult r = Simulator::run(c, opt);
        if (!r.ok)
            return -999.0;
        const Signal* s = r.signal(QStringLiteral("V(2)"));
        if (!s)
            return -999.0;
        double hi = -1e30;
        for (double y : s->y)
            hi = std::max(hi, y);
        return hi;
    };

    const double low = peak(0.3);
    const double mid = peak(0.7);
    const double high = peak(1.5);
    QVERIFY2(low > mid && mid > high,
             qPrintable(QStringLiteral("压降越大输出越低：%1 / %2 / %3")
                            .arg(low).arg(mid).arg(high)));
    // 0.7 V 的管子，5 V 峰值输入下输出应在 4.2~4.6 V（20 mA 标定、实际电流更小）
    QVERIFY2(mid > 4.2 && mid < 4.6,
             qPrintable(QStringLiteral("vf = 0.7 时峰值 %1 V").arg(mid)));
    // 压降大约按差值平移
    QVERIFY2(std::abs((mid - high) - 0.8) < 0.35,
             qPrintable(QStringLiteral("0.7→1.5 V 的落差只有 %1 V").arg(mid - high)));
}


namespace {

/// 稳压管反接：阴极经 1 kΩ 接 supply、阳极接地。返回阴极电压（= 稳压值）
double zenerClampVoltage(double supply, double vz, double vf = 0.7)
{
    Circuit c;
    Component v;
    v.type = ComponentType::VoltageSource;
    v.pos = QPointF(100, 380);
    v.rotation = 90;
    v.function = SourceFunction::Constant;
    v.dcValue = supply;
    addComponent(c, v);

    Component z;
    z.type = ComponentType::Zener;
    z.pos = QPointF(300, 300);
    z.value = vz;
    z.vf = vf;
    addComponent(c, z);

    addComponent(c, makeComp(ComponentType::Resistor, QPointF(200, 200), 1000.0));
    addComponent(c, makeComp(ComponentType::Ground, QPointF(100, 410), 0.0));

    // 引脚 0 = 阳极（左，接地），1 = 阴极（右，经电阻接正）
    c.wires.append(Wire{{QPointF(100, 350), QPointF(100, 200), QPointF(170, 200)}});
    c.wires.append(Wire{{QPointF(230, 200), QPointF(280, 200), QPointF(280, 300),
                         QPointF(330, 300)}});
    c.wires.append(Wire{{QPointF(270, 300), QPointF(240, 300), QPointF(240, 420),
                         QPointF(100, 420), QPointF(100, 410)}});

    const Netlist net = c.buildNetlist();
    if (!net.valid)
        return -999.0;
    SimulationOptions opt;
    opt.kind = AnalysisKind::Transient;
    opt.tStop = 1e-4;
    opt.tStep = 1e-6;
    const SimulationResult r = Simulator::run(c, opt);
    if (!r.ok)
        return -999.0;
    const Signal* k = r.signal(QStringLiteral("V(%1)").arg(net.nodeOf(2, 1)));
    return k && !k->y.isEmpty() ? k->y.last() : -999.0;
}

/// 稳压管正接：阳极经 1 kΩ 接 5 V、阴极接地。返回阳极电压（= 正向压降）
double zenerForwardVoltage(double vz, double vf)
{
    Circuit c;
    Component v;
    v.type = ComponentType::VoltageSource;
    v.pos = QPointF(100, 380);
    v.rotation = 90;
    v.function = SourceFunction::Constant;
    v.dcValue = 5.0;
    addComponent(c, v);

    Component z;
    z.type = ComponentType::Zener;
    z.pos = QPointF(300, 300);
    z.value = vz;
    z.vf = vf;
    addComponent(c, z);

    addComponent(c, makeComp(ComponentType::Resistor, QPointF(200, 200), 1000.0));
    addComponent(c, makeComp(ComponentType::Ground, QPointF(100, 410), 0.0));

    c.wires.append(Wire{{QPointF(100, 350), QPointF(100, 200), QPointF(170, 200)}});
    c.wires.append(Wire{{QPointF(230, 200), QPointF(280, 200), QPointF(280, 300),
                         QPointF(270, 300)}});
    c.wires.append(Wire{{QPointF(330, 300), QPointF(360, 300), QPointF(360, 420),
                         QPointF(100, 420), QPointF(100, 410)}});

    const Netlist net = c.buildNetlist();
    if (!net.valid)
        return -999.0;
    SimulationOptions opt;
    opt.kind = AnalysisKind::Transient;
    opt.tStop = 1e-4;
    opt.tStep = 1e-6;
    const SimulationResult r = Simulator::run(c, opt);
    if (!r.ok)
        return -999.0;
    const Signal* a = r.signal(QStringLiteral("V(%1)").arg(net.nodeOf(2, 0)));
    return a && !a->y.isEmpty() ? a->y.last() : -999.0;
}

} // namespace

void CoreDeviceTest::zenerClampsItsBreakdownVoltage()
{
    // 没到击穿电压：原样通过
    const double below = zenerClampVoltage(3.0, 5.1);
    QVERIFY2(std::abs(below - 3.0) < 0.05,
             qPrintable(QStringLiteral("3 V 输入 → %1 V").arg(below)));

    // 远超击穿电压：稳在 Vz 附近（软拐点，会略高于 Vz）
    const double clamped = zenerClampVoltage(20.0, 5.1);
    QVERIFY2(clamped > 5.0 && clamped < 5.3,
             qPrintable(QStringLiteral("20 V 输入、Vz=5.1 → %1 V").arg(clamped)));

    // 换档位：3.3 V / 12 V
    const double low = zenerClampVoltage(20.0, 3.3);
    QVERIFY2(low > 3.2 && low < 3.5,
             qPrintable(QStringLiteral("Vz=3.3 → %1 V").arg(low)));
    const double high = zenerClampVoltage(30.0, 12.0);
    QVERIFY2(high > 11.8 && high < 12.3,
             qPrintable(QStringLiteral("Vz=12 → %1 V").arg(high)));
}

void CoreDeviceTest::zenerFallbackIsForwardDiode()
{
    // 正接时就是一个普通二极管：压降由 vf 决定，与 Vz 无关
    const double thin = zenerForwardVoltage(5.1, 0.3);
    const double normal = zenerForwardVoltage(5.1, 0.7);
    QVERIFY2(normal > thin, qPrintable(QStringLiteral("%1 / %2").arg(thin).arg(normal)));
    QVERIFY2(normal > 0.55 && normal < 0.75,
             qPrintable(QStringLiteral("vf=0.7 时正向压降 %1 V").arg(normal)));

    // Vz 不影响正向
    const double other = zenerForwardVoltage(1.0, 0.7);
    QVERIFY2(std::abs(other - normal) < 0.02, "正向压降不该随 Vz 变");
}

void CoreDeviceTest::multimeterMeasuresVoltageCurrentResistance()
{
    // 万用表三个档位各测一次：电压档读分压、电流档读串入电流、电阻档读阻值
    auto makeMeter = [](MultimeterMode mode) {
        Circuit c;
        Component v;
        v.id = 1;
        v.type = ComponentType::VoltageSource;
        v.pos = QPointF(100, 300);
        v.rotation = 90;
        v.refdes = QStringLiteral("V1");
        v.dcValue = 5.0;
        c.components.append(v);
        Component r1;
        r1.id = 2;
        r1.type = ComponentType::Resistor;
        r1.pos = QPointF(220, 200);
        r1.refdes = QStringLiteral("R1");
        r1.value = 1000.0;
        c.components.append(r1);
        Component mm;
        mm.id = 3;
        mm.type = ComponentType::Multimeter;
        mm.pos = QPointF(300, 300);
        mm.refdes = QStringLiteral("XMM1");
        mm.multimeterMode = mode;
        c.components.append(mm);
        Component g;
        g.id = 4;
        g.type = ComponentType::Ground;
        g.pos = QPointF(100, 480);
        g.refdes = QStringLiteral("GND");
        c.components.append(g);
        // 引脚都在 ±30：XMM 的两个引脚是 (270,300) 与 (330,300)
        c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(190, 200)}});
        c.wires.append(Wire{{QPointF(250, 200), QPointF(270, 200), QPointF(270, 300)}});
        c.wires.append(Wire{{QPointF(330, 300), QPointF(330, 480), QPointF(100, 480)}});
        c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 480)}});
        return c;
    };

    auto readMeter = [](const Circuit& c, double* out) {
        SimulationOptions tr;
        tr.kind = AnalysisKind::Transient;
        tr.tStop = 0.01;
        tr.tStep = 1e-4;
        const SimulationResult r = Simulator::run(c, tr);
        QVERIFY2(r.ok, qPrintable(r.error));
        const Signal* sig = r.signal(QStringLiteral("XMM1"));
        QVERIFY(sig);
        QVERIFY(!sig->y.isEmpty());
        *out = sig->y.last();
    };

    // 电压档：并联在 R1（1 kΩ）上，读数应接近 5 V（10 MΩ 内阻的压降可忽略）
    double volts = 0.0;
    readMeter(makeMeter(MultimeterMode::Voltage), &volts);
    QVERIFY2(near(volts, 5.0, 0.01), qPrintable(QString::number(volts)));

    // 电流档：串进 5 V + 1 kΩ 回路，读数应接近 5 mA
    double amps = 0.0;
    readMeter(makeMeter(MultimeterMode::Current), &amps);
    QVERIFY2(near(amps, 0.005, 1e-4), qPrintable(QString::number(amps)));

    // 电阻档：同一条回路里量 R1，注入 1 A 读 V/I = 1000 Ω
    // （档位独立成一条支路时读的是「从引脚看进去」的电阻）
    Circuit ohm = makeMeter(MultimeterMode::Resistance);
    ohm.components[1].value = 1000.0;
    double ohms = 0.0;
    readMeter(ohm, &ohms);
    QVERIFY2(near(ohms, 1000.0, 5.0), qPrintable(QString::number(ohms)));

    // 存盘往返：档位不能丢
    Circuit saved = makeMeter(MultimeterMode::Resistance);
    const QJsonObject json = saved.toJson();
    QCOMPARE(json.value(QStringLiteral("components")).toArray().at(2).toObject()
                 .value(QStringLiteral("multimeterMode")).toInt(),
             int(MultimeterMode::Resistance));
    QString error;
    const Circuit back = Circuit::fromJson(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.components.at(2).multimeterMode, MultimeterMode::Resistance);
}

void CoreDeviceTest::multimeterMeasuresAcRms()
{
    // 交流档：220 V 有效值 50 Hz 正弦，电压档并联、电流档串进 1 kΩ 回路。
    // 读数应分别是 220 V 与 220 mA（交流档按一个 50 Hz 周期算有效值）。
    Circuit c;
    Component v;
    v.id = 1;
    v.type = ComponentType::VoltageSource;
    v.pos = QPointF(100, 350);
    v.rotation = 90;
    v.refdes = QStringLiteral("V1");
    v.function = SourceFunction::Sine;
    v.amplitude = 220.0;
    v.frequency = 50.0;
    c.components.append(v);

    Component mmV;
    mmV.id = 2;
    mmV.type = ComponentType::Multimeter;
    mmV.pos = QPointF(250, 350);
    mmV.rotation = 90;
    mmV.refdes = QStringLiteral("XMM1");
    mmV.multimeterMode = MultimeterMode::AcVoltage;
    c.components.append(mmV);

    Component r;
    r.id = 3;
    r.type = ComponentType::Resistor;
    r.pos = QPointF(350, 200);
    r.refdes = QStringLiteral("R1");
    r.value = 1000.0;
    c.components.append(r);

    Component mmA;
    mmA.id = 4;
    mmA.type = ComponentType::Multimeter;
    mmA.pos = QPointF(400, 350);
    mmA.rotation = 90;
    mmA.refdes = QStringLiteral("XMM2");
    mmA.multimeterMode = MultimeterMode::AcCurrent;
    c.components.append(mmA);

    Component g;
    g.id = 5;
    g.type = ComponentType::Ground;
    g.pos = QPointF(100, 500);
    g.refdes = QStringLiteral("GND");
    c.components.append(g);

    c.wires.append(Wire{{QPointF(100, 320), QPointF(100, 200), QPointF(320, 200)}});
    c.wires.append(Wire{{QPointF(380, 200), QPointF(400, 200), QPointF(400, 320)}});
    c.wires.append(Wire{{QPointF(400, 380), QPointF(400, 500), QPointF(100, 500)}});
    c.wires.append(Wire{{QPointF(100, 380), QPointF(100, 500)}});
    c.wires.append(Wire{{QPointF(250, 200), QPointF(250, 320)}});
    c.wires.append(Wire{{QPointF(250, 380), QPointF(250, 500)}});

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 0.2;
    tr.tStep = 2e-5;
    const SimulationResult r1 = Simulator::run(c, tr);
    QVERIFY2(r1.ok, qPrintable(r1.error));

    const Signal* volts = r1.signal(QStringLiteral("XMM1"));
    QVERIFY(volts);
    QCOMPARE(volts->unit, QStringLiteral("V"));
    // 有效值精度受「窗口末段不满一个桶」影响，约 0.3%
    QVERIFY2(near(volts->y.last(), 220.0, 1.0),
             qPrintable(QStringLiteral("交流电压 %1 V").arg(volts->y.last())));

    const Signal* amps = r1.signal(QStringLiteral("XMM2"));
    QVERIFY(amps);
    QCOMPARE(amps->unit, QStringLiteral("A"));
    QVERIFY2(near(amps->y.last(), 0.220, 1e-3),
             qPrintable(QStringLiteral("交流电流 %1 A").arg(amps->y.last())));

    // 窗口正好是一个整周期（h = 2e-5、period = 0.02 → 1000 个样本），
    // 有效值应当很准：220 V 读成 220.000
    QVERIFY2(near(volts->y.last(), 220.0, 0.05),
             qPrintable(QStringLiteral("精确值 %1 V").arg(volts->y.last(), 0, 'f', 4)));

    // 停止时间不足一个周期时不给读数（NaN → 界面画横杠），
    // 而不是给出一个随时间乱跳的数
    SimulationOptions brief = tr;
    brief.tStop = 0.005;
    const SimulationResult rShort = Simulator::run(c, brief);
    QVERIFY2(rShort.ok, qPrintable(rShort.error));
    const Signal* shortSig = rShort.signal(QStringLiteral("XMM1"));
    QVERIFY(shortSig);
    QVERIFY2(!shortSig->y.isEmpty() && !std::isfinite(shortSig->y.last()),
             "窗口没满时应当没有读数");
    // 日志里要告诉用户停止时间不够
    bool warned = false;
    for (const QString& line : rShort.log)
        if (line.contains(QStringLiteral("交流档")))
            warned = true;
    QVERIFY2(warned, qPrintable(rShort.log.join(QLatin1Char('\n'))));

    // 直流工作点下没有交流分量，交流档读 0
    SimulationOptions op;
    op.kind = AnalysisKind::OperatingPoint;
    const SimulationResult r2 = Simulator::run(c, op);
    QVERIFY2(r2.ok, qPrintable(r2.error));
    QVERIFY2(r2.report.contains(QStringLiteral("万用表读数")),
             qPrintable(r2.report));

    // 直流电压档读的是瞬时值（正弦的平均值为 0）
    Circuit dc = c;
    dc.components[1].multimeterMode = MultimeterMode::Voltage;
    const SimulationResult r3 = Simulator::run(dc, op);
    QVERIFY2(r3.ok, qPrintable(r3.error));
}

void CoreDeviceTest::groundKindsAreSeparateNets()
{
    // GND 是节点 0；AGND 是独立节点，只通过 1 mΩ 单点连到主地
    Circuit c;
    Component v;
    v.id = 1;
    v.type = ComponentType::VoltageSource;
    v.pos = QPointF(100, 300);
    v.rotation = 90;
    v.refdes = QStringLiteral("V1");
    v.dcValue = 10.0;
    c.components.append(v);

    Component r1;
    r1.id = 2;
    r1.type = ComponentType::Resistor;
    r1.pos = QPointF(200, 200);
    r1.refdes = QStringLiteral("R1");
    r1.value = 1000.0;
    c.components.append(r1);

    Component r2;
    r2.id = 3;
    r2.type = ComponentType::Resistor;
    r2.pos = QPointF(400, 200);
    r2.refdes = QStringLiteral("R2");
    r2.value = 1000.0;
    c.components.append(r2);

    Component gnd;
    gnd.id = 4;
    gnd.type = ComponentType::Ground;
    gnd.pos = QPointF(100, 400);
    gnd.refdes = QStringLiteral("GND");
    c.components.append(gnd);

    Component agnd;
    agnd.id = 5;
    agnd.type = ComponentType::Ground;
    agnd.pos = QPointF(500, 400);
    agnd.refdes = QStringLiteral("AGND");
    agnd.groundKind = GroundKind::Agnd;
    c.components.append(agnd);

    Component tp;
    tp.id = 6;
    tp.type = ComponentType::Probe;
    tp.pos = QPointF(300, 200);
    tp.refdes = QStringLiteral("TP1");
    tp.groundKind = GroundKind::Agnd;
    c.components.append(tp);

    // V1 → R1 → R2 → AGND
    c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(170, 200)}});
    c.wires.append(Wire{{QPointF(230, 200), QPointF(300, 200), QPointF(370, 200)}});
    c.wires.append(Wire{{QPointF(430, 200), QPointF(500, 200), QPointF(500, 400)}});
    c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400)}});

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QCOMPARE(net.nodeOf(4, 0), 0);              // 主地 = 节点 0
    QVERIFY(net.nodeOf(5, 0) > 0);              // AGND 是独立节点

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 1e-4;
    tr.tStep = 1e-5;
    const SimulationResult result = Simulator::run(c, tr);
    QVERIFY2(result.ok, qPrintable(result.error));

    // 测试点带参考地时会有 V(a,b) 的差分信号
    const int nodeA = net.nodeOf(6, 0);
    const int nodeB = net.nodeOf(5, 0);
    const Signal* diff = result.signal(QStringLiteral("V(%1,%2)").arg(nodeA).arg(nodeB));
    QVERIFY2(diff, "带参考地的测试点应当产生差分信号");
    const Signal* va = result.signal(QStringLiteral("V(%1)").arg(nodeA));
    const Signal* vb = result.signal(QStringLiteral("V(%1)").arg(nodeB));
    QVERIFY(va && vb);
    QVERIFY(std::abs(diff->y.last() - (va->y.last() - vb->y.last())) < 1e-9);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    csm::saveLanguage(csm::Language::Chinese);   // 断言里的提示文案是中文
    CoreDeviceTest tc;
    return QTest::qExec(&tc, argc, argv);
}


void CoreDeviceTest::signalGeneratorPlaysEveryWaveform()
{
    Component sg;
    sg.type = ComponentType::SignalGenerator;
    sg.refdes = QStringLiteral("FG1");
    sg.amplitude = 2.0;    // 峰值
    sg.frequency = 1000.0;
    sg.offset = 0.0;
    const double period = 1.0 / sg.frequency;
    const auto at = [&](double fraction) { return sg.signalValueAt(period * fraction); };

    // 正弦：1/4 周期到正峰、3/4 周期到负峰
    sg.waveform = Waveform::Sine;
    QVERIFY(std::abs(at(0.25) - 2.0) < 1e-9);
    QVERIFY(std::abs(at(0.75) + 2.0) < 1e-9);

    // 方波：占空比 25 %，前四分之一高、其余低
    sg.waveform = Waveform::Square;
    sg.duty = 0.25;
    QCOMPARE(at(0.10), 2.0);
    QCOMPARE(at(0.50), -2.0);
    QCOMPARE(at(0.90), -2.0);

    // 三角波：0 → +峰 → 0 → −峰
    sg.waveform = Waveform::Triangle;
    QVERIFY(std::abs(at(0.0)) < 1e-9);
    QVERIFY(std::abs(at(0.25) - 2.0) < 1e-9);
    QVERIFY(std::abs(at(0.50)) < 1e-9);
    QVERIFY(std::abs(at(0.75) + 2.0) < 1e-9);

    // 锯齿波：从 −峰 线性升到 +峰
    sg.waveform = Waveform::Sawtooth;
    QVERIFY(std::abs(at(0.0001) + 2.0) < 1e-3);
    QVERIFY(std::abs(at(0.9999) - 2.0) < 1e-3);

    // 相位：正弦超前 90° 时 t=0 就是正峰；方波移相 180° 时高低对调
    sg.waveform = Waveform::Sine;
    sg.phaseDeg = 90.0;
    QVERIFY(std::abs(at(0.0) - 2.0) < 1e-9);

    sg.waveform = Waveform::Square;
    sg.duty = 0.25;
    sg.phaseDeg = 180.0;
    QCOMPARE(at(0.10), -2.0);
    QCOMPARE(at(0.60), 2.0);
    sg.phaseDeg = 0.0;

    // 直流偏置整体抬升
    sg.offset = 1.5;
    sg.waveform = Waveform::Square;
    QCOMPARE(at(0.10), 3.5);
    QCOMPARE(at(0.50), -0.5);
}

void CoreDeviceTest::signalGeneratorDrivesCircuitAndRoundTrips()
{
    // FG1 串一个 1 kΩ 到地：节点电压应当跟着波形走，且幅度等于设定峰值
    Circuit circuit;
    Component sg;
    sg.type = ComponentType::SignalGenerator;
    sg.id = 1;
    sg.refdes = QStringLiteral("FG1");
    sg.waveform = Waveform::Sine;
    sg.amplitude = 2.0;
    sg.frequency = 1000.0;
    sg.pos = QPointF(100.0, 200.0);
    Component r;
    r.type = ComponentType::Resistor;
    r.id = 2;
    r.refdes = QStringLiteral("R1");
    r.value = 1000.0;
    r.pos = QPointF(300.0, 200.0);
    Component gnd;
    gnd.type = ComponentType::Ground;
    gnd.id = 3;
    gnd.refdes = QStringLiteral("GND");
    gnd.pos = QPointF(100.0, 300.0);
    circuit.components = {sg, r, gnd};
    Wire w1;
    w1.points = {sg.pinPosition(1), r.pinPosition(0)};
    Wire w2;
    w2.points = {r.pinPosition(1), gnd.pinPosition(0)};
    Wire w3;
    w3.points = {sg.pinPosition(0), gnd.pinPosition(0)};
    circuit.wires = {w1, w2, w3};

    const Netlist net = circuit.buildNetlist();
    const int node = net.nodeOf(sg.id, 1);
    QVERIFY(node > 0);

    SimulationOptions options;
    options.kind = AnalysisKind::Transient;
    options.tStop = 1e-3;
    options.tStep = 1e-6;
    const SimulationResult result = Simulator::run(circuit, options);
    QVERIFY2(result.ok, qPrintable(result.error));
    const Signal* wave = result.signal(QStringLiteral("V(%1)").arg(node));
    QVERIFY(wave);
    double lo = wave->y.first();
    double hi = wave->y.first();
    for (double v : wave->y) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    // 引脚 0 接地，所以测到的是源的反相输出；幅度必须等于设定的峰值
    QVERIFY2(std::abs(hi - lo - 4.0) < 0.05, qPrintable(QStringLiteral("峰峰值 %1").arg(hi - lo)));

    // JSON 往返：波形与占空比要存得住
    Circuit back = Circuit::fromJson(circuit.toJson());
    const Component* reloaded = nullptr;
    for (const Component& c : back.components) {
        if (c.refdes == QLatin1String("FG1"))
            reloaded = &c;
    }
    QVERIFY(reloaded);
    QCOMPARE(reloaded->type, ComponentType::SignalGenerator);
    QCOMPARE(reloaded->waveform, Waveform::Sine);
    QVERIFY(std::abs(reloaded->amplitude - 2.0) < 1e-12);
    QVERIFY(std::abs(reloaded->duty - 0.5) < 1e-12);
}

#include "test_core_device.moc"
