#include "ui/TitleBar.h"

#include "core/Language.h"
#include "ui/Theme.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

namespace csm {

namespace {
constexpr int kBarHeight = 34;
constexpr int kButtonWidth = 40;
} // namespace

// ---------------------------------------------------------------- 窗口按钮

WindowButton::WindowButton(Glyph glyph, QWidget* parent)
    : QAbstractButton(parent)
    , m_glyph(glyph)
{
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(kButtonWidth, kBarHeight);
    setToolTip(glyph == Glyph::Close ? CSM_TR("Close") : QString());
}

void WindowButton::setGlyph(Glyph glyph)
{
    if (m_glyph == glyph)
        return;
    m_glyph = glyph;
    update();
}

void WindowButton::enterEvent(QEnterEvent* event)
{
    QAbstractButton::enterEvent(event);
    update();
}

void WindowButton::leaveEvent(QEvent* event)
{
    QAbstractButton::leaveEvent(event);
    update();
}

void WindowButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 悬停底色：关闭键用红色（和系统一致），其余用次级底色
    if (underMouse() || isDown()) {
        const bool closing = m_glyph == Glyph::Close;
        QColor fill = closing ? QColor(0xe8, 0x11, 0x23) : Theme::surfaceAlt();
        if (closing)
            painter.setPen(Qt::NoPen);
        else
            painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(rect().adjusted(3, 5, -3, -5), 5, 5);
    }

    QColor stroke = Theme::textSecondary();
    if (m_glyph == Glyph::Close && (underMouse() || isDown()))
        stroke = QColor(Qt::white);

    QPen pen(stroke);
    pen.setWidthF(1.2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const QPointF center = rect().center() + QPointF(0, 0.5);
    const qreal half = 4.5;

    switch (m_glyph) {
    case Glyph::Minimize:
        painter.drawLine(QPointF(center.x() - half, center.y()),
                         QPointF(center.x() + half, center.y()));
        break;
    case Glyph::Maximize:
        painter.drawRect(QRectF(center.x() - half, center.y() - half, half * 2, half * 2));
        break;
    case Glyph::Restore: {
        const qreal offset = 1.8;
        painter.drawRect(QRectF(center.x() - half, center.y() - half + offset,
                                half * 2 - offset, half * 2 - offset));
        painter.drawLine(QPointF(center.x() - half + offset, center.y() - half),
                         QPointF(center.x() + half, center.y() - half));
        painter.drawLine(QPointF(center.x() + half, center.y() - half),
                         QPointF(center.x() + half, center.y() + half - offset));
        break;
    }
    case Glyph::Close:
        painter.drawLine(QPointF(center.x() - half, center.y() - half),
                         QPointF(center.x() + half, center.y() + half));
        painter.drawLine(QPointF(center.x() + half, center.y() - half),
                         QPointF(center.x() - half, center.y() + half));
        break;
    }
}

// ---------------------------------------------------------------- 标题栏

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("titleBar"));
    setFixedHeight(kBarHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    // 只要一截空白 + 三个按钮：窗口标题在任务栏里已经写着，顶栏不再重复
    row->addStretch(1);

    m_minimize = new WindowButton(WindowButton::Glyph::Minimize, this);
    m_minimize->setObjectName(QStringLiteral("windowMinimize"));
    m_minimize->setToolTip(CSM_TR("Minimize"));
    m_maximize = new WindowButton(WindowButton::Glyph::Maximize, this);
    m_maximize->setObjectName(QStringLiteral("windowMaximize"));
    m_maximize->setToolTip(CSM_TR("Maximize"));
    m_close = new WindowButton(WindowButton::Glyph::Close, this);
    m_close->setObjectName(QStringLiteral("windowClose"));
    m_close->setToolTip(CSM_TR("Close"));

    row->addWidget(m_minimize);
    row->addWidget(m_maximize);
    row->addWidget(m_close);

    connect(m_minimize, &QAbstractButton::clicked, this, &TitleBar::minimizeRequested);
    connect(m_maximize, &QAbstractButton::clicked, this, &TitleBar::maximizeRequested);
    connect(m_close, &QAbstractButton::clicked, this, &TitleBar::closeRequested);
}

void TitleBar::setMaximized(bool maximized)
{
    m_maximize->setGlyph(maximized ? WindowButton::Glyph::Restore
                                   : WindowButton::Glyph::Maximize);
    m_maximize->setToolTip(maximized ? CSM_TR("Restore") : CSM_TR("Maximize"));
}

void TitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // 优先交给窗口管理器（多显示器、缩放都对）；它不支持时再自己搬
        QWindow* handle = window()->windowHandle();
        if (handle && handle->startSystemMove()) {
            event->accept();
            return;
        }
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        window()->move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void TitleBar::mouseReleaseEvent(QMouseEvent* event)
{
    m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit maximizeRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace csm
