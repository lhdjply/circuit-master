#include "ui/WaveformWindow.h"

#include "core/Units.h"

#include <QChart>
#include <QChartView>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineSeries>
#include <QListWidget>
#include <QCategoryAxis>
#include <QLogValueAxis>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QTextStream>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace csm {

namespace {

const QColor kColors[] = {
    QColor(0x25, 0x63, 0xeb), QColor(0xdc, 0x26, 0x26), QColor(0x16, 0xa3, 0x4a),
    QColor(0x93, 0x33, 0xea), QColor(0xea, 0x58, 0x0c), QColor(0x08, 0x91, 0xb2),
    QColor(0xbe, 0x12, 0x3c), QColor(0x4d, 0x7c, 0x0f), QColor(0x7c, 0x3a, 0xed),
    QColor(0xb4, 0x53, 0x09),
};

QColor colorFor(int index)
{
    return kColors[index % int(sizeof(kColors) / sizeof(kColors[0]))];
}

QIcon swatch(const QColor& color)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(1.0, 1.0, 12.0, 12.0), 3.0, 3.0);
    p.end();
    return QIcon(pm);
}

struct NiceRange {
    double min;
    double max;
    double step;
};

NiceRange niceRange(double lo, double hi, int targetTicks)
{
    if (!(hi > lo)) {
        const double center = lo;
        lo = center - 0.5;
        hi = center + 0.5;
    }
    const double rawStep = (hi - lo) / std::max(1, targetTicks);
    const double magnitude = std::pow(10.0, std::floor(std::log10(rawStep)));
    const double normalized = rawStep / magnitude;
    double step = 10.0;
    if (normalized <= 1.0)
        step = 1.0;
    else if (normalized <= 2.0)
        step = 2.0;
    else if (normalized <= 2.5)
        step = 2.5;
    else if (normalized <= 5.0)
        step = 5.0;
    step *= magnitude;

    const double newMin = std::floor(lo / step) * step;
    const double newMax = std::ceil(hi / step) * step;
    return {newMin, newMax, step};
}

void applyNiceTicks(QValueAxis* axis, double lo, double hi)
{
    const NiceRange range = niceRange(lo, hi, 6);
    axis->setRange(range.min, range.max);
    const int ticks = int(std::round((range.max - range.min) / range.step)) + 1;
    axis->setTickCount(std::max(2, std::min(ticks, 20)));
}

QString kindName(AnalysisKind kind)
{
    switch (kind) {
    case AnalysisKind::OperatingPoint: return QStringLiteral("工作点");
    case AnalysisKind::Transient:      return QStringLiteral("瞬态分析");
    case AnalysisKind::AC:             return QStringLiteral("交流扫频");
    }
    return QString();
}

} // namespace

