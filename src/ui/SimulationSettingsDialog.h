#pragma once

#include "sim/Simulator.h"

#include "core/Units.h"

#include <QDialog>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace csm {

class SimulationSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SimulationSettingsDialog(const SimulationOptions& options, QWidget* parent = nullptr);

    SimulationOptions options() const { return m_options; }

protected:
    void accept() override;

private:
    void updateVisibility();
    bool parseField(QLineEdit* edit, double* out, const QString& label, UnitKind unit);

    SimulationOptions m_options;
    QComboBox* m_kind = nullptr;
    QComboBox* m_display = nullptr;
    QFormLayout* m_form = nullptr;
    QLineEdit* m_tStop = nullptr;
    QLineEdit* m_tStep = nullptr;
    QLineEdit* m_fStart = nullptr;
    QLineEdit* m_fStop = nullptr;
    QSpinBox* m_points = nullptr;
    QLabel* m_scopeHint = nullptr;
};

} // namespace csm
