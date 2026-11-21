#pragma once

// ---------------------------------------------------------------------------
//  设置窗口
//
//  两个标签页：
//    「外观」 主题（跟随系统 / 浅色 / 深色）、网格大小、界面语言
//    「快捷键」逐个命令的按键，可清空也可一键恢复默认
//
//  三项都只在点「确定」后生效；语言要重建界面，所以确认后再提示重启。
//  取消不做任何改动 —— 用户可能只是点进去试了一下。
// ---------------------------------------------------------------------------

#include <QDialog>
#include <QKeySequence>
#include <QHash>
#include <QVector>

#include <functional>

namespace csm {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    /// 一行可改的快捷键
    struct ShortcutRow {
        QString id;                 // 稳定标识，如 "file.new"
        QString label;              // 菜单里显示的名字
        QKeySequence current;       // 当前生效的
        QKeySequence defaultKey;    // 代码里的默认值，供「恢复默认」用
    };

    /// applyTheme：主题变了要通知主窗口重刷调色板、图标与元件库
    /// applyGrid ：网格间距变了要重画画布（吸附步长也跟着变）
    explicit SettingsDialog(QWidget* parent, std::function<void()> applyTheme,
                            std::function<void()> applyGrid,
                            const QVector<ShortcutRow>& shortcuts = {});

    /// 点确定后各命令的快捷键（id → 序列）；空序列表示不绑定
    QHash<QString, QKeySequence> shortcuts() const { return m_edited; }

private:
    /// 打开对话框时的主题，取消时用来还原
    int m_originalTheme = 0;
    std::function<void()> m_applyTheme;
    std::function<void()> m_applyGrid;

    QVector<ShortcutRow> m_rows;              // 表格里每一行对应的命令
    QHash<QString, QKeySequence> m_edited;    // 确定时收集到的结果
};

} // namespace csm
