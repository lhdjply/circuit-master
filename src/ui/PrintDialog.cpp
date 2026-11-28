#include "ui/PrintDialog.h"

#include "core/Language.h"
#include "ui/DialogFrame.h"
#include "ui/Dialogs.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPrintPreviewWidget>
#include <QDoubleSpinBox>
#include <QPrinter>
#include <QRadioButton>
#include <QPrinterInfo>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace csm {

PrintDialog::PrintDialog(QPrinter& printer, const PageSetup& page,
                         std::function<void(QPrinter&, const PrintOptions&)> paint,
                         QWidget* parent)
    : QDialog(parent)
    , m_printer(printer)
    , m_page(page)
    , m_paint(std::move(paint))
{
    setWindowTitle(CSM_TR("Print"));
    resize(980, 620);
    setMinimumSize(800, 480);
    Dialogs::applyStyle(*this);

    auto* outer = makeFramelessShell(*this, CSM_TR("Print"), QMargins(18, 14, 18, 14), 12);
    auto* body = new QHBoxLayout;
    body->setSpacing(16);

    // ---- 左边：照系统打印对话框的样子列打印机 ----
    auto* left = new QWidget(this);
    left->setFixedWidth(360);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    auto* group = new QGroupBox(CSM_TR("Printer"), left);
    auto* form = new QFormLayout(group);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_printerBox = new QComboBox(group);
    const QList<QPrinterInfo> printers = QPrinterInfo::availablePrinters();
    for (const QPrinterInfo& info : printers)
        m_printerBox->addItem(info.printerName(), info.printerName());
    // 末尾固定放一条「打印到文件（PDF）」：一台打印机都没有时它就是唯一选项
    m_printerBox->addItem(CSM_TR("Print to file (PDF)"), QString());
    const int current = m_printerBox->findData(printer.printerName());
    m_printerBox->setCurrentIndex(current >= 0 ? current : m_printerBox->count() - 1);

    form->addRow(CSM_TR("Name:"), m_printerBox);

    m_locationLabel = new QLabel(group);
    m_locationLabel->setWordWrap(true);
    form->addRow(CSM_TR("Location:"), m_locationLabel);

    m_typeLabel = new QLabel(group);
    m_typeLabel->setWordWrap(true);
    form->addRow(CSM_TR("Type:"), m_typeLabel);

    m_fileEdit = new QLineEdit(group);
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString base = printer.docName().isEmpty() ? QStringLiteral("circuit")
                                                     : printer.docName();
    m_fileEdit->setText(QFileInfo(documents, base + QStringLiteral(".pdf")).filePath());
    m_browseButton = new QPushButton(QStringLiteral("…"), group);
    m_browseButton->setAutoDefault(false);
    // 小方按钮：给个固定尺寸，别让它被整行拉成一长条
    // 只有两个字符宽：对话框那套样式给 QPushButton 定了 min-width: 56px，
    // 这里必须显式覆盖，否则固定尺寸也压不下去
    // 宽度交给样式表：QSS 的 min-width 会盖过 setFixedSize，直接设固定尺寸反而不准
    m_browseButton->setFixedHeight(24);
    m_browseButton->setStyleSheet(
        QStringLiteral("QPushButton { min-width: 26px; max-width: 26px; padding: 0px; }"));
    auto* fileRow = new QWidget(group);
    auto* fileLayout = new QHBoxLayout(fileRow);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(6);
    fileLayout->addWidget(m_fileEdit, 1);
    fileLayout->addWidget(m_browseButton);
    form->addRow(CSM_TR("Output file:"), fileRow);

    m_paperBox = new QComboBox(group);
    for (PaperSize size : {PaperSize::A5, PaperSize::A4, PaperSize::A3, PaperSize::A2,
                           PaperSize::A1}) {
        m_paperBox->addItem(paperSizeName(size), int(size));
    }
    m_paperBox->setCurrentIndex(m_paperBox->findData(int(m_page.size)));
    if (m_paperBox->currentIndex() < 0)
        m_paperBox->setCurrentIndex(m_paperBox->findData(int(PaperSize::A4)));
    form->addRow(CSM_TR("Paper:"), m_paperBox);

    auto* orientationRow = new QWidget(group);
    auto* orientationLayout = new QHBoxLayout(orientationRow);
    orientationLayout->setContentsMargins(0, 0, 0, 0);
    orientationLayout->setSpacing(14);
    m_landscape = new QRadioButton(CSM_TR("Landscape"), orientationRow);
    m_portrait = new QRadioButton(CSM_TR("Portrait"), orientationRow);
    m_landscape->setChecked(m_page.landscape);
    m_portrait->setChecked(!m_page.landscape);
    orientationLayout->addWidget(m_landscape);
    orientationLayout->addWidget(m_portrait);
    orientationLayout->addStretch(1);
    form->addRow(CSM_TR("Orientation:"), orientationRow);

    m_scaleBox = new QComboBox(group);
    m_scaleBox->addItem(CSM_TR("Fit to sheet"), 0);
    m_scaleBox->addItem(CSM_TR("Actual size (1:1)"), 1);
    m_scaleBox->addItem(CSM_TR("Custom"), 2);
    m_scaleSpin = new QDoubleSpinBox(group);
    m_scaleSpin->setRange(10.0, 400.0);
    m_scaleSpin->setDecimals(0);
    m_scaleSpin->setValue(100.0);
    m_scaleSpin->setSuffix(QStringLiteral(" %"));
    m_scaleSpin->setEnabled(false);
    auto* scaleRow = new QWidget(group);
    auto* scaleLayout = new QHBoxLayout(scaleRow);
    scaleLayout->setContentsMargins(0, 0, 0, 0);
    scaleLayout->setSpacing(6);
    scaleLayout->addWidget(m_scaleBox, 1);
    scaleLayout->addWidget(m_scaleSpin);
    form->addRow(CSM_TR("Scale:"), scaleRow);

    // 和「方向」一样用两个单选，别让同一列里一半下拉一半单选
    auto* colorRow = new QWidget(group);
    auto* colorLayout = new QHBoxLayout(colorRow);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    colorLayout->setSpacing(14);
    m_colorRadio = new QRadioButton(CSM_TR("Color"), colorRow);
    m_grayscaleRadio = new QRadioButton(CSM_TR("Black and white"), colorRow);
    m_colorRadio->setChecked(true);
    colorLayout->addWidget(m_colorRadio);
    colorLayout->addWidget(m_grayscaleRadio);
    colorLayout->addStretch(1);
    form->addRow(CSM_TR("Color:"), colorRow);

    leftLayout->addWidget(group);
    leftLayout->addStretch(1);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* okButton = new QPushButton(CSM_TR("Print"), left);
    okButton->setDefault(true);
    auto* cancelButton = new QPushButton(CSM_TR("Cancel"), left);
    cancelButton->setAutoDefault(false);
    buttons->addWidget(okButton);
    buttons->addWidget(cancelButton);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    leftLayout->addLayout(buttons);

    body->addWidget(left);

    // ---- 右边：预览 ----
    // 预览用单独的 QPrinter（输出到临时 PDF），免得改到真正要打印的那台
    m_previewPrinter = new QPrinter(QPrinter::HighResolution);
    m_previewPrinter->setOutputFormat(QPrinter::PdfFormat);
    m_previewPrinter->setOutputFileName(
        QDir::temp().filePath(QStringLiteral("circuit-master-preview.pdf")));


    m_preview = new QPrintPreviewWidget(m_previewPrinter, this);
    connect(m_preview, &QPrintPreviewWidget::paintRequested, this, [this](QPrinter* target) {
        if (m_paint)
            m_paint(*target, options());
    });
    // 预览 + 翻页：一张纸放不下时会有多页
    auto* previewSide = new QWidget(this);
    auto* previewLayout = new QVBoxLayout(previewSide);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(8);

    auto* pager = new QHBoxLayout;
    pager->setSpacing(6);
    auto* prevButton = new QPushButton(CSM_TR("Previous page"), previewSide);
    auto* nextButton = new QPushButton(CSM_TR("Next page"), previewSide);
    m_pageLabel = new QLabel(previewSide);
    Theme::styleWidget(m_pageLabel, [] {
        return QStringLiteral("color:%1; font-size:11px;").arg(Theme::textMuted().name());
    });
    prevButton->setAutoDefault(false);
    nextButton->setAutoDefault(false);
    pager->addWidget(prevButton);
    pager->addWidget(nextButton);
    pager->addWidget(m_pageLabel);
    pager->addStretch(1);
    previewLayout->addLayout(pager);

    m_preview->setZoomMode(QPrintPreviewWidget::FitInView);
    previewLayout->addWidget(m_preview, 1);
    body->addWidget(previewSide, 1);

    connect(prevButton, &QPushButton::clicked, this, [this] {
        m_preview->setCurrentPage(std::max(1, m_preview->currentPage() - 1));
    });
    connect(nextButton, &QPushButton::clicked, this, [this] {
        m_preview->setCurrentPage(std::min(m_preview->pageCount(),
                                           m_preview->currentPage() + 1));
    });
    connect(m_preview, &QPrintPreviewWidget::previewChanged, this, [this, prevButton, nextButton] {
        const int pages = m_preview->pageCount();
        m_pageLabel->setText(CSM_TR("Page %1 / %2")
                                 .arg(m_preview->currentPage())
                                 .arg(pages));
        prevButton->setEnabled(pages > 1);
        nextButton->setEnabled(pages > 1);
    });

    outer->addLayout(body, 1);

    connect(m_browseButton, &QPushButton::clicked, this, &PrintDialog::browseForFile);
    connect(m_printerBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] {
                const QString name = m_printerBox->currentData().toString();
                if (!name.isEmpty())
                    m_printer.setPrinterName(name);
                updatePrinterInfo();
                updateFileRow();
            });

    connect(m_landscape, &QRadioButton::toggled, this, [this](bool on) {
        if (!on)
            return;
        m_page.landscape = true;
        applyPaperToPrinters();
        refreshPreview();
    });
    connect(m_portrait, &QRadioButton::toggled, this, [this](bool on) {
        if (!on)
            return;
        m_page.landscape = false;
        applyPaperToPrinters();
        refreshPreview();
    });
    connect(m_scaleBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        m_scaleSpin->setEnabled(m_scaleBox->currentData().toInt() == 2);
        refreshPreview();
    });
    connect(m_scaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this] { refreshPreview(); });
    connect(m_grayscaleRadio, &QRadioButton::toggled, this, [this](bool on) {
        m_printer.setColorMode(on ? QPrinter::GrayScale : QPrinter::Color);
        refreshPreview();
    });
    connect(m_paperBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        m_page.size = PaperSize(m_paperBox->currentData().toInt());
        applyPaperToPrinters();
        refreshPreview();
    });

    applyPaperToPrinters();
    updatePrinterInfo();
    updateFileRow();
    refreshPreview();
}

