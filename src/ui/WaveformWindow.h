#pragma once

#include "sim/Simulator.h"

#include <QElapsedTimer>
#include <QWidget>

#include <memory>

QT_BEGIN_NAMESPACE
class QChart;
class QChartView;
class QLabel;
class QListWidget;
class QComboBox;
class QPushButton;
class QTimer;
class QValueAxis;
class QLineSeries;
QT_END_NAMESPACE

namespace csm {

/// 波形查看器：多信号叠加、缩放、CSV / 图片导出。
class WaveformWindow : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWindow(QWidget* parent = nullptr);

    void setResult(const SimulationResult& result);

    /// 以实时方式连续仿真并滚动显示（白色主题，与静态波形共用界面）
    void startLive(const class Circuit& circuit,
                   const QVector<QPair<QString, QString>>& probes);
    bool isLive() const { return m_live != nullptr; }
    double liveTime() const;

signals:
    void scopeRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void populateList();
    void rebuild();
    void liveTick();
    void stopLive();
    void setLivePlaying(bool playing);
    void exportCsv();
    void exportImage();
    void autoScale();
    void selectAll(bool on);

    SimulationResult m_result;
    QChart* m_chart = nullptr;
    QChartView* m_view = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_info = nullptr;
    QPushButton* m_scopeButton = nullptr;
    QPushButton* m_autoButton = nullptr;
    bool m_building = false;

    std::unique_ptr<class LiveTransient> m_live;
    QTimer* m_liveTimer = nullptr;
    QElapsedTimer m_liveClock;
    QVector<Signal> m_liveBuffers;
    QVector<QLineSeries*> m_liveSeries;
    QValueAxis* m_liveAxisX = nullptr;
    QValueAxis* m_liveAxisY = nullptr;
    QValueAxis* m_liveAxisY2 = nullptr;
    QVector<QPair<QString, QString>> m_probes;
    Circuit m_liveCircuit;
    QWidget* m_liveControls = nullptr;
    QPushButton* m_liveButton = nullptr;
    QComboBox* m_liveWindowBox = nullptr;
    QComboBox* m_liveVoltDivBox = nullptr;
    QComboBox* m_liveAmpDivBox = nullptr;
    QWidget* m_liveVoltWidget = nullptr;
    QWidget* m_liveAmpWidget = nullptr;
    double m_liveVoltDiv = 5.0;
    double m_liveAmpDiv = 1e-3;
    bool m_liveHasVoltage = false;
    bool m_liveHasCurrent = false;
    double m_liveDiv = 0.5e-3;
    double m_liveWindow = 5e-3;
    double m_liveSpeed = 1.0;
    int m_liveDecimation = 1;
    int m_liveSampleCounter = 0;
    bool m_livePlaying = false;

    void updateLiveDecimation();
    void restartLiveEngine();
    void applyLiveScales();
};

} // namespace csm
