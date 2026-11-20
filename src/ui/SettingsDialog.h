#pragma once

// ---------------------------------------------------------------------------
//  设置窗口
//
//  目前只有两项：主题（跟随系统 / 浅色 / 深色）与界面语言（中文 / English）。
//  主题改完当场生效（对话框本身也跟着变）；语言要重建界面，所以确认后提示重启。
//
//  取消时把主题恢复成打开对话框之前的样子 —— 用户可能只是点进去试了一下。
// ---------------------------------------------------------------------------

#include <QDialog>

#include <functional>

namespace csm {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    /// applyTheme：主题变了要通知主窗口重刷调色板、图标与元件库
    /// applyTheme：主题变了要重刷调色板、图标与元件库
    /// applyGrid ：网格间距变了要重画画布（吸附步长也跟着变）
    explicit SettingsDialog(QWidget* parent, std::function<void()> applyTheme,
                            std::function<void()> applyGrid);

private:
    /// 打开对话框时的主题，取消时用来还原
    int m_originalTheme = 0;
    std::function<void()> m_applyTheme;
    std::function<void()> m_applyGrid;
};

} // namespace csm
