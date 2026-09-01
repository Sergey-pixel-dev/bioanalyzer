#ifndef MONITORINGPAGE_H
#define MONITORINGPAGE_H

#include <QWidget>
#include <QVector>
#include <QHash>
#include <cstdint>

#include "adapters/qtdevicesessionadapter.h"
#include "dsp/biquad.h"

class AppContext;
class QtDeviceSessionAdapter;
class QCustomPlot;
class QCPGraph;
class QCPAxisRect;
class QCPItemLine;
class QTimer;
class QTabWidget;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QSlider;
class QLabel;
class QTableWidget;
class QCPTextElement;
class QMouseEvent;
class QWheelEvent;

// Live monitoring page.
//
// Organised into three tabs: Graphs (stacked per-channel traces), Filters
// (enable/disable individual biquad stages and choose their application
// order), and FFT (spectrum recomputed automatically every second). The top
// control bar carries the display options (time window, draw rate, units) plus
// recording and — only when a recording is loaded — playback transport.
//
// Graph interactions: mouse-wheel zoom and left-drag rubber-band area zoom
// change the visible part of the fixed [0, 10] second cycle; a middle-click
// restores the full cycle. The FFT plot behaves independently.
//
// Per-channel sample data lives in rolling buffers here (the display copy);
// the core SampleBuffer keeps the authoritative history for analysis. Values
// are stored in microvolts and converted to the selected display unit at draw
// time so switching units rebuilds axes, traces, and tables consistently.
class MonitoringPage : public QWidget
{
    Q_OBJECT
public:
    explicit MonitoringPage(AppContext *context, QWidget *parent = nullptr);
    ~MonitoringPage() override;

private slots:


    void onSessionChanged(QtDeviceSessionAdapter *session);
    void onDeviceInfoChanged(int channelCount);
    void onMuxTestChanged(bool enabled);
    void onChannelToggled(bool checked);
    void onSamples(const QtDeviceSessionAdapter::SampleBlock &block);
    void onRedraw();
    void onTimeWindowChanged(double seconds);
    void onDecimationChanged(int pointsPerSec);
    void onUnitChanged(int index);
    void onXRangeChanged();

    // Plot interaction.
    void onPlotMousePress(QMouseEvent *event);
    void onPlotWheel(QWheelEvent *event);
    void onFftMousePress(QMouseEvent *event);
    void onFftWheel(QWheelEvent *event);

    // Filters tab.
    void applyFilters();
    void clearFilters();

    // FFT tab.
    void computeFft();

    // Recording / playback.
    void onOpenRecording();
    void toggleRecording();
    void onPlayPause();
    void onSeek(int sliderValue);
    void onSpeedChanged(int index);
    void onPlaybackPosition(quint64 sampleIndex);

private:
    struct ChannelView
    {
        quint8 physIndex = 0;
        bool enabled = true;
        QCPAxisRect *axisRect = nullptr;
        QCPGraph *currentGraph = nullptr;
        QCPItemLine *cycleDivider = nullptr;
        QCPTextElement *noiseLabel = nullptr;
        QVector<double> previousRaw;
        QVector<double> currentRaw;
        QVector<double> previousFiltered;
        QVector<double> currentFiltered;
        dsp::FilterChain filter;  // per-channel display filter
        double noiseRms = 0.0;

    };

    // One user-configurable filter stage in the Filters tab.
    struct FilterStage
    {
        dsp::Biquad::Type type = dsp::Biquad::Type::HighPass;
        QCheckBox *enable = nullptr;
        QSpinBox *order = nullptr;
        QDoubleSpinBox *freq = nullptr; // corner/center frequency (Hz)
        QDoubleSpinBox *q = nullptr;    // quality factor
    };

    void buildUi();
    QWidget *buildGraphTab();
    QWidget *buildFilterTab();
    QWidget *buildFftTab();
    void rebuildChannels(const QVector<quint8> &channels);
    void clearChannels();
    void redrawChannel(ChannelView &cv);
    // Discard the display buffers and restart the logical 10-second cycle.
    void resetTimeBaseTo(quint64 sampleIndex);
    int currentSampleRate() const;

    void updateFilterChains();
    void reprocessFilters(); // recompute every channel's filtered buffer from raw
    void resetPlotScale();
    void applyUnitLabels();
    void rebuildChannelChecks(int channelCount);


    AppContext *m_context = nullptr;
    QtDeviceSessionAdapter *m_session = nullptr;

    QTabWidget *m_tabs = nullptr;
    QCustomPlot *m_plot = nullptr;
    QWidget *m_channelSelector = nullptr;
    QVector<QCheckBox *> m_channelChecks;
    bool m_updatingChannelChecks = false;
    QVector<ChannelView> m_channels;
    QTimer *m_redrawTimer = nullptr;

    // Display controls.
    QDoubleSpinBox *m_timeWindowSpin = nullptr;
    QSpinBox *m_decimationSpin = nullptr;
    QComboBox *m_unitCombo = nullptr;
    double m_timeWindowSec = 10.0;
    double m_viewSpanSec = 10.0;  // current X span (never wider than 10 s)
    int m_pointsPerSec = 250;     // decimation target

    uint64_t m_cycleSampleCount = 0;
    bool m_hasPreviousCycle = false;


    double m_unitScale = 1.0; // µV -> display unit multiplier
    QString m_unitSuffix = QStringLiteral("µV");
    bool m_syncing = false;           // guards recursive range sync
    bool m_followLatest = true;       // retained for reset/compatibility
    bool m_autoScaleY = true;         // amplitude auto-fits until the user zooms Y
    bool m_programmaticRange = false; // set while WE change axis ranges


    // Filter tab controls.
    bool m_filterEnabled = false;
    QCheckBox *m_filterEnableCheck = nullptr;
    QVector<FilterStage> m_filterStages;

    // FFT tab controls.
    QComboBox *m_fftChannelCombo = nullptr;
    QComboBox *m_fftWindowCombo = nullptr;
    QCustomPlot *m_fftPlot = nullptr;
    QTableWidget *m_harmonicsTable = nullptr;
    QTimer *m_fftTimer = nullptr;
    bool m_fftAutoScale = true; // auto-fit the spectrum until the user zooms

    // Recording / playback.
    QPushButton *m_openButton = nullptr;
    QPushButton *m_recordButton = nullptr;
    bool m_recording = false;
    QWidget *m_transportWidget = nullptr;
    QPushButton *m_playButton = nullptr;
    QSlider *m_seekSlider = nullptr;
    QComboBox *m_speedCombo = nullptr;
    QLabel *m_positionLabel = nullptr;
    bool m_isPlayback = false;
    bool m_seekSliderHeld = false;
    bool m_resumeAfterScrub = false; // playback was running when the drag began
};


#endif // MONITORINGPAGE_H
