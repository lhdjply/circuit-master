#include "ui/WaveformWindow.h"

#include "core/Circuit.h"

#include "core/Units.h"

#include <QChart>
#include <QCloseEvent>
#include <QComboBox>
#include <QChartView>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
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
#include <QTimer>
#include <QTextStream>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
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

void applyFixedTicks(QValueAxis* axis, double lo, double hi, int divisions)
{
    double span = hi - lo;
    if (!(span > 0.0))
        span = 1.0;
    const double rawStep = span / std::max(1, divisions);
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

    const double min = std::floor(lo / step) * step;
    axis->setRange(min, min + step * divisions);
    axis->setTickCount(divisions + 1);
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
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("波形监控 — 电路仿真大师"));
    setMinimumSize(720, 320);

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
    m_autoButton = new QPushButton(QStringLiteral("自动缩放"), chartSide);
    QPushButton* autoButton = m_autoButton;
    m_scopeButton = new QPushButton(QStringLiteral("示波器"), chartSide);
    m_scopeButton->setToolTip(QStringLiteral("以动态示波器方式回放瞬态波形"));
    m_scopeButton->setEnabled(false);
    auto* csvButton = new QPushButton(QStringLiteral("导出 CSV"), chartSide);
    auto* pngButton = new QPushButton(QStringLiteral("导出图片"), chartSide);
    toolbar->addWidget(autoButton);
    toolbar->addWidget(m_scopeButton);
    toolbar->addSpacing(12);
    m_tipLabel = new QLabel(QStringLiteral("在图上拖拽可框选放大，右键复位"), chartSide);
    m_tipLabel->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:11px;"));
    toolbar->addWidget(m_tipLabel);
    toolbar->addStretch(1);
    m_liveControls = new QWidget(chartSide);
    Q_UNUSED(m_liveControls);
    auto* liveLayout = new QHBoxLayout(m_liveControls);
    liveLayout->setContentsMargins(0, 0, 0, 0);
    liveLayout->setSpacing(6);
    m_liveButton = new QPushButton(QStringLiteral("暂停"), m_liveControls);
    m_liveWindowBox = new QComboBox(m_liveControls);
    const QVector<QPair<QString, double>> divisions = {
        {QStringLiteral("5 ns/div"), 5e-9},    {QStringLiteral("10 ns/div"), 1e-8},
        {QStringLiteral("20 ns/div"), 2e-8},   {QStringLiteral("50 ns/div"), 5e-8},
        {QStringLiteral("100 ns/div"), 1e-7},  {QStringLiteral("200 ns/div"), 2e-7},
        {QStringLiteral("500 ns/div"), 5e-7},  {QStringLiteral("1 µs/div"), 1e-6},
        {QStringLiteral("2 µs/div"), 2e-6},    {QStringLiteral("5 µs/div"), 5e-6},
        {QStringLiteral("10 µs/div"), 1e-5},   {QStringLiteral("20 µs/div"), 2e-5},
        {QStringLiteral("50 µs/div"), 5e-5},   {QStringLiteral("100 µs/div"), 1e-4},
        {QStringLiteral("200 µs/div"), 2e-4},  {QStringLiteral("500 µs/div"), 5e-4},
        {QStringLiteral("1 ms/div"), 1e-3},    {QStringLiteral("2 ms/div"), 2e-3},
        {QStringLiteral("5 ms/div"), 5e-3},    {QStringLiteral("10 ms/div"), 1e-2},
        {QStringLiteral("20 ms/div"), 2e-2},   {QStringLiteral("50 ms/div"), 5e-2},
        {QStringLiteral("100 ms/div"), 1e-1},  {QStringLiteral("200 ms/div"), 2e-1},
        {QStringLiteral("500 ms/div"), 5e-1},  {QStringLiteral("1 s/div"), 1.0},
        {QStringLiteral("2 s/div"), 2.0},      {QStringLiteral("5 s/div"), 5.0},
        {QStringLiteral("10 s/div"), 10.0},    {QStringLiteral("20 s/div"), 20.0},
        {QStringLiteral("50 s/div"), 50.0},    {QStringLiteral("100 s/div"), 100.0},
    };
    m_liveWindowBox->setMaxVisibleItems(20);
    m_liveWindowBox->setMinimumContentsLength(10);
    m_liveWindowBox->setMinimumWidth(104);
    for (const auto& entry : divisions)
        m_liveWindowBox->addItem(entry.first, entry.second);
    m_liveWindowBox->setCurrentIndex(15); // 500 µs/div
    m_liveVoltDivBox = new QComboBox(m_liveControls);
    const QVector<QPair<QString, double>> voltDivs = {
        {QStringLiteral("1 mV"), 1e-3},   {QStringLiteral("2 mV"), 2e-3},
        {QStringLiteral("5 mV"), 5e-3},   {QStringLiteral("10 mV"), 1e-2},
        {QStringLiteral("20 mV"), 2e-2},  {QStringLiteral("50 mV"), 5e-2},
        {QStringLiteral("100 mV"), 1e-1}, {QStringLiteral("200 mV"), 2e-1},
        {QStringLiteral("500 mV"), 5e-1}, {QStringLiteral("1 V"), 1.0},
        {QStringLiteral("2 V"), 2.0},     {QStringLiteral("5 V"), 5.0},
        {QStringLiteral("10 V"), 10.0},   {QStringLiteral("20 V"), 20.0},
        {QStringLiteral("50 V"), 50.0},   {QStringLiteral("100 V"), 100.0},
    };
    for (const auto& entry : voltDivs)
        m_liveVoltDivBox->addItem(entry.first, entry.second);
    m_liveVoltDivBox->setMinimumWidth(96);
    m_liveVoltDivBox->setCurrentIndex(11); // 5 V

    m_liveAmpDivBox = new QComboBox(m_liveControls);
    const QVector<QPair<QString, double>> ampDivs = {
        {QStringLiteral("1 µA"), 1e-6},   {QStringLiteral("2 µA"), 2e-6},
        {QStringLiteral("5 µA"), 5e-6},   {QStringLiteral("10 µA"), 1e-5},
        {QStringLiteral("20 µA"), 2e-5},  {QStringLiteral("50 µA"), 5e-5},
        {QStringLiteral("100 µA"), 1e-4}, {QStringLiteral("200 µA"), 2e-4},
        {QStringLiteral("500 µA"), 5e-4}, {QStringLiteral("1 mA"), 1e-3},
        {QStringLiteral("2 mA"), 2e-3},   {QStringLiteral("5 mA"), 5e-3},
        {QStringLiteral("10 mA"), 1e-2},  {QStringLiteral("20 mA"), 2e-2},
        {QStringLiteral("50 mA"), 5e-2},  {QStringLiteral("100 mA"), 1e-1},
        {QStringLiteral("1 A"), 1.0},
    };
    for (const auto& entry : ampDivs)
        m_liveAmpDivBox->addItem(entry.first, entry.second);
    m_liveAmpDivBox->setMinimumWidth(96);
    m_liveAmpDivBox->setCurrentIndex(9); // 1 mA

    auto makePair = [this](const QString& text, QComboBox* box) {
        auto* widget = new QWidget(m_liveControls);
        auto* layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        layout->addWidget(new QLabel(text, widget));
        layout->addWidget(box);
        return widget;
    };

    m_liveVoltWidget = makePair(QStringLiteral("电压/格"), m_liveVoltDivBox);
    m_liveAmpWidget = makePair(QStringLiteral("电流/格"), m_liveAmpDivBox);

    auto* stopLiveButton = new QPushButton(QStringLiteral("停止"), m_liveControls);
    liveLayout->addWidget(m_liveButton);
    liveLayout->addWidget(makePair(QStringLiteral("时间/格"), m_liveWindowBox));
    liveLayout->addWidget(m_liveVoltWidget);
    liveLayout->addWidget(m_liveAmpWidget);
    liveLayout->addWidget(stopLiveButton);
    m_liveControls->setVisible(false);

    toolbar->addWidget(m_liveControls);
    toolbar->addWidget(csvButton);
    toolbar->addWidget(pngButton);
    chartLayout->addLayout(toolbar);

    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(33);
    connect(m_liveTimer, &QTimer::timeout, this, &WaveformWindow::liveTick);
    connect(m_liveButton, &QPushButton::clicked, this,
            [this] { setLivePlaying(!m_livePlaying); });
    connect(stopLiveButton, &QPushButton::clicked, this, &WaveformWindow::stopLive);
    // ── 分析参数（静态波形页）────────────────────────────────
    m_analysisBar = new QWidget(chartSide);
    auto* analysisLayout = new QHBoxLayout(m_analysisBar);
    analysisLayout->setContentsMargins(10, 0, 10, 6);
    analysisLayout->setSpacing(6);

    m_kindBox = new QComboBox(m_analysisBar);
    m_kindBox->addItem(QStringLiteral("瞬态"), int(AnalysisKind::Transient));
    m_kindBox->addItem(QStringLiteral("交流扫频"), int(AnalysisKind::AC));
    m_kindBox->addItem(QStringLiteral("工作点"), int(AnalysisKind::OperatingPoint));

    const auto makeField = [this](const QString& placeholder, int width) {
        auto* edit = new QLineEdit(m_analysisBar);
        edit->setPlaceholderText(placeholder);
        edit->setFixedWidth(width);
        return edit;
    };
    m_tStopEdit = makeField(QStringLiteral("5m"), 78);
    m_tStepEdit = makeField(QStringLiteral("自动"), 78);
    m_fStartEdit = makeField(QStringLiteral("10"), 74);
    m_fStopEdit = makeField(QStringLiteral("1M"), 74);
    m_pointsEdit = makeField(QStringLiteral("100"), 60);

    auto* runButton = new QPushButton(QStringLiteral("重新仿真"), m_analysisBar);

    analysisLayout->addWidget(new QLabel(QStringLiteral("分析"), m_analysisBar));
    analysisLayout->addWidget(m_kindBox);
    analysisLayout->addWidget(new QLabel(QStringLiteral("停止时间"), m_analysisBar));
    analysisLayout->addWidget(m_tStopEdit);
    analysisLayout->addWidget(new QLabel(QStringLiteral("最大步长"), m_analysisBar));
    analysisLayout->addWidget(m_tStepEdit);
    analysisLayout->addWidget(new QLabel(QStringLiteral("起始"), m_analysisBar));
    analysisLayout->addWidget(m_fStartEdit);
    analysisLayout->addWidget(new QLabel(QStringLiteral("终止"), m_analysisBar));
    analysisLayout->addWidget(m_fStopEdit);
    analysisLayout->addWidget(new QLabel(QStringLiteral("点数"), m_analysisBar));
    analysisLayout->addWidget(m_pointsEdit);
    analysisLayout->addWidget(runButton);
    analysisLayout->addStretch(1);

    chartLayout->addWidget(m_analysisBar);

    connect(m_kindBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { updateAnalysisRows(); });
    for (QLineEdit* edit : {m_tStopEdit, m_tStepEdit, m_fStartEdit, m_fStopEdit, m_pointsEdit})
        connect(edit, &QLineEdit::editingFinished, this, &WaveformWindow::applyAnalysisFields);
    connect(runButton, &QPushButton::clicked, this, [this] {
        applyAnalysisFields();
        emit analysisRequested(m_options);
    });

    connect(m_liveWindowBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] {
                m_liveDiv = m_liveWindowBox->currentData().toDouble();
                m_liveWindow = m_liveDiv * 10.0;
                if (m_live)
                    restartLiveEngine();
                emit liveScalesChanged(m_liveDiv, m_liveVoltDiv, m_liveAmpDiv);
            });
    connect(m_liveVoltDivBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] {
                m_liveVoltDiv = m_liveVoltDivBox->currentData().toDouble();
                applyLiveScales();
                emit liveScalesChanged(m_liveDiv, m_liveVoltDiv, m_liveAmpDiv);
            });
    connect(m_liveAmpDivBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] {
                m_liveAmpDiv = m_liveAmpDivBox->currentData().toDouble();
                applyLiveScales();
                emit liveScalesChanged(m_liveDiv, m_liveVoltDiv, m_liveAmpDiv);
            });

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
    connect(m_scopeButton, &QPushButton::clicked, this, &WaveformWindow::scopeRequested);
    connect(csvButton, &QPushButton::clicked, this, &WaveformWindow::exportCsv);
    connect(pngButton, &QPushButton::clicked, this, &WaveformWindow::exportImage);
    m_view->installEventFilter(this);

    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem*) {
        if (!m_building)
            rebuild();
    });
}

