#include "ui/PropertyPanel.h"

#include "core/Units.h"
#include "ui/ComponentItem.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>

namespace csm {

PropertyPanel::PropertyPanel(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    m_stack->setCurrentWidget(m_placeholder);
}

namespace {

QString quantityText(double value, UnitKind unit)
{
    return formatEng(value, unitSymbol(unit), 6);
}

UnitKind valueUnitFor(ComponentType type)
{
    switch (type) {
    case ComponentType::Resistor:  return UnitKind::Ohm;
    case ComponentType::Capacitor: return UnitKind::Farad;
    case ComponentType::Inductor:  return UnitKind::Henry;
    case ComponentType::VoltageSource: return UnitKind::Volt;
    case ComponentType::CurrentSource: return UnitKind::Ampere;
    default: return UnitKind::None;
    }
}

UnitKind sourceUnitFor(ComponentType type)
{
    return type == ComponentType::CurrentSource ? UnitKind::Ampere : UnitKind::Volt;
}

bool parseField(QLineEdit* edit, UnitKind unit, double* value, QString* reason)
{
    bool ok = false;
    const double parsed = parseQuantity(edit->text(), unit, &ok, reason);
    if (!ok)
        return false;
    *value = parsed;
    return true;
}

QLineEdit* makeEdit(const QString& placeholder = QString())
{
    auto* edit = new QLineEdit;
    edit->setPlaceholderText(placeholder);
    edit->setClearButtonEnabled(false);
    return edit;
}

} // namespace

void PropertyPanel::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    m_stack = new QStackedWidget(this);

    m_placeholder = new QLabel(QStringLiteral("在原理图中选择一个元件\n以编辑其参数"), this);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setStyleSheet(QStringLiteral("color:#94a3b8; padding:30px;"));
    m_stack->addWidget(m_placeholder);

    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(10);

    m_title = new QLabel(page);
    m_title->setStyleSheet(QStringLiteral("font-size:14px; font-weight:600; color:#1e293b;"));
    pageLayout->addWidget(m_title);

    m_subtitle = new QLabel(page);
    m_subtitle->setStyleSheet(QStringLiteral("color:#64748b;"));
    pageLayout->addWidget(m_subtitle);

    m_form = new QFormLayout();
    m_form->setLabelAlignment(Qt::AlignLeft);
    m_form->setFormAlignment(Qt::AlignTop);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_form->setHorizontalSpacing(10);
    m_form->setVerticalSpacing(8);

    m_refdes = makeEdit();
    m_form->addRow(QStringLiteral("编号"), m_refdes);

    m_value = makeEdit(QStringLiteral("例如 10k / 4.7u"));
    m_form->addRow(QStringLiteral("数值"), m_value);
    m_valueLabel = qobject_cast<QLabel*>(m_form->labelForField(m_value));

    m_function = new QComboBox(page);
    m_function->addItem(QStringLiteral("直流 (DC)"), int(SourceFunction::Constant));
    m_function->addItem(QStringLiteral("正弦 (SIN)"), int(SourceFunction::Sine));
    m_function->addItem(QStringLiteral("脉冲 (PULSE)"), int(SourceFunction::Pulse));
    m_form->addRow(QStringLiteral("波形"), m_function);

    m_dc = makeEdit(QStringLiteral("5"));
    m_form->addRow(QStringLiteral("直流值"), m_dc);

    m_amplitude = makeEdit();
    m_form->addRow(QStringLiteral("幅值"), m_amplitude);
    m_frequency = makeEdit();
    m_form->addRow(QStringLiteral("频率"), m_frequency);
    m_phase = makeEdit();
    m_form->addRow(QStringLiteral("相位 °"), m_phase);
    m_offset = makeEdit();
    m_form->addRow(QStringLiteral("偏置"), m_offset);
    m_delay = makeEdit();
    m_form->addRow(QStringLiteral("延迟"), m_delay);

