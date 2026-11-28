#include "core/Circuit.h"
#include "core/Units.h"
#include "core/Autosave.h"
#include "core/PageSetup.h"
#include "core/Shortcuts.h"
#include "core/UpdateChecker.h"
#include "csm_version.h"
#include "sim/Simulator.h"

#include <QDir>
#include <QJsonArray>
#include <QSettings>
#include <QJsonDocument>
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

class CoreModelTest : public QObject {
    Q_OBJECT

private slots:
    void netLabelsWithSameNameConnectNets();
    void quantitiesWithUnits();
    void unitsRoundTrip();
    void netlistFindsNodes();
    void wireEndpointOnSegmentConnects();
    void mirrorChangesPinGeometry();
    void jsonRoundTrip();
    void shortcutOverridesRoundTrip();
    void autosaveWritesAndClearsRecovery();
    void acDcSupplyExampleRuns();
    void tl431ExampleLoads();
    void pageSetupSizesAndOrientation();
    void pageSetupDefaultIsA4Landscape();
    void pageSetupRoundTripsThroughJson();
};


void CoreModelTest::pageSetupSizesAndOrientation()
{
    // 纸张尺寸按 ISO 216：竖版 A4 是 210 × 297，横向就反过来
    PageSetup page;
    page.size = PaperSize::A4;
    page.landscape = false;
    QCOMPARE(page.sizeMm(), QSizeF(210.0, 297.0));
    page.landscape = true;
    QCOMPARE(page.sizeMm(), QSizeF(297.0, 210.0));

    // 画布坐标：1 mm = 96/25.4 个单位
    const QRectF rect = page.sceneRect();
    QVERIFY2(std::abs(rect.width() - 297.0 * 96.0 / 25.4) < 0.01,
             qPrintable(QStringLiteral("宽 %1").arg(rect.width())));
    QVERIFY2(std::abs(rect.height() - 210.0 * 96.0 / 25.4) < 0.01, "高对不上");
    // 左上角钉在原点
    QCOMPARE(rect.topLeft(), QPointF(0.0, 0.0));

    // 自定义尺寸：横向时宽取大的那个，别让「选自定义 + 点横向」毫无反应
    page.size = PaperSize::Custom;
    page.customWidthMm = 400.0;
    page.customHeightMm = 300.0;
    page.landscape = true;
    QCOMPARE(page.sizeMm(), QSizeF(400.0, 300.0));
    page.landscape = false;
    QCOMPARE(page.sizeMm(), QSizeF(300.0, 400.0));
}

void CoreModelTest::pageSetupDefaultIsA4Landscape()
{
    // 默认图纸就是 A4 横向，而且默认值不写进文件
    const PageSetup fallback = PageSetup::fromJson(QJsonObject());
    QCOMPARE(fallback.size, PaperSize::A4);
    QCOMPARE(fallback.landscape, true);
    QVERIFY(fallback.isDefault());

    PageSetup portrait = fallback;
    portrait.landscape = false;
    QVERIFY(!portrait.isDefault());

    PageSetup a3 = fallback;
    a3.size = PaperSize::A3;
    QVERIFY(!a3.isDefault());
}

void CoreModelTest::pageSetupRoundTripsThroughJson()
{
    PageSetup page;
    page.size = PaperSize::Custom;
    page.landscape = false;
    page.customWidthMm = 320.0;
    page.customHeightMm = 240.0;

    const PageSetup back = PageSetup::fromJson(page.toJson());
    QCOMPARE(back.size, PaperSize::Custom);
    QCOMPARE(back.landscape, false);
    QCOMPARE(back.customWidthMm, 320.0);
    QCOMPARE(back.customHeightMm, 240.0);

    // 缺键的老文件：默认 A4 横向
    const PageSetup fallback = PageSetup::fromJson(QJsonObject());
    QCOMPARE(fallback.size, PaperSize::A4);
    QCOMPARE(fallback.landscape, true);
}

