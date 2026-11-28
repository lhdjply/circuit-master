#pragma once

// ---------------------------------------------------------------------------
//  打印对话框
//
//  系统自带的 QPrintDialog / QPrintPreviewDialog 都是平台样式的窗口，
//  和本程序其它窗口不一致。这里自己画一个，左边照系统对话框的样子列
//  打印机（名称 / 位置 / 类型 / 输出文件），右边直接就是预览。
// ---------------------------------------------------------------------------

#include "core/PageSetup.h"

#include <QDialog>

#include <functional>

class QComboBox;
class QLabel;
class QDoubleSpinBox;
class QLineEdit;
class QPrintPreviewWidget;
class QRadioButton;
class QPrinter;
class QPushButton;

namespace csm {

/// 打印时要用的设置：纸张（含方向）与比例
struct PrintOptions {
    PageSetup page;
    /// 0 = 适应图纸，1 = 实际大小（1:1），2 = 自定义百分比
    int scaleMode = 0;
    double scalePercent = 100.0;
    /// true = 黑白（灰度）打印
    bool grayscale = false;
};

class PrintDialog : public QDialog {
    Q_OBJECT

public:
    /// page：图纸默认的纸张（用户可以在对话框里改）
    /// paint：把整张图纸画到给定的 printer 上（预览与真正打印共用同一个回调），
    /// 第二个参数是当前选定的纸张与比例
    PrintDialog(QPrinter& printer, const PageSetup& page,
                std::function<void(QPrinter&, const PrintOptions&)> paint,
                QWidget* parent = nullptr);

    /// 对话框里最终选定的纸张与比例
    const PageSetup& pageSetup() const { return m_page; }
    PrintOptions options() const;
    ~PrintDialog() override;

    /// 点「打印」后把选择写回 printer：选了文件就设成 PDF 输出到该路径
    void applyTo(QPrinter& printer) const;

private:
    void browseForFile();
    void updateFileRow();
    void updatePrinterInfo();
    /// 把选定的纸张（竖版尺寸 + 方向）应用到打印 / 预览两台 printer
    void applyPaperToPrinters();
    /// 重画预览并保持「单页视图」
    void refreshPreview();

    QPrinter& m_printer;
    PageSetup m_page;
    std::function<void(QPrinter&, const PrintOptions&)> m_paint;
    QPrinter* m_previewPrinter = nullptr;   // 预览专用，免得动到真正要打印的那台
    QComboBox* m_printerBox = nullptr;
    QLabel* m_locationLabel = nullptr;
    QLabel* m_typeLabel = nullptr;
    QLineEdit* m_fileEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QComboBox* m_paperBox = nullptr;
    QRadioButton* m_landscape = nullptr;
    QRadioButton* m_portrait = nullptr;
    QComboBox* m_scaleBox = nullptr;
    QRadioButton* m_colorRadio = nullptr;
    QRadioButton* m_grayscaleRadio = nullptr;
    QDoubleSpinBox* m_scaleSpin = nullptr;
    QPrintPreviewWidget* m_preview = nullptr;
    QLabel* m_pageLabel = nullptr;
};

} // namespace csm