PrintDialog::~PrintDialog()
{
    delete m_previewPrinter;
}

void PrintDialog::refreshPreview()
{
    if (!m_preview)
        return;
    m_preview->updatePreview();
    // 单页视图：和上面的「上一页 / 下一页」配套。updatePreview() 会把视图模式
    // 重置回连续滚动，所以每次刷完都要再设一次
    m_preview->setViewMode(QPrintPreviewWidget::SinglePageView);
}

PrintOptions PrintDialog::options() const
{
    PrintOptions out;
    out.page = m_page;
    out.scaleMode = m_scaleBox ? m_scaleBox->currentData().toInt() : 0;
    out.scalePercent = m_scaleSpin ? m_scaleSpin->value() : 100.0;
    out.grayscale = m_grayscaleRadio && m_grayscaleRadio->isChecked();
    return out;
}

void PrintDialog::applyPaperToPrinters()
{
    // 纸张：竖版基础尺寸 + 明确方向。直接塞横版尺寸的话，驱动会按竖版纸去打
    const QPageSize paper(m_page.portraitSizeMm(), QPageSize::Millimeter, QString(),
                          QPageSize::ExactMatch);
    const QPageLayout::Orientation orientation =
        m_page.landscape ? QPageLayout::Landscape : QPageLayout::Portrait;
    const QPrinter::ColorMode colorMode = options().grayscale ? QPrinter::GrayScale
                                                              : QPrinter::Color;
    m_printer.setPageSize(paper);
    m_printer.setPageOrientation(orientation);
    m_printer.setColorMode(colorMode);
    if (m_previewPrinter) {
        m_previewPrinter->setPageSize(paper);
        m_previewPrinter->setPageOrientation(orientation);
        m_previewPrinter->setColorMode(colorMode);
    }
}

