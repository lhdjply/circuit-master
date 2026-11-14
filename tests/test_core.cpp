#include "core/Circuit.h"
#include "core/Units.h"
#include "sim/Simulator.h"

#include <QDebug>
#include <QTest>

#include <cmath>

using namespace csm;

namespace {

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

/// 构造一个矩形串联回路：V1(左) - A(上) - B(右) - 底边导线 - GND。
/// 引脚坐标为 10 的整数倍，T 型连接与导线连接都会被用到。
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

/// 反相放大器：V1 - R1 - U1(-)  |  R2 反馈  |  U1(+) 接地
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

class CoreTest : public QObject {
    Q_OBJECT

private slots:
    void unitsRoundTrip()
    {
        bool ok = false;
        QVERIFY(near(parseEng(QStringLiteral("10k"), &ok), 10000.0, 1e-6));
        QVERIFY(ok);
        QVERIFY(near(parseEng(QStringLiteral("4.7u"), &ok), 4.7e-6, 1e-12));
        QVERIFY(ok);
        QVERIFY(near(parseEng(QStringLiteral("2.2M")), 2.2e6, 1e-3));
        QVERIFY(near(parseEng(QStringLiteral("1meg")), 1e6, 1e-6));
        QVERIFY(near(parseEng(QStringLiteral("100nF")), 100e-9, 1e-15));
        QVERIFY(near(parseEng(QStringLiteral("1e-3")), 1e-3, 1e-12));
        QCOMPARE(formatEng(4700.0, QStringLiteral("Ω")), QStringLiteral("4.7 kΩ"));
        QCOMPARE(formatEng(0.0001, QStringLiteral("F")), QStringLiteral("100 µF"));
        QCOMPARE(formatEng(5.0, QStringLiteral("V")), QStringLiteral("5 V"));
        QCOMPARE(formatEng(100000.0, QStringLiteral("Ω"), 2), QStringLiteral("100 kΩ"));
        QCOMPARE(formatEng(1.5e-6, QStringLiteral("s"), 3), QStringLiteral("1.5 µs"));
        parseEng(QStringLiteral("abc"), &ok);
        QVERIFY(!ok);
    }

    void netlistFindsNodes()
    {
        Circuit c = loopCircuit(ComponentType::Resistor, 1000.0,
                                ComponentType::Resistor, 2000.0, 10.0);
        const Netlist net = c.buildNetlist();
        QVERIFY2(net.valid, qPrintable(net.error));
        QCOMPARE(net.nodeCount, 2);
        QCOMPARE(net.nodeOf(c.components.at(0).id, 0), 1); // V1 正端
        QCOMPARE(net.nodeOf(c.components.at(0).id, 1), 0); // V1 负端接地
        QCOMPARE(net.nodeOf(c.components.at(1).id, 0), 1); // R1 左
        QCOMPARE(net.nodeOf(c.components.at(1).id, 1), 2); // R1 右 = 分压点
        QCOMPARE(net.nodeOf(c.components.at(2).id, 1), 0); // R2 下端接地
    }

    void voltageDividerDc()
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

    void rcCharging()
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

    void opAmpInvertingDc()
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

        const QString spice = c.toSpiceNetlist();
        QVERIFY(spice.contains(QStringLiteral("EU1")));
        QVERIFY(spice.contains(QStringLiteral("100000")));
    }

    void mirrorChangesPinGeometry()
    {
        Component op;
        op.id = 1;
        op.type = ComponentType::OpAmp;
        op.pos = QPointF(300, 300);
        op.refdes = QStringLiteral("U1");
        op.value = 1e5;

        QCOMPARE(op.pinPosition(0), QPointF(270, 280));
        QCOMPARE(op.pinPosition(2), QPointF(330, 300));

        op.mirrorX = true;
        QCOMPARE(op.pinPosition(0), QPointF(330, 280));
        QCOMPARE(op.pinPosition(1), QPointF(330, 320));
        QCOMPARE(op.pinPosition(2), QPointF(270, 300));

        op.mirrorX = false;
        op.rotation = 90;
        op.mirrorY = true;
        QCOMPARE(op.pinPosition(0), QPointF(320, 330));

        Circuit circuit;
        circuit.components.append(op);
        const Circuit loaded = Circuit::fromJson(circuit.toJson());
        QVERIFY(!loaded.components.isEmpty());
        QVERIFY(loaded.components.first().mirrorY);
        QVERIFY(!loaded.components.first().mirrorX);
        QCOMPARE(loaded.components.first().rotation, 90);
    }

    void probeMapsToNodeWithoutAffectingResult()
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

        const QString spice = c.toSpiceNetlist();
        QVERIFY(!spice.contains(QStringLiteral("TP1")));

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

    void currentProbeMeasuresBranch()
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

        const QString spice = c.toSpiceNetlist();
        QVERIFY(spice.contains(QStringLiteral("VIP1")));

        const Circuit loaded = Circuit::fromJson(c.toJson());
        QVERIFY(loaded.find(ip.id));
        QCOMPARE(loaded.find(ip.id)->type, ComponentType::CurrentProbe);
    }

    void opAmpInvertingAc()
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

    void diodeRectifier()
    {
        Circuit c = loopCircuit(ComponentType::Diode, 0.0,
                                ComponentType::Resistor, 1000.0, 0.0);
        Component& v = c.components[0];
        v.function = SourceFunction::Sine;
        v.amplitude = 5.0;
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

    void rcLowPassAc()
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

    void jsonRoundTrip()
    {
        Circuit c = loopCircuit(ComponentType::Resistor, 1000.0,
                                ComponentType::Resistor, 2000.0, 10.0);
        const QJsonObject json = c.toJson();

        QString error;
        const Circuit loaded = Circuit::fromJson(json, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.components.size(), 4);
        QCOMPARE(loaded.wires.size(), 3);
        QCOMPARE(loaded.components.at(1).type, ComponentType::Resistor);
        QCOMPARE(loaded.components.at(1).value, 1000.0);
        QCOMPARE(loaded.components.at(1).pos, QPointF(200, 200));
        QCOMPARE(loaded.components.at(2).rotation, 90);

        const Netlist net = loaded.buildNetlist();
        QVERIFY2(net.valid, qPrintable(net.error));
        QCOMPARE(net.nodeCount, 2);
    }

    void spiceNetlistExport()
    {
        Circuit c = loopCircuit(ComponentType::Resistor, 1000.0,
                                ComponentType::Resistor, 2000.0, 10.0);
        const QString spice = c.toSpiceNetlist();
        QVERIFY(spice.contains(QStringLiteral("R1 ")));
        QVERIFY(spice.contains(QStringLiteral("V1 ")));
        QVERIFY(spice.contains(QStringLiteral(".end")));
    }

    void groundRequired()
    {
        Circuit c;
        addComponent(c, makeComp(ComponentType::Resistor, QPointF(0, 0), 100.0));
        addComponent(c, makeComp(ComponentType::VoltageSource, QPointF(-60, 0), 5.0));
        const Netlist net = c.buildNetlist();
        QVERIFY(!net.valid);
        QVERIFY(!net.error.isEmpty());
    }
};

QTEST_APPLESS_MAIN(CoreTest)

#include "test_core.moc"
