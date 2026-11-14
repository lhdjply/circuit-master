#pragma once

#include "sim/Simulator.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QChart;
class QChartView;
class QLabel;
class QListWidget;
QT_END_NAMESPACE

namespace csm {

/// 波形查看器：多信号叠加、缩放、CSV / 图片导出。
class WaveformWindow : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWindow(QWidget* parent = nullptr);

    void setResult(const SimulationResult& result);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void populateList();
    void rebuild();
    void exportCsv();
    void exportImage();
    void autoScale();
    void selectAll(bool on);

    SimulationResult m_result;
    QChart* m_chart = nullptr;
    QChartView* m_view = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_info = nullptr;
    bool m_building = false;
};

} // namespace csm
