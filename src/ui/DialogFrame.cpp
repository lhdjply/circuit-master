#include "ui/DialogFrame.h"

#include "core/Language.h"
#include "ui/Theme.h"
#include "ui/TitleBar.h"

#include <QAbstractButton>
#include <QDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWindow>

namespace csm {

namespace {

constexpr int kEdgeThickness = 5;

/// 一条拖拽边：按住就让窗口管理器改尺寸（X11 上是 _NET_WM_MOVERESIZE）
class ResizeEdge : public QWidget
{
public:
    ResizeEdge(QWidget* window, Qt::Edges edges, Qt::CursorShape cursor)
        : QWidget(window)
        , m_edges(edges)
    {
        setCursor(cursor);
        setAttribute(Qt::WA_NoMousePropagation);
        setMouseTracking(true);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (QWindow* handle = window()->windowHandle()) {
                handle->startSystemResize(m_edges);
                event->accept();
                return;
            }
        }
        QWidget::mousePressEvent(event);
    }

private:
    Qt::Edges m_edges;
};

/// 对话框顶栏：标题 + 关闭按钮，按住可拖动
class DialogHeader : public QWidget
{
public:
    DialogHeader(const QString& title, QWidget* dialog)
        : QWidget(dialog)
    {
        setObjectName(QStringLiteral("dialogHeader"));
        setFixedHeight(38);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(14, 0, 0, 0);
        row->setSpacing(0);

        auto* label = new QLabel(title, this);
        label->setObjectName(QStringLiteral("dialogHeaderTitle"));
        label->setAlignment(Qt::AlignCenter);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_label = label;
        row->addWidget(label, 1);

        auto* close = new WindowButton(WindowButton::Glyph::Close, this);
        close->setObjectName(QStringLiteral("dialogHeaderClose"));
        close->setToolTip(CSM_TR("Close"));
        QObject::connect(close, &QAbstractButton::clicked, this, [dialog] {
            if (auto* asDialog = qobject_cast<QDialog*>(dialog))
                asDialog->reject();
            else
                dialog->close();
        });
        row->addWidget(close, 0, Qt::AlignVCenter);
    }

public:
    void setLabel(const QString& text) { m_label->setText(text); }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            QWindow* handle = window()->windowHandle();
            if (handle && handle->startSystemMove()) {
                event->accept();
                return;
            }
            m_dragging = true;
            m_offset = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_dragging && (event->buttons() & Qt::LeftButton)) {
            window()->move(event->globalPosition().toPoint() - m_offset);
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        m_dragging = false;
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Theme::surface());
        painter.setPen(Theme::border());
        painter.drawLine(0, height() - 1, width(), height() - 1);
    }

private:
    QLabel* m_label = nullptr;
    bool m_dragging = false;
    QPoint m_offset;
};

} // namespace

ResizeGrips::ResizeGrips(QWidget* window)
    : QObject(window)
    , m_window(window)
{
    struct Spec {
        Qt::Edges edges;
        Qt::CursorShape cursor;
    };
    const QVector<Spec> specs = {
        {Qt::TopEdge, Qt::SizeVerCursor},
        {Qt::BottomEdge, Qt::SizeVerCursor},
        {Qt::LeftEdge, Qt::SizeHorCursor},
        {Qt::RightEdge, Qt::SizeHorCursor},
        {Qt::TopEdge | Qt::LeftEdge, Qt::SizeFDiagCursor},
        {Qt::BottomEdge | Qt::RightEdge, Qt::SizeFDiagCursor},
        {Qt::TopEdge | Qt::RightEdge, Qt::SizeBDiagCursor},
        {Qt::BottomEdge | Qt::LeftEdge, Qt::SizeBDiagCursor},
    };
    for (const Spec& spec : specs)
        m_edges.append(new ResizeEdge(window, spec.edges, spec.cursor));

    window->installEventFilter(this);
    layout();
}

bool ResizeGrips::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window
        && (event->type() == QEvent::Resize || event->type() == QEvent::WindowStateChange)) {
        layout();
    }
    return QObject::eventFilter(watched, event);
}

void ResizeGrips::layout()
{
    if (!m_window || m_edges.size() != 8)
        return;

    // 最大化 / 全屏时没有边可拖
    const bool resizable = !m_window->isMaximized() && !m_window->isFullScreen();
    for (QWidget* edge : m_edges) {
        edge->setVisible(resizable);
        if (resizable)
            edge->raise();
    }
    if (!resizable)
        return;

    const int w = m_window->width();
    const int h = m_window->height();
    const int t = kEdgeThickness;

    // 四条边先摆，四个角再压在上面（角的拖拽方向是斜的）
    m_edges[0]->setGeometry(t, 0, w - 2 * t, t);           // 上
    m_edges[1]->setGeometry(t, h - t, w - 2 * t, t);       // 下
    m_edges[2]->setGeometry(0, t, t, h - 2 * t);           // 左
    m_edges[3]->setGeometry(w - t, t, t, h - 2 * t);       // 右
    m_edges[4]->setGeometry(0, 0, t, t);                   // 左上
    m_edges[5]->setGeometry(w - t, h - t, t, t);           // 右下
    m_edges[6]->setGeometry(w - t, 0, t, t);               // 右上
    m_edges[7]->setGeometry(0, h - t, t, t);               // 左下
    for (QWidget* edge : m_edges)
        edge->raise();
}

namespace {
/// 去掉「— Circuit Master」这类后缀，顶栏里只留主体
QString shortTitle(const QString& title)
{
    QString text = title;
    const int dash = text.lastIndexOf(QStringLiteral(" — "));
    if (dash > 0)
        text.truncate(dash);
    return text;
}
} // namespace

QVBoxLayout* makeFramelessShell(QWidget& dialog, const QString& title,
                                const QMargins& contentMargins, int spacing,
                                bool followWindowTitle)
{
    dialog.setWindowFlag(Qt::FramelessWindowHint);

    auto* shell = new QVBoxLayout(&dialog);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);
    auto* header = new DialogHeader(title, &dialog);
    shell->addWidget(header);
    if (followWindowTitle) {
        header->setLabel(shortTitle(dialog.windowTitle()));
        QObject::connect(&dialog, &QWidget::windowTitleChanged, header,
                         [header](const QString& text) { header->setLabel(shortTitle(text)); });
    }

    auto* content = new QVBoxLayout;
    content->setContentsMargins(contentMargins);
    content->setSpacing(spacing);
    shell->addLayout(content, 1);
    return content;
}

} // namespace csm