    m_pulseV1 = makeEdit();
    m_form->addRow(QStringLiteral("初始值"), m_pulseV1);
    m_pulseV2 = makeEdit();
    m_form->addRow(QStringLiteral("脉冲值"), m_pulseV2);
    m_rise = makeEdit();
    m_form->addRow(QStringLiteral("上升时间"), m_rise);
    m_fall = makeEdit();
    m_form->addRow(QStringLiteral("下降时间"), m_fall);
    m_width = makeEdit();
    m_form->addRow(QStringLiteral("脉宽"), m_width);
    m_period = makeEdit();
    m_form->addRow(QStringLiteral("周期"), m_period);

    m_closed = new QCheckBox(QStringLiteral("开关闭合"), page);
    m_form->addRow(QString(), m_closed);

    pageLayout->addLayout(m_form);
    pageLayout->addStretch(1);

    auto* hint = new QLabel(
        QStringLiteral("提示：数值可直接带单位（10kΩ、4.7µF、10mH、5V、1mA）；"
                       "不写单位时按元件默认单位。"),
        page);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:11px;"));
    pageLayout->addWidget(hint);

    m_stack->addWidget(page);
    root->addWidget(m_stack);

    connect(m_refdes, &QLineEdit::editingFinished, this, &PropertyPanel::applyRefdes);
    connect(m_value, &QLineEdit::editingFinished, this, &PropertyPanel::applyValue);
    connect(m_function, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::applyFunction);
    connect(m_closed, &QCheckBox::toggled, this, &PropertyPanel::applyClosed);

    connect(m_dc, &QLineEdit::editingFinished, this, &PropertyPanel::applyDc);
    for (QLineEdit* edit : {m_amplitude, m_frequency, m_phase, m_offset, m_delay})
        connect(edit, &QLineEdit::editingFinished, this, &PropertyPanel::applySine);
    for (QLineEdit* edit : {m_pulseV1, m_pulseV2, m_rise, m_fall, m_width, m_period})
        connect(edit, &QLineEdit::editingFinished, this, &PropertyPanel::applyPulse);
}

void PropertyPanel::setComponent(ComponentItem* item)
{
    m_item = item;
    if (!item) {
        m_stack->setCurrentWidget(m_placeholder);
        return;
    }
    m_stack->setCurrentIndex(1);
    refreshFromItem();
    updateVisibility();
}

void PropertyPanel::refreshFromItem()
{
    if (!m_item)
        return;

    m_updating = true;
    const Component& c = m_item->component();

    m_title->setText(QStringLiteral("%1  ·  %2").arg(typeName(c.type), c.refdes));

    m_refdes->setText(c.refdes);
    m_value->setText(quantityText(c.value, valueUnitFor(c.type)));
    m_function->setCurrentIndex(m_function->findData(int(c.function)));

    const UnitKind unit = sourceUnitFor(c.type);
    m_dc->setText(quantityText(c.dcValue, unit));
    m_amplitude->setText(quantityText(c.amplitude, unit));
    m_frequency->setText(quantityText(c.frequency, UnitKind::Hertz));
    m_phase->setText(formatEng(c.phaseDeg, QStringLiteral("°"), 6));
    m_offset->setText(quantityText(c.offset, unit));
    m_delay->setText(quantityText(c.delayTime, UnitKind::Second));

    m_pulseV1->setText(quantityText(c.pulseV1, unit));
    m_pulseV2->setText(quantityText(c.pulseV2, unit));
    m_rise->setText(quantityText(c.pulseRise, UnitKind::Second));
    m_fall->setText(quantityText(c.pulseFall, UnitKind::Second));
    m_width->setText(quantityText(c.pulseWidth, UnitKind::Second));
    m_period->setText(quantityText(c.pulsePeriod, UnitKind::Second));

    m_closed->setChecked(c.closed);

    m_updating = false;
    updateSubtitle();
}

