#include "ui/PropertyPanel.h"
#include "core/Language.h"

#include "ui/Theme.h"

#include "core/Units.h"
#include "ui/ComponentItem.h"

#include <QCheckBox>
#include <QComboBox>

#include <cmath>
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
    m_placeholder->setVisible(true);
    m_stack->setVisible(false);
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
    case ComponentType::Diode:     return UnitKind::Volt;
    case ComponentType::Zener:     return UnitKind::Volt;
    case ComponentType::VoltageSource: return UnitKind::Volt;
    case ComponentType::CurrentSource: return UnitKind::Ampere;
    default: return UnitKind::None;
    }
}

UnitKind sourceUnitFor(ComponentType type)
{
    return type == ComponentType::CurrentSource ? UnitKind::Ampere : UnitKind::Volt;
}

bool parseField(QLineEdit* edit, UnitKind unit, double* value, QString* reason,
                 const QString& textOverride = QString())
{
    bool ok = false;
    // textOverride 用于「先把 ° / deg 去掉再解析」这类预处理过的文本
    const QString text = textOverride.isNull() ? edit->text() : textOverride;
    const double parsed = parseQuantity(text, unit, &ok, reason);
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

    // 空态提示**不放进 QStackedWidget**：stack 的 sizeHint 取所有页里最大的，
    // 放进去会被表单页一起撑到上千像素高 —— 提示文字被顶到很下面，
    // 右边还多出一条滚动条。单独摆一个标签，没选中元件时它才是这一页的全部内容，
    // 于是面板只需要文字那么高，滚动区域再把它拉到视口大小，文字正好居中。
    m_placeholder = new QLabel(CSM_TR("Select a part on the schematic\nto edit its parameters"), this);
    m_placeholder->setAlignment(Qt::AlignCenter);
    Theme::styleWidget(m_placeholder, [] { return QStringLiteral("color:%1;").arg(Theme::textMuted().name()); });
    m_placeholder->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    root->addWidget(m_placeholder, 1);

    m_stack = new QStackedWidget(this);

    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(10);

    m_title = new QLabel(page);
    Theme::styleWidget(m_title, [] { return QStringLiteral("font-size:14px; font-weight:600; color:%1;").arg(Theme::textPrimary().name()); });
    pageLayout->addWidget(m_title);

    m_subtitle = new QLabel(page);
    Theme::styleWidget(m_subtitle, [] { return QStringLiteral("color:%1;").arg(Theme::textMuted().name()); });
    pageLayout->addWidget(m_subtitle);

    m_form = new QFormLayout();
    m_form->setLabelAlignment(Qt::AlignLeft);
    m_form->setFormAlignment(Qt::AlignTop);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_form->setHorizontalSpacing(10);
    m_form->setVerticalSpacing(8);

    m_refdes = makeEdit();
    m_form->addRow(CSM_TR("Designator"), m_refdes);

    m_value = makeEdit(CSM_TR("e.g. 10k / 4.7u"));
    m_form->addRow(CSM_TR("Value"), m_value);
    m_valueLabel = qobject_cast<QLabel*>(m_form->labelForField(m_value));

    m_polarized = new QCheckBox(CSM_TR("Polarized (electrolytic)"), page);
    m_polarized->setToolTip(CSM_TR("Marked with '+': positive terminal on pin 1"));
    m_form->addRow(CSM_TR("Polarity"), m_polarized);

    m_k = makeEdit(CSM_TR("e.g. 2m"));
    m_form->addRow(QStringLiteral("K (A/V²)"), m_k);
    m_vf = makeEdit(CSM_TR("e.g. 0.7"));
    m_form->addRow(CSM_TR("On-state drop Vf"), m_vf);
    m_ctr = makeEdit(CSM_TR("e.g. 3"));
    m_form->addRow(CSM_TR("CTR"), m_ctr);
    m_vceSat = makeEdit(CSM_TR("e.g. 0.2"));
    m_form->addRow(CSM_TR("Saturation Vce(sat)"), m_vceSat);
    m_lpri = makeEdit(CSM_TR("e.g. 1"));
    m_form->addRow(CSM_TR("Primary inductance L1"), m_lpri);

    // 运放：只暴露真正影响结果的那几个参数。默认值按通用型运放
    //（±15 V、1 MHz、0.5 V/µs）给，不动这些框就是「能用」的模型
    m_opPowerPins = new QCheckBox(CSM_TR("V+ / V- power pins"), page);
    m_opPowerPins->setToolTip(
        CSM_TR("Adds V+ (pin 4) and V- (pin 5).\n"
               "With them wired, the output swing follows the real supply rails "
               "(minus %1 V) instead of the two swing fields below.")
            .arg(Component::kOpampRailHeadroom, 0, 'g', 2));
    m_form->addRow(CSM_TR("Power pins"), m_opPowerPins);

    m_opVsatHigh = makeEdit(CSM_TR("e.g. 15"));
    m_form->addRow(CSM_TR("Output swing high (V)"), m_opVsatHigh);
    m_opVsatLow = makeEdit(CSM_TR("e.g. -15"));
    m_form->addRow(CSM_TR("Output swing low (V)"), m_opVsatLow);
    m_opGbw = makeEdit(CSM_TR("e.g. 1M"));
    m_opGbw->setToolTip(CSM_TR("Gain-bandwidth product. The dominant pole sits at GBW / A0;\n"
                               "0 means no bandwidth limit (ideal)."));
    m_form->addRow(CSM_TR("Gain-bandwidth (Hz)"), m_opGbw);
    m_opSlew = makeEdit(CSM_TR("e.g. 0.5"));
    m_opSlew->setToolTip(CSM_TR("Volts per microsecond. 0 means unlimited."));
    m_form->addRow(CSM_TR("Slew rate (V/µs)"), m_opSlew);
    m_opVos = makeEdit(CSM_TR("e.g. 2m"));
    m_opVos->setToolTip(CSM_TR("Input offset voltage: shifts the +/− input difference."));
    m_form->addRow(CSM_TR("Input offset (V)"), m_opVos);
    m_opIbias = makeEdit(CSM_TR("e.g. 100n"));
    m_opIbias->setToolTip(CSM_TR("Bias current flowing into each input terminal."));
    m_form->addRow(CSM_TR("Input bias current (A)"), m_opIbias);
    m_opRin = makeEdit(CSM_TR("e.g. 2M"));
    m_opRin->setToolTip(CSM_TR("Differential resistance between the + and − inputs."));
    m_form->addRow(CSM_TR("Input resistance (Ω)"), m_opRin);
    m_opRout = makeEdit(CSM_TR("e.g. 50"));
    m_opRout->setToolTip(CSM_TR("Series resistance at the output; the output sags under load."));
    m_form->addRow(CSM_TR("Output resistance (Ω)"), m_opRout);

    // 下拉项不要写「（1 初级 + 1 次级）」「（6 引脚）」这类补充说明：
    // 面板只有 290 上下宽，长文字会把整行顶出可视区，右边被裁掉
    m_windings = new QComboBox(page);
    m_windings->addItem(CSM_TR("Single winding"), int(TransformerKind::Single));
    m_windings->addItem(CSM_TR("Primary + auxiliary + secondary"),
                        int(TransformerKind::PrimaryAuxSecondary));
    m_form->addRow(CSM_TR("Windings"), m_windings);

    m_meterMode = new QComboBox(page);
    m_meterMode->setObjectName(QStringLiteral("multimeterModeCombo"));
    // 下拉项只写档位名。原来「直流电压档（并联，10 MΩ 内阻）」这种长条目
    // 会把下拉框撑得比整个面板还宽，右边被裁掉半句
    for (MultimeterMode mode : {MultimeterMode::Voltage, MultimeterMode::Current,
                                MultimeterMode::Resistance, MultimeterMode::AcVoltage,
                                MultimeterMode::AcCurrent}) {
        m_meterMode->addItem(multimeterModeName(mode), int(mode));
    }
    // 面板宽度只有 290 上下：下拉框按「最短内容长度」定宽，
    // 不再让某项的长文字把整行顶出去
    m_meterMode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_meterMode->setMinimumContentsLength(7);

    m_form->addRow(CSM_TR("Range"), m_meterMode);

    // 门类型不在这里改：元件库里与门 / 或门 / 非门…各占一条，
    // 要换门就删掉重放。面板只留真正需要调的参数
    m_logicHigh = makeEdit();
    m_logicHigh->setToolTip(CSM_TR("Output high level. Low level is fixed at 0 V; "
                                   "input threshold is half of it (5% hysteresis)."));
    m_logicHigh->setPlaceholderText(CSM_TR("e.g. 3.3"));
    m_form->addRow(CSM_TR("High level (V)"), m_logicHigh);

    m_acFrequency = makeEdit();
    // 范围从常量来，别在文案里写死数字 —— 改常量时提示会跟着变
    m_acFrequency->setToolTip(
        CSM_TR("AC mode averages one full period: window = 1 / this frequency "
               "(%1-%2 Hz).\nSet it to the measured signal's frequency, otherwise the "
               "window does not line up with the waveform and the reading drifts.")
            .arg(Component::kAcFrequencyMin, 0, 'g', 3)
            .arg(Component::kAcFrequencyMax, 0, 'g', 3));
    m_acFrequency->setPlaceholderText(CSM_TR("e.g. 50Hz"));
    m_form->addRow(CSM_TR("AC frequency"), m_acFrequency);

    m_groundKind = new QComboBox(page);
    m_groundKind->addItem(CSM_TR("GND (main)"), int(GroundKind::Gnd));
    m_groundKind->addItem(CSM_TR("AGND (analog)"), int(GroundKind::Agnd));
    m_groundKind->addItem(CSM_TR("DGND (digital)"), int(GroundKind::Dgnd));
    m_groundKind->addItem(CSM_TR("PGND (power)"), int(GroundKind::Pgnd));
    m_groundKind->addItem(CSM_TR("EARTH (protective)"), int(GroundKind::Earth));
    m_form->addRow(CSM_TR("Ground type"), m_groundKind);

    m_polarity = new QComboBox(page);
    m_polarity->addItem(CSM_TR("Dots on top (in phase)"), 0);
    m_polarity->addItem(CSM_TR("Secondary dot at bottom (inverted)"), 1);
    m_form->addRow(CSM_TR("Dot polarity"), m_polarity);


    m_function = new QComboBox(page);
    m_function->addItem(CSM_TR("DC"), int(SourceFunction::Constant));
    m_function->addItem(CSM_TR("Sine (SIN)"), int(SourceFunction::Sine));
    m_function->addItem(CSM_TR("Pulse (PULSE)"), int(SourceFunction::Pulse));
    m_form->addRow(CSM_TR("Waveform"), m_function);

    // 信号发生器：波形、峰值、频率、偏置、占空比
    m_waveform = new QComboBox(page);
    m_waveform->setObjectName(QStringLiteral("waveformCombo"));   // 测试按名字找
    m_waveform->addItem(CSM_TR("Sine"), int(Waveform::Sine));
    m_waveform->addItem(CSM_TR("Square"), int(Waveform::Square));
    m_waveform->addItem(CSM_TR("Triangle"), int(Waveform::Triangle));
    m_waveform->addItem(CSM_TR("Sawtooth"), int(Waveform::Sawtooth));
    m_form->addRow(CSM_TR("Waveform"), m_waveform);

    // 面板宽度只有 290 上下：所有下拉框都按「最短内容长度」定宽，
    // 免得某一项的长文字把整行顶出可视区（右边被裁、底下多出横向滚动条）。
    // 必须等这些下拉框都建好再统一设 —— 提前引用还没 new 的成员会直接崩。
    for (QComboBox* box : {m_windings, m_meterMode, m_groundKind, m_polarity, m_function,
                           m_waveform}) {
        if (!box)
            continue;
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(8);
    }

    m_peak = makeEdit();
    m_peak->setToolTip(CSM_TR("**Peak** amplitude, measured from the DC offset"));
    m_form->addRow(CSM_TR("Amplitude (peak)"), m_peak);

    m_duty = makeEdit();
    m_duty->setObjectName(QStringLiteral("dutyEdit"));   // 测试按名字找
    m_duty->setToolTip(CSM_TR("Duty cycle of the square wave, 0-100 %"));
    m_form->addRow(CSM_TR("Duty %"), m_duty);

    m_dc = makeEdit(QStringLiteral("5"));
    m_form->addRow(CSM_TR("DC value"), m_dc);

    m_amplitude = makeEdit();
    m_amplitude->setToolTip(CSM_TR("**RMS** value of the sine source; peak = RMS x 1.414"));
    m_form->addRow(CSM_TR("RMS"), m_amplitude);
    m_frequency = makeEdit();
    m_form->addRow(CSM_TR("Frequency"), m_frequency);
    m_phase = makeEdit();
    m_form->addRow(CSM_TR("Phase °"), m_phase);
    m_offset = makeEdit();
    m_form->addRow(CSM_TR("Offset"), m_offset);
    m_delay = makeEdit();
    m_form->addRow(CSM_TR("Delay"), m_delay);

    m_pulseV1 = makeEdit();
    m_form->addRow(CSM_TR("Initial value"), m_pulseV1);
    m_pulseV2 = makeEdit();
    m_form->addRow(CSM_TR("Pulse value"), m_pulseV2);
    m_rise = makeEdit();
    m_form->addRow(CSM_TR("Rise time"), m_rise);
    m_fall = makeEdit();
    m_form->addRow(CSM_TR("Fall time"), m_fall);
    m_width = makeEdit();
    m_form->addRow(CSM_TR("Pulse width"), m_width);
    m_period = makeEdit();
    m_form->addRow(CSM_TR("Period"), m_period);

    m_netLabel = makeEdit();
    m_netLabel->setObjectName(QStringLiteral("netLabelEdit"));
    m_netLabel->setToolTip(
        CSM_TR("Net labels with the same name are merged into one net, so no wire is needed."));
    m_netLabel->setPlaceholderText(CSM_TR("e.g. VCC / GND_D / OUT"));
    m_form->addRow(CSM_TR("Net name"), m_netLabel);

    m_closed = new QCheckBox(CSM_TR("Switch closed"), page);

    // 按钮开关：只有「常开 / 常闭」可设。按下是瞬时的，不进属性面板
    m_normallyClosed = new QCheckBox(CSM_TR("Normally closed (conducts while released)"), page);
    m_normallyClosed->setToolTip(
        CSM_TR("Unchecked: normally open, conducts while held during simulation.\n"
               "Checked: normally closed, opens while held."));
    m_form->addRow(QString(), m_normallyClosed);
    m_form->addRow(QString(), m_closed);

    pageLayout->addLayout(m_form);
    pageLayout->addStretch(1);

    auto* hint = new QLabel(
        CSM_TR("Tip: values may carry units (10kΩ, 4.7µF, 10mH, 5V, 1mA); "
               "without a unit the component default applies."),
        page);
    hint->setWordWrap(true);
    Theme::styleWidget(hint, [] { return QStringLiteral("color:%1; font-size:11px;").arg(Theme::textMuted().name()); });
    pageLayout->addWidget(hint);

    m_stack->addWidget(page);
    root->addWidget(m_stack, 1);

    connect(m_refdes, &QLineEdit::editingFinished, this, &PropertyPanel::applyRefdes);
    connect(m_polarized, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating || !m_item)
            return;
        Component c = m_item->component();
        if (c.polarized == on)
            return;
        c.polarized = on;
        m_item->setComponent(c); // 走同一条路径，撤销/重做与脏标记都能生效
        emit changed();
    });

    connect(m_value, &QLineEdit::editingFinished, this, &PropertyPanel::applyValue);
    connect(m_k, &QLineEdit::editingFinished, this, &PropertyPanel::applyValue);
    connect(m_vf, &QLineEdit::editingFinished, this, &PropertyPanel::applyValue);
    connect(m_ctr, &QLineEdit::editingFinished, this, &PropertyPanel::applyValue);
    connect(m_vceSat, &QLineEdit::editingFinished, this, &PropertyPanel::applyValue);
    for (QLineEdit* edit : {m_opVsatHigh, m_opVsatLow, m_opGbw, m_opSlew, m_opVos, m_opIbias,
                            m_opRin, m_opRout})
        connect(edit, &QLineEdit::editingFinished, this, &PropertyPanel::applyValue);
    connect(m_opPowerPins, &QCheckBox::toggled, this, &PropertyPanel::applyValue);
    connect(m_lpri, &QLineEdit::editingFinished, this, &PropertyPanel::applyValue);
    connect(m_meterMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::applyValue);
    connect(m_acFrequency, &QLineEdit::editingFinished, this, &PropertyPanel::applyValue);
    connect(m_logicHigh, &QLineEdit::editingFinished, this, &PropertyPanel::applyValue);
    connect(m_netLabel, &QLineEdit::editingFinished, this, &PropertyPanel::applyNetLabel);
    connect(m_windings, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyValue(); });
    connect(m_polarity, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyValue(); });
    connect(m_groundKind, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyValue(); });
    connect(m_function, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PropertyPanel::applyFunction);
    connect(m_closed, &QCheckBox::toggled, this, &PropertyPanel::applyClosed);
    connect(m_normallyClosed, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_item || m_updating || m_item->component().type != ComponentType::PushButton)
            return;
        Component c = m_item->component();
        c.normallyClosed = on;
        c.closed = on; // 静止状态跟着变：常闭时默认就是通的
        m_item->setComponent(c);
        updateSubtitle();
        emit changed();
    });

    connect(m_dc, &QLineEdit::editingFinished, this, &PropertyPanel::applyDc);
    for (QLineEdit* edit : {m_amplitude, m_frequency, m_phase, m_offset, m_delay})
        connect(edit, &QLineEdit::editingFinished, this, &PropertyPanel::applySine);
    for (QLineEdit* edit : {m_peak, m_duty})
        connect(edit, &QLineEdit::editingFinished, this, &PropertyPanel::applyGenerator);
    connect(m_waveform, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyGenerator(); });
    for (QLineEdit* edit : {m_pulseV1, m_pulseV2, m_rise, m_fall, m_width, m_period})
        connect(edit, &QLineEdit::editingFinished, this, &PropertyPanel::applyPulse);
}