void WaveformWindow::closeEvent(QCloseEvent* event)
{
    stopLive();
    QWidget::closeEvent(event);
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

void WaveformWindow::setOptions(const SimulationOptions& options)
{
    m_options = options;
    if (m_kindBox) {
        const int index = m_kindBox->findData(int(options.kind));
        if (index >= 0) {
            QSignalBlocker blocker(m_kindBox);
            m_kindBox->setCurrentIndex(index);
        }
    }
    const auto setText = [](QLineEdit* edit, const QString& text) {
        if (edit)
            edit->setText(text);
    };
    setText(m_tStopEdit, formatEng(m_options.tStop, QStringLiteral("s"), 4));
    setText(m_tStepEdit, formatEng(m_options.tStep, QStringLiteral("s"), 4));
    setText(m_fStartEdit, formatEng(m_options.fStart, QStringLiteral("Hz"), 4));
    setText(m_fStopEdit, formatEng(m_options.fStop, QStringLiteral("Hz"), 4));
    setText(m_pointsEdit, QString::number(m_options.acPoints));
    updateAnalysisRows();
}

void WaveformWindow::updateAnalysisRows()
{
    if (!m_kindBox)
        return;
    const auto kind = AnalysisKind(m_kindBox->currentData().toInt());
    const bool transient = kind == AnalysisKind::Transient;
    const bool ac = kind == AnalysisKind::AC;

    m_tStopEdit->setVisible(transient);
    m_tStepEdit->setVisible(transient);
    m_fStartEdit->setVisible(ac);
    m_fStopEdit->setVisible(ac);
    m_pointsEdit->setVisible(ac);

    // 标签跟着字段一起显隐：遍历分析条里的 QLabel，按顺序配对
    const QList<QLabel*> labels = m_analysisBar->findChildren<QLabel*>();
    for (QLabel* label : labels) {
        const QString text = label->text();
        if (text == QStringLiteral("停止时间") || text == QStringLiteral("最大步长"))
            label->setVisible(transient);
        else if (text == QStringLiteral("起始") || text == QStringLiteral("终止")
                 || text == QStringLiteral("点数"))
            label->setVisible(ac);
    }
}

void WaveformWindow::applyAnalysisFields()
{
    if (!m_kindBox)
        return;

    m_options.kind = AnalysisKind(m_kindBox->currentData().toInt());

    const auto parse = [](QLineEdit* edit, UnitKind unit, double* out) {
        if (!edit || edit->text().trimmed().isEmpty())
            return true; // 空着就沿用默认值
        bool ok = false;
        QString reason;
        const double value = parseQuantity(edit->text(), unit, &ok, &reason);
        if (!ok) {
            edit->setStyleSheet(QStringLiteral("border:1px solid #dc2626;"));
            return false;
        }
        edit->setStyleSheet(QString());
        *out = value;
        return true;
    };

    bool ok = true;
    if (m_options.kind == AnalysisKind::Transient) {
        ok = parse(m_tStopEdit, UnitKind::Second, &m_options.tStop) && ok;
        ok = parse(m_tStepEdit, UnitKind::Second, &m_options.tStep) && ok;
    } else if (m_options.kind == AnalysisKind::AC) {
        ok = parse(m_fStartEdit, UnitKind::Hertz, &m_options.fStart) && ok;
        ok = parse(m_fStopEdit, UnitKind::Hertz, &m_options.fStop) && ok;
        if (m_pointsEdit && !m_pointsEdit->text().trimmed().isEmpty()) {
            bool pointsOk = false;
            const int points = m_pointsEdit->text().toInt(&pointsOk);
            if (pointsOk && points >= 2)
                m_options.acPoints = points;
        }
    }
    Q_UNUSED(ok);
}

bool WaveformWindow::updateLiveCircuit(const Circuit& circuit,
                                       const QVector<QPair<QString, QString>>& probes)
{
    if (!m_live || !m_livePlaying)
        return false;

    // 测试点变了（例如节点编号变了）就要重建，否则原地更新即可
    if (probes != m_probes)
        return false;

    QString error;
    if (!m_live->updateCircuit(circuit, &error)) {
        m_info->setText(QStringLiteral("电路更新失败：%1").arg(error));
        return false;
    }

    m_liveCircuit = circuit;
    m_info->setText(QStringLiteral("电路已更新，仿真继续"));
    emit liveUpdated(); // 立刻按新电流刷新 LED 等指示
    return true;
}

void WaveformWindow::setLiveScalePreferences(double timeDiv, double voltDiv, double ampDiv)
{
    const auto pick = [](QComboBox* box, double value) {
        if (!box || box->count() == 0 || value <= 0.0)
            return;
        int best = 0;
        double bestDiff = std::abs(box->itemData(0).toDouble() - value);
        for (int i = 1; i < box->count(); ++i) {
            const double diff = std::abs(box->itemData(i).toDouble() - value);
            if (diff < bestDiff) {
                bestDiff = diff;
                best = i;
            }
        }
        QSignalBlocker blocker(box);
        box->setCurrentIndex(best);
    };
    pick(m_liveWindowBox, timeDiv);
    pick(m_liveVoltDivBox, voltDiv);
    pick(m_liveAmpDivBox, ampDiv);
}

void WaveformWindow::setResult(const SimulationResult& result)
{
    stopLive();
    m_result = result;
    populateList();
    rebuild();
    setWindowTitle(QStringLiteral("波形查看器 — %1 — 电路仿真大师").arg(kindName(result.kind)));
    if (m_scopeButton)
        m_scopeButton->setEnabled(result.kind == AnalysisKind::Transient
                                  && !result.waves.isEmpty());
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
        // 时间轴严格止于仿真结束时间，按 10 格等分
        valueAxis->setRange(xmin * xFactor, xmax * xFactor);
        valueAxis->setTickCount(11);
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
        applyFixedTicks(axisY, others.min, others.max, 6);
        yTitle = QStringLiteral("幅值 (dB)");
    } else if (dualAxis) {
        applyFixedTicks(axisY, volts.min * voltageScale, volts.max * voltageScale, 6);
        yTitle = QStringLiteral("电压 (%1V)").arg(voltagePrefix);
    } else if (volts.valid()) {
        applyFixedTicks(axisY, volts.min * voltageScale, volts.max * voltageScale, 6);
        yTitle = QStringLiteral("电压 (%1V)").arg(voltagePrefix);
    } else if (amps.valid()) {
        applyFixedTicks(axisY, amps.min * currentScale, amps.max * currentScale, 6);
        yTitle = QStringLiteral("电流 (%1A)").arg(currentPrefix);
    } else {
        applyFixedTicks(axisY, others.min, others.max, 6);
        yTitle = QStringLiteral("数值");
    }
    axisY->setLabelFormat(QStringLiteral("%.4g"));
    axisY->setTitleText(yTitle);

    QValueAxis* axisY2 = nullptr;
    if (dualAxis) {
        axisY2 = new QValueAxis();
        applyFixedTicks(axisY2, amps.min * currentScale, amps.max * currentScale, 6);
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

void WaveformWindow::applyLiveScales()
{
    if (!m_liveAxisY)
        return;

    const bool leftIsCurrent = !m_liveHasVoltage && m_liveHasCurrent;
    if (leftIsCurrent)
        m_liveAxisY->setTitleText(QStringLiteral("电流 (A)"));
    else
        m_liveAxisY->setTitleText(QStringLiteral("电压 (V)"));

    m_liveAxisY->setTickCount(7);
    m_liveAxisY->setRange(-3.0 * (leftIsCurrent ? m_liveAmpDiv : m_liveVoltDiv),
                          3.0 * (leftIsCurrent ? m_liveAmpDiv : m_liveVoltDiv));

    if (m_liveAxisY2) {
        m_liveAxisY2->setTickCount(7);
        m_liveAxisY2->setTitleText(QStringLiteral("电流 (A)"));
        m_liveAxisY2->setRange(-3.0 * m_liveAmpDiv, 3.0 * m_liveAmpDiv);
    }
}

/// 启动后按峰值选一次量程；之后只有在波形超出量程时才自动放大，
/// 不会随信号变化来回跳动（缩小需用户手动调整）。
void WaveformWindow::autoPickLiveScales()
{
    double voltPeak = 0.0;
    double ampPeak = 0.0;
    int samples = 0;
    for (const Signal& wave : m_liveBuffers) {
        samples = std::max(samples, int(wave.y.size()));
        for (double value : wave.y) {
            if (wave.unit == QLatin1String("V"))
                voltPeak = std::max(voltPeak, std::abs(value));
            else
                ampPeak = std::max(ampPeak, std::abs(value));
        }
    }

    // 数据太少时不判断，避免把启动瞬间的 0 V 当成峰值
    if (samples < 50)
        return;

    const bool needInitial = !m_liveAutoScaled;
    const bool voltageClipped = m_liveHasVoltage && voltPeak > m_liveVoltDiv * 2.9;
    const bool currentClipped = m_liveHasCurrent && ampPeak > m_liveAmpDiv * 2.9;
    if (!needInitial && !voltageClipped && !currentClipped)
        return;

    auto pick = [](QComboBox* box, double peak) {
        if (!box || peak <= 0.0)
            return;
        // 峰值占据约 2.5 格：选“不小于 peak/2.5 的最小档”，保证不超出 ±3 格
        const double target = peak / 2.5;
        int best = box->count() - 1;
        for (int i = 0; i < box->count(); ++i) {
            if (box->itemData(i).toDouble() >= target) {
                best = i;
                break;
            }
        }
        box->setCurrentIndex(best);
    };

    if (m_liveHasVoltage && (needInitial || voltageClipped))
        pick(m_liveVoltDivBox, voltPeak);
    if (m_liveHasCurrent && (needInitial || currentClipped))
        pick(m_liveAmpDivBox, ampPeak);

    m_liveVoltDiv = m_liveVoltDivBox->currentData().toDouble();
    m_liveAmpDiv = m_liveAmpDivBox->currentData().toDouble();
    m_liveAutoScaled = true;
    applyLiveScales();
}

void WaveformWindow::restartLiveEngine()
{
    m_live = std::make_unique<LiveTransient>();

    // 步长同时受两件事约束：
    //  1) 时间窗分辨率：每个窗口约 500 个采样点
    //  2) 信号本身：每个电源周期至少 20 点，避免大时基下采样混叠（正弦被采成 0）
    double step = m_liveWindow / 500.0;
    const double suggested = m_liveCircuit.suggestedMaxStep();
    if (suggested > 0.0)
        step = std::min(step, suggested);
    // 下限防止步数爆炸（每窗口最多 2 万步）
    step = std::max({step, m_liveWindow / 20000.0, 1e-12});
    QString error;
    if (!m_live->start(m_liveCircuit, step, &error)) {
        m_live.reset();
        m_info->setText(QStringLiteral("无法启动实时仿真：%1").arg(error));
        return;
    }

    for (Signal& buffer : m_liveBuffers) {
        buffer.x.clear();
        buffer.y.clear();
    }
    m_liveAutoScaled = false;
    updateLiveDecimation();
    for (QLineSeries* series : m_liveSeries)
        series->clear();
}

void WaveformWindow::updateLiveDecimation()
{
    const double h = m_live ? m_live->step() : 1e-5;
    const double pointsPerWindow = m_liveWindow / std::max(h, 1e-12);
    m_liveDecimation = std::max(1, int(std::ceil(pointsPerWindow / 4000.0)));
    m_liveSampleCounter = 0;
}

double WaveformWindow::liveTime() const
{
    return m_live ? m_live->time() : 0.0;
}

void WaveformWindow::startLive(const Circuit& circuit,
                               const QVector<QPair<QString, QString>>& probes)
{
    m_restartingLive = true;
    stopLive();
    m_restartingLive = false;
    m_probes = probes;
    m_liveCircuit = circuit;

    if (!m_liveTimer)
        return;

    m_liveBuffers.clear();
    for (const QPair<QString, QString>& probe : probes) {
        Signal wave;
        wave.name = probe.first;
        wave.unit = probe.second.startsWith(QLatin1String("V(")) ? QStringLiteral("V")
                                                                  : QStringLiteral("A");
        m_liveBuffers.append(wave);
    }

    m_building = true;
    m_list->clear();
    for (int i = 0; i < probes.size(); ++i) {
        auto* item = new QListWidgetItem(swatch(colorFor(i)), probes.at(i).first);
        item->setFlags(Qt::ItemIsEnabled);
        m_list->addItem(item);
    }
    m_building = false;

    m_chart->removeAllSeries();
    const QList<QAbstractAxis*> oldAxes = m_chart->axes();
    for (QAbstractAxis* axis : oldAxes) {
        m_chart->removeAxis(axis);
        delete axis;
    }

    bool hasVoltage = false;
    bool hasCurrent = false;
    for (const Signal& wave : m_liveBuffers) {
        if (wave.unit == QLatin1String("V"))
            hasVoltage = true;
        else
            hasCurrent = true;
    }
    m_liveHasVoltage = hasVoltage;
    m_liveHasCurrent = hasCurrent;
    m_liveAutoScaled = false;
    m_liveVoltDiv = m_liveVoltDivBox->currentData().toDouble();
    m_liveAmpDiv = m_liveAmpDivBox->currentData().toDouble();
    if (m_liveVoltWidget)
        m_liveVoltWidget->setVisible(hasVoltage);
    if (m_liveAmpWidget)
        m_liveAmpWidget->setVisible(hasCurrent);

    m_liveAxisX = new QValueAxis();
    m_liveAxisX->setLabelFormat(QStringLiteral("%.4g"));
    m_liveAxisX->setTickCount(11);
    m_chart->addAxis(m_liveAxisX, Qt::AlignBottom);

    m_liveAxisY = new QValueAxis();
    m_liveAxisY->setLabelFormat(QStringLiteral("%.4g"));
    m_liveAxisY->setTickCount(7);
    m_liveAxisY->setTitleText(hasVoltage ? QStringLiteral("电压 (V)")
                                         : QStringLiteral("电流 (A)"));
    m_chart->addAxis(m_liveAxisY, Qt::AlignLeft);

    m_liveAxisY2 = nullptr;
    if (hasVoltage && hasCurrent) {
        m_liveAxisY2 = new QValueAxis();
        m_liveAxisY2->setLabelFormat(QStringLiteral("%.4g"));
        m_liveAxisY2->setTickCount(7);
        m_liveAxisY2->setTitleText(QStringLiteral("电流 (A)"));
        m_liveAxisY2->setGridLineVisible(false);
        m_chart->addAxis(m_liveAxisY2, Qt::AlignRight);
    }

    m_liveSeries.clear();
    for (int i = 0; i < probes.size(); ++i) {
        auto* series = new QLineSeries();
        series->setName(probes.at(i).first);
        series->setPen(QPen(colorFor(i), 1.8));
        m_chart->addSeries(series);
        series->attachAxis(m_liveAxisX);
        const bool current = m_liveBuffers.at(i).unit == QLatin1String("A") && m_liveAxisY2;
        series->attachAxis(current ? m_liveAxisY2 : m_liveAxisY);
        m_liveSeries.append(series);
    }

    m_liveDiv = m_liveWindowBox->currentData().toDouble();
    m_liveWindow = m_liveDiv * 10.0;
    m_liveAxisX->setRange(0.0, m_liveWindow);
    applyLiveScales();

    m_chart->setTitle(QString());
    m_liveControls->setVisible(true);
    m_scopeButton->setVisible(false);
    if (m_tipLabel)
        m_tipLabel->setVisible(false);
    if (m_autoButton)
        m_autoButton->setVisible(false);
    // 实时模式没有「分析参数」的概念，整条收起
    if (m_analysisBar)
        m_analysisBar->setVisible(false);
    if (m_autoButton)
        m_autoButton->setEnabled(false);
    setWindowTitle(QStringLiteral("实时示波器 — 电路仿真大师"));
    m_info->setText(m_liveBuffers.isEmpty()
                        ? QStringLiteral("电路中没有测试点，波形为空。\n"
                                         "放置电压测试点（TP）或电流测试点（IP）后再运行。")
                        : QStringLiteral("实时仿真启动…"));

    restartLiveEngine();
    m_liveClock.start();
    setLivePlaying(true);
    liveTick();
    emit liveStateChanged(true);
}

void WaveformWindow::stopLiveScope()
{
    stopLive();
}

void WaveformWindow::stopLive()
{
    const bool wasLive = m_live != nullptr;
    if (m_liveTimer)
        m_liveTimer->stop();
    m_livePlaying = false;
    m_live.reset();
    m_liveBuffers.clear();
    m_liveSeries.clear();
    m_liveAxisX = nullptr;
    m_liveAxisY = nullptr;
    m_liveAxisY2 = nullptr;
    if (m_liveControls)
        m_liveControls->setVisible(false);
    if (m_scopeButton)
        m_scopeButton->setVisible(true);
    if (m_tipLabel)
        m_tipLabel->setVisible(true);
    if (m_autoButton)
        m_autoButton->setVisible(true);
    if (m_analysisBar)
        m_analysisBar->setVisible(true);
    if (m_autoButton)
        m_autoButton->setEnabled(true);
    if (wasLive && !m_restartingLive)
        emit liveStateChanged(false);
}

void WaveformWindow::setLivePlaying(bool playing)
{
    m_livePlaying = playing;
    if (m_liveButton)
        m_liveButton->setText(playing ? QStringLiteral("暂停") : QStringLiteral("继续"));
    if (!m_liveTimer)
        return;
    if (playing) {
        m_liveClock.restart();
        m_liveTimer->start();
    } else {
        m_liveTimer->stop();
    }
}

QHash<int, double> WaveformWindow::liveBlownCapacitors() const
{
    return m_live ? m_live->reverseCapacitors() : QHash<int, double>();
}

double WaveformWindow::liveValue(const QString& name) const
{
    if (!m_live)
        return 0.0;
    const int index = m_live->signalNames().indexOf(name);
    if (index < 0)
        return 0.0;
    const QVector<double> values = m_live->values();
    return index < values.size() ? values.at(index) : 0.0;
}

void WaveformWindow::liveTick()
{
    if (!m_live || !m_livePlaying)
        return;

    double dt = double(m_liveClock.restart()) / 1000.0;
    dt = std::min(dt, 0.1);
    const double h = m_live->step();
    int steps = int(std::lround(dt * m_liveSpeed / std::max(h, 1e-12)));
    steps = std::max(1, std::min(steps, 200));

    QString error;
    const QStringList names = m_live->signalNames();
    QVector<int> indices;
    for (const QPair<QString, QString>& probe : m_probes)
        indices.append(names.indexOf(probe.second));

    for (int k = 0; k < steps; ++k) {
        if (!m_live->advance(1, &error)) {
            setLivePlaying(false);
            m_info->setText(QStringLiteral("实时仿真中止：%1").arg(error));
            return;
        }
        const bool keep = (m_liveSampleCounter % m_liveDecimation == 0) || (k == steps - 1);
        ++m_liveSampleCounter;
        if (!keep)
            continue;

        const double sampleTime = m_live->time();
        const QVector<double> values = m_live->values();
        for (int i = 0; i < m_liveBuffers.size(); ++i) {
            const int index = indices.at(i);
            if (index < 0 || index >= values.size())
                continue;
            m_liveBuffers[i].x.append(sampleTime);
            m_liveBuffers[i].y.append(values.at(index));
        }
    }

    emit liveUpdated();

    const double time = m_live->time();
    const double cutoff = time - 2.0 * m_liveWindow;
    for (int i = 0; i < m_liveBuffers.size(); ++i) {
        int drop = 0;
        while (drop < m_liveBuffers.at(i).x.size()
               && m_liveBuffers.at(i).x.at(drop) < cutoff)
            ++drop;
        if (drop > 0) {
            m_liveBuffers[i].x.remove(0, drop);
            m_liveBuffers[i].y.remove(0, drop);
        }
    }

    double timeScale = 1.0;
    QString timeUnit = QStringLiteral("s");
    if (m_liveWindow < 1e-6) {
        timeScale = 1e9;
        timeUnit = QStringLiteral("ns");
    } else if (m_liveWindow < 1e-3) {
        timeScale = 1e6;
        timeUnit = QStringLiteral("µs");
    } else if (m_liveWindow < 1.0) {
        timeScale = 1e3;
        timeUnit = QStringLiteral("ms");
    }
    const QString timeTitle = QStringLiteral("时间 (%1)").arg(timeUnit);
    if (m_liveAxisX->titleText() != timeTitle)
        m_liveAxisX->setTitleText(timeTitle);

    // 时间轴固定为 0..窗口，数据在其中向左滚动
    const double timeOffset = std::max(0.0, time - m_liveWindow);
    m_liveAxisX->setRange(0.0, m_liveWindow * timeScale);

    for (int i = 0; i < m_liveSeries.size(); ++i) {
        QVector<QPointF> points;
        const Signal& wave = m_liveBuffers.at(i);
        points.reserve(wave.x.size());
        for (int k = 0; k < wave.x.size(); ++k) {
            if (wave.x.at(k) < timeOffset)
                continue;
            points.append(QPointF((wave.x.at(k) - timeOffset) * timeScale, wave.y.at(k)));
        }
        m_liveSeries[i]->replace(points);
    }

    autoPickLiveScales();

    m_info->setText(QStringLiteral("t = %1  ·  %2/div  ·  步长 %3")
                        .arg(formatEng(time, QStringLiteral("s"), 3),
                             formatEng(m_liveDiv, QStringLiteral("s"), 2),
                             formatEng(h, QStringLiteral("s"), 2)));
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