void CoreModelTest::netLabelsWithSameNameConnectNets()
{
    // 两个互不相连的子电路，靠同名网络标签并成一个网络
    auto build = [](bool withLabels) {
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
        r1.pos = QPointF(250, 200);
        r1.refdes = QStringLiteral("R1");
        r1.value = 1000.0;
        c.components.append(r1);
        Component r2;
        r2.id = 3;
        r2.type = ComponentType::Resistor;
        r2.pos = QPointF(550, 200);
        r2.refdes = QStringLiteral("R2");
        r2.value = 1000.0;
        c.components.append(r2);
        Component g;
        g.id = 4;
        g.type = ComponentType::Ground;
        g.pos = QPointF(280, 400);
        g.refdes = QStringLiteral("GND");
        c.components.append(g);
        if (withLabels) {
            Component l1;
            l1.id = 5;
            l1.type = ComponentType::NetLabel;
            l1.pos = QPointF(150, 200);
            l1.netLabel = QStringLiteral("VCC");
            c.components.append(l1);
            Component l2;
            l2.id = 6;
            l2.type = ComponentType::NetLabel;
            l2.pos = QPointF(480, 200);
            l2.netLabel = QStringLiteral("VCC");
            c.components.append(l2);
        }
        c.wires.append(Wire{{QPointF(100, 270), QPointF(100, 200), QPointF(220, 200)}});
        c.wires.append(Wire{{QPointF(280, 200), QPointF(280, 400)}});
        c.wires.append(Wire{{QPointF(100, 330), QPointF(100, 400), QPointF(280, 400)}});
        c.wires.append(Wire{{QPointF(480, 200), QPointF(520, 200)}});
        c.wires.append(Wire{{QPointF(580, 200), QPointF(580, 400), QPointF(280, 400)}});
        return c;
    };

    const Netlist without = build(false).buildNetlist();
    QVERIFY2(without.valid, qPrintable(without.error));
    QCOMPARE(without.nodeCount, 2);
    QVERIFY(without.nodeOf(2, 0) != without.nodeOf(3, 0)); // 两块各自独立

    const Circuit merged = build(true);
    const Netlist with = merged.buildNetlist();
    QVERIFY2(with.valid, qPrintable(with.error));
    QCOMPARE(with.nodeCount, 1);
    QCOMPARE(with.nodeOf(2, 0), with.nodeOf(3, 0)); // 同名标签把它们并到一起
    // 节点用网络名显示，报告里不出现「L1.1」这种编号
    QCOMPARE(with.nodeNames.value(with.nodeOf(2, 0)), QStringLiteral("VCC"));

    // R2 本来悬空，并过来以后两端有 10 V，电流 10 mA
    SimulationOptions op;
    op.kind = AnalysisKind::OperatingPoint;
    const SimulationResult r = Simulator::run(merged, op);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY2(near(std::abs(r.elementCurrents.value(QStringLiteral("I(R2)"))), 0.010, 1e-4),
             qPrintable(QString::number(r.elementCurrents.value(QStringLiteral("I(R2)")))));
    QVERIFY2(r.report.contains(QStringLiteral("VCC")), qPrintable(r.report));

    // 存盘往返：网络名不能丢
    const QJsonObject json = merged.toJson();
    QString error;
    const Circuit back = Circuit::fromJson(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.components.at(4).netLabel, QStringLiteral("VCC"));
    QCOMPARE(back.components.at(4).type, ComponentType::NetLabel);
}