void PropertyPanel::setComponent(ComponentItem* item)
{
    m_item = item;
    // 没选中：只显示空态提示；选中了：隐藏提示、显示表单页
    m_placeholder->setVisible(!item);
    m_stack->setVisible(item != nullptr);
    if (!item)
        return;
    m_stack->setCurrentIndex(0);
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
    switch (c.type) {
    case ComponentType::Diode:
    case ComponentType::Led:
    case ComponentType::Optocoupler:
        m_value->setText(quantityText(c.vf, UnitKind::Volt));
        break;
    case ComponentType::Npn:
    case ComponentType::Pnp:
        m_value->setText(QString::number(c.beta, 'g', 6));
        break;
    case ComponentType::Transformer: {
        // 一行写完 N1:N2(:N3)，内部仍按「次级比 / 辅助比」存
        const QString n1 = QString::number(1.0, 'g', 4);
        const QString n2 = QString::number(c.auxRatio, 'g', 4);
        const QString n3 = QString::number(c.value, 'g', 4);
        m_value->setText(c.transformerKind == TransformerKind::PrimaryAuxSecondary
                             ? QStringLiteral("%1:%2:%3").arg(n1, n2, n3)
                             : QStringLiteral("%1:%2").arg(n1, n3));
        break;
    }
    case ComponentType::Nmos:
    case ComponentType::Pmos:
    case ComponentType::Igbt:
        m_value->setText(quantityText(c.vth, UnitKind::Volt));
        break;
    default:
        m_value->setText(quantityText(c.value, valueUnitFor(c.type)));
        break;
    }
    m_k->setText(QString::number(c.k, 'g', 4));
    m_vf->setText(quantityText(c.vf, UnitKind::Volt));
    m_ctr->setText(QString::number(c.ctr, 'g', 4));
    m_vceSat->setText(quantityText(c.vceSat, UnitKind::Volt));
    m_lpri->setText(quantityText(c.lpri, UnitKind::Henry));
    m_opPowerPins->setChecked(c.opampPowerPins);
    m_opVsatHigh->setText(quantityText(c.vSatHigh, UnitKind::Volt));
    m_opVsatLow->setText(quantityText(c.vSatLow, UnitKind::Volt));
    m_opGbw->setText(quantityText(c.gbw, UnitKind::Hertz));
    m_opSlew->setText(QString::number(c.slewRate / 1e6, 'g', 4));
    m_opVos->setText(quantityText(c.vos, UnitKind::Volt));
    m_opIbias->setText(quantityText(c.ibias, UnitKind::Ampere));
    m_opRin->setText(quantityText(c.rin, UnitKind::Ohm));
    m_opRout->setText(quantityText(c.rout, UnitKind::Ohm));
    m_windings->setCurrentIndex(m_windings->findData(int(c.transformerKind)));
    m_meterMode->setCurrentIndex(m_meterMode->findData(int(c.multimeterMode)));
    m_logicHigh->setText(quantityText(c.logicHigh, UnitKind::Volt));
    m_netLabel->setText(c.netLabel);
    m_acFrequency->setText(quantityText(c.acFrequency, UnitKind::Hertz));
    m_polarity->setCurrentIndex(m_polarity->findData(c.secondaryInverted ? 1 : 0));
    m_groundKind->setCurrentIndex(m_groundKind->findData(int(c.groundKind)));
    m_function->setCurrentIndex(m_function->findData(int(c.function)));

    const UnitKind unit = sourceUnitFor(c.type);
    // 信号发生器：峰值、频率、偏置、占空比
    m_peak->setText(quantityText(c.amplitude, UnitKind::Volt));
    m_duty->setText(QString::number(c.duty * 100.0, 'g', 3));
    m_waveform->setCurrentIndex(std::max(0, m_waveform->findData(int(c.waveform))));
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
    m_normallyClosed->setChecked(c.normallyClosed);
    m_polarized->setChecked(c.polarized);

    m_updating = false;
    updateSubtitle();
}

