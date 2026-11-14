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

QString numberText(double v)
{
    return QString::number(v, 'g', 10);
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

    auto* hint = new QLabel(QStringLiteral("提示：数值支持 10k、4.7u、1meg、2.2n 等写法。"), page);
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
    m_value->setText(numberText(c.value));
    m_function->setCurrentIndex(m_function->findData(int(c.function)));

    m_dc->setText(numberText(c.dcValue));
    m_amplitude->setText(numberText(c.amplitude));
    m_frequency->setText(numberText(c.frequency));
    m_phase->setText(numberText(c.phaseDeg));
    m_offset->setText(numberText(c.offset));
    m_delay->setText(numberText(c.delayTime));

    m_pulseV1->setText(numberText(c.pulseV1));
    m_pulseV2->setText(numberText(c.pulseV2));
    m_rise->setText(numberText(c.pulseRise));
    m_fall->setText(numberText(c.pulseFall));
    m_width->setText(numberText(c.pulseWidth));
    m_period->setText(numberText(c.pulsePeriod));

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
        const bool opamp = c.type == ComponentType::OpAmp;
        m_valueLabel->setText(opamp ? QStringLiteral("开环增益") : QStringLiteral("数值"));
        m_value->setPlaceholderText(opamp ? QStringLiteral("例如 100k")
                                          : QStringLiteral("例如 10k / 4.7u"));
    }

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
    bool ok = false;
    const double v = parseEng(m_value->text(), &ok);
    if (!ok) {
        markInvalid(m_value);
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
    bool ok = false;
    const double v = parseEng(m_dc->text(), &ok);
    if (!ok) {
        markInvalid(m_dc);
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
    QLineEdit* edits[5] = {m_amplitude, m_frequency, m_phase, m_offset, m_delay};
    double values[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 5; ++i) {
        bool ok = false;
        values[i] = parseEng(edits[i]->text(), &ok);
        if (!ok) {
            markInvalid(edits[i]);
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
    double values[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        values[i] = parseEng(edits[i]->text(), &ok);
        if (!ok) {
            markInvalid(edits[i]);
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

void PropertyPanel::markInvalid(QLineEdit* edit)
{
    edit->setStyleSheet(QStringLiteral("border:1px solid #dc2626; border-radius:3px;"));
    edit->setToolTip(QStringLiteral("无法识别该数值，示例：10k、4.7u、1meg"));
    edit->setFocus();
}

void PropertyPanel::markValid(QLineEdit* edit)
{
    edit->setStyleSheet(QString());
    edit->setToolTip(QString());
}

} // namespace csm
