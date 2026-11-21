#include "ui/Symbols.h"

#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QtSvg/QSvgRenderer>
#include <QPolygonF>

namespace csm {

namespace {

QPen symbolPen(const QColor& color, qreal width = 2.0)
{
    QPen pen(color, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

/// 引脚极性标记：0 号引脚是「+」、1 号是「−」。
/// 万用表与电压源共用同一套位置，免得同一个极性在两处画得高低不一。
/// 调用时画笔已处在「符号被旋转过」的坐标系里，文字再反着转回来，
/// 保证转到哪个角度都正着读。
void drawPinPolarity(QPainter* p, int rotation, bool mirrorX, bool mirrorY)
{
    QFont font = p->font();
    font.setPointSizeF(7.0);
    font.setBold(true);
    p->save();
    p->setFont(font);
    const auto mark = [&](const QPointF& at, const QString& text) {
        p->save();
        p->translate(at);
        p->rotate(-double(rotation));
        p->scale(mirrorX ? -1.0 : 1.0, mirrorY ? -1.0 : 1.0);
        p->drawText(QRectF(-7.0, -7.0, 14.0, 14.0), Qt::AlignCenter, text);
        p->restore();
    };
    mark(QPointF(-21.0, -8.0), QStringLiteral("+"));
    mark(QPointF(21.0, -8.0), QStringLiteral("−"));
    p->restore();
}

void drawPinDots(QPainter* p, const Component& comp, bool selected)
{
    const QVector<QPointF> offsets = pinOffsets(comp);
    if (offsets.isEmpty())
        return;
    const QColor color = selected ? Theme::selected() : Theme::pin();
    p->setPen(QPen(color, 1.3));
    p->setBrush(Theme::symbolFill());
    for (const QPointF& off : offsets)
        p->drawEllipse(off, 2.8, 2.8);
}

} // namespace

void paintVoltageSourceOverlay(QPainter* p, const Component& comp, const QColor& color)
{
    const bool generator = (comp.type == ComponentType::SignalGenerator);
    if (comp.type != ComponentType::VoltageSource && !generator)
        return;
    const bool centred = generator || (comp.function != SourceFunction::Constant);

    // 波形图案：画在圆心，保持正立
    if (centred) {
        p->save();
        p->setPen(symbolPen(color, 1.8));
        p->setBrush(Qt::NoBrush);
        if (generator) {
            // 四种波形各画一个小图案，一眼能认出是哪种
            QPainterPath wave;
            switch (comp.waveform) {
            case Waveform::Sine:
                wave.moveTo(-7.0, 0.0);
                wave.cubicTo(-5.0, -7.0, -2.0, -7.0, 0.0, 0.0);
                wave.cubicTo(2.0, 7.0, 5.0, 7.0, 7.0, 0.0);
                break;
            case Waveform::Square:
                wave.moveTo(-7.0, 3.0);
                wave.lineTo(-4.0, 3.0);
                wave.lineTo(-4.0, -3.0);
                wave.lineTo(1.0, -3.0);
                wave.lineTo(1.0, 3.0);
                wave.lineTo(7.0, 3.0);
                break;
            case Waveform::Triangle:
                wave.moveTo(-7.0, 3.0);
                wave.lineTo(-3.5, -4.0);
                wave.lineTo(0.0, 3.0);
                wave.lineTo(3.5, -4.0);
                wave.lineTo(7.0, 3.0);
                break;
            case Waveform::Sawtooth:
                // 两段斜坡 + 竖直回落，读起来才像锯齿
                wave.moveTo(-7.0, 3.0);
                wave.lineTo(-2.0, -4.0);
                wave.lineTo(-2.0, 3.0);
                wave.lineTo(3.0, -4.0);
                wave.lineTo(3.0, 3.0);
                wave.lineTo(7.0, -1.0);
                break;
            }
            p->drawPath(wave);
        } else if (comp.function == SourceFunction::Sine) {
            QPainterPath wave;
            wave.moveTo(-7.0, 0.0);
            wave.cubicTo(-5.0, -7.0, -2.0, -7.0, 0.0, 0.0);
            wave.cubicTo(2.0, 7.0, 5.0, 7.0, 7.0, 0.0);
            p->drawPath(wave);
        } else {
            QPainterPath wave;
            wave.moveTo(-7.0, 2.0);
            wave.lineTo(-4.0, 2.0);
            wave.lineTo(-4.0, -4.0);
            wave.lineTo(1.0, -4.0);
            wave.lineTo(1.0, 2.0);
            wave.lineTo(7.0, 2.0);
            p->drawPath(wave);
        }
        p->restore();
    }

}

QRectF symbolBounds(const Component& comp)
{
    QRectF bounds(-42.0, -27.0, 84.0, 54.0);
    if (comp.type == ComponentType::MeasureProbe)
        bounds = bounds.united(probeReadoutBox());
    return bounds;
}

QRectF probeReadoutBox()
{
    // 居中放在探针接点正上方，够放 6 行读数（画布 1 单位 ≈ 1 px，5.5pt 一行约 8 px）
    return QRectF(-46.0, -74.0, 92.0, 50.0);
}

QRectF probeReadoutBoxFor(int rotation)
{
    const QRectF box = probeReadoutBox();
    const int angle = ((rotation % 360) + 360) % 360;
    // 只按 90° 的整数倍摆：斜着放时框取最近的 90°，免得框跟着歪
    const QPointF center = box.center();
    QPointF rotated;
    switch (angle) {
    case 90:  rotated = QPointF(-center.y(), center.x()); break;
    case 180: rotated = QPointF(-center.x(), -center.y()); break;
    case 270: rotated = QPointF(center.y(), -center.x()); break;
    default:  rotated = center; break;
    }
    // 尺寸不跟着转：框里是横排的一行行读数，转成竖条就装不下了。
    // 旋转只改变框摆在哪一侧。
    return QRectF(rotated.x() - box.width() / 2.0, rotated.y() - box.height() / 2.0,
                  box.width(), box.height());
}

void paintProbeBox(QPainter* p, const QRectF& box)
{
    p->save();
    p->setPen(QPen(QColor(0xca, 0x8a, 0x04), 1.0));
    p->setBrush(QColor(0xfe, 0xf9, 0xc3, 235));
    p->drawRoundedRect(box, 3.0, 3.0);
    p->restore();
}

QPainterPath symbolShape(ComponentType type)
{
    QPainterPath path;
    if (type == ComponentType::Ground) {
        path.addRoundedRect(QRectF(-14.0, -6.0, 28.0, 34.0), 4.0, 4.0);
        return path;
    }
    if (type == ComponentType::NetLabel) {
        // 旗标可以朝四个方向伸，命中区按「引脚附近的方块」给，
        // 比按名字长度算简单，也不会把远处的导线抢走
        path.addRoundedRect(QRectF(-26.0, -26.0, 52.0, 52.0), 4.0, 4.0);
        return path;
    }
    if (type == ComponentType::OpAmp) {
        // 三角形本体即可，别把整块外接矩形都算成命中区，否则会把背后的导线抢走
        QPolygonF tri;
        tri << QPointF(-26.0, -22.0) << QPointF(-26.0, 22.0) << QPointF(24.0, 0.0);
        path.addPolygon(tri);
        path.closeSubpath();
        return path;
    }
    if (type == ComponentType::PushButton) {
        // 符号比两端元件高（按杆伸到 -24），命中区也跟着高一点
        path.addRoundedRect(QRectF(-30.0, -24.0, 60.0, 30.0), 4.0, 4.0);
        return path;
    }
    if (type == ComponentType::LogicGate) {
        path.addRoundedRect(QRectF(-24.0, -18.0, 48.0, 36.0), 4.0, 4.0);
        return path;
    }
    if (type == ComponentType::Transformer) {
        path.addRoundedRect(QRectF(-30.0, -22.0, 60.0, 44.0), 4.0, 4.0);
        return path;
    }
    if (type == ComponentType::Ob2362) {
        path.addRoundedRect(QRectF(-24.0, -26.0, 48.0, 52.0), 4.0, 4.0);
        return path;
    }
    if (type == ComponentType::Tl431) {
        path.addRoundedRect(QRectF(-22.0, -20.0, 44.0, 40.0), 4.0, 4.0);
        return path;
    }
    if (type == ComponentType::Optocoupler) {
        path.addRoundedRect(QRectF(-30.0, -22.0, 60.0, 44.0), 4.0, 4.0);
        return path;
    }
    if (isThreeTerminalDevice(type)) {
        path.addRoundedRect(QRectF(-16.0, -16.0, 46.0, 32.0), 4.0, 4.0);
        return path;
    }
    if (type == ComponentType::MeasureProbe) {
        // 探针本体 = 黄框 + 一小段引线 + 接点小球
        QPainterPath probe;
        probe.addRoundedRect(probeReadoutBox(), 3.0, 3.0);
        probe.addRect(QRectF(-5.0, probeReadoutBox().bottom() - 2.0, 10.0,
                             -probeReadoutBox().bottom() + 6.0));
        return probe;
    }
    if (isVoltageTap(type)) {
        // 符号本体只有一根短棍 + 一个小圈，命中区给得比图形略大一点（还是贴着它），
        // 免得必须点在那条细线上
        path.addRoundedRect(QRectF(-9.0, -24.0, 18.0, 27.0), 6.0, 6.0);
        return path;
    }
    // 两端元件：贴着符号本体，高度收到 ±12，避免把旁边经过的导线挡住
    path.addRoundedRect(QRectF(-30.0, -12.0, 60.0, 24.0), 4.0, 4.0);
    path.addEllipse(QPointF(-30.0, 0.0), 5.0, 5.0);
    path.addEllipse(QPointF(30.0, 0.0), 5.0, 5.0);
    return path;
}

void paintSymbol(QPainter* p, const Component& comp, bool selected, bool showPins, int rotation,
                 bool mirrorX, bool mirrorY, double glow, double heat)
{
    QColor color = selected ? Theme::selected() : Theme::symbol();
    if (heat > 0.01) {
        // 红温：符号颜色朝炽红过渡，heat = 1 时基本是暗红
        const QColor hot(200, 30, 10);
        const double h = std::min(1.0, heat);
        color = QColor(int(color.red() + (hot.red() - color.red()) * h),
                       int(color.green() + (hot.green() - color.green()) * h),
                       int(color.blue() + (hot.blue() - color.blue()) * h));
    }
    const QPen pen = symbolPen(color, selected ? 2.4 : 2.0);

    p->save();
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);

    switch (comp.type) {
    case ComponentType::Resistor: {
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-20.0, 0.0));
        p->drawLine(QPointF(20.0, 0.0), QPointF(30.0, 0.0));
        QPolygonF zigzag;
        zigzag << QPointF(-20.0, 0.0) << QPointF(-15.0, -8.0) << QPointF(-5.0, 8.0)
               << QPointF(5.0, -8.0) << QPointF(15.0, 8.0) << QPointF(20.0, 0.0);
        p->drawPolyline(zigzag);
        break;
    }