void PropertyPanel::updateVisibility()
{
    if (!m_item)
        return;

    const Component c = m_item->component();
    const bool source = isSource(c.type);
    const bool analog = c.type == ComponentType::Resistor || c.type == ComponentType::Capacitor
        || c.type == ComponentType::Inductor || c.type == ComponentType::OpAmp;

    if (m_valueLabel) {
        switch (c.type) {
        case ComponentType::Resistor:
            m_valueLabel->setText(QStringLiteral("数值 (Ω)"));
            m_value->setPlaceholderText(QStringLiteral("例如 10kΩ / 4.7k"));
            break;
        case ComponentType::Capacitor:
            m_valueLabel->setText(QStringLiteral("数值 (F)"));
            m_value->setPlaceholderText(QStringLiteral("例如 100nF / 1µF"));
            break;
        case ComponentType::Inductor:
            m_valueLabel->setText(QStringLiteral("数值 (H)"));
            m_value->setPlaceholderText(QStringLiteral("例如 10mH"));
            break;
        case ComponentType::OpAmp:
            m_valueLabel->setText(QStringLiteral("开环增益"));
            m_value->setPlaceholderText(QStringLiteral("例如 100k"));
            break;
        default:
            m_valueLabel->setText(QStringLiteral("数值"));
            break;
        }
    }

    if (isSource(c.type)) {
        const QString unit = unitSymbol(sourceUnitFor(c.type));
        m_dc->setPlaceholderText(QStringLiteral("例如 5%1").arg(unit));
        m_amplitude->setPlaceholderText(QStringLiteral("例如 1%1").arg(unit));
        m_offset->setPlaceholderText(QStringLiteral("例如 0%1").arg(unit));
        m_pulseV1->setPlaceholderText(QStringLiteral("例如 0%1").arg(unit));
        m_pulseV2->setPlaceholderText(QStringLiteral("例如 5%1").arg(unit));
    }
    m_frequency->setPlaceholderText(QStringLiteral("例如 1kHz"));
    m_delay->setPlaceholderText(QStringLiteral("例如 1ms"));
    for (QLineEdit* edit : {m_rise, m_fall, m_width, m_period})
        edit->setPlaceholderText(QStringLiteral("例如 1ms"));

    m_form->setRowVisible(m_value, analog);
    m_form->setRowVisible(m_function, source);
    m_form->setRowVisible(m_dc, source && c.function == SourceFunction::Constant);
    m_form->setRowVisible(m_closed, c.type == ComponentType::Switch);

    const bool sine = source && c.function == SourceFunction::Sine;
    for (QLineEdit* edit : {m_amplitude, m_frequency, m_phase, m_offset, m_delay})
        m_form->setRowVisible(edit, sine);

    const bool pulse = source && c.function == SourceFunction::Pulse;
    for (QLineEdit* edit : {m_pulseV1, m_pulseV2, m_rise, m_fall, m_width, m_period})
        m_form->setRowVisible(edit, pulse);
}

void PropertyPanel::updateSubtitle()
{
    if (!m_item)
        return;
    const Component& c = m_item->component();
    QString text = c.displayValue();
    if (c.type == ComponentType::Diode)
        text = QStringLiteral("小信号二极管");
    m_subtitle->setText(text);
}

void PropertyPanel::applyRefdes()
{
    if (!m_item || m_updating)
        return;
    const QString ref = m_refdes->text().trimmed();
    if (ref.isEmpty()) {
        markInvalid(m_refdes);
        return;
    }
    markValid(m_refdes);
    m_item->setRefdes(ref);
    m_title->setText(QStringLiteral("%1  ·  %2").arg(typeName(m_item->component().type), ref));
    emit changed();
}

void PropertyPanel::applyValue()
{
    if (!m_item || m_updating)
        return;
    QString reason;
    bool ok = false;
    const double v = parseQuantity(m_value->text(), valueUnitFor(m_item->component().type), &ok,
                                   &reason);
    if (!ok) {
        markInvalid(m_value, reason);
        return;
    }
    markValid(m_value);
    m_item->setValue(v);
    updateSubtitle();
    emit changed();
}

