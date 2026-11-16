#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QCheckBox;
class QLineEdit;
class QStackedWidget;

namespace csm {

class ComponentItem;

/// 右侧属性面板：编辑选中元件的编号、数值与独立源波形参数。
class PropertyPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertyPanel(QWidget* parent = nullptr);

    void setComponent(ComponentItem* item);
    ComponentItem* component() const { return m_item; }

signals:
    void changed();

private:
    void buildUi();
    void refreshFromItem();
    void updateVisibility();
    void updateSubtitle();
    void applyValue();
    void applyRefdes();
    void applyFunction();
    void applyClosed();
    void applyDc();
    void applySine();
    void applyPulse();
    void markInvalid(QLineEdit* edit, const QString& reason = QString());
    void markValid(QLineEdit* edit);

    ComponentItem* m_item = nullptr;
    bool m_updating = false;

    QStackedWidget* m_stack = nullptr;
    QLabel* m_placeholder = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;

    QFormLayout* m_form = nullptr;
    QLineEdit* m_refdes = nullptr;
    QLineEdit* m_value = nullptr;
    QCheckBox* m_polarized = nullptr;
    QLineEdit* m_k = nullptr;
    QLineEdit* m_vf = nullptr;
    QLabel* m_valueLabel = nullptr;
    QComboBox* m_function = nullptr;
    QLineEdit* m_dc = nullptr;
    QLineEdit* m_amplitude = nullptr;
    QLineEdit* m_frequency = nullptr;
    QLineEdit* m_phase = nullptr;
    QLineEdit* m_offset = nullptr;
    QLineEdit* m_delay = nullptr;
    QLineEdit* m_pulseV1 = nullptr;
    QLineEdit* m_pulseV2 = nullptr;
    QLineEdit* m_rise = nullptr;
    QLineEdit* m_fall = nullptr;
    QLineEdit* m_width = nullptr;
    QLineEdit* m_period = nullptr;
    QCheckBox* m_closed = nullptr;
};

} // namespace csm