    case ComponentType::Capacitor: {
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-5.0, 0.0));
        p->drawLine(QPointF(6.0, 0.0), QPointF(30.0, 0.0));
        p->setPen(symbolPen(color, 2.8));

        if (comp.polarized) {
            // 电解电容：正极是直板（引脚 0），负极是弯板（引脚 1）
            p->drawLine(QPointF(-5.0, -13.0), QPointF(-5.0, 13.0));
            QPainterPath arc;
            arc.moveTo(6.0, -13.0);
            arc.quadTo(-1.0, 0.0, 6.0, 13.0);
            p->setBrush(Qt::NoBrush);
            p->drawPath(arc);
            p->setPen(symbolPen(color, 1.8));
            // 正极标记
            p->drawLine(QPointF(-19.0, -18.0), QPointF(-11.0, -18.0));
            p->drawLine(QPointF(-15.0, -22.0), QPointF(-15.0, -14.0));
        } else {
            p->drawLine(QPointF(-5.0, -13.0), QPointF(-5.0, 13.0));
            p->drawLine(QPointF(5.0, -13.0), QPointF(5.0, 13.0));
        }
        break;
    }

    case ComponentType::Inductor: {
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-20.0, 0.0));
        p->drawLine(QPointF(20.0, 0.0), QPointF(30.0, 0.0));
        QPainterPath coil;
        coil.moveTo(-20.0, 0.0);
        for (int i = 0; i < 4; ++i) {
            const qreal x = -20.0 + i * 10.0;
            coil.cubicTo(x + 2.5, -10.0, x + 7.5, -10.0, x + 10.0, 0.0);
        }
        p->drawPath(coil);
        break;
    }

    case ComponentType::Led: {
        // 和普通二极管完全一致的画法：实心三角形朝右 + 右侧阴极竖条
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-8.0, 0.0));
        p->drawLine(QPointF(8.0, 0.0), QPointF(30.0, 0.0));
        QPolygonF triangle;
        triangle << QPointF(-8.0, -11.0) << QPointF(-8.0, 11.0) << QPointF(8.0, 0.0);

        // 点亮时三角形从主题色过渡到暖红，亮度越高越红
        QColor fill = color;
        QColor stroke = color;
        if (glow > 0.01) {
            const QColor hot(255, 72, 24);
            fill = QColor(int(color.red() + (hot.red() - color.red()) * glow),
                          int(color.green() + (hot.green() - color.green()) * glow),
                          int(color.blue() + (hot.blue() - color.blue()) * glow));
            stroke = fill;
        }
        p->setBrush(fill);
        p->drawPolygon(triangle);
        p->setBrush(Qt::NoBrush);
        p->setPen(symbolPen(stroke, 2.8));
        p->drawLine(QPointF(8.0, -11.0), QPointF(8.0, 11.0));

        // 两个 45° 斜向右上的箭头，表示发光
        p->setPen(symbolPen(color, 1.6));
        for (int i = 0; i < 2; ++i) {
            const double dy = i * 7.0;
            p->drawLine(QPointF(12.0, -12.0 - dy), QPointF(22.0, -20.0 - dy));
            QPolygonF head;
            head << QPointF(22.0, -20.0 - dy) << QPointF(16.0, -18.0 - dy)
                 << QPointF(20.0, -14.0 - dy);
            p->setBrush(color);
            p->drawPolygon(head);
            p->setBrush(Qt::NoBrush);
        }
        p->setPen(pen);
        break;
    }

    case ComponentType::Diode: {
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-8.0, 0.0));
        p->drawLine(QPointF(8.0, 0.0), QPointF(30.0, 0.0));
        QPolygonF triangle;
        triangle << QPointF(-8.0, -11.0) << QPointF(-8.0, 11.0) << QPointF(8.0, 0.0);
        p->setBrush(color);
        p->drawPolygon(triangle);
        p->setBrush(Qt::NoBrush);
        p->setPen(symbolPen(color, 2.8));
        p->drawLine(QPointF(8.0, -11.0), QPointF(8.0, 11.0));
        break;
    }

    case ComponentType::OpAmp: {
        p->drawLine(QPointF(-30.0, -20.0), QPointF(-18.0, -20.0));
        p->drawLine(QPointF(-30.0, 20.0), QPointF(-18.0, 20.0));
        p->drawLine(QPointF(18.0, 0.0), QPointF(30.0, 0.0));
        QPolygonF triangle;
        triangle << QPointF(-18.0, -22.0) << QPointF(-18.0, 22.0) << QPointF(18.0, 0.0);
        p->setBrush(Theme::symbolFill());
        p->drawPolygon(triangle);
        p->setBrush(Qt::NoBrush);
        // 同相输入 +
        p->drawLine(QPointF(-13.0, -12.0), QPointF(-7.0, -12.0));
        p->drawLine(QPointF(-10.0, -15.0), QPointF(-10.0, -9.0));
        // 反相输入 −
        p->drawLine(QPointF(-13.0, 12.0), QPointF(-7.0, 12.0));
        break;
    }

    case ComponentType::LogicGate: {
        const GateKind kind = comp.gateKind;
        const int inputs = gateInputCount(kind);
        const bool curved = kind == GateKind::Or || kind == GateKind::Xor
            || kind == GateKind::Xnor;

        // 输入 / 输出引线
        const double bodyLeft = (kind == GateKind::Xor || kind == GateKind::Xnor) ? -27.0 : -18.0;
        if (inputs == 1) {
            p->drawLine(QPointF(-30.0, 0.0), QPointF(bodyLeft, 0.0));
        } else {
            p->drawLine(QPointF(-30.0, -10.0), QPointF(bodyLeft, -10.0));
            p->drawLine(QPointF(-30.0, 10.0), QPointF(bodyLeft, 10.0));
        }

        QPainterPath body;
        if (kind == GateKind::Not) {
            QPolygonF tri;
            tri << QPointF(-18.0, 15.0) << QPointF(-18.0, -15.0) << QPointF(14.0, 0.0);
            body.addPolygon(tri);
            body.closeSubpath();
        } else if (!curved) {
            // 与门：左直边 + 右半圆。用三次贝塞尔画那半个圆 ——
            // arcTo 的扫掠方向在屏幕坐标系里容易画反，写死曲线更稳
            body.moveTo(-18.0, 16.0);
            body.lineTo(-2.0, 16.0);
            body.cubicTo(QPointF(17.0, 16.0), QPointF(17.0, -16.0), QPointF(-2.0, -16.0));
            body.lineTo(-18.0, -16.0);
            body.closeSubpath();
        } else {
            // 或门：右侧收成尖角，后沿内凹
            body.moveTo(-20.0, 16.0);
            body.quadTo(-8.0, 16.0, 18.0, 0.0);
            body.quadTo(-8.0, -16.0, -20.0, -16.0);
            body.quadTo(-12.0, 0.0, -20.0, 16.0);
            body.closeSubpath();
        }

        p->setBrush(Theme::symbolFill());
        p->drawPath(body);
        p->setBrush(Qt::NoBrush);

        // 异或 / 同或：后沿外面再加一条弧
        if (kind == GateKind::Xor || kind == GateKind::Xnor) {
            QPainterPath back;
            back.moveTo(-27.0, 16.0);
            back.quadTo(-19.0, 0.0, -27.0, -16.0);
            p->drawPath(back);
        }

        // 非门 / 同或门：输出端的小圆圈，再补一截引线到引脚
        if (kind == GateKind::Not) {
            p->drawEllipse(QPointF(18.0, 0.0), 4.0, 4.0);
            p->drawLine(QPointF(22.0, 0.0), QPointF(30.0, 0.0));
        } else if (kind == GateKind::Xnor) {
            p->drawEllipse(QPointF(22.0, 0.0), 4.0, 4.0);
            p->drawLine(QPointF(26.0, 0.0), QPointF(30.0, 0.0));
        } else {
            // 与门 / 或门 / 异或门的输出引脚从符号最右端算起
            // 与门的右缘在 x≈16，或门 / 异或门的尖角在 18
            p->drawLine(QPointF(kind == GateKind::And ? 16.0 : 18.0, 0.0),
                        QPointF(30.0, 0.0));
        }
        break;
    }

    case ComponentType::Npn:
    case ComponentType::Pnp: {
        const bool pnp = comp.type == ComponentType::Pnp;
        // 基极引线 + 竖条
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-8.0, 0.0));
        p->setPen(symbolPen(color, 3.0));
        p->drawLine(QPointF(-8.0, -15.0), QPointF(-8.0, 15.0));
        p->setPen(pen);
        // 集电极 / 发射极引线
        p->drawLine(QPointF(30.0, -30.0), QPointF(30.0, -14.0));
        p->drawLine(QPointF(30.0, -14.0), QPointF(-8.0, -7.0));
        p->drawLine(QPointF(30.0, 30.0), QPointF(30.0, 14.0));
        p->drawLine(QPointF(30.0, 14.0), QPointF(-8.0, 7.0));

        // 箭头：NPN 指向发射极（朝外），PNP 指向基极（朝内）
        QPolygonF arrow;
        if (pnp) {
            arrow << QPointF(-8.0, 7.0) << QPointF(6.0, 3.4) << QPointF(6.0, 10.6);
        } else {
            arrow << QPointF(30.0, 14.0) << QPointF(16.0, 7.4) << QPointF(16.0, 14.6);
        }
        p->setBrush(color);
        p->drawPolygon(arrow);
        p->setBrush(Qt::NoBrush);
        break;
    }

    case ComponentType::Nmos:
    case ComponentType::Pmos:
    case ComponentType::Igbt: {
        const bool pChannel = comp.type == ComponentType::Pmos;
        // 栅极引线 + 栅极板
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-17.0, 0.0));
        p->setPen(symbolPen(color, 2.6));
        p->drawLine(QPointF(-17.0, -13.0), QPointF(-17.0, 13.0));
        // 沟道：三段短竖线
        p->setPen(symbolPen(color, 3.0));
        for (int i = -1; i <= 1; ++i) {
            const double y = i * 9.0;
            p->drawLine(QPointF(-9.0, y - 4.5), QPointF(-9.0, y + 4.5));
        }
        p->setPen(pen);
        // 漏极 / 源极引线
        p->drawLine(QPointF(30.0, -30.0), QPointF(30.0, -14.0));
        p->drawLine(QPointF(30.0, -14.0), QPointF(-9.0, -9.0));
        p->drawLine(QPointF(30.0, 30.0), QPointF(30.0, 14.0));
        p->drawLine(QPointF(30.0, 14.0), QPointF(-9.0, 9.0));

        // 箭头：N 沟道指向沟道，P 沟道背离沟道
        QPolygonF arrow;
        if (pChannel) {
            arrow << QPointF(6.0, 9.0) << QPointF(-6.0, 5.0) << QPointF(-6.0, 13.0);
        } else {
            arrow << QPointF(-9.0, 9.0) << QPointF(4.0, 5.0) << QPointF(4.0, 13.0);
        }
        p->setBrush(color);
        p->drawPolygon(arrow);
        p->setBrush(Qt::NoBrush);

        if (comp.type == ComponentType::Igbt) {
            // 输出侧的二极管：表示 IGBT 的导通压降
            p->setPen(symbolPen(color, 1.6));
            const double x = 18.0;
            QPolygonF tri;
            tri << QPointF(x - 5.0, 22.0) << QPointF(x + 5.0, 22.0) << QPointF(x, 13.0);
            p->setBrush(color);
            p->drawPolygon(tri);
            p->setBrush(Qt::NoBrush);
            p->drawLine(QPointF(x - 5.0, 11.0), QPointF(x + 5.0, 11.0));
        }
        break;
    }

    case ComponentType::Transformer: {
        // 三绕组：左边初级（1、2）与辅助绕组（4、5），右边次级（8、6）；单绕组只有一组
        const bool dual = comp.transformerKind == TransformerKind::PrimaryAuxSecondary;
        p->setPen(symbolPen(color, 1.4));

        const auto lead = [&](const QPointF& from, const QPointF& to) {
            p->drawLine(from, to);
        };

        if (dual) {
            // 初级（y −50 … −20）
            lead(QPointF(-30.0, -50.0), QPointF(-16.0, -50.0));
            lead(QPointF(-16.0, -50.0), QPointF(-16.0, -20.0));
            lead(QPointF(-16.0, -20.0), QPointF(-30.0, -20.0));
            // 辅助绕组（y 20 … 50）
            lead(QPointF(-30.0, 20.0), QPointF(-16.0, 20.0));
            lead(QPointF(-16.0, 20.0), QPointF(-16.0, 50.0));
            lead(QPointF(-16.0, 50.0), QPointF(-30.0, 50.0));
            // 次级（y −50 … 50）
            lead(QPointF(30.0, -50.0), QPointF(16.0, -50.0));
            lead(QPointF(16.0, -50.0), QPointF(16.0, 50.0));
            lead(QPointF(16.0, 50.0), QPointF(30.0, 50.0));

            p->setPen(symbolPen(color, 2.0));
            for (int i = 0; i < 5; ++i) {
                p->drawArc(QRectF(-16.0, -50.0 + i * 6.0, 8.0, 6.0), 90 * 16, 180 * 16);
                p->drawArc(QRectF(-16.0, 20.0 + i * 6.0, 8.0, 6.0), 90 * 16, 180 * 16);
            }
            for (int i = 0; i < 10; ++i)
                p->drawArc(QRectF(8.0, -50.0 + i * 10.0, 8.0, 10.0), 90 * 16, 180 * 16);

            // 铁芯：两组绕组各一对，次级整条
            p->setPen(symbolPen(color, 2.4));
            for (const double top : {-50.0, 20.0}) {
                p->drawLine(QPointF(-2.0, top), QPointF(-2.0, top + 30.0));
                p->drawLine(QPointF(2.0, top), QPointF(2.0, top + 30.0));
            }
        } else {
            p->drawLine(QPointF(-30.0, -10.0), QPointF(-16.0, -10.0));
            p->drawLine(QPointF(-16.0, -10.0), QPointF(-16.0, 10.0));
            p->drawLine(QPointF(-16.0, 10.0), QPointF(-30.0, 10.0));
            p->drawLine(QPointF(30.0, -10.0), QPointF(16.0, -10.0));
            p->drawLine(QPointF(16.0, -10.0), QPointF(16.0, 10.0));
            p->drawLine(QPointF(16.0, 10.0), QPointF(30.0, 10.0));

            p->setPen(symbolPen(color, 2.0));
            for (int i = 0; i < 3; ++i) {
                const double y = -9.0 + i * 6.0;
                p->drawArc(QRectF(-16.0, y, 8.0, 6.0), 90 * 16, 180 * 16);
                p->drawArc(QRectF(8.0, y, 8.0, 6.0), 90 * 16, 180 * 16);
            }
            p->setPen(symbolPen(color, 2.4));
            p->drawLine(QPointF(-2.0, -12.0), QPointF(-2.0, 12.0));
            p->drawLine(QPointF(2.0, -12.0), QPointF(2.0, 12.0));
        }

        // 同名端圆点 + 绕组编号（数字要抵消旋转与镜像，始终正着写）
        // 次级同名端在下方时，圆点画到下端（文字仍放在上端，避免压住符号）
        const bool inverted = comp.secondaryInverted;
        QVector<QPair<QPointF, QString>> marks;
        QVector<QPointF> dots;
        if (dual) {
            // 编号与属性面板的 N1 / N2 / N3 对应
            marks = {{QPointF(-22.0, -60.0), QStringLiteral("1")},
                     {QPointF(-22.0, 10.0), QStringLiteral("2")},
                     {QPointF(22.0, -60.0), QStringLiteral("3")}};
            // 初级 1 的同名端在下端（与辅助绕组反相）
            dots = {QPointF(-22.0, -20.0), QPointF(-22.0, 20.0),
                    inverted ? QPointF(22.0, 50.0) : QPointF(22.0, -50.0)};
        } else {
            marks = {{QPointF(-22.0, -21.0), QStringLiteral("1")},
                     {QPointF(22.0, -21.0), QStringLiteral("2")}};
            dots = {QPointF(-22.0, -10.0),
                    inverted ? QPointF(22.0, 10.0) : QPointF(22.0, -10.0)};
        }
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        for (const QPointF& dot : dots)
            p->drawEllipse(dot, 2.4, 2.4);
        p->setBrush(Qt::NoBrush);

        QFont f = p->font();
        f.setPointSizeF(6.0);
        f.setBold(true);
        p->setFont(f);
        p->setPen(color);
        for (const auto& mark : marks) {
            p->save();
            p->translate(mark.first);
            p->rotate(-double(rotation));
            p->scale(mirrorX ? -1.0 : 1.0, mirrorY ? -1.0 : 1.0);
            p->drawText(QRectF(-8.0, -5.0, 16.0, 10.0), Qt::AlignCenter, mark.second);
            p->restore();
        }
        break;
    }

    case ComponentType::Ob2362: {
        // SOT23-6：上 VCC、下 GND、左 FB/PRT、右 GATE/CS
        p->setPen(symbolPen(color, 1.5));
        p->setBrush(Qt::NoBrush);
        p->drawRect(QRectF(-22.0, -26.0, 44.0, 52.0));

        p->setPen(pen);
        p->drawLine(QPointF(0.0, -30.0), QPointF(0.0, -26.0));   // VCC
        p->drawLine(QPointF(0.0, 26.0), QPointF(0.0, 30.0));     // GND
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-22.0, 0.0));   // FB
        p->drawLine(QPointF(-30.0, 20.0), QPointF(-22.0, 20.0)); // PRT
        p->drawLine(QPointF(22.0, -10.0), QPointF(30.0, -10.0)); // GATE
        p->drawLine(QPointF(22.0, 10.0), QPointF(30.0, 10.0));   // CS

        QFont f = p->font();
        f.setPointSizeF(5.0);
        f.setBold(false);
        p->setFont(f);
        p->setPen(color);
        p->drawText(QRectF(-20.0, -24.0, 40.0, 8.0), Qt::AlignHCenter | Qt::AlignTop,
                    QStringLiteral("VCC"));
        p->drawText(QRectF(-20.0, 18.0, 40.0, 7.0), Qt::AlignHCenter | Qt::AlignBottom,
                    QStringLiteral("GND"));
        p->drawText(QRectF(-20.0, -6.0, 18.0, 8.0), Qt::AlignLeft | Qt::AlignVCenter,
                    QStringLiteral("FB"));
        p->drawText(QRectF(-20.0, 6.0, 18.0, 8.0), Qt::AlignLeft | Qt::AlignVCenter,
                    QStringLiteral("PRT"));
        p->drawText(QRectF(2.0, -16.0, 19.0, 8.0), Qt::AlignRight | Qt::AlignVCenter,
                    QStringLiteral("GATE"));
        p->drawText(QRectF(2.0, 4.0, 19.0, 8.0), Qt::AlignRight | Qt::AlignVCenter,
                    QStringLiteral("CS"));
        break;
    }

    case ComponentType::Tl431: {
        // 可调基准：三角形（阴极朝上）+ 阴极横杠 + 左侧参考端
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-14.0, 0.0)); // 参考端引线
        p->setPen(pen);
        p->drawLine(QPointF(0.0, -30.0), QPointF(0.0, -12.0)); // 阴极
        p->drawLine(QPointF(0.0, 12.0), QPointF(0.0, 30.0));   // 阳极
        QPolygonF tri;
        tri << QPointF(-11.0, 10.0) << QPointF(11.0, 10.0) << QPointF(0.0, -10.0);
        p->setBrush(Qt::NoBrush);
        p->drawPolygon(tri);
        p->setPen(symbolPen(color, 2.6));
        p->drawLine(QPointF(-11.0, -10.0), QPointF(11.0, -10.0)); // 阴极横杠
        // 参考端接到三角形侧边，末端画一个小折角表示「取样」
        p->setPen(pen);
        p->drawLine(QPointF(-14.0, 0.0), QPointF(-14.0, 0.0));
        QPolygonF ref;
        ref << QPointF(-14.0, 0.0) << QPointF(-6.0, -3.0) << QPointF(-6.0, 3.0);
        p->setBrush(color);
        p->drawPolygon(ref);
        p->setBrush(Qt::NoBrush);
        break;
    }

    case ComponentType::Optocoupler: {
        // 参考常见画法：外框 + 左侧输入 LED + 两支斜向箭头 + 右侧光敏三极管
        p->setPen(symbolPen(color, 1.4));
        p->setBrush(Qt::NoBrush);
        p->drawRect(QRectF(-24.0, -19.0, 48.0, 38.0));

        // 输入侧 LED：阳极在上（引脚 0），阴极在下（引脚 1），三角形朝下
        p->setPen(pen);
        p->drawLine(QPointF(-30.0, -10.0), QPointF(-16.0, -10.0));
        p->drawLine(QPointF(-16.0, -10.0), QPointF(-16.0, -5.0));
        p->drawLine(QPointF(-16.0, 5.0), QPointF(-16.0, 10.0));
        p->drawLine(QPointF(-16.0, 10.0), QPointF(-30.0, 10.0));
        QPolygonF led;
        led << QPointF(-21.0, -5.0) << QPointF(-11.0, -5.0) << QPointF(-16.0, 5.0);
        p->setBrush(color);
        p->drawPolygon(led);
        p->setBrush(Qt::NoBrush);
        p->setPen(symbolPen(color, 2.2));
        p->drawLine(QPointF(-21.0, 5.0), QPointF(-11.0, 5.0));

        // 两支斜向右下的箭头：光从 LED 射向三极管
        p->setPen(symbolPen(color, 1.5));
        for (int i = 0; i < 2; ++i) {
            const QPointF from(-6.0 + i * 1.5, -7.0 + i * 7.0);
            const QPointF to(5.0 + i * 1.5, 1.0 + i * 7.0);
            p->drawLine(from, to);
            const QPointF dir = to - from;
            const double len = std::hypot(dir.x(), dir.y());
            if (len > 0.1) {
                const QPointF unit = dir / len;
                const QPointF normal(-unit.y(), unit.x());
                QPolygonF head;
                head << to << (to - unit * 4.2 + normal * 2.4)
                     << (to - unit * 4.2 - normal * 2.4);
                p->setBrush(color);
                p->drawPolygon(head);
                p->setBrush(Qt::NoBrush);
            }
        }

        // 输出侧光敏三极管：集电极在上（引脚 2），发射极在下（引脚 3）
        p->setPen(symbolPen(color, 2.4));
        p->drawLine(QPointF(12.0, -8.0), QPointF(12.0, 8.0)); // 基极板（光照射到这里）
        p->setPen(pen);
        p->drawLine(QPointF(30.0, -10.0), QPointF(22.0, -10.0));
        p->drawLine(QPointF(22.0, -10.0), QPointF(12.0, -5.0));
        p->drawLine(QPointF(30.0, 10.0), QPointF(22.0, 10.0));
        p->drawLine(QPointF(22.0, 10.0), QPointF(12.0, 5.0));
        // 发射极箭头：指向外（朝引脚 3）
        QPolygonF arrow;
        arrow << QPointF(22.0, 10.0) << QPointF(14.0, 9.0) << QPointF(18.0, 4.0);
        p->setBrush(color);
        p->drawPolygon(arrow);
        p->setBrush(Qt::NoBrush);
        break;
    }

    case ComponentType::Switch: {
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-12.0, 0.0));
        p->drawLine(QPointF(12.0, 0.0), QPointF(30.0, 0.0));
        p->setBrush(color);
        p->drawEllipse(QPointF(-12.0, 0.0), 2.6, 2.6);
        p->drawEllipse(QPointF(12.0, 0.0), 2.6, 2.6);
        p->setBrush(Qt::NoBrush);
        if (comp.closed)
            p->drawLine(QPointF(-12.0, 0.0), QPointF(12.0, 0.0));
        else
            p->drawLine(QPointF(-12.0, 0.0), QPointF(9.0, -12.0));
        break;
    }

    case ComponentType::PushButton: {
        // 两个触点 + 一根横杠（按下去才搭上）+ 顶上的按杆
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-12.0, 0.0));
        p->drawLine(QPointF(12.0, 0.0), QPointF(30.0, 0.0));
        p->setBrush(color);
        p->drawEllipse(QPointF(-12.0, 0.0), 2.6, 2.6);
        p->drawEllipse(QPointF(12.0, 0.0), 2.6, 2.6);
        p->setBrush(Qt::NoBrush);

        // 横杠的位置直接反映当前通断：搭在触点上 = 导通，抬起来 = 断开
        const double barY = comp.closed ? 0.0 : -9.0;
        p->setPen(symbolPen(color, 2.6));
        p->drawLine(QPointF(-13.0, barY), QPointF(13.0, barY));
        // 按杆：从横杠中间往上，顶端一小段横杠表示手指按的地方
        p->drawLine(QPointF(0.0, barY), QPointF(0.0, -22.0));
        p->setPen(symbolPen(color, 2.4));
        p->drawLine(QPointF(-8.0, -22.0), QPointF(8.0, -22.0));
        break;
    }

    case ComponentType::VoltageSource: {
        if (comp.function == SourceFunction::Constant) {
            // 直流：画电池（长片为正、短片为负），再配 + / −
            p->setPen(symbolPen(color, 1.6));
            p->drawLine(QPointF(-30.0, 0.0), QPointF(-3.5, 0.0));
            p->drawLine(QPointF(3.5, 0.0), QPointF(30.0, 0.0));
            p->setPen(symbolPen(color, 1.8));
            p->drawLine(QPointF(-3.5, -11.0), QPointF(-3.5, 11.0));   // 长片：正极
            p->setPen(symbolPen(color, 4.0));
            p->drawLine(QPointF(3.5, -6.0), QPointF(3.5, 6.0));       // 短片：负极
        } else {
            // 正弦 / 脉冲：只画引线与圆圈，波形图案由 overlay 画（保持正立）
            p->drawLine(QPointF(-30.0, 0.0), QPointF(-14.0, 0.0));
            p->drawLine(QPointF(14.0, 0.0), QPointF(30.0, 0.0));
            p->setBrush(Theme::symbolFill());
            p->drawEllipse(QPointF(0.0, 0.0), 14.0, 14.0);
            p->setBrush(Qt::NoBrush);
        }
        // 极性标记与万用表共用同一套位置
        drawPinPolarity(p, rotation, mirrorX, mirrorY);
        break;
    }

    case ComponentType::SignalGenerator: {
        // 仪器外观：圆角矩形机身 + 圈内波形。用方形而不是圆圈，
        // 是为了和正弦电压源（同样的圆圈 + 正弦）一眼分开。
        // 机身宽度收到 ±13.5：极性标记画在 ±21（文字宽 14，从 14 起），
        // 机身再宽就会把 + / − 压住
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-13.5, 0.0));
        p->drawLine(QPointF(13.5, 0.0), QPointF(30.0, 0.0));
        p->setBrush(Theme::symbolFill());
        p->drawRoundedRect(QRectF(-13.5, -12.0, 27.0, 24.0), 5.0, 5.0);
        p->setBrush(Qt::NoBrush);
        drawPinPolarity(p, rotation, mirrorX, mirrorY);
        break;
    }

    case ComponentType::CurrentSource: {
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-14.0, 0.0));
        p->drawLine(QPointF(14.0, 0.0), QPointF(30.0, 0.0));
        p->setBrush(Theme::symbolFill());
        p->drawEllipse(QPointF(0.0, 0.0), 14.0, 14.0);
        p->setBrush(Qt::NoBrush);

        // 圈里画一支「顺着导线」的实心箭头：箭头指向就是电流的参考方向
        // （求解器里电流从引脚 0 经元件流向引脚 1，所以 0° 时朝右）
        p->setPen(symbolPen(color, 2.2));
        p->drawLine(QPointF(-7.5, 0.0), QPointF(1.0, 0.0));
        QPolygonF head;
        head << QPointF(10.0, 0.0) << QPointF(1.0, -5.2) << QPointF(1.0, 5.2);
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        p->drawPolygon(head);
        p->setBrush(Qt::NoBrush);
        break;
    }

    case ComponentType::MeasureProbe: {
        // 测量探针 = 黄框 + 下面一个小圆球（接点）。不再画圆圈字母 ——
        // 框本身就是它的样子，多一个圈反而抢眼
        // 只有接点小球：引线与黄框由调用方在「未旋转」的坐标系里画，
        // 否则框转了、字还是正的，就错位了
        p->setPen(symbolPen(color, 1.6));
        p->setBrush(Theme::symbolFill());
        p->drawEllipse(QPointF(0.0, 0.0), 3.5, 3.5);
        p->setBrush(Qt::NoBrush);
        break;
    }

    case ComponentType::Probe: {
        p->setPen(symbolPen(color, 1.6));
        p->drawLine(QPointF(0.0, 0.0), QPointF(0.0, -8.0));
        p->setBrush(Theme::symbolFill());
        p->drawEllipse(QPointF(0.0, -16.0), 7.0, 7.0);
        p->setBrush(Qt::NoBrush);
        QFont font = p->font();
        font.setPointSizeF(7.0);
        font.setBold(true);
        p->setFont(font);
        p->save();
        p->translate(0.0, -16.0);
        p->rotate(-double(rotation));
        p->scale(mirrorX ? -1.0 : 1.0, mirrorY ? -1.0 : 1.0);
        p->drawText(QRectF(-7.0, -6.0, 14.0, 12.0), Qt::AlignCenter, QStringLiteral("V"));
        p->restore();
        break;
    }

    case ComponentType::CurrentProbe: {
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-12.0, 0.0));
        p->drawLine(QPointF(12.0, 0.0), QPointF(30.0, 0.0));
        p->setBrush(Theme::symbolFill());
        p->drawEllipse(QPointF(0.0, 0.0), 12.0, 12.0);
        p->setBrush(Qt::NoBrush);
        QFont font = p->font();
        font.setPointSizeF(7.5);
        font.setBold(true);
        p->setFont(font);
        p->save();
        p->translate(0.0, 0.0);
        p->rotate(-double(rotation));
        p->scale(mirrorX ? -1.0 : 1.0, mirrorY ? -1.0 : 1.0);
        p->drawText(QRectF(-8.0, -8.0, 16.0, 16.0), Qt::AlignCenter, QStringLiteral("A"));
        p->restore();
        // 极性：0 号引脚是「+」，电流沿 + → − 为正（与万用表、电压源同一套位置）
        drawPinPolarity(p, rotation, mirrorX, mirrorY);
        break;
    }

    case ComponentType::Multimeter: {
        // 圆形表盘（双圈，与电压/电流测试点区分），中间是档位字母
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-14.0, 0.0));
        p->drawLine(QPointF(14.0, 0.0), QPointF(30.0, 0.0));
        p->setBrush(Theme::symbolFill());
        p->drawEllipse(QPointF(0.0, 0.0), 15.0, 15.0);
        p->setBrush(Qt::NoBrush);
        p->setPen(symbolPen(color, 1.0));
        p->drawEllipse(QPointF(0.0, 0.0), 12.0, 12.0);
        p->setPen(symbolPen(color, 1.8));

        const QString letter = multimeterModeLetter(comp.multimeterMode);
        QFont font = p->font();
        // 交流档的 "V~" / "A~" 是两个字符，字号收小一点才塞得进表盘
        font.setPointSizeF(letter.size() > 1 ? 6.5 : 8.0);
        font.setBold(true);
        p->setFont(font);

        // 表盘里的档位字母：逆着旋转摆正
        p->save();
        p->rotate(-double(rotation));
        p->scale(mirrorX ? -1.0 : 1.0, mirrorY ? -1.0 : 1.0);
        p->drawText(QRectF(-11.0, -9.0, 22.0, 18.0), Qt::AlignCenter, letter);
        p->restore();

        // 引脚极性：0 号引脚是「+」（红表笔），1 号是「−」（COM / 黑表笔）
        drawPinPolarity(p, rotation, mirrorX, mirrorY);
        break;
    }

    case ComponentType::NetLabel: {
        // 网络标签的旗标跟着元件转，但文字永远不倒着写：
        //   0°   旗标向右、文字横排
        //   90°  旗标向上、文字竖排（自下而上）
        //   180° 旗标向左、文字仍然横排（只翻位置不翻字）
        //   270° 旗标向下、文字竖排
        const QString name = comp.netLabel.isEmpty() ? QStringLiteral("NET") : comp.netLabel;
        QFont font = p->font();
        font.setPointSizeF(7.5);
        font.setBold(true);
        p->setFont(font);
        const QFontMetricsF fm(font);
        const qreal textW = fm.horizontalAdvance(name);
        const qreal h = 16.0;
        const qreal x0 = 6.0;
        const qreal w = textW + 12.0;

        const int rot = ((rotation % 360) + 360) % 360;
        // 旗标朝向（逆时针）：0° 右、90° 上、180° 左、270° 下
        // 整个标签当作一块整体转：90° 时文字自下而上，270° 时自上而下，
        // 这样四个角度看着都是「跟着元件转」，而不是文字一直横着拧。
        const bool positive = (rot != 180); // 180° 时画在 -x
        const qreal dx = positive ? 1.0 : -1.0;
        const qreal bx = positive ? x0 : -(x0 + w);

        p->save();
        // 元件整体是按 painter->rotate(+rotation) 转的，而且「旋转」动作给的是
        // rotation-90（逆时针），于是 R=270 才是「转一次」。旗标必须跟着同一套
        // 变换走：R=90 朝下、R=270 朝上，否则和别的元件转起来方向相反。
        p->rotate(-double(rotation)); // 先抵消元件自身的旋转
        if (rot == 90)
            p->rotate(90.0); // 顺时针竖起（文字自上而下）
        else if (rot == 270)
            p->rotate(-90.0); // 逆时针竖起（文字自下而上）
        if (mirrorX)
            p->scale(-1.0, 1.0);

        p->drawLine(QPointF(0.0, 0.0), QPointF(dx * x0, 0.0));
        p->setBrush(Theme::symbolFill());
        p->drawRoundedRect(QRectF(bx, -h / 2.0, w, h), 3.0, 3.0);
        p->setBrush(Qt::NoBrush);
        p->drawText(QRectF(bx + 6.0, -h / 2.0, textW + 1.0, h),
                    Qt::AlignVCenter | Qt::AlignLeft, name);
        p->restore();
        break;
    }

    case ComponentType::Ground: {
        p->drawLine(QPointF(0.0, 0.0), QPointF(0.0, 13.0));
        if (comp.groundKind == GroundKind::Earth) {
            // 保护地/大地：一条横杠 + 三道斜线
            p->setPen(symbolPen(color, 2.6));
            p->drawLine(QPointF(-11.0, 13.0), QPointF(11.0, 13.0));
            p->setPen(symbolPen(color, 2.2));
            for (int i = 0; i < 3; ++i) {
                const double x = -8.0 + i * 8.0;
                p->drawLine(QPointF(x, 15.0), QPointF(x - 3.0, 22.0));
            }
            break;
        }
        p->setPen(symbolPen(color, 2.6));
        p->drawLine(QPointF(-11.0, 13.0), QPointF(11.0, 13.0));
        if (comp.groundKind == GroundKind::Agnd) {
            // 模拟地：两横杠 + 一个小三角（空心）
            p->drawLine(QPointF(-7.0, 18.0), QPointF(7.0, 18.0));
            QPolygonF tri;
            tri << QPointF(-4.0, 21.0) << QPointF(4.0, 21.0) << QPointF(0.0, 26.0);
            p->setBrush(Qt::NoBrush);
            p->drawPolygon(tri);
        } else if (comp.groundKind == GroundKind::Dgnd) {
            // 数字地：三横杠，最下面一杠画成虚线状（两段）
            p->drawLine(QPointF(-7.0, 18.0), QPointF(7.0, 18.0));
            p->drawLine(QPointF(-5.0, 23.0), QPointF(-1.0, 23.0));
            p->drawLine(QPointF(1.0, 23.0), QPointF(5.0, 23.0));
        } else if (comp.groundKind == GroundKind::Pgnd) {
            // 功率地：三横杠加粗
            p->setPen(symbolPen(color, 3.4));
            p->drawLine(QPointF(-7.0, 18.0), QPointF(7.0, 18.0));
            p->drawLine(QPointF(-3.0, 23.0), QPointF(3.0, 23.0));
        } else {
            p->drawLine(QPointF(-7.0, 18.0), QPointF(7.0, 18.0));
            p->drawLine(QPointF(-3.0, 23.0), QPointF(3.0, 23.0));
        }
        break;
    }
    }

    if (showPins)
        drawPinDots(p, comp, selected);

    p->restore();
}

