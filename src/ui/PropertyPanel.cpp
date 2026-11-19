#include "ui/PropertyPanel.h"

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

    // 空态提示**不放进 QStackedWidget**：stack 的 sizeHint 取所有页里最大的，
    // 放进去会被表单页一起撑到上千像素高 —— 提示文字被顶到很下面，
    // 右边还多出一条滚动条。单独摆一个标签，没选中元件时它才是这一页的全部内容，
    // 于是面板只需要文字那么高，滚动区域再把它拉到视口大小，文字正好居中。
    m_placeholder = new QLabel(QStringLiteral("在原理图中选择一个元件\n以编辑其参数"), this);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setStyleSheet(QStringLiteral("color:#94a3b8;"));
    m_placeholder->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    root->addWidget(m_placeholder, 1);

    m_stack = new QStackedWidget(this);

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

    m_rotation = makeEdit(QStringLiteral("例如 45"));
    m_rotation->setToolTip(QStringLiteral("单位是度，可以填任意角度，例如 15、45、60"));
    m_form->addRow(QStringLiteral("旋转角度 (°)"), m_rotation);

    m_value = makeEdit(QStringLiteral("例如 10k / 4.7u"));
    m_form->addRow(QStringLiteral("数值"), m_value);
    m_valueLabel = qobject_cast<QLabel*>(m_form->labelForField(m_value));

    m_polarized = new QCheckBox(QStringLiteral("有极性（电解电容）"), page);
    m_polarized->setToolTip(QStringLiteral("符号带「+」，正极在引脚 1 一侧"));
    m_form->addRow(QStringLiteral("极性"), m_polarized);

    m_k = makeEdit(QStringLiteral("例如 2m"));
    m_form->addRow(QStringLiteral("K (A/V²)"), m_k);
    m_vf = makeEdit(QStringLiteral("例如 0.7"));
    m_form->addRow(QStringLiteral("导通压降 Vf"), m_vf);
    m_ctr = makeEdit(QStringLiteral("例如 3"));
    m_form->addRow(QStringLiteral("电流传输比 CTR"), m_ctr);
    m_vceSat = makeEdit(QStringLiteral("例如 0.2"));
    m_form->addRow(QStringLiteral("饱和压降 Vce(sat)"), m_vceSat);
    m_lpri = makeEdit(QStringLiteral("例如 1"));
    m_form->addRow(QStringLiteral("初级电感 L1"), m_lpri);

    m_windings = new QComboBox(page);
    m_windings->addItem(QStringLiteral("单绕组（1 初级 + 1 次级）"),
                        int(TransformerKind::Single));
    m_windings->addItem(QStringLiteral("初级 + 辅助绕组 + 次级（6 引脚）"),
                        int(TransformerKind::PrimaryAuxSecondary));
    m_form->addRow(QStringLiteral("绕组形式"), m_windings);

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

    m_form->addRow(QStringLiteral("档位"), m_meterMode);

    // 门类型不在这里改：元件库里与门 / 或门 / 非门…各占一条，
    // 要换门就删掉重放。面板只留真正需要调的参数
    m_logicHigh = makeEdit();
    m_logicHigh->setToolTip(QStringLiteral("输出的高电平。低电平固定 0 V，"
                                           "输入阈值取它的一半（带 5% 回差）。"));
    m_logicHigh->setPlaceholderText(QStringLiteral("例如 3.3"));
    m_form->addRow(QStringLiteral("高电平 (V)"), m_logicHigh);

    m_acFrequency = makeEdit();
    m_acFrequency->setToolTip(
        QStringLiteral("交流档按一个周期算有效值：窗口 = 1 / 这个频率。\n"
                       "被测信号不是 50 Hz 时必须跟着改，否则读数会随时间跳动。"));
    m_acFrequency->setPlaceholderText(QStringLiteral("例如 50Hz"));
    m_form->addRow(QStringLiteral("交流频率"), m_acFrequency);

    m_groundKind = new QComboBox(page);
    m_groundKind->addItem(QStringLiteral("GND（主地）"), int(GroundKind::Gnd));
    m_groundKind->addItem(QStringLiteral("AGND（模拟地）"), int(GroundKind::Agnd));
    m_groundKind->addItem(QStringLiteral("DGND（数字地）"), int(GroundKind::Dgnd));
    m_groundKind->addItem(QStringLiteral("PGND（功率地）"), int(GroundKind::Pgnd));
    m_groundKind->addItem(QStringLiteral("EARTH（保护地）"), int(GroundKind::Earth));
    m_form->addRow(QStringLiteral("接地种类"), m_groundKind);

    m_polarity = new QComboBox(page);
    m_polarity->addItem(QStringLiteral("同名端都在上（同相）"), 0);
    m_polarity->addItem(QStringLiteral("次级同名端在下（反相）"), 1);
    m_form->addRow(QStringLiteral("同名端"), m_polarity);

    m_function = new QComboBox(page);
    m_function->addItem(QStringLiteral("直流 (DC)"), int(SourceFunction::Constant));
    m_function->addItem(QStringLiteral("正弦 (SIN)"), int(SourceFunction::Sine));
    m_function->addItem(QStringLiteral("脉冲 (PULSE)"), int(SourceFunction::Pulse));
    m_form->addRow(QStringLiteral("波形"), m_function);

    m_dc = makeEdit(QStringLiteral("5"));
    m_form->addRow(QStringLiteral("直流值"), m_dc);

    m_amplitude = makeEdit();
    m_amplitude->setToolTip(QStringLiteral("正弦源的**有效值**（RMS）；峰值 = 有效值 × 1.414"));
    m_form->addRow(QStringLiteral("有效值 (RMS)"), m_amplitude);
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

    m_netLabel = makeEdit();
    m_netLabel->setObjectName(QStringLiteral("netLabelEdit"));
    m_netLabel->setToolTip(QStringLiteral(
        "同名的网络标签在网表里并成一个网络 —— 不用拉线也能把两处连起来。"));
    m_netLabel->setPlaceholderText(QStringLiteral("例如 VCC / GND_D / OUT"));
    m_form->addRow(QStringLiteral("网络名"), m_netLabel);

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
    connect(m_rotation, &QLineEdit::editingFinished, this, [this] {
        if (!m_item || m_updating)
            return;
        QString reason;
        bool ok = false;
        const double degrees = parseQuantity(m_rotation->text(), UnitKind::None, &ok, &reason);
        if (!ok) {
            markInvalid(m_rotation, reason);
            return;
        }
        markValid(m_rotation);
        m_item->setRotationAngle(int(std::lround(degrees)));
        m_rotation->setText(QString::number(m_item->component().rotation));
        emit changed();
    });

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
    m_windings->setCurrentIndex(m_windings->findData(int(c.transformerKind)));
    m_meterMode->setCurrentIndex(m_meterMode->findData(int(c.multimeterMode)));
    m_logicHigh->setText(quantityText(c.logicHigh, UnitKind::Volt));
    m_netLabel->setText(c.netLabel);
    m_acFrequency->setText(quantityText(c.acFrequency, UnitKind::Hertz));
    m_polarity->setCurrentIndex(m_polarity->findData(c.secondaryInverted ? 1 : 0));
    m_groundKind->setCurrentIndex(m_groundKind->findData(int(c.groundKind)));
    m_rotation->setText(QString::number(c.rotation));
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
    m_polarized->setChecked(c.polarized);

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
        || c.type == ComponentType::Inductor || c.type == ComponentType::OpAmp
        || c.type == ComponentType::Led || c.type == ComponentType::Optocoupler
        || c.type == ComponentType::Tl431 || c.type == ComponentType::Transformer
        || isThreeTerminalDevice(c.type);

    const bool isMos = c.type == ComponentType::Nmos || c.type == ComponentType::Pmos
        || c.type == ComponentType::Igbt;
    m_form->setRowVisible(m_polarized, c.type == ComponentType::Capacitor);
    m_form->setRowVisible(m_k, isMos);
    m_form->setRowVisible(m_vf, c.type == ComponentType::Igbt
                                      || c.type == ComponentType::Led
                                      || c.type == ComponentType::Optocoupler);
    m_form->setRowVisible(m_ctr, c.type == ComponentType::Optocoupler);
    m_form->setRowVisible(m_vceSat, c.type == ComponentType::Optocoupler);
    m_form->setRowVisible(m_lpri, c.type == ComponentType::Transformer);
    m_form->setRowVisible(m_logicHigh, c.type == ComponentType::LogicGate);

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
        case ComponentType::Npn:
        case ComponentType::Pnp:
            m_valueLabel->setText(QStringLiteral("电流放大 β"));
            m_value->setPlaceholderText(QStringLiteral("例如 100"));
            break;
        case ComponentType::Nmos:
        case ComponentType::Pmos:
        case ComponentType::Igbt:
            m_valueLabel->setText(QStringLiteral("开启电压 Vth"));
            m_value->setPlaceholderText(QStringLiteral("例如 2"));
            break;
        case ComponentType::Led:
            // LED 只暴露正向压降一个参数，见下面的 applyValue
            m_valueLabel->setText(QStringLiteral("正向压降 Vf"));
            m_value->setPlaceholderText(QStringLiteral("例如 2"));
            break;
        case ComponentType::Optocoupler:
            m_valueLabel->setText(QStringLiteral("正向压降 Vf"));
            m_value->setPlaceholderText(QStringLiteral("例如 1.2"));
            break;
        case ComponentType::Tl431:
            m_valueLabel->setText(QStringLiteral("基准电压 Vref"));
            m_value->setPlaceholderText(QStringLiteral("例如 2.5"));
            break;
        case ComponentType::Transformer:
            m_valueLabel->setText(c.transformerKind == TransformerKind::PrimaryAuxSecondary
                                      ? QStringLiteral("匝数比 N1:N2:N3")
                                      : QStringLiteral("匝数比 N1:N2"));
            m_value->setPlaceholderText(QStringLiteral("例如 2"));
            break;
        default:
            m_valueLabel->setText(QStringLiteral("数值"));
            break;
        }
    }

    if (isSource(c.type)) {
        const QString unit = unitSymbol(sourceUnitFor(c.type));
        m_dc->setPlaceholderText(QStringLiteral("例如 5%1").arg(unit));
        m_amplitude->setPlaceholderText(QStringLiteral("例如 220%1").arg(unit));
        m_offset->setPlaceholderText(QStringLiteral("例如 0%1").arg(unit));
        m_pulseV1->setPlaceholderText(QStringLiteral("例如 0%1").arg(unit));
        m_pulseV2->setPlaceholderText(QStringLiteral("例如 5%1").arg(unit));
    }
    m_frequency->setPlaceholderText(QStringLiteral("例如 1kHz"));
    m_delay->setPlaceholderText(QStringLiteral("例如 1ms"));
    for (QLineEdit* edit : {m_rise, m_fall, m_width, m_period})
        edit->setPlaceholderText(QStringLiteral("例如 1ms"));

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

void PropertyPanel::applyNetLabel()
{
    if (!m_item || m_updating)
        return;
    if (m_item->component().type != ComponentType::NetLabel)
        return;
    const QString name = m_netLabel->text().trimmed();
    if (name.isEmpty()) {
        markInvalid(m_netLabel, QStringLiteral("网络名不能为空"));
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

    if (type == ComponentType::Led || type == ComponentType::Optocoupler) {
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
            markInvalid(m_logicHigh, ok ? QStringLiteral("高电平必须大于 0") : reason);
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
            markInvalid(m_value, QStringLiteral("请按 N1:N2 或 N1:N2:N3 填写"));
            return;
        }
        QVector<double> turns;
        for (const QString& part : parts) {
            const double v = parseQuantity(part, UnitKind::None, &ok, &reason);
            if (!ok || v <= 0.0) {
                markInvalid(m_value, QStringLiteral("匝数比必须是正数"));
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
