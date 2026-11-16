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
    void updateCheckerReadsManifest()
    {
        // 本地起一个最小 HTTP 服务，模拟 Release 上的 latest.json
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        QByteArray payload;
        connect(&server, &QTcpServer::newConnection, this, [&server, &payload] {
            QTcpSocket* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &payload] {
                socket->readAll();
                const QByteArray body = payload;
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                              "Content-Length: "
                              + QByteArray::number(body.size())
                              + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });

        const QUrl url(QStringLiteral("http://127.0.0.1:%1/latest.json").arg(server.serverPort()));

        auto manifest = [](const QString& version, const QString& tag, const QString& page) {
            QJsonObject object;
            object.insert(QStringLiteral("version"), version);
            object.insert(QStringLiteral("tag"), tag);
            object.insert(QStringLiteral("name"), QStringLiteral("circuit-master %1").arg(tag));
            object.insert(QStringLiteral("notes"), QStringLiteral("### 新增\n\n- 条目"));
            object.insert(QStringLiteral("published_at"), QStringLiteral("2026-12-01T10:00:00Z"));
            if (!page.isEmpty())
                object.insert(QStringLiteral("page"), page);
            return QJsonDocument(object).toJson(QJsonDocument::Compact);
        };

        // 1) 清单版本更新 → updateAvailable，且不可信的发布页地址被丢弃
        payload = manifest(QStringLiteral("99.0.0"), QStringLiteral("v99.0.0"),
                           QStringLiteral("https://evil.example.com/x"));
        {
            UpdateChecker checker;
            checker.setMirrorPrefixes({});
            checker.setManifestUrl(url);
            QSignalSpy spy(&checker, &UpdateChecker::updateAvailable);
            QSignalSpy failedSpy(&checker, &UpdateChecker::failed);
            QVERIFY(checker.check());
            QVERIFY(spy.wait(5000));
            QCOMPARE(spy.count(), 1);
            QCOMPARE(failedSpy.count(), 0);

            const auto release = spy.first().at(0).value<ReleaseInfo>();
            QCOMPARE(release.version, QStringLiteral("99.0.0"));
            QCOMPARE(release.tagName, QStringLiteral("v99.0.0"));
            QCOMPARE(release.pageUrl.host(), QStringLiteral("github.com"));
            QVERIFY(release.pageUrl.path().contains(QStringLiteral("/lhdjply/circuit-master/")));
        }

        // 2) 清单版本更旧 → upToDate
        payload = manifest(QStringLiteral("0.0.1"), QStringLiteral("v0.0.1"), QString());
        {
            UpdateChecker checker;
            checker.setMirrorPrefixes({});
            checker.setManifestUrl(url);
            QSignalSpy spy(&checker, &UpdateChecker::upToDate);
            QVERIFY(checker.check());
            QVERIFY(spy.wait(5000));
            QCOMPARE(spy.count(), 1);
            QCOMPARE(spy.first().at(0).toString(), QStringLiteral(CSM_VERSION_NUMBER));
        }

        // 3) 正文不是 JSON（转发服务常见的 HTML 错误页）→ failed，不能当成“没有更新”
        payload = QByteArray("<html><body>502 Bad Gateway</body></html>");
        {
            UpdateChecker checker;
            checker.setMirrorPrefixes({});
            checker.setManifestUrl(url);
            QSignalSpy spy(&checker, &UpdateChecker::failed);
            QSignalSpy upSpy(&checker, &UpdateChecker::upToDate);
            QVERIFY(checker.check());
            QVERIFY(spy.wait(5000));
            QCOMPARE(spy.count(), 1);
            QCOMPARE(upSpy.count(), 0);
            QVERIFY(!spy.first().at(0).toString().isEmpty());
        }
    }

    void bipolarTransistorAmplifies()
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

    void polarizedCapacitorReverseDetected()
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

    void ledForwardVoltage()
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

    void pnpTransistorMirrorsNpn()
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

    void mosfetSwitches()
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

    void pmosConductsWithNegativeGate()
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

    void igbtConductsAboveThreshold()
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

    void updatePickerChoosesPlatformAsset()
    {
        ReleaseInfo release;
        release.version = QStringLiteral("0.2.0");
        const auto add = [&release](const QString& name) {
            ReleaseAsset asset;
            asset.name = name;
            asset.url = QStringLiteral("https://example.invalid/") + name;
            asset.size = 1024;
            release.assets.append(asset);
        };
        add(QStringLiteral("circuit-master_0.2.0_amd64.deb"));
        add(QStringLiteral("circuit-master_0.2.0_arm64.deb"));
        add(QStringLiteral("circuit-master_0.2.0_loong64.deb"));
        add(QStringLiteral("circuit-master-0.2.0-linux-amd64.tar.gz"));
        add(QStringLiteral("circuit-master-0.2.0-windows-x64.zip"));
        add(QStringLiteral("circuit-master-0.2.0-windows-x64-setup.exe"));
        add(QStringLiteral("circuit-master-0.2.0-windows-arm64-setup.exe"));

        // Linux：按架构挑 deb，优先 deb 而不是 tar.gz
        const auto pick = [&release](HostPlatform os, HostArch arch,
                                     InstallKind kind = InstallKind::System) {
            return UpdateChecker::pickAsset(release, os, arch, kind).name;
        };
        QCOMPARE(pick(HostPlatform::Linux, HostArch::Amd64),
                 QStringLiteral("circuit-master_0.2.0_amd64.deb"));
        QCOMPARE(pick(HostPlatform::Linux, HostArch::Arm64),
                 QStringLiteral("circuit-master_0.2.0_arm64.deb"));
        QCOMPARE(pick(HostPlatform::Linux, HostArch::Loong64),
                 QStringLiteral("circuit-master_0.2.0_loong64.deb"));

        // Windows：优先安装包
        QCOMPARE(pick(HostPlatform::Windows, HostArch::Amd64),
                 QStringLiteral("circuit-master-0.2.0-windows-x64-setup.exe"));
        QCOMPARE(pick(HostPlatform::Windows, HostArch::Arm64),
                 QStringLiteral("circuit-master-0.2.0-windows-arm64-setup.exe"));

        // 某架构没出包时退回 portable
        ReleaseInfo only = release;
        only.assets.clear();
        ReleaseAsset zip;
        zip.name = QStringLiteral("circuit-master-0.2.0-windows-x64.zip");
        zip.url = QStringLiteral("https://example.invalid/x.zip");
        only.assets.append(zip);
        QCOMPARE(UpdateChecker::pickAsset(only, HostPlatform::Windows, HostArch::Amd64,
                                          InstallKind::System).name,
                 QStringLiteral("circuit-master-0.2.0-windows-x64.zip"));

        // 包形态枚举：Linux 出 deb + 便携 tar.gz，Windows 出 setup + 便携 zip
        const auto formats = [](HostPlatform os, InstallKind kind) {
            return UpdateChecker::formatsFor(os, kind);
        };
        QCOMPARE(formats(HostPlatform::Linux, InstallKind::System).size(), 2);
        QCOMPARE(formats(HostPlatform::Windows, InstallKind::System).size(), 2);
        QVERIFY(formats(HostPlatform::MacOS, InstallKind::System).isEmpty());

        // 便携版运行时要优先给便携包，和安装方式保持一致
        QCOMPARE(formats(HostPlatform::Linux, InstallKind::System).first(), PackageFormat::Deb);
        QCOMPARE(formats(HostPlatform::Linux, InstallKind::Portable).first(),
                 PackageFormat::PortableTarGz);
        QCOMPARE(formats(HostPlatform::Windows, InstallKind::System).first(),
                 PackageFormat::SetupExe);
        QCOMPARE(formats(HostPlatform::Windows, InstallKind::Portable).first(),
                 PackageFormat::PortableZip);

        // 挑包跟着安装方式走
        QCOMPARE(pick(HostPlatform::Linux, HostArch::Amd64, InstallKind::Portable),
                 QStringLiteral("circuit-master-0.2.0-linux-amd64.tar.gz"));
        QCOMPARE(pick(HostPlatform::Linux, HostArch::Amd64, InstallKind::System),
                 QStringLiteral("circuit-master_0.2.0_amd64.deb"));

        QCOMPARE(UpdateChecker::suffixFor(HostPlatform::Linux, HostArch::Amd64, PackageFormat::Deb),
                 QStringLiteral("_amd64.deb"));
        QCOMPARE(UpdateChecker::suffixFor(HostPlatform::Linux, HostArch::Riscv64,
                                          PackageFormat::PortableTarGz),
                 QStringLiteral("-linux-riscv64.tar.gz"));
        QCOMPARE(UpdateChecker::suffixFor(HostPlatform::Windows, HostArch::Arm64,
                                          PackageFormat::SetupExe),
                 QStringLiteral("-windows-arm64-setup.exe"));
        QCOMPARE(UpdateChecker::suffixFor(HostPlatform::Windows, HostArch::Amd64,
                                          PackageFormat::PortableZip),
                 QStringLiteral("-windows-x64.zip"));
        // 平台与形态不匹配时没有后缀
        QVERIFY(UpdateChecker::suffixFor(HostPlatform::Linux, HostArch::Amd64,
                                         PackageFormat::SetupExe).isEmpty());

        // 同一平台两种形态都要能列出来
        const auto linuxPackages = UpdateChecker::hostPackagesFor(
            release, HostPlatform::Linux, HostArch::Amd64, InstallKind::System);
        QCOMPARE(linuxPackages.size(), 2);
        QCOMPARE(linuxPackages.at(0).format, PackageFormat::Deb);
        QCOMPARE(linuxPackages.at(1).format, PackageFormat::PortableTarGz);
        const auto winPackages = UpdateChecker::hostPackagesFor(
            release, HostPlatform::Windows, HostArch::Amd64, InstallKind::System);
        QCOMPARE(winPackages.size(), 2);
        QCOMPARE(winPackages.at(0).format, PackageFormat::SetupExe);
        QCOMPARE(winPackages.at(1).format, PackageFormat::PortableZip);

        // 便携版只看得到便携包在最前
        const auto portable = UpdateChecker::hostPackagesFor(
            release, HostPlatform::Linux, HostArch::Amd64, InstallKind::Portable);
        QCOMPARE(portable.at(0).format, PackageFormat::PortableTarGz);

        // 环境变量可覆盖检测结果
        qputenv("CSM_INSTALL_KIND", "portable");
        QCOMPARE(UpdateChecker::detectInstallKind(), InstallKind::Portable);
        qputenv("CSM_INSTALL_KIND", "system");
        QCOMPARE(UpdateChecker::detectInstallKind(), InstallKind::System);
        qunsetenv("CSM_INSTALL_KIND");

        // 不支持的平台：返回空，界面回退到发布页
        QVERIFY(UpdateChecker::pickAsset(release, HostPlatform::MacOS, HostArch::Arm64,
                                         InstallKind::System).name.isEmpty());
        // 当前构建目标一定能挑到 Linux 包（本机是 Linux-amd64）
        QVERIFY(!UpdateChecker::pickAssetForHost(release).name.isEmpty());
    }

    void downloadUrlsPreferWorkingMirror()
    {
        const QString asset = QStringLiteral(
            "https://github.com/lhdjply/circuit-master/releases/download/v0.2.0/a.deb");

        // 取清单时用的是 ghfast.top：下载也应当先走它
        const QVector<QUrl> preferred =
            UpdateChecker::downloadUrls(asset, QStringLiteral("https://ghfast.top"));
        QVERIFY(preferred.size() >= 2);
        QCOMPARE(preferred.first().toString(),
                 QStringLiteral("https://ghfast.top/") + asset);
        // 直链放在最后
        QCOMPARE(preferred.last().toString(), asset);
        // 同样的镜像不会重复出现
        int ghfast = 0;
        for (const QUrl& url : preferred) {
            if (url.toString().startsWith(QStringLiteral("https://ghfast.top/")))
                ++ghfast;
        }
        QCOMPARE(ghfast, 1);

        // 没记录通路时：先镜像，最后直链
        const QVector<QUrl> plain = UpdateChecker::downloadUrls(asset, QString());
        QVERIFY(plain.size() >= 2);
        QVERIFY(plain.first().toString().startsWith(QStringLiteral("https://gh-proxy.org/")));
        QCOMPARE(plain.last().toString(), asset);

        // 空资产地址返回空列表
        QVERIFY(UpdateChecker::downloadUrls(QString(), QString()).isEmpty());
    }

    void manifestRecordsMirrorForDownloads()
    {
        // 本地起一个「镜像」，同时提供 latest.json 和安装包
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray assetPayload = "package-bytes";
        connect(&server, &QTcpServer::newConnection, this, [&server, &assetPayload] {
            QTcpSocket* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &assetPayload] {
                const QByteArray request = socket->readAll();
                const bool isAsset = request.contains("/pkg.bin");
                const QByteArray body = isAsset ? assetPayload : QByteArray();
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                              "Content-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n"
                              + body);
                socket->disconnectFromHost();
            });
        });

        const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        QJsonObject object;
        object.insert(QStringLiteral("version"), QStringLiteral("99.0.0"));
        object.insert(QStringLiteral("tag"), QStringLiteral("v99.0.0"));
        QJsonObject asset;
        asset.insert(QStringLiteral("name"), QStringLiteral("circuit-master_99.0.0_amd64.deb"));
        asset.insert(QStringLiteral("url"), base + QStringLiteral("/pkg.bin"));
        asset.insert(QStringLiteral("size"), qint64(assetPayload.size()));
        object.insert(QStringLiteral("assets"), QJsonArray{asset});
        const QByteArray manifest = QJsonDocument(object).toJson(QJsonDocument::Compact);

        QTcpServer manifestServer;
        QVERIFY(manifestServer.listen(QHostAddress::LocalHost));
        connect(&manifestServer, &QTcpServer::newConnection, this, [&manifestServer, &manifest] {
            QTcpSocket* socket = manifestServer.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &manifest] {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                              "Content-Length: "
                              + QByteArray::number(manifest.size())
                              + "\r\nConnection: close\r\n\r\n" + manifest);
                socket->disconnectFromHost();
            });
        });

        const QString mirror = QStringLiteral("http://127.0.0.1:%1").arg(manifestServer.serverPort());

        UpdateChecker checker;
        checker.setMirrorPrefixes({mirror});
        checker.setManifestUrl(QUrl(base + QStringLiteral("/latest.json")));
        QSignalSpy spy(&checker, &UpdateChecker::updateAvailable);
        QVERIFY(checker.check());
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.count(), 1);

        const auto release = spy.first().at(0).value<ReleaseInfo>();
        QCOMPARE(release.assets.size(), 1);
        QVERIFY2(!release.mirror.isEmpty(), "应当记录取到清单所用的镜像");
        QVERIFY(release.mirror.startsWith(mirror));
    }

    void versionComparison()
    {
        QVERIFY(UpdateChecker::compareVersions(QStringLiteral("0.2.0"), QStringLiteral("0.1.0")) > 0);
        QVERIFY(UpdateChecker::compareVersions(QStringLiteral("0.1.0"), QStringLiteral("0.2.0")) < 0);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("0.1.0"), QStringLiteral("0.1.0")), 0);

        // v 前缀、预发布后缀、缺失的段都不影响比较
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("v0.1.0"), QStringLiteral("0.1.0")), 0);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("0.2.0-rc1"), QStringLiteral("0.2")), 0);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.2"), QStringLiteral("1.2.0")), 0);

        // 按数字段比较，而不是按字符串：0.10.0 比 0.9.9 新
        QVERIFY(UpdateChecker::compareVersions(QStringLiteral("0.10.0"), QStringLiteral("0.9.9")) > 0);
        QVERIFY(UpdateChecker::compareVersions(QStringLiteral("1.0.0"), QStringLiteral("0.99.99")) > 0);

        // 清单里的 version 一般不带 v，比较时当前版本来自 CSM_VERSION_NUMBER
        QVERIFY(UpdateChecker::compareVersions(QStringLiteral("99.0.0"),
                                               QStringLiteral(CSM_VERSION_NUMBER)) > 0);
    }

    void quantitiesWithUnits()
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

    void wireEndpointOnSegmentConnects()
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

    void suggestedStepAvoidsAliasing()
    {
        Circuit c = loopCircuit(ComponentType::Resistor, 1000.0,
                                ComponentType::Resistor, 1000.0, 0.0);
        Component& v = c.components[0];
        v.function = SourceFunction::Sine;
        v.amplitude = 220.0;
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

    void liveTransientMatchesBatch()
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

QTEST_GUILESS_MAIN(CoreTest)

#include "test_core.moc"
