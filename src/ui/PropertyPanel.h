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
    void applyNetLabel();
    void applyRefdes();
    void applyFunction();
    void applyClosed();
    void applyDc();
    void applySine();
    void applyGenerator();
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
    QLineEdit* m_rotation = nullptr;   // 旋转角度（可自由输入 15 / 45 / 60…）
    QLineEdit* m_value = nullptr;
    QCheckBox* m_polarized = nullptr;
    QLineEdit* m_k = nullptr;
    QLineEdit* m_vf = nullptr;
    QLineEdit* m_ctr = nullptr;
    QLineEdit* m_vceSat = nullptr;
    QLineEdit* m_lpri = nullptr;
    // 运放：让模型不再「理想」的六个参数
    QCheckBox* m_opPowerPins = nullptr; // 运放：是否引出 V+ / V−
    QLineEdit* m_opVsatHigh = nullptr;
    QLineEdit* m_opVsatLow = nullptr;
    QLineEdit* m_opGbw = nullptr;
    QLineEdit* m_opSlew = nullptr;      // V/µs（存的是 V/s）
    QLineEdit* m_opVos = nullptr;
    QLineEdit* m_opIbias = nullptr;
    QLineEdit* m_opRin = nullptr;
    QLineEdit* m_opRout = nullptr;
    QComboBox* m_windings = nullptr;   // 变压器绕组形式
    QComboBox* m_polarity = nullptr;   // 变压器次级同名端
    QComboBox* m_groundKind = nullptr; // 接地种类 / 测试点参考地
    QComboBox* m_meterMode = nullptr;  // 万用表档位
    QCheckBox* m_normallyClosed = nullptr; // 按钮开关：常闭
    QLineEdit* m_logicHigh = nullptr;  // 逻辑门输出高电平
    QLineEdit* m_acFrequency = nullptr; // 万用表交流档的测量频率
    QLineEdit* m_netLabel = nullptr;    // 网络标签的名字
    QLabel* m_valueLabel = nullptr;
    QComboBox* m_function = nullptr;
    QComboBox* m_waveform = nullptr;    // 信号发生器的波形
    QLineEdit* m_peak = nullptr;        // 信号发生器的峰值幅值
    QLineEdit* m_duty = nullptr;        // 方波的占空比
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