QIcon svgIcon(const QString& path, bool forceFallback)
{
    if (!forceFallback) {
        QIcon icon(path);
        // 光看 isNull() 不够：没有 SVG 图标插件时 QIcon 可能既不为空、又渲染不出
        // 任何像素（pixmap 尺寸为 0）。两种情况下都要走下面的兜底。
        if (!icon.isNull() && !icon.pixmap(24, 24).isNull()
            && icon.pixmap(24, 24).width() > 0)
            return icon;
    }

    QIcon icon;
    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        return icon;

    for (int size : {16, 22, 24, 32, 48, 64, 128}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&painter);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

QIcon iconForDarkSurface(const QIcon& source, int size)
{
    if (source.isNull())
        return source;

    const qreal ratio = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    QImage image = source.pixmap(QSize(size, size), ratio).toImage().convertToFormat(QImage::Format_ARGB32);
    if (image.isNull())
        return source;

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor color = image.pixelColor(x, y);
            if (color.alpha() == 0)
                continue;
            float h = 0.0F;
            float s = 0.0F;
            float l = 0.0F;
            float a = 0.0F;
            color.getHslF(&h, &s, &l, &a);
            if (s < 0.25F) {
                // 中性色：深浅对调（#334155 → 浅灰、#e2e8f0 → 深灰）
                l = 1.0F - l;
            } else {
                // 有彩色：保住色相，把亮度抬到深底上看得清的位置
                l = qBound(0.0F, l * 0.45F + 0.45F, 0.85F);
            }
            color.setHslF(h < 0 ? 0 : h, s, l, a);
            image.setPixelColor(x, y, color);
        }
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(ratio);
    return QIcon(pixmap);
}

