#pragma once

#include "core/Units.h"
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
class QLineEdit;
class QPushButton;
class QTimer;
class QValueAxis;
class QStackedWidget;
class QPlainTextEdit;
class QLineSeries;
QT_END_NAMESPACE

namespace csm {

/// 波形查看器：多信号叠加、缩放、CSV / 图片导出。
class WaveformWindow : public QWidget {
    Q_OBJECT
public:
    /// 按当前主题刷新图表配色（切深浅色时调用）
    void applyTheme();
    explicit WaveformWindow(QWidget* parent = nullptr);

    void setResult(const SimulationResult& result);

    /// 静态波形标签页里的分析参数（替代原来的「分析设置」对话框）
    void setOptions(const SimulationOptions& options);
    SimulationOptions options() const { return m_options; }

    /// 以实时方式连续仿真并滚动显示（白色主题，与静态波形共用界面）
    void startLive(const class Circuit& circuit,
                   const QVector<QPair<QString, QString>>& probes);

    /// 用新电路继续实时仿真，不清空波形与时间轴。
    /// 返回 false 表示无法原地更新（测试点变了或电路不可解），调用方决定是否重启。
    bool updateLiveCircuit(const class Circuit& circuit,
                           const QVector<QPair<QString, QString>>& probes);

    /// 启动前预设示波器刻度（来自工程文件）
    void setLiveScalePreferences(double timeDiv, double voltDiv, double ampDiv);
    double liveTimeDiv() const { return m_liveDiv; }
    /// 是否已经自动定过量程（改时基不该把它清掉，见 restartLiveEngine 的注释）
    bool liveAutoScaled() const { return m_liveAutoScaled; }
    double liveVoltDiv() const { return m_liveVoltDiv; }
    double liveAmpDiv() const { return m_liveAmpDiv; }
    bool isLive() const { return m_live != nullptr; }
    /// 停止实时仿真（切换到静态波形或关闭面板时调用）
    void stopLiveScope();
    double liveTime() const;

    /// 实时仿真每推进一批采样点后发出，主窗口用它刷新 LED 等指示
    double liveValue(const QString& name) const;
    /// 反接击穿的极性电容（元件 id → 反向电压）
    QHash<int, double> liveBlownCapacitors() const;

signals:
    /// 用户在静态波形页改了参数并点了「重新仿真」
    void analysisRequested(const SimulationOptions& options);
    void liveUpdated();
    /// 实时仿真的运行状态变化，主窗口用它启用/禁用「停止仿真」
    void liveStateChanged(bool running);
    /// 用户改了时间/格、电压/格、电流/格
    void liveScalesChanged(double timeDiv, double voltDiv, double ampDiv);

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
    void updateAnalysisRows();
    void applyAnalysisFields();
    void selectAll(bool on);

    SimulationResult m_result;
    QChart* m_chart = nullptr;
    QChartView* m_view = nullptr;
    QStackedWidget* m_stack = nullptr;      // 图表 / 工作点报告二选一
    QPlainTextEdit* m_report = nullptr;     // 直流工作点报告（没有时间序列时显示）
    QListWidget* m_list = nullptr;
    QPushButton* m_allButton = nullptr;
    QPushButton* m_noneButton = nullptr;
    QLabel* m_info = nullptr;
    QPushButton* m_liveAutoButton = nullptr; // 动态示波器的「自动缩放」
    QWidget* m_analysisBar = nullptr;
    QComboBox* m_kindBox = nullptr;
    QLineEdit* m_tStopEdit = nullptr;
    QLineEdit* m_tStepEdit = nullptr;
    QLineEdit* m_fStartEdit = nullptr;
    QLineEdit* m_fStopEdit = nullptr;
    QLineEdit* m_pointsEdit = nullptr;
    SimulationOptions m_options;
    QLabel* m_tipLabel = nullptr;
    bool m_building = false;

    std::unique_ptr<class LiveTransient> m_live;
    QTimer* m_liveTimer = nullptr;
    bool m_restartingLive = false; // 重建引擎时不向外发「已停止」
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
    /// 用户手动选过时基后就不再自动换挡（和电压档的 m_liveScaleLocked 同理）
    bool m_liveTimeLocked = false;
    /// 正由「自动换挡」改写时基：此时不要把它记成用户选择
    bool m_liveAutoPicking = false;
    int m_liveDecimation = 1;
    int m_liveSampleCounter = 0;
    bool m_livePlaying = false;
    bool m_liveAutoScaled = false;
    /// 用户手动改过「电压/格」或「电流/格」之后，就不再自动换挡。
    /// 否则选了 500 mV/格，一旦信号超过 ±1.5 V 又被自动跳到 2 V/格，
    /// 用户的选择形同虚设。
    bool m_liveScaleLocked = false;

    void updateLiveDecimation();
    /// 实时页：按勾选状态显示/隐藏曲线
    void updateLiveVisibility();
    void restartLiveEngine();
    /// 按电路里最快的信号自动挑时基（默认 500 µs/格 看不清 1 MHz 的波形）
    void autoPickLiveTimeBase();
    void applyLiveScales();
    void autoPickLiveScales();
    /// 从当前缓冲里估信号周期（找不到返回 0）
    double estimateLivePeriod() const;
    /// 按周期挑一个能看两三个周期的时基
    void pickLiveTimeBase();
};

} // namespace csm
