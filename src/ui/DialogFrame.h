#pragma once

// ---------------------------------------------------------------------------
//  无边框窗口的公共件
//
//  自绘标题栏之后，主窗口、提示框、设置窗口都没有系统边框了。这两件事是共用的：
//    · ResizeGrips —— 四边 / 四角的拖拽区，用来改窗口大小
//    · makeFramelessShell —— 给对话框套上「标题 + 关闭按钮」的顶栏
// ---------------------------------------------------------------------------

#include <QMargins>
#include <QObject>
#include <QVector>

class QVBoxLayout;
class QWidget;

namespace csm {

/// 贴在窗口边缘的 8 个透明拖拽区。窗口尺寸变化时自动重排，最大化 / 全屏时隐藏。
class ResizeGrips : public QObject
{
public:
    explicit ResizeGrips(QWidget* window);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void layout();

    QWidget* m_window = nullptr;
    QVector<QWidget*> m_edges;
};

/// 把窗口 / 对话框改成无边框，并在顶部放上自绘标题栏；返回放内容的布局。
/// followWindowTitle 为真时顶栏标题跟着 windowTitle 走（去掉「— Circuit Master」
/// 之类的后缀），适合会换标题的窗口，比如波形监控。
QVBoxLayout* makeFramelessShell(QWidget& window, const QString& title,
                                const QMargins& contentMargins, int spacing,
                                bool followWindowTitle = false);

} // namespace csm