QIcon tintIcon(const QIcon& source, const QColor& color, int size)
{
    if (source.isNull())
        return source;

    const qreal ratio = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    QPixmap pixmap = source.pixmap(QSize(size, size), ratio);
    if (pixmap.isNull())
        return source;
    pixmap.setDevicePixelRatio(ratio);

    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    painter.end();

    return QIcon(pixmap);
}

QIcon themedSvgIcon(const QString& path, int size, const QColor& color)
{
    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        return QIcon();

    const qreal ratio = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0, 0, size, size));
    // 用源像素的 alpha 当蒙版，整体刷成主题色
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(QRectF(0, 0, size, size), color.isValid() ? color : Theme::symbol());
    painter.end();

    return QIcon(pixmap);
}

QIcon componentIcon(ComponentType type, const QSize& size)
{
    Component comp;
    comp.type = type;
    comp.value = 1000.0;
    comp.function = SourceFunction::Sine;
    comp.closed = true;
    return componentIcon(comp, size);
}

QIcon componentIcon(const Component& comp, const QSize& size)
{
    QPixmap pm(size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 按符号实际占的范围缩放居中：测量探针多了个读数框，用固定比例会被切掉
    const QRectF bounds = symbolBounds(comp);
    const double sx = size.width() / bounds.width();
    const double sy = size.height() / bounds.height();
    const double scale = std::min(sx, sy);
    p.translate(size.width() / 2.0, size.height() / 2.0);
    p.scale(scale, scale);
    p.translate(-bounds.center().x(), -bounds.center().y());
    paintSymbol(&p, comp, false, false);
    paintVoltageSourceOverlay(&p, comp, Theme::symbol());
    if (comp.type == ComponentType::MeasureProbe)
        paintProbeBox(&p, probeReadoutBox());
    p.end();

    return QIcon(pm);
}

} // namespace csm