void CoreModelTest::quantitiesWithUnits()
{
    bool ok = false;
    QString error;

    QVERIFY(near(parseQuantity(QStringLiteral("10k"), UnitKind::Ohm, &ok), 10000.0, 1e-6));
    QVERIFY(ok);
    QVERIFY(near(parseQuantity(QStringLiteral("10kΩ"), UnitKind::Ohm, &ok), 10000.0, 1e-6));
    QVERIFY(ok);
    QVERIFY(near(parseQuantity(QStringLiteral("4.7µF"), UnitKind::Farad, &ok), 4.7e-6, 1e-12));
    QVERIFY(ok);
    QVERIFY(near(parseQuantity(QStringLiteral("100nF"), UnitKind::Farad, &ok), 100e-9, 1e-15));
    QVERIFY(ok);
    QVERIFY(near(parseQuantity(QStringLiteral("10mH"), UnitKind::Henry, &ok), 0.01, 1e-9));
    QVERIFY(ok);
    QVERIFY(near(parseQuantity(QStringLiteral("5V"), UnitKind::Volt, &ok), 5.0, 1e-9));
    QVERIFY(ok);
    QVERIFY(near(parseQuantity(QStringLiteral("1mA"), UnitKind::Ampere, &ok), 1e-3, 1e-12));
    QVERIFY(ok);
    QVERIFY(near(parseQuantity(QStringLiteral("1kHz"), UnitKind::Hertz, &ok), 1000.0, 1e-6));
    QVERIFY(ok);
    QVERIFY(near(parseQuantity(QStringLiteral("2ms"), UnitKind::Second, &ok), 2e-3, 1e-12));
    QVERIFY(ok);
    QVERIFY(near(parseQuantity(QStringLiteral("1meg"), UnitKind::Ohm, &ok), 1e6, 1e-3));
    QVERIFY(ok);
    QVERIFY(near(parseQuantity(QStringLiteral("100k"), UnitKind::None, &ok), 1e5, 1e-3));
    QVERIFY(ok);

    parseQuantity(QStringLiteral("5V"), UnitKind::Ohm, &ok, &error);
    QVERIFY(!ok);
    QVERIFY(error.contains(QStringLiteral("Ω")));

    parseQuantity(QStringLiteral("5X"), UnitKind::Volt, &ok, &error);
    QVERIFY(!ok);

    parseQuantity(QStringLiteral("5V"), UnitKind::None, &ok, &error);
    QVERIFY(!ok);
}

void CoreModelTest::unitsRoundTrip()
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

void CoreModelTest::netlistFindsNodes()
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

void CoreModelTest::wireEndpointOnSegmentConnects()
{
    Circuit c;

    Component v = makeComp(ComponentType::VoltageSource, QPointF(-60, 0), 0.0, 180);
    v.function = SourceFunction::Constant;
    v.dcValue = 10.0;
    addComponent(c, v);

    addComponent(c, makeComp(ComponentType::Resistor, QPointF(0, 0), 1000.0));
    addComponent(c, makeComp(ComponentType::Resistor, QPointF(60, 90), 2000.0, 90));
    addComponent(c, makeComp(ComponentType::Ground, QPointF(-90, 0), 0.0));
    addComponent(c, makeComp(ComponentType::Ground, QPointF(60, 120), 0.0));

    // 导线 A：R1 右端 → 右侧；导线 B 的端点 (60,0) 落在 A 的内部（T 型接线）
    c.wires.append(Wire{{QPointF(30, 0), QPointF(90, 0)}});
    c.wires.append(Wire{{QPointF(60, 0), QPointF(60, 60)}});

    const Netlist net = c.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QCOMPARE(net.nodeCount, 2);
    QVERIFY2(net.warnings.isEmpty(), qPrintable(net.warnings.join(QStringLiteral("; "))));

    // R2 上端应与 R1 右端同节点
    QCOMPARE(net.nodeOf(c.components.at(2).id, 0), net.nodeOf(c.components.at(1).id, 1));

    SimulationOptions opt;
    opt.kind = AnalysisKind::Transient;
    opt.tStop = 1e-3;
    opt.tStep = 1e-4;
    const SimulationResult r = Simulator::run(c, opt);
    QVERIFY2(r.ok, qPrintable(r.error));
    const Signal* out = r.signal(QStringLiteral("V(2)"));
    QVERIFY(out);
    QVERIFY2(near(out->y.first(), 6.6667, 1e-3),
             qPrintable(QString::number(out->y.first())));
}

void CoreModelTest::mirrorChangesPinGeometry()
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

void CoreModelTest::jsonRoundTrip()
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