void PrintDialog::updatePrinterInfo()
{
    if (m_printerBox->currentData().toString().isEmpty()) {
        // 「打印到文件（PDF）」：没有位置与型号可报
        m_locationLabel->setText(QStringLiteral("—"));
        m_typeLabel->setText(CSM_TR("PDF document"));
        return;
    }
    // 位置 / 类型来自 CUPS 给的信息；拿不到就留空行，不留「未找到」之类的噪音
    const QPrinterInfo info = QPrinterInfo::printerInfo(m_printer.printerName());
    if (info.isNull()) {
        m_locationLabel->setText(QStringLiteral("—"));
        m_typeLabel->setText(CSM_TR("Print to file"));
        return;
    }
    m_locationLabel->setText(info.location().isEmpty() ? QStringLiteral("—") : info.location());
    const QString type = info.makeAndModel().isEmpty() ? info.description() : info.makeAndModel();
    m_typeLabel->setText(type.isEmpty() ? QStringLiteral("—") : type);
}

void PrintDialog::updateFileRow()
{
    // 只有选了「打印到文件（PDF）」时，输出文件才可编辑
    const bool toFile = m_printerBox->currentData().toString().isEmpty();
    m_fileEdit->setEnabled(toFile);
    m_browseButton->setEnabled(toFile);
}

void PrintDialog::browseForFile()
{
    const QString path = QFileDialog::getSaveFileName(
        this, CSM_TR("Print to file"), m_fileEdit->text(),
        CSM_TR("PDF document (*.pdf)"));
    if (!path.isEmpty())
        m_fileEdit->setText(path);
}

void PrintDialog::applyTo(QPrinter& printer) const
{
    printer.setPageSize(QPageSize(m_page.portraitSizeMm(), QPageSize::Millimeter, QString(),
                                  QPageSize::ExactMatch));
    printer.setPageOrientation(m_page.landscape ? QPageLayout::Landscape
                                                : QPageLayout::Portrait);
    printer.setColorMode(options().grayscale ? QPrinter::GrayScale : QPrinter::Color);
    if (m_printerBox->currentData().toString().isEmpty()) {
        QString path = m_fileEdit->text().trimmed();
        if (path.isEmpty())
            path = QFileInfo(QDir::homePath(), QStringLiteral("circuit.pdf")).filePath();
        if (!path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
            path += QStringLiteral(".pdf");
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        return;
    }
    if (m_printerBox->currentData().isValid())
        printer.setPrinterName(m_printerBox->currentData().toString());
    printer.setOutputFormat(QPrinter::NativeFormat);
}

} // namespace csm
