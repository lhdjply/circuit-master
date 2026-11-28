#pragma once

// ---------------------------------------------------------------------------
//  页面设置：纸张大小（A4 / A3 / A2 / A1 / 自定义）与方向（横向 / 纵向）
//
//  只负责取值，改不改画布由调用方决定 —— 这样「取消」天然什么都不做。
// ---------------------------------------------------------------------------

#include "core/PageSetup.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;

namespace csm {

class PageSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit PageSetupDialog(const PageSetup& current, QWidget* parent = nullptr);

    PageSetup pageSetup() const;

private:
    void updateCustomEnabled();
    void updatePreview();

    QComboBox* m_sizeBox = nullptr;
    QRadioButton* m_landscape = nullptr;
    QRadioButton* m_portrait = nullptr;
    QDoubleSpinBox* m_width = nullptr;
    QDoubleSpinBox* m_height = nullptr;
    QWidget* m_customRow = nullptr;
    QLabel* m_preview = nullptr;
};

} // namespace csm
