#include "core/Circuit.h"
#include "core/Units.h"
#include "sim/Simulator.h"

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