void PropertyPanel::updateVisibility()
{
    if (!m_item)
        return;

    const Component c = m_item->component();
    const bool generator = (c.type == ComponentType::SignalGenerator);
    const bool source = isSource(c.type) && !generator;
    const bool analog = c.type == ComponentType::Resistor || c.type == ComponentType::Capacitor
        || c.type == ComponentType::Inductor || c.type == ComponentType::OpAmp
        || c.type == ComponentType::Diode || c.type == ComponentType::Zener
        || c.type == ComponentType::Led
        || c.type == ComponentType::Optocoupler
        || c.type == ComponentType::Tl431 || c.type == ComponentType::Transformer
        || isThreeTerminalDevice(c.type);

    const bool isMos = c.type == ComponentType::Nmos || c.type == ComponentType::Pmos
        || c.type == ComponentType::Igbt;
    m_form->setRowVisible(m_polarized, c.type == ComponentType::Capacitor);
    m_form->setRowVisible(m_k, isMos);
    m_form->setRowVisible(m_vf, c.type == ComponentType::Igbt
                                      || c.type == ComponentType::Zener
                                      || c.type == ComponentType::Led
                                      || c.type == ComponentType::Optocoupler);
    m_form->setRowVisible(m_ctr, c.type == ComponentType::Optocoupler);
    m_form->setRowVisible(m_vceSat, c.type == ComponentType::Optocoupler);
    m_form->setRowVisible(m_lpri, c.type == ComponentType::Transformer);
    const bool opamp = (c.type == ComponentType::OpAmp);
    for (QLineEdit* edit : {m_opVsatHigh, m_opVsatLow, m_opGbw, m_opSlew, m_opVos, m_opIbias,
                            m_opRin, m_opRout})
        m_form->setRowVisible(edit, opamp);
    m_form->setRowVisible(m_opPowerPins, opamp);
    // 有电源引脚时摆幅由电源轨算出来，这两行改成只读说明
    m_form->setRowVisible(m_opVsatHigh, opamp && !c.opampPowerPins);
    m_form->setRowVisible(m_opVsatLow, opamp && !c.opampPowerPins);
    m_form->setRowVisible(m_logicHigh, c.type == ComponentType::LogicGate);

    if (m_valueLabel) {
        switch (c.type) {
        case ComponentType::Resistor:
            m_valueLabel->setText(CSM_TR("Value (Ω)"));
            m_value->setPlaceholderText(CSM_TR("e.g. 10kΩ / 4.7k"));
            break;
        case ComponentType::Capacitor:
            m_valueLabel->setText(CSM_TR("Value (F)"));
            m_value->setPlaceholderText(CSM_TR("e.g. 100nF / 1µF"));
            break;
        case ComponentType::Inductor:
            m_valueLabel->setText(CSM_TR("Value (H)"));
            m_value->setPlaceholderText(CSM_TR("e.g. 10mH"));
            break;
        case ComponentType::OpAmp:
            m_valueLabel->setText(CSM_TR("Open-loop gain"));
            m_value->setPlaceholderText(CSM_TR("e.g. 100k"));
            break;
        case ComponentType::Npn:
        case ComponentType::Pnp:
            m_valueLabel->setText(CSM_TR("Current gain β"));
            m_value->setPlaceholderText(CSM_TR("e.g. 100"));
            break;
        case ComponentType::Nmos:
        case ComponentType::Pmos:
        case ComponentType::Igbt:
            m_valueLabel->setText(CSM_TR("Threshold Vth"));
            m_value->setPlaceholderText(CSM_TR("e.g. 2"));
            break;
        case ComponentType::Zener:
            m_valueLabel->setText(CSM_TR("Breakdown Vz"));
            m_value->setPlaceholderText(CSM_TR("e.g. 5.1"));
            break;
        case ComponentType::Diode:
        case ComponentType::Led:
            // 二极管 / LED 只暴露正向压降一个参数，见下面的 applyValue
            m_valueLabel->setText(CSM_TR("Forward drop Vf"));
            m_value->setPlaceholderText(CSM_TR("e.g. 2"));
            break;
        case ComponentType::Optocoupler:
            m_valueLabel->setText(CSM_TR("Forward drop Vf"));
            m_value->setPlaceholderText(CSM_TR("e.g. 1.2"));
            break;
        case ComponentType::Tl431:
            m_valueLabel->setText(CSM_TR("Reference Vref"));
            m_value->setPlaceholderText(CSM_TR("e.g. 2.5"));
            break;
        case ComponentType::Transformer:
            m_valueLabel->setText(c.transformerKind == TransformerKind::PrimaryAuxSecondary
                                      ? CSM_TR("Turns ratio N1:N2:N3")
                                      : CSM_TR("Turns ratio N1:N2"));
            m_value->setPlaceholderText(CSM_TR("e.g. 2"));
            break;
        default:
            m_valueLabel->setText(CSM_TR("Value"));
            break;
        }
    }

    if (isSource(c.type)) {
        const QString unit = unitSymbol(sourceUnitFor(c.type));
        m_dc->setPlaceholderText(CSM_TR("e.g. 5%1").arg(unit));
        m_amplitude->setPlaceholderText(CSM_TR("e.g. 220%1").arg(unit));
        m_offset->setPlaceholderText(CSM_TR("e.g. 0%1").arg(unit));
        m_pulseV1->setPlaceholderText(CSM_TR("e.g. 0%1").arg(unit));
        m_pulseV2->setPlaceholderText(CSM_TR("e.g. 5%1").arg(unit));
    }
    m_frequency->setPlaceholderText(CSM_TR("e.g. 1kHz"));
    m_delay->setPlaceholderText(CSM_TR("e.g. 1ms"));
    for (QLineEdit* edit : {m_rise, m_fall, m_width, m_period})
        edit->setPlaceholderText(CSM_TR("e.g. 1ms"));

    // 网络标签只有「网络名」，没有编号，也不该有数值
    m_form->setRowVisible(m_refdes, c.type != ComponentType::NetLabel);
    m_form->setRowVisible(m_value, analog);
    m_form->setRowVisible(m_function, source);
    m_form->setRowVisible(m_netLabel, c.type == ComponentType::NetLabel);
    // 交流频率只对交流档有意义
    m_form->setRowVisible(m_acFrequency,
                          c.type == ComponentType::Multimeter
                              && isAcMeterMode(c.multimeterMode));
    m_form->setRowVisible(m_meterMode, c.type == ComponentType::Multimeter);
    m_form->setRowVisible(m_windings, c.type == ComponentType::Transformer);
    m_form->setRowVisible(m_polarity, c.type == ComponentType::Transformer);
    m_form->setRowVisible(m_groundKind, c.type == ComponentType::Ground
                                           || c.type == ComponentType::Probe);
    m_form->setRowVisible(m_dc, source && c.function == SourceFunction::Constant);
    m_form->setRowVisible(m_closed, c.type == ComponentType::Switch);
    m_form->setRowVisible(m_normallyClosed, c.type == ComponentType::PushButton);

    const bool sine = source && c.function == SourceFunction::Sine;
    for (QLineEdit* edit : {m_amplitude, m_frequency, m_phase, m_offset, m_delay})
        m_form->setRowVisible(edit, sine);

    const bool pulse = source && c.function == SourceFunction::Pulse;
    for (QLineEdit* edit : {m_pulseV1, m_pulseV2, m_rise, m_fall, m_width, m_period})
        m_form->setRowVisible(edit, pulse);

    // 信号发生器：波形 / 峰值 / 频率 / 偏置；占空比只对方波有意义
    m_form->setRowVisible(m_waveform, generator);
    m_form->setRowVisible(m_peak, generator);
    m_form->setRowVisible(m_duty, generator && c.waveform == Waveform::Square);
    for (QLineEdit* edit : {m_frequency, m_offset, m_phase})
        m_form->setRowVisible(edit, sine || generator);
}