WaveformWindow::WaveformWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(QStringLiteral("波形查看器 — 电路仿真大师"));
    resize(1040, 640);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    root->addWidget(splitter);

    auto* side = new QWidget(splitter);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(10, 10, 10, 10);
    sideLayout->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("信号"), side);
    title->setStyleSheet(QStringLiteral("font-weight:600; color:#1e293b;"));
    sideLayout->addWidget(title);

    m_list = new QListWidget(side);
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    sideLayout->addWidget(m_list, 1);

    auto* buttons = new QHBoxLayout();
    auto* allButton = new QPushButton(QStringLiteral("全选"), side);
    auto* noneButton = new QPushButton(QStringLiteral("清除"), side);
    buttons->addWidget(allButton);
    buttons->addWidget(noneButton);
    sideLayout->addLayout(buttons);

    m_info = new QLabel(side);
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color:#64748b; font-size:11px;"));
    sideLayout->addWidget(m_info);

    auto* chartSide = new QWidget(splitter);
    auto* chartLayout = new QVBoxLayout(chartSide);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->setSpacing(0);

    auto* toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(10, 8, 10, 8);
    auto* autoButton = new QPushButton(QStringLiteral("自动缩放"), chartSide);
    auto* csvButton = new QPushButton(QStringLiteral("导出 CSV"), chartSide);
    auto* pngButton = new QPushButton(QStringLiteral("导出图片"), chartSide);
    toolbar->addWidget(autoButton);
    toolbar->addSpacing(12);
    auto* tip = new QLabel(QStringLiteral("在图上拖拽可框选放大，右键复位"), chartSide);
    tip->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:11px;"));
    toolbar->addWidget(tip);
    toolbar->addStretch(1);
    toolbar->addWidget(csvButton);
    toolbar->addWidget(pngButton);
    chartLayout->addLayout(toolbar);

    m_chart = new QChart();
    m_chart->legend()->hide();
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->setBackgroundBrush(Qt::white);
    m_chart->setPlotAreaBackgroundVisible(true);
    m_chart->setPlotAreaBackgroundBrush(QColor(0xf8, 0xfa, 0xfc));
    m_chart->setMargins(QMargins(10, 10, 10, 10));
    m_chart->setTitleBrush(QColor(0x64, 0x74, 0x8b));

    m_view = new QChartView(m_chart, chartSide);
    m_view->setRenderHint(QPainter::Antialiasing, true);
    m_view->setRubberBand(QChartView::RectangleRubberBand);
    m_view->setBackgroundBrush(Qt::white);
    chartLayout->addWidget(m_view, 1);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 800});

    connect(allButton, &QPushButton::clicked, this, [this] { selectAll(true); });
    connect(noneButton, &QPushButton::clicked, this, [this] { selectAll(false); });
    connect(autoButton, &QPushButton::clicked, this, &WaveformWindow::autoScale);
    connect(csvButton, &QPushButton::clicked, this, &WaveformWindow::exportCsv);
    connect(pngButton, &QPushButton::clicked, this, &WaveformWindow::exportImage);
    m_view->installEventFilter(this);

    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem*) {
        if (!m_building)
            rebuild();
    });
}

bool WaveformWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            if (m_chart)
                m_chart->zoomReset();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void WaveformWindow::setResult(const SimulationResult& result)
{
    m_result = result;
    populateList();
    rebuild();
    setWindowTitle(QStringLiteral("波形查看器 — %1 — 电路仿真大师").arg(kindName(result.kind)));
}

void WaveformWindow::populateList()
{
    m_building = true;
    m_list->clear();

    const bool hasProbes = !m_result.probeWaves.isEmpty();
    for (int i = 0; i < m_result.waves.size(); ++i) {
        const Signal& s = m_result.waves.at(i);
        auto* item = new QListWidgetItem(swatch(colorFor(i)), s.name);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        const bool isVoltage = s.name.startsWith(QLatin1String("V("));
        if (hasProbes)
            item->setCheckState(m_result.probeWaves.contains(i) ? Qt::Checked : Qt::Unchecked);
        else
            item->setCheckState(isVoltage ? Qt::Checked : Qt::Unchecked);
        QString tip = s.name;
        if (s.name.startsWith(QLatin1String("V("))) {
            bool ok = false;
            const int node = s.name.mid(2, s.name.size() - 3).toInt(&ok);
            if (ok && node >= 1 && node <= m_result.nodeLabels.size())
                tip = QStringLiteral("%1\n%2").arg(s.name, m_result.nodeLabels.at(node - 1));
        }
        item->setToolTip(tip);
        m_list->addItem(item);
    }

    bool anyChecked = false;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->checkState() == Qt::Checked)
            anyChecked = true;
    }
    if (!anyChecked && !hasProbes) {
        for (int i = 0; i < std::min(3, m_list->count()); ++i)
            m_list->item(i)->setCheckState(Qt::Checked);
    }
    m_building = false;
    m_list->clearSelection();
    m_list->setCurrentRow(-1);
}

