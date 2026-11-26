#pragma once

// ---------------------------------------------------------------------------
//  自绘标题栏
//
//  窗口是无边框的（Qt::FramelessWindowHint），系统那条标题栏就没有了；
//  这里把「菜单 + 标题 + 最小化/最大化/关闭」排成一行，省下整整一条高度。
//
//  拖动与双击最大化走 QWindow::startSystemMove()：交给窗口管理器处理，
//  和原生拖动一样（X11 上是 _NET_WM_MOVERESIZE，Wayland 上走 compositor）。
// ---------------------------------------------------------------------------

#include <QAbstractButton>
#include <QWidget>

namespace csm {

class WindowButton : public QAbstractButton
{
    Q_OBJECT
public:
    enum class Glyph { Minimize, Maximize, Restore, Close };

    explicit WindowButton(Glyph glyph, QWidget* parent = nullptr);
    void setGlyph(Glyph glyph);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    Glyph m_glyph;
};

class TitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBar(QWidget* parent = nullptr);

    /// 最大化状态变了：最大化按钮换图标（□ ↔ 还原）
    void setMaximized(bool maximized);

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    /// startSystemMove() 不被窗口管理器支持时，自己按偏移量搬窗口
    bool m_dragging = false;
    QPoint m_dragOffset;
    WindowButton* m_minimize = nullptr;
    WindowButton* m_maximize = nullptr;
    WindowButton* m_close = nullptr;
};

} // namespace csm