void PropertyPanel::updateSubtitle()
{
    if (!m_item)
        return;
    const Component& c = m_item->component();
    m_subtitle->setText(c.displayValue());
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

void PropertyPanel::applyNetLabel()
{
    if (!m_item || m_updating)
        return;
    if (m_item->component().type != ComponentType::NetLabel)
        return;
    const QString name = m_netLabel->text().trimmed();
    if (name.isEmpty()) {
        markInvalid(m_netLabel, CSM_TR("The net name cannot be empty"));
        return;
    }
    markValid(m_netLabel);
    Component c = m_item->component();
    if (c.netLabel == name)
        return;
    c.netLabel = name;
    m_item->setComponent(c);
    updateSubtitle();
    emit changed();
}

void PropertyPanel::applyValue()
{
    if (!m_item || m_updating)
        return;
    const ComponentType type = m_item->component().type;

    if (type == ComponentType::Zener) {
        Component c = m_item->component();
        QString reason;
        bool ok = false;
        const double vz = parseQuantity(m_value->text(), UnitKind::Volt, &ok, &reason);
        if (!ok) {
            markInvalid(m_value, reason);
            return;
        }
        markValid(m_value);
        const double vf = parseQuantity(m_vf->text(), UnitKind::Volt, &ok, &reason);
        if (!ok) {
            markInvalid(m_vf, reason);
            return;
        }
        markValid(m_vf);
        c.value = std::max(vz, 0.5);
        c.vf = std::max(vf, 0.05);
        m_item->setComponent(c);
        updateSubtitle();
        emit changed();
        return;
    }

    if (type == ComponentType::Diode || type == ComponentType::Led
        || type == ComponentType::Optocoupler) {
        QString reason;
        bool ok = false;
        const double vf = parseQuantity(m_value->text(), UnitKind::Volt, &ok, &reason);
        if (!ok) {
            markInvalid(m_value, reason);
            return;
        }
        markValid(m_value);
        Component c = m_item->component();
        c.vf = vf;
        if (type == ComponentType::Optocoupler) {
            const double ctr = parseQuantity(m_ctr->text(), UnitKind::None, &ok, &reason);
            if (!ok) {
                markInvalid(m_ctr, reason);
                return;
            }
            markValid(m_ctr);
            c.ctr = ctr;

            const double vceSat = parseQuantity(m_vceSat->text(), UnitKind::Volt, &ok, &reason);
            if (!ok) {
                markInvalid(m_vceSat, reason);
                return;
            }
            markValid(m_vceSat);
            c.vceSat = vceSat;
        }
        m_item->setComponent(c);
        updateSubtitle();
        emit changed();
        return;
    }

    if (type == ComponentType::Ground || type == ComponentType::Probe) {
        Component c = m_item->component();
        c.groundKind = static_cast<GroundKind>(m_groundKind->currentData().toInt());
        m_item->setComponent(c);
        updateSubtitle();
        emit changed();
        return;
    }

    if (type == ComponentType::LogicGate) {
        Component c = m_item->component();
        bool ok = false;
        QString reason;
        const double high = parseQuantity(m_logicHigh->text(), UnitKind::Volt, &ok, &reason);
        if (!ok || high <= 0.0) {
            markInvalid(m_logicHigh, ok ? CSM_TR("High level must be greater than 0") : reason);
            return;
        }
        markValid(m_logicHigh);
        c.logicHigh = high;
        m_item->setComponent(c);
        updateSubtitle();
        emit changed();
        return;
    }

    if (type == ComponentType::Multimeter) {
        Component c = m_item->component();
        if (m_meterMode)
            c.multimeterMode = static_cast<MultimeterMode>(m_meterMode->currentData().toInt());
        // 交流频率以前只显示、从来不写回：输入框改了等于没改，仿真永远按 50 Hz
        // 取窗口。这里补上解析与范围检查（20~500 Hz）。
        if (isAcMeterMode(c.multimeterMode) && m_acFrequency) {
            QString reason;
            bool ok = false;
            const double hz = parseQuantity(m_acFrequency->text(), UnitKind::Hertz, &ok, &reason);
            if (!ok) {
                markInvalid(m_acFrequency, reason);
                return;
            }
            if (hz < Component::kAcFrequencyMin || hz > Component::kAcFrequencyMax) {
                markInvalid(m_acFrequency,
                            CSM_TR("AC frequency must be between %1 and %2 Hz")
                                .arg(Component::kAcFrequencyMin, 0, 'g', 3)
                                .arg(Component::kAcFrequencyMax, 0, 'g', 3));
                return;
            }
            markValid(m_acFrequency);
            c.acFrequency = hz;
        }
        m_item->setComponent(c);
        updateSubtitle();
        emit changed();
        return;
    }

    if (type == ComponentType::Transformer) {
        Component c = m_item->component();
        QString reason;
        bool ok = false;

        // 匝数比写成 N1:N2(:N3)：第一个是初级，第二个是辅助（单绕组时是次级），
        // 第三个是次级。内部仍然只存「次级比」和「辅助比」。
        const QStringList parts = m_value->text().trimmed().split(QLatin1Char(':'));
        if (parts.size() < 2 || parts.size() > 3) {
            markInvalid(m_value, CSM_TR("Enter N1:N2 or N1:N2:N3"));
            return;
        }
        QVector<double> turns;
        for (const QString& part : parts) {
            const double v = parseQuantity(part, UnitKind::None, &ok, &reason);
            if (!ok || v <= 0.0) {
                markInvalid(m_value, CSM_TR("The turns ratio must be positive"));
                return;
            }
            turns.append(v);
        }
        markValid(m_value);
        const double n1 = turns.first();
        if (turns.size() == 3) {
            c.auxRatio = turns.at(1) / n1;
            c.value = turns.at(2) / n1;
        } else {
            c.auxRatio = 1.0;
            c.value = turns.at(1) / n1;
        }

        const double lpri = parseQuantity(m_lpri->text(), UnitKind::Henry, &ok, &reason);
        if (!ok) {
            markInvalid(m_lpri, reason);
            return;
        }
        markValid(m_lpri);
        c.lpri = lpri;

        if (m_windings)
            c.transformerKind = static_cast<TransformerKind>(m_windings->currentData().toInt());

        if (m_polarity)
            c.secondaryInverted = m_polarity->currentData().toInt() != 0;

        m_item->setComponent(c);
        updateSubtitle();
        emit changed();
        // 绕组形式变了要重新刷一遍字段（标签从 N1:N2 变成 N1:N2:N3）
        refreshFromItem();
        updateVisibility();
        return;
    }

    if (isThreeTerminalDevice(type)) {
        // 三端器件：β 是无量纲数，Vth 带电压单位
        Component c = m_item->component();
        QString reason;
        bool ok = false;
        if (type == ComponentType::Npn || type == ComponentType::Pnp) {
            const double beta = parseQuantity(m_value->text(), UnitKind::None, &ok, &reason);
            if (!ok) {
                markInvalid(m_value, reason);
                return;
            }
            c.beta = beta;
        } else {
            const double vth = parseQuantity(m_value->text(), UnitKind::Volt, &ok, &reason);
            if (!ok) {
                markInvalid(m_value, reason);
                return;
            }
            c.vth = vth;
        }
        markValid(m_value);

        const double k = parseQuantity(m_k->text(), UnitKind::None, &ok, &reason);
        if (!ok) {
            markInvalid(m_k, reason);
            return;
        }
        markValid(m_k);
        c.k = k;

        if (type == ComponentType::Igbt) {
            const double vf = parseQuantity(m_vf->text(), UnitKind::Volt, &ok, &reason);
            if (!ok) {
                markInvalid(m_vf, reason);
                return;
            }
            markValid(m_vf);
            c.vf = vf;
        }

        m_item->setComponent(c);
        updateSubtitle();
        emit changed();
        return;
    }

    // 运放：一次要把七个参数都读一遍（任何一个编辑框改完都会走到这里）
    if (type == ComponentType::OpAmp) {
        Component c = m_item->component();
        QString reason;
        bool ok = false;
        const auto read = [&](QLineEdit* edit, UnitKind unit, double scale, double* out) {
            const double parsed = parseQuantity(edit->text(), unit, &ok, &reason);
            if (!ok) {
                markInvalid(edit, reason);
                return false;
            }
            markValid(edit);
            *out = parsed * scale;
            return true;
        };
        if (!read(m_value, UnitKind::None, 1.0, &c.value))
            return;
        c.opampPowerPins = m_opPowerPins && m_opPowerPins->isChecked();
        // 摆幅两行在有电源引脚时是隐藏的：不读它们的文本，免得隐藏行里的旧内容
        // 把有效参数覆盖掉
        if (!c.opampPowerPins) {
            if (!read(m_opVsatHigh, UnitKind::Volt, 1.0, &c.vSatHigh))
                return;
            if (!read(m_opVsatLow, UnitKind::Volt, 1.0, &c.vSatLow))
                return;
        }
        if (!read(m_opGbw, UnitKind::Hertz, 1.0, &c.gbw))
            return;
        if (!read(m_opSlew, UnitKind::None, 1e6, &c.slewRate))   // V/µs → V/s
            return;
        if (!read(m_opVos, UnitKind::Volt, 1.0, &c.vos))
            return;
        if (!read(m_opIbias, UnitKind::Ampere, 1.0, &c.ibias))
            return;
        if (!read(m_opRin, UnitKind::Ohm, 1.0, &c.rin))
            return;
        if (!read(m_opRout, UnitKind::Ohm, 1.0, &c.rout))
            return;
        // 上限必须大于下限，否则饱和函数没有意义
        if (c.vSatHigh <= c.vSatLow) {
            const double t = c.vSatHigh;
            c.vSatHigh = c.vSatLow;
            c.vSatLow = t;
        }
        m_item->setComponent(c);
        updateSubtitle();
        emit changed();
        // 勾掉「电源引脚」时那两个引脚就没了，摆幅两行要跟着回来（或藏起来）
        updateVisibility();
        return;
    }

    QString reason;
    bool ok = false;
    const double v = parseQuantity(m_value->text(), valueUnitFor(type), &ok, &reason);
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

void PropertyPanel::applyGenerator()
{
    if (!m_item || m_updating)
        return;
    if (m_item->component().type != ComponentType::SignalGenerator)
        return;

    double peak = 0.0;
    double duty = 0.0;
    double phase = 0.0;
    QString reason;
    if (!parseField(m_peak, UnitKind::Volt, &peak, &reason)) {
        markInvalid(m_peak, reason);
        return;
    }
    markValid(m_peak);
    if (!parseField(m_duty, UnitKind::None, &duty, &reason)) {
        markInvalid(m_duty, reason);
        return;
    }
    markValid(m_duty);
    QString phaseText = m_phase->text();
    phaseText.remove(QStringLiteral("°"));
    phaseText.remove(QStringLiteral("deg"), Qt::CaseInsensitive);
    if (!parseField(m_phase, UnitKind::None, &phase, &reason, phaseText)) {
        markInvalid(m_phase, reason);
        return;
    }
    markValid(m_phase);

    Component c = m_item->component();
    c.waveform = Waveform(m_waveform->currentData().toInt());
    c.amplitude = std::max(0.0, peak);
    c.duty = std::clamp(duty / 100.0, 0.0, 1.0);
    c.phaseDeg = phase;
    m_item->setComponent(c);
    // 占空比只在方波时显示：换了波形要立刻反映出来，
    // 否则得重新选中元件（重走 refreshFromItem）才出现
    updateVisibility();
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
    edit->setStyleSheet(QStringLiteral("border:1px solid ") + Theme::danger().name() + QStringLiteral("; border-radius:3px;"));
    edit->setToolTip(reason.isEmpty()
                         ? CSM_TR("Cannot read that value, e.g. 10kΩ, 4.7µF, 1meg")
                         : reason);
    edit->setFocus();
}

void PropertyPanel::markValid(QLineEdit* edit)
{
    edit->setStyleSheet(QString());
    edit->setToolTip(QString());
}

} // namespace csm
