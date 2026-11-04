#ifndef MONITORINGPAGE_H
#define MONITORINGPAGE_H

#include <QWidget>
#include <QVector>
#include <QHash>
#include <cstdint>

#include "device/qtdevicesessionadapter.h"
#include "monitoring/biquad.h"
#include "monitoring/streamprocessor.h"

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
class QLabel;
class QTableWidget;
class QSplitter;
class QCPTextElement;
class QMouseEvent;
class QWheelEvent;
class QMenu;
class QAction;

// Live monitoring page.
//
// Organised into three tabs: Graphs (stacked per-channel traces), Filters
// (enable/disable individual biquad stages and choose their application
// order), and FFT (spectrum recomputed periodically). The top
// control bar carries the display options (time window, draw rate, units).
// Recording the stream to a file and playing a recording back are device
// matters: both live on the device page (playback through the virtual device
// and the playback bar).
//
// Graph interactions: mouse-wheel zoom and left-drag rubber-band area zoom
// change the visible part of the sweep cycle, whose length (1–60 s) is set in
// the "Time window" field; a middle-click restores the full cycle. A
// right-click opens the menu of the amplitude axis: auto-scale, a fixed range
// or a fixed span (Y_max − Y_min) centred on the signal; the choice applies to
// every channel. The FFT plot behaves independently.
//
// Samples are accumulated by dsp::StreamProcessor, which subscribes to the
// stream itself and works in the packet-receiving thread; the page reads the
// accumulated cycle under the processor's lock on its redraw timer. Values
// are stored in microvolts and converted to the selected display unit at draw
// time so switching units rebuilds axes, traces, and tables consistently.
class MonitoringPage : public QWidget
{
    Q_OBJECT
public:
    explicit MonitoringPage(AppContext *context, QWidget *parent = nullptr);
    ~MonitoringPage() override;

protected:
    // Графики перерисовываются, только пока страница видна: скрытая
    // страница не тратит время потока интерфейса на отрисовку.
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:


    void onSessionChanged(QtDeviceSessionAdapter *session);
    void onDeviceInfoChanged(int channelCount);
    void onMuxTestChanged(bool enabled);
    void onChannelToggled(bool checked);
    void onRedraw();
    void onTimeWindowChanged(double seconds);
    void onDecimationChanged(int pointsPerSec);
    void onUnitChanged(int index);
    void onXRangeChanged();

    // Plot interaction.
    void onPlotMousePress(QMouseEvent *event);
    void onPlotWheel(QWheelEvent *event);
    void onPlotContextMenu(const QPoint &pos);
    void onFftMousePress(QMouseEvent *event);
    void onFftWheel(QWheelEvent *event);

    // Filters tab.
    void applyFilters();
    void clearFilters();

    // FFT tab.
    void computeFft();


private:
    // Запускает таймер осциллограмм только на видимой вкладке «Graphs», а
    // таймер спектра --- только на видимой вкладке «FFT»; остальное время
    // обработчик потока продолжает накапливать отсчёты без отрисовки.
    void updateRefreshTimers(bool pageVisible);

    // Виджеты одного канала. Отсчёты, фильтр и уровень шума хранит
    // dsp::StreamProcessor: данные отделены от их представления.
    struct ChannelView
    {
        quint8 physIndex = 0;
        bool enabled = true;
        QCPAxisRect *axisRect = nullptr;
        QCPGraph *currentGraph = nullptr;
        QCPItemLine *cycleDivider = nullptr;
        QCPTextElement *noiseLabel = nullptr;
    };

    // One user-configurable filter stage in the Filters tab.
    struct FilterStage
    {
        dsp::Biquad::Type type = dsp::Biquad::Type::HighPass;
        QCheckBox *enable = nullptr;
        QSpinBox *digitalOrder = nullptr;
        QDoubleSpinBox *freq = nullptr; // corner/center frequency (Hz)
        QDoubleSpinBox *q = nullptr;    // quality factor
    };

