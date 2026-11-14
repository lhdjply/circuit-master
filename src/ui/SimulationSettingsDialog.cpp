#include "ui/SimulationSettingsDialog.h"

#include "core/Units.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>

#include <cmath>
#include <QVBoxLayout>

namespace csm {

SimulationSettingsDialog::SimulationSettingsDialog(const SimulationOptions& options, QWidget* parent)
    : QDialog(parent)
    , m_options(options)
{
    setWindowTitle(QStringLiteral("分析设置"));
    setMinimumWidth(420);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(10);

    auto* intro = new QLabel(QStringLiteral("选择分析类型并设置参数。数值支持 10m、1u、1meg 等写法。"),
                             this);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color:#64748b;"));
    root->addWidget(intro);

    m_form = new QFormLayout();
    m_form->setVerticalSpacing(9);

    m_kind = new QComboBox(this);
    m_kind->addItem(QStringLiteral("直流工作点"), int(AnalysisKind::OperatingPoint));
    m_kind->addItem(QStringLiteral("瞬态分析"), int(AnalysisKind::Transient));
    m_kind->addItem(QStringLiteral("交流扫频（波特图）"), int(AnalysisKind::AC));
    m_kind->setCurrentIndex(m_kind->findData(int(options.kind)));
    m_form->addRow(QStringLiteral("分析类型"), m_kind);

    m_tStop = new QLineEdit(QString::number(options.tStop, 'g', 6), this);
    m_form->addRow(QStringLiteral("停止时间 (s)"), m_tStop);

    m_tStep = new QLineEdit(QString::number(options.tStep, 'g', 6), this);
    m_form->addRow(QStringLiteral("最大步长 (s)"), m_tStep);

    m_fStart = new QLineEdit(QString::number(options.fStart, 'g', 6), this);
    m_form->addRow(QStringLiteral("起始频率 (Hz)"), m_fStart);

    m_fStop = new QLineEdit(QString::number(options.fStop, 'g', 6), this);
    m_form->addRow(QStringLiteral("终止频率 (Hz)"), m_fStop);

    m_points = new QSpinBox(this);
    m_points->setRange(10, 2000);
    m_points->setValue(options.acPoints);
    m_form->addRow(QStringLiteral("扫频点数"), m_points);

    root->addLayout(m_form);
    root->addStretch(1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    root->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &SimulationSettingsDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &SimulationSettingsDialog::reject);
    connect(m_kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SimulationSettingsDialog::updateVisibility);

    updateVisibility();
}

void SimulationSettingsDialog::updateVisibility()
{
    const auto kind = AnalysisKind(m_kind->currentData().toInt());
    m_form->setRowVisible(m_tStop, kind == AnalysisKind::Transient);
    m_form->setRowVisible(m_tStep, kind == AnalysisKind::Transient);
    m_form->setRowVisible(m_fStart, kind == AnalysisKind::AC);
    m_form->setRowVisible(m_fStop, kind == AnalysisKind::AC);
    m_form->setRowVisible(m_points, kind == AnalysisKind::AC);
}

bool SimulationSettingsDialog::parseField(QLineEdit* edit, double* out, const QString& label)
{
    bool ok = false;
    const double value = parseEng(edit->text(), &ok);
    if (!ok || !std::isfinite(value)) {
        QMessageBox::warning(this, QStringLiteral("参数无效"),
                             QStringLiteral("%1 的数值无法识别：%2").arg(label, edit->text()));
        edit->setFocus();
        edit->selectAll();
        return false;
    }
    *out = value;
    return true;
}

void SimulationSettingsDialog::accept()
{
    SimulationOptions opt = m_options;
    opt.kind = AnalysisKind(m_kind->currentData().toInt());

    if (opt.kind == AnalysisKind::Transient) {
        if (!parseField(m_tStop, &opt.tStop, QStringLiteral("停止时间"))
            || !parseField(m_tStep, &opt.tStep, QStringLiteral("最大步长")))
            return;
        if (opt.tStop <= 0.0 || opt.tStep <= 0.0) {
            QMessageBox::warning(this, QStringLiteral("参数无效"),
                                 QStringLiteral("停止时间与步长必须大于 0。"));
            return;
        }
        if (opt.tStep > opt.tStop)
            opt.tStep = opt.tStop / 1000.0;
    } else if (opt.kind == AnalysisKind::AC) {
        if (!parseField(m_fStart, &opt.fStart, QStringLiteral("起始频率"))
            || !parseField(m_fStop, &opt.fStop, QStringLiteral("终止频率")))
            return;
        if (opt.fStart <= 0.0 || opt.fStop <= opt.fStart) {
            QMessageBox::warning(this, QStringLiteral("参数无效"),
                                 QStringLiteral("频率需满足 0 < 起始频率 < 终止频率。"));
            return;
        }
        opt.acPoints = m_points->value();
    }

    m_options = opt;
    QDialog::accept();
}

} // namespace csm