void PropertyPanel::applyFunction()
{
    if (!m_item || m_updating)
        return;
    Component c = m_item->component();
    c.function = SourceFunction(m_function->currentData().toInt());
    m_item->setComponent(c);
    updateVisibility();
    updateSubtitle();
    emit changed();
}

void PropertyPanel::applyClosed()
{
    if (!m_item || m_updating)
        return;
    m_item->setClosed(m_closed->isChecked());
    updateSubtitle();
    emit changed();
}

void PropertyPanel::applyDc()
{
    if (!m_item || m_updating)
        return;
    QString reason;
    double v = 0.0;
    if (!parseField(m_dc, sourceUnitFor(m_item->component().type), &v, &reason)) {
        markInvalid(m_dc, reason);
        return;
    }
    markValid(m_dc);
    Component c = m_item->component();
    c.dcValue = v;
    m_item->setComponent(c);
    updateSubtitle();
    emit changed();
}

void PropertyPanel::applySine()
{
    if (!m_item || m_updating)
        return;
    QString phaseText = m_phase->text();
    phaseText.remove(QStringLiteral("°"));
    phaseText.remove(QStringLiteral("deg"), Qt::CaseInsensitive);

    QLineEdit* edits[5] = {m_amplitude, m_frequency, m_phase, m_offset, m_delay};
    const UnitKind units[5] = {sourceUnitFor(m_item->component().type), UnitKind::Hertz,
                               UnitKind::None, sourceUnitFor(m_item->component().type),
                               UnitKind::Second};
    double values[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 5; ++i) {
        QString reason;
        const QString text = (i == 2) ? phaseText : edits[i]->text();
        bool ok = false;
        values[i] = parseQuantity(text, units[i], &ok, &reason);
        if (!ok) {
            markInvalid(edits[i], reason);
            return;
        }
        markValid(edits[i]);
    }
    Component c = m_item->component();
    c.amplitude = values[0];
    c.frequency = values[1];
    c.phaseDeg = values[2];
    c.offset = values[3];
    c.delayTime = values[4];
    m_item->setComponent(c);
    updateSubtitle();
    emit changed();
}

void PropertyPanel::applyPulse()
{
    if (!m_item || m_updating)
        return;
    QLineEdit* edits[6] = {m_pulseV1, m_pulseV2, m_rise, m_fall, m_width, m_period};
    const UnitKind units[6] = {sourceUnitFor(m_item->component().type),
                               sourceUnitFor(m_item->component().type), UnitKind::Second,
                               UnitKind::Second, UnitKind::Second, UnitKind::Second};
    double values[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 6; ++i) {
        QString reason;
        bool ok = false;
        values[i] = parseQuantity(edits[i]->text(), units[i], &ok, &reason);
        if (!ok) {
            markInvalid(edits[i], reason);
            return;
        }
        markValid(edits[i]);
    }
    Component c = m_item->component();
    c.pulseV1 = values[0];
    c.pulseV2 = values[1];
    c.pulseRise = values[2];
    c.pulseFall = values[3];
    c.pulseWidth = values[4];
    c.pulsePeriod = values[5];
    m_item->setComponent(c);
    updateSubtitle();
    emit changed();
}

void PropertyPanel::markInvalid(QLineEdit* edit, const QString& reason)
{
    edit->setStyleSheet(QStringLiteral("border:1px solid #dc2626; border-radius:3px;"));
    edit->setToolTip(reason.isEmpty()
                         ? QStringLiteral("无法识别该数值，示例：10kΩ、4.7µF、1meg")
                         : reason);
    edit->setFocus();
}

void PropertyPanel::markValid(QLineEdit* edit)
{
    edit->setStyleSheet(QString());
    edit->setToolTip(QString());
}

} // namespace csm
