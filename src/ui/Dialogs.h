#pragma once

// ---------------------------------------------------------------------------
//  统一的提示框
//
//  只是换观感，语义与 QMessageBox 完全一致（同样的按钮、同样的返回值）。
//  改了三处：
//   · 图标用自绘的圆形徽标，跟着程序的强调色 —— 原生 QMessageBox 在各平台
//     画的是紫色 info、蓝色 question 位图，跟界面完全不搭；
//   · 去掉按钮上由系统主题加的小图标（绿勾 / 垃圾桶 / 红叉），只留文字；
//   · 圆角、内边距、字号统一。
//
//  样式只作用在单个对话框实例上，不碰全局 QSS。
// ---------------------------------------------------------------------------

#include <QDialog>
#include <QMessageBox>
#include <QString>

class QWidget;

namespace csm {

class Dialogs {
public:
    /// 给自建对话框套上与本程序其它提示框一致的观感（底色、按钮样式、
    /// 去掉系统主题给按钮加的小图标）。设置窗口用它，别自己再写一套 QSS。
    static void applyStyle(QDialog& dialog);
    /// 普通提示（对应 QMessageBox::Information）
    static void info(QWidget* parent, const QString& title, const QString& text);
    /// 警告（对应 QMessageBox::Warning）
    static void warn(QWidget* parent, const QString& title, const QString& text);
    /// 错误（对应 QMessageBox::Critical）
    static void error(QWidget* parent, const QString& title, const QString& text);

    /// 保存 / 丢弃 / 取消。返回用户选的按钮（三者之一）。
    static QMessageBox::StandardButton askSaveDiscardCancel(QWidget* parent, const QString& title,
                                                            const QString& text);

    /// 富文本说明（「快速上手」这类比较长的内容），比 info() 宽一些
    static void guide(QWidget* parent, const QString& title, const QString& richText);

    /// 「关于」窗口：左边应用图标，右边版本与简介
    static void about(QWidget* parent, const QString& version);

    /// 「更新日志」窗口：把编进程序的 CHANGELOG.md 按 Markdown 渲染出来
    static void changelog(QWidget* parent);
};

} // namespace csm