    void buildUi();
    QWidget *buildGraphTab();
    QWidget *buildFilterTab();
    QWidget *buildFftTab();
    void rebuildChannels(const QVector<quint8> &channels);
    void clearChannels();
    // Передать обработчику частоту и каналы идущего потока из сеанса устройства.
    void setProcessorStream();
    void redrawChannel(int index);
    // Уровень шума показывается только во внутренних тестовых режимах
    // устройства: вне их измеряется не шум тракта, а мышечная активность.
    bool noiseMeasurable() const;
    void updateNoiseLabels();
    int currentSampleRate() const;

    // Цепочка включённых звеньев в порядке строк таблицы.
    std::vector<dsp::FilterSpec> filterChainFromTable() const;
    // Частота среза и центральная частота ограничены половиной частоты
    // дискретизации (ТЗ): поле не принимает большего значения.
    void updateFilterRanges();
    void resetPlotScale();
    // Масштаб оси амплитуд канала по выбранному способу.
    void applyValueScale(ChannelView &cv);
    void setYScaleMode(int mode);
    void syncYScaleActions();
    // Окна ввода постоянного диапазона и размаха; поля заполняются текущим
    // диапазоном канала, по которому щёлкнули.
    void askFixedRange();
    void askFixedSpan();
    QDoubleSpinBox *makeAmplitudeSpin(QWidget *parent) const;
    void applyUnitLabels();
    void rebuildChannelChecks(const QVector<quint8> &channels);
    void applyThemeColors();


    AppContext *m_context = nullptr;
    QtDeviceSessionAdapter *m_session = nullptr;

    QTabWidget *m_tabs = nullptr;
    QWidget *m_graphTab = nullptr;
    QWidget *m_fftTab = nullptr;
    QCustomPlot *m_plot = nullptr;
    QWidget *m_channelSelector = nullptr;
    QVector<QCheckBox *> m_channelChecks;
    bool m_updatingChannelChecks = false;
    QVector<ChannelView> m_channels;
    dsp::StreamProcessor m_processor;
    QTimer *m_redrawTimer = nullptr;

    // Display controls.
    QDoubleSpinBox *m_timeWindowSpin = nullptr;
    QSpinBox *m_decimationSpin = nullptr;
    QComboBox *m_unitCombo = nullptr;
    double m_timeWindowSec = 10.0;
    double m_viewSpanSec = 10.0;  // current X span (never wider than the sweep)
    int m_pointsPerSec = 250;     // decimation target

    double m_unitScale = 1.0; // µV -> display unit multiplier
    QString m_unitSuffix = QStringLiteral("µV");
    bool m_syncing = false;           // guards recursive range sync
    bool m_followLatest = true;
    bool m_programmaticRange = false; // set while WE change axis ranges

    // Масштаб оси амплитуд: автомасштаб, постоянный диапазон или постоянный
    // размах с центром по сигналу. Значения хранятся в микровольтах, поэтому
    // смена единиц их не меняет.
    enum YScaleMode
    {
        YAuto,
        YFixedRange,
        YFixedSpan
    };
    int m_yScaleMode = YAuto;
    double m_yLowUv = -100.0;
    double m_yHighUv = 100.0;
    double m_ySpanUv = 200.0;
    int m_menuChannel = 0; // канал, по которому открыто меню
    QMenu *m_plotMenu = nullptr;
    QAction *m_yAutoAction = nullptr;
    QAction *m_yRangeAction = nullptr;
    QAction *m_ySpanAction = nullptr;


    // Filter tab controls. Таблица — то, что оператор правит; на поток
    // действует только цепочка, принятая кнопкой «Применить»: правка
    // параметров и перетаскивание строк до неё ничего не меняют.
    QTableWidget *m_filterTable = nullptr;
    QVector<FilterStage> m_filterStages;
    std::vector<dsp::FilterSpec> m_appliedFilters;

    // FFT tab controls.
    QComboBox *m_fftChannelCombo = nullptr;
    QComboBox *m_fftWindowCombo = nullptr;
    QCustomPlot *m_fftPlot = nullptr;
    QTableWidget *m_harmonicsTable = nullptr;
    QSplitter *m_fftSplitter = nullptr;
    QTimer *m_fftTimer = nullptr;
    bool m_fftAutoScale = true; // auto-fit the spectrum until the user zooms

};


#endif // MONITORINGPAGE_H