void WaveformWindow::rebuild()
{
    if (!m_chart)
        return;

    m_chart->removeAllSeries();
    const QList<QAbstractAxis*> oldAxes = m_chart->axes();
    for (QAbstractAxis* axis : oldAxes) {
        m_chart->removeAxis(axis);
        delete axis;
    }

    m_chart->setTitle(QString());

    QVector<int> selected;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->checkState() == Qt::Checked)
            selected.append(i);
    }

    const int pointCount = m_result.waves.isEmpty() ? 0 : m_result.waves.first().y.size();
    if (selected.isEmpty() || m_result.waves.isEmpty()) {
        m_chart->setTitle(QStringLiteral(
            "没有可显示的波形\n请在原理图中放置电压测试点（TP）或电流测试点（IP）后重新运行"));
        m_info->setText(QStringLiteral("没有测试点，波形为空。"));
        return;
    }

    struct Range {
        double min = std::numeric_limits<double>::max();
        double max = -std::numeric_limits<double>::max();
        bool valid() const { return max >= min; }
    };

    double xmin = std::numeric_limits<double>::max();
    double xmax = -std::numeric_limits<double>::max();
    Range volts;
    Range amps;
    Range others;

    for (int index : selected) {
        const Signal& s = m_result.waves.at(index);
        Range& range = s.unit == QLatin1String("V") ? volts
            : s.unit == QLatin1String("A")          ? amps
                                                    : others;
        for (double v : s.x) {
            xmin = std::min(xmin, v);
            xmax = std::max(xmax, v);
        }
        for (double v : s.y) {
            range.min = std::min(range.min, v);
            range.max = std::max(range.max, v);
        }
    }
    if (!(xmax > xmin))
        xmax = xmin + 1.0;

    const bool ac = m_result.kind == AnalysisKind::AC;

    // 时间轴自动选择 ms / µs，避免出现 0.00125 这样的刻度
    double xFactor = 1.0;
    QString xUnit = QStringLiteral("s");
    if (!ac) {
        const double magnitude = std::max(std::abs(xmin), std::abs(xmax));
        if (magnitude < 1e-6) {
            xFactor = 1e9;
            xUnit = QStringLiteral("ns");
        } else if (magnitude < 1e-3) {
            xFactor = 1e6;
            xUnit = QStringLiteral("µs");
        } else if (magnitude < 1.0) {
            xFactor = 1e3;
            xUnit = QStringLiteral("ms");
        }
    }

    QAbstractAxis* axisX = nullptr;
    bool logX = false;
    if (ac) {
        // 用 log10(f) 作为横轴，配合类别轴实现等间距的十倍频刻度
        auto* categoryAxis = new QCategoryAxis();
        const double lo = std::log10(std::max(xmin, 1e-12));
        const double hi = std::log10(std::max(xmax, 1e-11));
        categoryAxis->setRange(lo, hi);
        categoryAxis->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
        for (int decade = int(std::ceil(lo)); decade <= int(std::floor(hi)); ++decade) {
            QString label = formatEng(std::pow(10.0, decade), QString(), 2);
            label.remove(QLatin1Char(' '));
            categoryAxis->append(label, double(decade));
        }
        categoryAxis->setTitleText(QStringLiteral("频率 (Hz)"));
        axisX = categoryAxis;
        logX = true;
    } else {
        auto* valueAxis = new QValueAxis();
        applyNiceTicks(valueAxis, xmin * xFactor, xmax * xFactor);
        valueAxis->setLabelFormat(QStringLiteral("%.4g"));
        valueAxis->setTitleText(QStringLiteral("时间 (%1)").arg(xUnit));
        axisX = valueAxis;
    }

    // 电压与电流同时存在时使用左右双 Y 轴，避免 mA 级电流被 V 级电压压平
    const bool dualAxis = !ac && volts.valid() && amps.valid();

    auto scaleFor = [](const Range& range, QString* prefix) {
        const double magnitude = std::max(std::abs(range.min), std::abs(range.max));
        if (magnitude > 0.0 && magnitude < 1e-4) {
            *prefix = QStringLiteral("µ");
            return 1e6;
        }
        if (magnitude > 0.0 && magnitude < 1.0) {
            *prefix = QStringLiteral("m");
            return 1e3;
        }
        *prefix = QString();
        return 1.0;
    };

    QString voltagePrefix;
    QString currentPrefix;
    const double voltageScale = volts.valid() ? scaleFor(volts, &voltagePrefix) : 1.0;
    const double currentScale = amps.valid() ? scaleFor(amps, &currentPrefix) : 1.0;

    auto* axisY = new QValueAxis();
    QString yTitle;
    if (ac) {
        applyNiceTicks(axisY, others.min, others.max);
        yTitle = QStringLiteral("幅值 (dB)");
    } else if (dualAxis) {
        applyNiceTicks(axisY, volts.min * voltageScale, volts.max * voltageScale);
        yTitle = QStringLiteral("电压 (%1V)").arg(voltagePrefix);
    } else if (volts.valid()) {
        applyNiceTicks(axisY, volts.min * voltageScale, volts.max * voltageScale);
        yTitle = QStringLiteral("电压 (%1V)").arg(voltagePrefix);
    } else if (amps.valid()) {
        applyNiceTicks(axisY, amps.min * currentScale, amps.max * currentScale);
        yTitle = QStringLiteral("电流 (%1A)").arg(currentPrefix);
    } else {
        applyNiceTicks(axisY, others.min, others.max);
        yTitle = QStringLiteral("数值");
    }
    axisY->setLabelFormat(QStringLiteral("%.4g"));
    axisY->setTitleText(yTitle);

    QValueAxis* axisY2 = nullptr;
    if (dualAxis) {
        axisY2 = new QValueAxis();
        applyNiceTicks(axisY2, amps.min * currentScale, amps.max * currentScale);
        axisY2->setLabelFormat(QStringLiteral("%.4g"));
        axisY2->setTitleText(QStringLiteral("电流 (%1A)").arg(currentPrefix));
        axisY2->setGridLineVisible(false);
    }

    const QVector<QPair<QAbstractAxis*, Qt::Alignment>> axisList = {
        {axisX, Qt::AlignBottom}, {axisY, Qt::AlignLeft}, {axisY2, Qt::AlignRight}};
    for (const auto& entry : axisList) {
        if (!entry.first)
            continue;
        QAbstractAxis* axis = entry.first;
        axis->setGridLineColor(QColor(0xe2, 0xe8, 0xf0));
        axis->setLinePenColor(QColor(0x94, 0xa3, 0xb8));
        axis->setLabelsColor(QColor(0x47, 0x55, 0x69));
        axis->setTitleBrush(QColor(0x47, 0x55, 0x69));
        m_chart->addAxis(axis, entry.second);
    }

    for (int index : selected) {
        const Signal& s = m_result.waves.at(index);
        auto* series = new QLineSeries();
        series->setName(s.name);
        series->setPen(QPen(colorFor(index), 1.8));
        const int count = std::min(s.x.size(), s.y.size());
        const double yScale = s.unit == QLatin1String("V") ? voltageScale
            : s.unit == QLatin1String("A")                  ? currentScale
                                                            : 1.0;
        for (int i = 0; i < count; ++i) {
            const double xValue = logX ? std::log10(std::max(s.x.at(i), 1e-12))
                                       : s.x.at(i) * xFactor;
            series->append(xValue, s.y.at(i) * yScale);
        }
        m_chart->addSeries(series);
        series->attachAxis(axisX);
        if (axisY2 && s.unit == QLatin1String("A"))
            series->attachAxis(axisY2);
        else
            series->attachAxis(axisY);
    }

    m_info->setText(QStringLiteral("%1 个信号，已选 %2 个\n共 %3 个数据点")
                        .arg(m_result.waves.size())
                        .arg(selected.size())
                        .arg(pointCount));
}

