#include "ui/Symbols.h"

#include <QPainter>
#include <QPixmap>
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
    const QVector<QPointF> offsets = pinOffsets(comp.type);
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
    path.addRoundedRect(QRectF(-33.0, -16.0, 66.0, 32.0), 5.0, 5.0);
    path.addEllipse(QPointF(-30.0, 0.0), 5.0, 5.0);
    path.addEllipse(QPointF(30.0, 0.0), 5.0, 5.0);
    return path;
}

void paintSymbol(QPainter* p, const Component& comp, bool selected, bool showPins)
{
    const QColor color = selected ? theme::selected : theme::symbol;
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
        p->drawLine(QPointF(5.0, 0.0), QPointF(30.0, 0.0));
        p->setPen(symbolPen(color, 2.8));
        p->drawLine(QPointF(-5.0, -13.0), QPointF(-5.0, 13.0));
        p->drawLine(QPointF(5.0, -13.0), QPointF(5.0, 13.0));
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

    case ComponentType::Ground: {
        p->drawLine(QPointF(0.0, 0.0), QPointF(0.0, 13.0));
        p->setPen(symbolPen(color, 2.6));
        p->drawLine(QPointF(-11.0, 13.0), QPointF(11.0, 13.0));
        p->drawLine(QPointF(-7.0, 18.0), QPointF(7.0, 18.0));
        p->drawLine(QPointF(-3.0, 23.0), QPointF(3.0, 23.0));
        break;
    }
    }

    if (showPins)
        drawPinDots(p, comp, selected);

    p->restore();
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
    const double sx = size.width() / 84.0;
    const double sy = size.height() / 54.0;
    p.scale(sx, sy);
    paintSymbol(&p, comp, false, false);
    p.end();

    return QIcon(pm);
}

} // namespace csm
