#include "ui/PageSetupDialog.h"

#include "ui/DialogFrame.h"
#include "ui/Dialogs.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace csm {

PageSetupDialog::PageSetupDialog(const PageSetup& current, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(CSM_TR("Page Setup"));
    setMinimumWidth(420);
    Dialogs::applyStyle(*this);

    auto* outer = makeFramelessShell(*this, CSM_TR("Page Setup"), QMargins(20, 16, 20, 14), 14);

    auto* form = new QFormLayout;
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_sizeBox = new QComboBox(this);
    for (PaperSize size : {PaperSize::A4, PaperSize::A3, PaperSize::A2, PaperSize::A1,
                           PaperSize::Custom}) {
        m_sizeBox->addItem(paperSizeName(size), int(size));
    }
    m_sizeBox->setCurrentIndex(m_sizeBox->findData(int(current.size)));
    form->addRow(CSM_TR("Paper size"), m_sizeBox);

    // 方向：两个单选按钮并排，和常见打印对话框一致
    auto* orientationRow = new QWidget(this);
    auto* orientationLayout = new QHBoxLayout(orientationRow);
    orientationLayout->setContentsMargins(0, 0, 0, 0);
    orientationLayout->setSpacing(16);
    m_landscape = new QRadioButton(CSM_TR("Landscape"), orientationRow);
    m_portrait = new QRadioButton(CSM_TR("Portrait"), orientationRow);
    m_landscape->setChecked(current.landscape);
    m_portrait->setChecked(!current.landscape);
    orientationLayout->addWidget(m_landscape);
    orientationLayout->addWidget(m_portrait);
    orientationLayout->addStretch(1);
    form->addRow(CSM_TR("Orientation"), orientationRow);

    m_width = new QDoubleSpinBox(this);
    m_width->setRange(10.0, 5000.0);
    m_width->setDecimals(0);
    m_width->setSuffix(CSM_TR(" mm"));
    m_width->setValue(current.customWidthMm);

    m_height = new QDoubleSpinBox(this);
    m_height->setRange(10.0, 5000.0);
    m_height->setDecimals(0);
    m_height->setSuffix(CSM_TR(" mm"));
    m_height->setValue(current.customHeightMm);

    m_customRow = new QWidget(this);
    auto* customLayout = new QHBoxLayout(m_customRow);
    customLayout->setContentsMargins(0, 0, 0, 0);
    customLayout->setSpacing(8);
    customLayout->addWidget(m_width);
    customLayout->addWidget(new QLabel(QStringLiteral("×"), m_customRow));
    customLayout->addWidget(m_height);
    customLayout->addStretch(1);
    form->addRow(CSM_TR("Custom size"), m_customRow);

    outer->addLayout(form);

    m_preview = new QLabel(this);
    Theme::styleWidget(m_preview, [] {
        return QStringLiteral("color:%1; font-size:11px;").arg(Theme::textMuted().name());
    });
    outer->addWidget(m_preview);

    // 普通按钮：QDialogButtonBox 会给确定 / 取消加上系统主题的绿勾、红叉图标，
    // 和本程序其它提示框（纯文字按钮）不一致
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    auto* okButton = new QPushButton(CSM_TR("OK"), this);
    okButton->setDefault(true);
    okButton->setAutoDefault(true);
    auto* cancelButton = new QPushButton(CSM_TR("Cancel"), this);
    cancelButton->setAutoDefault(false);
    buttonRow->addWidget(okButton);
    buttonRow->addWidget(cancelButton);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    outer->addLayout(buttonRow);

    connect(m_sizeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        updateCustomEnabled();
        updatePreview();
    });
    connect(m_landscape, &QRadioButton::toggled, this, [this] { updatePreview(); });
    connect(m_width, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this] { updatePreview(); });
    connect(m_height, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this] { updatePreview(); });

    updateCustomEnabled();
    updatePreview();
    adjustSize();
}

void PageSetupDialog::updateCustomEnabled()
{
    const bool custom = PaperSize(m_sizeBox->currentData().toInt()) == PaperSize::Custom;
    m_customRow->setEnabled(custom);
}

void PageSetupDialog::updatePreview()
{
    // 预览直接报出换算后的画布尺寸，省得用户猜「A3 到底多大」
    const PageSetup page = pageSetup();
    const QSizeF mm = page.sizeMm();
    m_preview->setText(CSM_TR("%1 × %2 mm · %3 × %4 units on the canvas")
                           .arg(std::lround(mm.width()))
                           .arg(std::lround(mm.height()))
                           .arg(std::lround(page.sceneRect().width()))
                           .arg(std::lround(page.sceneRect().height())));
}

PageSetup PageSetupDialog::pageSetup() const
{
    PageSetup page;
    page.size = PaperSize(m_sizeBox->currentData().toInt());
    page.landscape = m_landscape->isChecked();
    page.customWidthMm = m_width->value();
    page.customHeightMm = m_height->value();
    return page;
}

} // namespace csm