void WaveformWindow::autoScale()
{
    if (!m_chart)
        return;
    m_chart->zoomReset();
    rebuild();
}

void WaveformWindow::selectAll(bool on)
{
    m_building = true;
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    m_building = false;
    rebuild();
}

void WaveformWindow::exportCsv()
{
    if (m_result.waves.isEmpty())
        return;

    QVector<int> selected;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->checkState() == Qt::Checked)
            selected.append(i);
    }
    if (selected.isEmpty())
        return;

    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出波形数据"),
                                                      QStringLiteral("waveform.csv"),
                                                      QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }
    QTextStream out(&file);
    out << "x";
    for (int index : selected)
        out << "," << m_result.waves.at(index).name;
    out << "\n";

    const int count = m_result.waves.first().x.size();
    for (int row = 0; row < count; ++row) {
        out << QString::number(m_result.waves.first().x.at(row), 'g', 10);
        for (int index : selected) {
            const Signal& s = m_result.waves.at(index);
            out << "," << (row < s.y.size() ? QString::number(s.y.at(row), 'g', 10) : QString());
        }
        out << "\n";
    }
    file.close();
}

void WaveformWindow::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出图片"),
                                                      QStringLiteral("waveform.png"),
                                                      QStringLiteral("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;
    const QPixmap shot = m_view->grab();
    if (!shot.save(path))
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入图片文件。"));
}

} // namespace csm
