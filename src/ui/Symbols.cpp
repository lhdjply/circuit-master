#include "ui/Symbols.h"

#include <QFont>
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

void drawPinDots(QPainter* p, const Component& comp, bool selected)
{
    const QVector<QPointF> offsets = pinOffsets(comp);
    if (offsets.isEmpty())
        return;
    const QColor color = selected ? theme::selected : theme::pin;
    p->setPen(QPen(color, 1.3));
    p->setBrush(theme::symbolFill);
    for (const QPointF& off : offsets)
        p->drawEllipse(off, 2.8, 2.8);
}

void drawVoltageSourceInside(QPainter* p, const Component& comp, const QColor& color)
{
    p->setPen(symbolPen(color, 1.8));
    p->setBrush(Qt::NoBrush);

    if (comp.function == SourceFunction::Sine) {
        QPainterPath wave;
        wave.moveTo(-7.0, 0.0);
        wave.cubicTo(-5.0, -7.0, -2.0, -7.0, 0.0, 0.0);
        wave.cubicTo(2.0, 7.0, 5.0, 7.0, 7.0, 0.0);
        p->drawPath(wave);
    } else if (comp.function == SourceFunction::Pulse) {
        QPainterPath wave;
        wave.moveTo(-7.0, 2.0);
        wave.lineTo(-4.0, 2.0);
        wave.lineTo(-4.0, -4.0);
        wave.lineTo(1.0, -4.0);
        wave.lineTo(1.0, 2.0);
        wave.lineTo(7.0, 2.0);
        p->drawPath(wave);
    } else {
        // 直流：画 + / −
        p->drawLine(QPointF(-7.0, -4.0), QPointF(-2.0, -4.0));
        p->drawLine(QPointF(-4.5, -6.5), QPointF(-4.5, -1.5));
        p->drawLine(QPointF(2.0, 4.0), QPointF(7.0, 4.0));
    }
}

} // namespace

QPainterPath symbolShape(ComponentType type)
{
    QPainterPath path;
    if (type == ComponentType::Ground) {
        path.addRoundedRect(QRectF(-14.0, -6.0, 28.0, 34.0), 4.0, 4.0);
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
    if (type == ComponentType::Probe) {
        path.addEllipse(QPointF(0.0, -15.0), 8.0, 8.0);
        path.addRect(QRectF(-4.0, -10.0, 8.0, 12.0));
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
    QColor color = selected ? theme::selected : theme::symbol;
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
        p->setBrush(theme::symbolFill);
        p->drawPolygon(triangle);
        p->setBrush(Qt::NoBrush);
        // 同相输入 +
        p->drawLine(QPointF(-13.0, -12.0), QPointF(-7.0, -12.0));
        p->drawLine(QPointF(-10.0, -15.0), QPointF(-10.0, -9.0));
        // 反相输入 −
        p->drawLine(QPointF(-13.0, 12.0), QPointF(-7.0, 12.0));
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

    case ComponentType::VoltageSource: {
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-14.0, 0.0));
        p->drawLine(QPointF(14.0, 0.0), QPointF(30.0, 0.0));
        p->setBrush(theme::symbolFill);
        p->drawEllipse(QPointF(0.0, 0.0), 14.0, 14.0);
        p->setBrush(Qt::NoBrush);
        drawVoltageSourceInside(p, comp, color);
        break;
    }

    case ComponentType::CurrentSource: {
        p->drawLine(QPointF(-30.0, 0.0), QPointF(-14.0, 0.0));
        p->drawLine(QPointF(14.0, 0.0), QPointF(30.0, 0.0));
        p->setBrush(theme::symbolFill);
        p->drawEllipse(QPointF(0.0, 0.0), 14.0, 14.0);
        p->setBrush(Qt::NoBrush);
        p->setPen(symbolPen(color, 1.8));
        p->drawLine(QPointF(0.0, 8.0), QPointF(0.0, -5.0));
        p->drawLine(QPointF(0.0, -9.0), QPointF(-4.0, -3.0));
        p->drawLine(QPointF(0.0, -9.0), QPointF(4.0, -3.0));
        break;
    }

    case ComponentType::Probe: {
        p->drawLine(QPointF(0.0, 0.0), QPointF(0.0, -8.0));
        p->setBrush(theme::symbolFill);
        p->drawEllipse(QPointF(0.0, -16.0), 7.0, 7.0);
        p->setBrush(Qt::NoBrush);
        QFont font = p->font();
        font.setPointSizeF(6.0);
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
        p->setBrush(theme::symbolFill);
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

QIcon componentIcon(ComponentType type, const QSize& size)
{
    QPixmap pm(size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    Component comp;
    comp.type = type;
    comp.value = 1000.0;
    comp.function = SourceFunction::Sine;
    comp.closed = true;

    p.translate(size.width() / 2.0, size.height() / 2.0);
    if (type == ComponentType::Probe)
        p.translate(0.0, 8.0);
    const double sx = size.width() / 84.0;
    const double sy = size.height() / 54.0;
    p.scale(sx, sy);
    paintSymbol(&p, comp, false, false);
    p.end();

    return QIcon(pm);
}

} // namespace csm