void CoreModelTest::acDcSupplyExampleRuns()
{
    // 220 V 交流 → 桥式整流 → MOSFET 斩波 → 反激变压器 → 输出约 12 V
    QFile file(QStringLiteral(CSM_EXAMPLE_DIR "/08_ac_dc_supply.csm"));
    QVERIFY2(file.open(QIODevice::ReadOnly), "示例文件缺失");
    QString error;
    const Circuit circuit = Circuit::fromJson(QJsonDocument::fromJson(file.readAll()).object(),
                                              &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const Netlist net = circuit.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QVERIFY2(net.warnings.isEmpty(), qPrintable(net.warnings.join(QStringLiteral("; "))));

    SimulationOptions tr;
    tr.kind = AnalysisKind::Transient;
    tr.tStop = 0.15;
    tr.tStep = 2e-6;
    const SimulationResult result = Simulator::run(circuit, tr);
    QVERIFY2(result.ok, qPrintable(result.error));

    const Component* tp = nullptr;
    for (const Component& comp : circuit.components)
        if (comp.type == ComponentType::Probe)
            tp = &comp;
    QVERIFY(tp);
    const Signal* out = result.signal(QStringLiteral("V(%1)").arg(net.nodeOf(tp->id, 0)));
    QVERIFY(out);
    double peak = 0.0;
    for (qsizetype i = out->y.size() / 2; i < out->y.size(); ++i)
        peak = std::max(peak, out->y[i]);
    QVERIFY2(peak > 10.0 && peak < 14.0,
             qPrintable(QStringLiteral("输出 %1 V").arg(peak)));
}

void CoreModelTest::tl431ExampleLoads()
{
    // 内置示例必须能打开、能跑出稳定结果
    QFile file(QStringLiteral(CSM_EXAMPLE_DIR "/07_tl431_regulator.csm"));
    QVERIFY2(file.open(QIODevice::ReadOnly), "示例文件缺失");
    QString error;
    const Circuit circuit = Circuit::fromJson(QJsonDocument::fromJson(file.readAll()).object(),
                                              &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(circuit.components.size() >= 10);

    const Netlist net = circuit.buildNetlist();
    QVERIFY2(net.valid, qPrintable(net.error));
    QVERIFY2(net.warnings.isEmpty(), qPrintable(net.warnings.join(QStringLiteral("; "))));

    SimulationOptions opt;
    opt.kind = AnalysisKind::OperatingPoint;
    const SimulationResult result = Simulator::run(circuit, opt);
    QVERIFY2(result.ok, qPrintable(result.error));

    // 分压 10k/10k 把输出稳在 2·Vref ≈ 5 V
    const int out = net.nodeOf(7, 0); // TP1
    const QString needle = QStringLiteral("节点 %1").arg(out);
    QVERIFY2(result.report.contains(QStringLiteral("4.9")), qPrintable(result.report.left(400)));
    Q_UNUSED(needle);
}

QTEST_GUILESS_MAIN(CoreModelTest)


void CoreModelTest::shortcutOverridesRoundTrip()
{
    // QSettings 需要组织名 / 程序名才落到文件上，而 QTEST_GUILESS_MAIN 不设这两个；
    // 这里临时指到系统临时目录下的 ini，既不污染真实配置，也能验证读写
    QCoreApplication::setOrganizationName(QStringLiteral("CircuitMasterTest"));
    QCoreApplication::setApplicationName(QStringLiteral("CircuitMasterTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());

    // 没有记录时用默认值；存过之后用存的值；与默认相同则不落记录
    Shortcuts::clearAll();
    QCOMPARE(Shortcuts::load(QStringLiteral("test.cmd"), QStringLiteral("Ctrl+T")),
             QStringLiteral("Ctrl+T"));

    Shortcuts::save(QStringLiteral("test.cmd"), QStringLiteral("Ctrl+Shift+T"),
                    QStringLiteral("Ctrl+T"));
    QCOMPARE(Shortcuts::load(QStringLiteral("test.cmd"), QStringLiteral("Ctrl+T")),
             QStringLiteral("Ctrl+Shift+T"));

    // 空串表示「不绑定」，要能存住（不能被当成"没有记录"）
    Shortcuts::save(QStringLiteral("test.cmd"), QString(), QStringLiteral("Ctrl+T"));
    QVERIFY(Shortcuts::load(QStringLiteral("test.cmd"), QStringLiteral("Ctrl+T")).isEmpty());

    // 改回与默认一致时把记录删掉
    Shortcuts::save(QStringLiteral("test.cmd"), QStringLiteral("Ctrl+T"), QStringLiteral("Ctrl+T"));
    QCOMPARE(Shortcuts::load(QStringLiteral("test.cmd"), QStringLiteral("Ctrl+T")),
             QStringLiteral("Ctrl+T"));

    Shortcuts::clearAll();
    QFile::remove(QSettings().fileName());
}


void CoreModelTest::autosaveWritesAndClearsRecovery()
{
    // QSettings 要落到能写的地方：单独跑这个用例时没有别的用例替它设过路径
    QCoreApplication::setOrganizationName(QStringLiteral("CircuitMasterTest"));
    QCoreApplication::setApplicationName(QStringLiteral("CircuitMasterTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
    QFile::remove(QSettings().fileName());   // 上一轮跑剩的配置会污染「默认值」断言

    // 恢复文件放在应用数据目录下；测试时改到临时目录，别碰用户真实数据
    const QString dataHome = QDir::tempPath() + QStringLiteral("/csm-autosave-test");
    QDir(dataHome).removeRecursively();
    qputenv("XDG_DATA_HOME", dataHome.toUtf8());
    Autosave::clearAll();

    QCOMPARE(Autosave::enabled(), true);          // 默认开
    QCOMPARE(Autosave::intervalMinutes(), 10);    // 默认 10 分钟

    Autosave::setIntervalMinutes(0);              // 小于下限被夹到 1
    QCOMPARE(Autosave::intervalMinutes(), 1);
    Autosave::setIntervalMinutes(999);            // 大于上限被夹到 120
    QCOMPARE(Autosave::intervalMinutes(), 120);
    Autosave::setIntervalMinutes(10);

    Autosave::Record record;
    record.id = QStringLiteral("doc-1");
    record.originalPath = QStringLiteral("/tmp/foo.csm");
    record.savedAt = QDateTime::currentDateTime();
    record.circuit = QJsonObject{{QStringLiteral("components"), QJsonArray{1, 2}}};
    QVERIFY(Autosave::write(record));

    const QVector<Autosave::Record> pending = Autosave::pending();
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending.first().id, QStringLiteral("doc-1"));
    QCOMPARE(pending.first().originalPath, QStringLiteral("/tmp/foo.csm"));
    QCOMPARE(pending.first().circuit.value(QStringLiteral("components")).toArray().size(), 2);

    Autosave::Record back;
    QVERIFY(Autosave::read(QStringLiteral("doc-1"), &back));
    QCOMPARE(back.id, QStringLiteral("doc-1"));

    Autosave::remove(QStringLiteral("doc-1"));
    QVERIFY(Autosave::pending().isEmpty());

    // 关掉开关要存得住
    Autosave::write(record);
    QCOMPARE(Autosave::pending().size(), 1);
    Autosave::clearAll();
    QVERIFY(Autosave::pending().isEmpty());

    Autosave::setEnabled(false);
    QCOMPARE(Autosave::enabled(), false);
    Autosave::setEnabled(true);

    // 「崩溃恢复」是另一个独立开关，默认开；它有自己的间隔（默认 2 分钟）
    QCOMPARE(Autosave::recoveryEnabled(), true);
    QCOMPARE(Autosave::recoveryIntervalMinutes(), 2);
    Autosave::setRecoveryIntervalMinutes(0);       // 夹到下限 1
    QCOMPARE(Autosave::recoveryIntervalMinutes(), 1);
    Autosave::setRecoveryIntervalMinutes(999);     // 夹到上限
    QCOMPARE(Autosave::recoveryIntervalMinutes(), 60);
    Autosave::setRecoveryIntervalMinutes(2);
    Autosave::setRecoveryEnabled(false);
    QCOMPARE(Autosave::recoveryEnabled(), false);
    Autosave::setRecoveryEnabled(true);
    QCOMPARE(Autosave::recoveryEnabled(), true);
    QDir(dataHome).removeRecursively();
    QFile::remove(QSettings().fileName());
}

#include "test_core_model.moc"
