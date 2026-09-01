#include "pages/MonitoringPage.h"

#include "app/AppContext.h"
#include "adapters/qtdevicesessionadapter.h"
#include "core/ecgadcprotocol.h"
#include "dsp/spectrum.h"
#include "qcustomplot.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QGroupBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QScrollArea>
#include <QProxyStyle>
#include <cmath>
#include <utility>

#include <QTimer>
#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtAlgorithms>
#include <algorithm>

namespace
{
    // A small palette so stacked channels are visually distinct.
    QColor channelColor(int i)
    {
        static const QColor palette[] = {
            QColor(0, 114, 189), QColor(217, 83, 25), QColor(237, 177, 32),
            QColor(126, 47, 142), QColor(119, 172, 48), QColor(77, 190, 238),
            QColor(162, 20, 47), QColor(0, 150, 136)};
        return palette[i % 8];
    }

    // Makes a left-click anywhere on a slider's groove jump the handle to that
    // spot AND start a drag from there — the YouTube-style scrub behaviour.
    // (By default Qt only page-steps toward the click.) Applied to the seek
    // slider so grab/drag/hold-to-pause all work through the native handle.
    class AbsoluteSeekStyle : public QProxyStyle
    {
    public:
        using QProxyStyle::QProxyStyle;
        int styleHint(StyleHint hint, const QStyleOption *option,
                      const QWidget *widget, QStyleHintReturn *ret) const override
        {
            if (hint == QStyle::SH_Slider_AbsoluteSetButtons)
                return Qt::LeftButton | Qt::MiddleButton;
            return QProxyStyle::styleHint(hint, option, widget, ret);
        }
    };
}


MonitoringPage::MonitoringPage(AppContext *context, QWidget *parent)
    : QWidget(parent), m_context(context)
{
    buildUi();

    m_redrawTimer = new QTimer(this);
    m_redrawTimer->setInterval(33); // ~30 fps
    connect(m_redrawTimer, &QTimer::timeout, this, &MonitoringPage::onRedraw);
    m_redrawTimer->start();

    // FFT auto-recomputes once per second per the spec.
    m_fftTimer = new QTimer(this);
    m_fftTimer->setInterval(1000);
    connect(m_fftTimer, &QTimer::timeout, this, &MonitoringPage::computeFft);
    m_fftTimer->start();

    if (m_context)
    {
        connect(m_context, &AppContext::sessionChanged,
                this, &MonitoringPage::onSessionChanged);
        onSessionChanged(m_context->session());
    }
}

MonitoringPage::~MonitoringPage() = default;

void MonitoringPage::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // --- Top control bar (full page width) ---
    auto *controlBar = new QGroupBox(this);
    auto *controls = new QHBoxLayout(controlBar);

    controls->addWidget(new QLabel(tr("Time window:")));
    m_timeWindowSpin = new QDoubleSpinBox(this);
    m_timeWindowSpin->setRange(10.0, 10.0);
    m_timeWindowSpin->setValue(10.0);
    m_timeWindowSpin->setEnabled(false);
    m_timeWindowSpin->setSuffix(tr(" s"));
    controls->addWidget(m_timeWindowSpin);

    controls->addWidget(new QLabel(tr("Draw rate:")));
    m_decimationSpin = new QSpinBox(this);
    m_decimationSpin->setRange(10, 500); // spec: 10–500 pts/s
    m_decimationSpin->setValue(m_pointsPerSec);
    m_decimationSpin->setSuffix(tr(" pts/s"));
    controls->addWidget(m_decimationSpin);

    controls->addWidget(new QLabel(tr("Units:")));
    m_unitCombo = new QComboBox(this);
    m_unitCombo->addItem(QStringLiteral("µV"), 1.0);
    m_unitCombo->addItem(QStringLiteral("mV"), 1.0e-3);
    controls->addWidget(m_unitCombo);

    controls->addStretch();

    m_openButton = new QPushButton(tr("Open recording..."), this);
    controls->addWidget(m_openButton);
    m_recordButton = new QPushButton(tr("Record"), this);
    m_recordButton->setCheckable(true);
    // Recording is only meaningful with a live device/session attached;
    // enabled dynamically in onSessionChanged.
    m_recordButton->setEnabled(false);
    m_recordButton->setToolTip(tr("Connect a device to enable recording"));
    controls->addWidget(m_recordButton);


    root->addWidget(controlBar);

    // --- Playback transport (shown only when a recording is loaded) ---
    m_transportWidget = new QWidget(this);
    auto *transport = new QHBoxLayout(m_transportWidget);
    transport->setContentsMargins(0, 0, 0, 0);
    m_playButton = new QPushButton(tr("Play"), m_transportWidget);
    m_seekSlider = new QSlider(Qt::Horizontal, m_transportWidget);
    m_seekSlider->setRange(0, 1000);
    m_speedCombo = new QComboBox(m_transportWidget);
    m_speedCombo->addItem(tr("0.25x"), 0.25);
    m_speedCombo->addItem(tr("0.5x"), 0.5);
    m_speedCombo->addItem(tr("1x"), 1.0);
    m_speedCombo->addItem(tr("2x"), 2.0);
    m_speedCombo->addItem(tr("4x"), 4.0);
    m_speedCombo->setCurrentIndex(2);
    m_positionLabel = new QLabel(tr("0 / 0"), m_transportWidget);

    transport->addWidget(m_playButton);
    transport->addWidget(m_seekSlider, 1);
    transport->addWidget(new QLabel(tr("Speed:"), m_transportWidget));
    transport->addWidget(m_speedCombo);
    transport->addWidget(m_positionLabel);
    m_transportWidget->setVisible(false);
    root->addWidget(m_transportWidget);

    // --- Tabs: Graphs / Filters / FFT ---
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildGraphTab(), tr("Graphs"));
    m_tabs->addTab(buildFilterTab(), tr("Filters"));
    m_tabs->addTab(buildFftTab(), tr("FFT"));
    root->addWidget(m_tabs, 1);

    // Wire control signals.
    connect(m_timeWindowSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &MonitoringPage::onTimeWindowChanged);
    connect(m_decimationSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &MonitoringPage::onDecimationChanged);
    connect(m_unitCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MonitoringPage::onUnitChanged);
    connect(m_openButton, &QPushButton::clicked, this, &MonitoringPage::onOpenRecording);
    connect(m_recordButton, &QPushButton::clicked, this, &MonitoringPage::toggleRecording);

    connect(m_playButton, &QPushButton::clicked, this, &MonitoringPage::onPlayPause);
    connect(m_speedCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MonitoringPage::onSpeedChanged);
    // YouTube-style scrubbing: grabbing the handle pauses playback and lets
    // the user drag; every move seeks live so the graph scrubs with the
    // handle; releasing resumes playback if it was running before the grab.
    connect(m_seekSlider, &QSlider::sliderPressed, this, [this]
            {
        m_seekSliderHeld = true;
        m_resumeAfterScrub = m_isPlayback && m_playButton->text() == tr("Pause");
        if (m_resumeAfterScrub && m_session)
            m_session->pause(); });
    connect(m_seekSlider, &QSlider::sliderMoved, this, [this](int v)
            { onSeek(v); });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]
            {
        m_seekSliderHeld = false;
        onSeek(m_seekSlider->value());
        if (m_resumeAfterScrub && m_session)
        {
            m_session->play();
            m_playButton->setText(tr("Pause"));
        }
        m_resumeAfterScrub = false; });
    // A plain left-click anywhere on the groove jumps the handle there and
    // begins a drag from that spot (YouTube-style), instead of Qt's default
    // page-step. The proxy style is parented to the slider so it lives as long.
    m_seekSlider->setStyle(new AbsoluteSeekStyle(m_seekSlider->style()));
}

QWidget *MonitoringPage::buildGraphTab()

{
    auto *tab = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_plot = new QCustomPlot(tab);
    m_plot->plotLayout()->clear(); // we manage axis rects manually
    // Wheel zoom + left-drag rubber-band area zoom.
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->setSelectionRectMode(QCP::srmZoom);
    m_plot->setMinimumHeight(300);

    // Channel visibility/stream selection. The number of checkboxes is
    // refreshed from the connected device's GetDeviceInfo response.
    auto *channelBox = new QGroupBox(tr("Channels"), tab);
    auto *channelLayout = new QHBoxLayout(channelBox);
    channelLayout->setContentsMargins(6, 2, 6, 2);
    m_channelSelector = channelBox;
    layout->addWidget(channelBox);

    // Host the plot in a scroll area so stacked channels each get a usable
    // height; the plot's minimum height grows with the channel count.
    auto *scroll = new QScrollArea(tab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(m_plot);
    layout->addWidget(scroll, 1);

    // QCustomPlot delivers its own mouse events; use them so interactions work
    // (an installed eventFilter never sees them). Wheel/drag pin the view;
    // middle-click restores the rolling default and resumes following.
    connect(m_plot, &QCustomPlot::mousePress, this, &MonitoringPage::onPlotMousePress);
    connect(m_plot, &QCustomPlot::mouseWheel, this, &MonitoringPage::onPlotWheel);

    return tab;
}

void MonitoringPage::rebuildChannelChecks(int channelCount)
{
    if (!m_channelSelector)
        return;
    auto *layout = qobject_cast<QHBoxLayout *>(m_channelSelector->layout());
    if (!layout)
        return;
    m_updatingChannelChecks = true;
    qDeleteAll(m_channelChecks);
    m_channelChecks.clear();
    while (layout->count())
    {
        auto *item = layout->takeAt(0);
        delete item;
    }
    if (channelCount <= 0)
    {
        m_channelSelector->setVisible(false);
        m_updatingChannelChecks = false;
        return;
    }
    m_channelSelector->setVisible(true);
    const int count = std::clamp(channelCount, 1, EcgAdcProtocol::kMaxChannels);
    for (int i = 0; i < count; ++i)
    {
        auto *cb = new QCheckBox(QStringLiteral("CH%1").arg(i), m_channelSelector);
        cb->setChecked(true);
        connect(cb, &QCheckBox::toggled, this, &MonitoringPage::onChannelToggled);
        m_channelChecks.push_back(cb);
        layout->addWidget(cb);
    }
    layout->addStretch();
    m_updatingChannelChecks = false;
}


QWidget *MonitoringPage::buildFilterTab()
{
    auto *tab = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(tab);

    m_filterEnableCheck = new QCheckBox(tr("Enable display filters"), tab);
    layout->addWidget(m_filterEnableCheck);

    auto *info = new QLabel(
        tr("Each stage can be toggled independently. The Order column sets the "
           "mathematical application sequence (lowest first)."),
        tab);
    info->setWordWrap(true);
    info->setEnabled(false);
    layout->addWidget(info);

    auto *box = new QGroupBox(tr("Filter stages"), tab);
    auto *grid = new QGridLayout(box);
    grid->addWidget(new QLabel(tr("Enable"), box), 0, 0);
    grid->addWidget(new QLabel(tr("Stage"), box), 0, 1);
    grid->addWidget(new QLabel(tr("Order"), box), 0, 2);
    grid->addWidget(new QLabel(tr("Frequency (Hz)"), box), 0, 3);
    grid->addWidget(new QLabel(tr("Q"), box), 0, 4);

    struct Preset
    {
        dsp::Biquad::Type type;
        const char *name;
        double freq;
        double q;
    };
    const Preset presets[] = {
        {dsp::Biquad::Type::HighPass, "High-pass", 0.5, 0.707},
        {dsp::Biquad::Type::LowPass, "Low-pass", 40.0, 0.707},
        {dsp::Biquad::Type::Notch, "Mains notch", 50.0, 30.0},
    };

    int row = 1;
    for (const auto &p : presets)
    {
        FilterStage stage;
        stage.type = p.type;
        stage.enable = new QCheckBox(box);
        auto *nameLabel = new QLabel(tr(p.name), box);
        stage.order = new QSpinBox(box);
        stage.order->setRange(1, 9);
        stage.order->setValue(row);
        stage.freq = new QDoubleSpinBox(box);
        stage.freq->setRange(0.1, 2000.0);
        stage.freq->setValue(p.freq);
        stage.q = new QDoubleSpinBox(box);
        stage.q->setRange(0.1, 100.0);
        stage.q->setValue(p.q);

        grid->addWidget(stage.enable, row, 0);
        grid->addWidget(nameLabel, row, 1);
        grid->addWidget(stage.order, row, 2);
        grid->addWidget(stage.freq, row, 3);
        grid->addWidget(stage.q, row, 4);

        m_filterStages.push_back(stage);
        ++row;
    }
    layout->addWidget(box);

    auto *btnRow = new QHBoxLayout();
    auto *applyBtn = new QPushButton(tr("Apply"), tab);
    auto *clearBtn = new QPushButton(tr("Clear"), tab);
    btnRow->addWidget(applyBtn);
    btnRow->addWidget(clearBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);
    layout->addStretch();

    connect(applyBtn, &QPushButton::clicked, this, &MonitoringPage::applyFilters);
    connect(clearBtn, &QPushButton::clicked, this, &MonitoringPage::clearFilters);

    return tab;
}

QWidget *MonitoringPage::buildFftTab()
{
    auto *tab = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(tab);

    auto *form = new QFormLayout();
    m_fftChannelCombo = new QComboBox(tab);
    form->addRow(tr("Channel:"), m_fftChannelCombo);
    m_fftWindowCombo = new QComboBox(tab);
    m_fftWindowCombo->addItem(tr("Hann"), 1);
    m_fftWindowCombo->addItem(tr("Hamming"), 2);
    m_fftWindowCombo->addItem(tr("Blackman"), 3);
    m_fftWindowCombo->addItem(tr("Rectangular"), 0);
    form->addRow(tr("Window:"), m_fftWindowCombo);
    layout->addLayout(form);

    auto *note = new QLabel(tr("Spectrum updates automatically every second."), tab);
    note->setEnabled(false);
    layout->addWidget(note);

    m_fftPlot = new QCustomPlot(tab);
    m_fftPlot->addGraph();
    m_fftPlot->xAxis->setLabel(tr("Frequency (Hz)"));
    m_fftPlot->yAxis->setLabel(tr("Magnitude (µV)"));
    m_fftPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_fftPlot->setSelectionRectMode(QCP::srmZoom);
    layout->addWidget(m_fftPlot, 1);

    // Same interaction model as the channel plot: wheel/drag zoom pins the
    // view, middle-click restores auto-fit.
    connect(m_fftPlot, &QCustomPlot::mousePress, this, &MonitoringPage::onFftMousePress);
    connect(m_fftPlot, &QCustomPlot::mouseWheel, this, &MonitoringPage::onFftWheel);

    m_harmonicsTable = new QTableWidget(0, 2, tab);
    m_harmonicsTable->setHorizontalHeaderLabels({tr("Frequency (Hz)"), tr("Magnitude (µV)")});
    m_harmonicsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_harmonicsTable->setMaximumHeight(180);
    layout->addWidget(m_harmonicsTable);

    return tab;
}

void MonitoringPage::onPlotMousePress(QMouseEvent *event)
{
    // Middle-click restores the default rolling window; wheel and left-drag
    // rubber-band both only change the visible TIME span (see rebuildChannels,
    // where each rect's zoom/drag is constrained to the horizontal axis). The
    // right edge stays pinned to the newest sample in redrawChannel, so
    // playback never appears to stop — we simply keep following at the new
    // span. Nothing to freeze here.
    if (event->button() == Qt::MiddleButton)
        resetPlotScale();
}

void MonitoringPage::onPlotWheel(QWheelEvent *)
{
    // The resulting horizontal range change is captured in onXRangeChanged,
    // which updates m_viewSpanSec so every stacked channel adopts the same
    // zoom on the next redraw. Amplitude stays auto-fit.
}



void MonitoringPage::onFftMousePress(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton)
    {
        m_fftAutoScale = true;
        m_fftPlot->rescaleAxes();
        m_fftPlot->replot();
    }
    else
    {
        m_fftAutoScale = false;
    }
}

void MonitoringPage::onFftWheel(QWheelEvent *)
{
    m_fftAutoScale = false;
}

void MonitoringPage::resetPlotScale()
{
    // Restore the full logical cycle and auto-fit each channel's amplitude.
    m_followLatest = true;
    m_autoScaleY = true;
    m_viewSpanSec = m_timeWindowSec;
    m_programmaticRange = true;

    for (ChannelView &cv : m_channels)
    {
        if (!cv.axisRect)
            continue;
        cv.axisRect->axis(QCPAxis::atBottom)->setRange(0.0, m_viewSpanSec);
        if (cv.currentGraph && !cv.currentRaw.isEmpty())
            cv.currentGraph->rescaleValueAxis(false, true);
    }
    m_programmaticRange = false;
    m_plot->replot();
}


void MonitoringPage::onSessionChanged(QtDeviceSessionAdapter *session)
{
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);

    m_session = session;
    clearChannels();

    if (!m_session)
    {
        m_isPlayback = false;
        m_transportWidget->setVisible(false);
        // No device attached: recording would only capture garbage, so keep
        // the button disabled until a live session arrives.
        m_recordButton->setEnabled(false);
        m_recordButton->setToolTip(tr("Connect a device to enable recording"));
        rebuildChannelChecks(0);
        return;
    }

    connect(m_session, &QtDeviceSessionAdapter::samplesReady,
            this, &MonitoringPage::onSamples);
    connect(m_session, &QtDeviceSessionAdapter::deviceInfoChanged,
            this, &MonitoringPage::onDeviceInfoChanged);
    // Noise is meaningful only while a controller MUX test input is active
    // (short-to-ground or internal test signal), never for normal electrodes.
    connect(m_session, &QtDeviceSessionAdapter::testModeChanged,
            this, &MonitoringPage::onMuxTestChanged);
    connect(m_session, &QtDeviceSessionAdapter::playbackPositionChanged,
            this, &MonitoringPage::onPlaybackPosition);

    m_isPlayback = m_session->isPlayback();
    // The transport strip appears only when a recording is loaded; the Record
    // button always remains on the control bar.
    m_transportWidget->setVisible(m_isPlayback);
    if (m_isPlayback)
        m_playButton->setText(tr("Play"));

    // Recording only makes sense against a live device, not while replaying a
    // file (that would just duplicate the source). Gate the button on that.
    const bool canRecord = !m_isPlayback;
    m_recordButton->setEnabled(canRecord);
    m_recordButton->setToolTip(canRecord
                                   ? tr("Record the live device stream to a .bsig file")
                                   : tr("Recording is unavailable during playback"));
    rebuildChannelChecks(m_session->channelCount());
    onMuxTestChanged(m_session->testModeEnabled());
}

void MonitoringPage::onDeviceInfoChanged(int channelCount)
{
    rebuildChannelChecks(channelCount);
}

void MonitoringPage::onMuxTestChanged(bool enabled)
{
    for (ChannelView &cv : m_channels)
    {
        if (!cv.noiseLabel)
            continue;
        cv.noiseLabel->setText(enabled
                                   ? tr("Noise RMS: %1 %2").arg(cv.noiseRms * m_unitScale, 0, 'f', 1).arg(m_unitSuffix)
                                   : QString());
    }
    if (m_plot)
        m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void MonitoringPage::onChannelToggled(bool checked)
{
    if (m_updatingChannelChecks || !m_session)
        return;
    // Keep at least one channel selected; the protocol rejects an empty set.
    if (!checked)
    {
        bool any = false;
        for (auto *cb : m_channelChecks)
            any |= cb->isChecked();
        if (!any)
        {
            if (auto *cb = qobject_cast<QCheckBox *>(sender()))
            {
                m_updatingChannelChecks = true;
                cb->setChecked(true);
                m_updatingChannelChecks = false;
            }
            return;
        }
    }
    QVector<quint8> selected;
    for (int i = 0; i < m_channelChecks.size(); ++i)
        if (m_channelChecks[i]->isChecked())
            selected.push_back(static_cast<quint8>(i));
    if (!selected.isEmpty())
        m_session->startStream(selected);
}


int MonitoringPage::currentSampleRate() const
{
    if (m_session && m_session->isPlayback())
        return m_session->playbackSampleRate();
    return m_session ? m_session->sampleRateHz() : 250;
}

void MonitoringPage::clearChannels()
{
    if (!m_plot)
        return;
    // Graphs and items keep axis pointers. Remove them before destroying the
    // manually managed axis rects, otherwise QCustomPlot may draw a graph with
    // dangling key/value axes.
    m_plot->clearGraphs();
    m_plot->clearItems();
    m_plot->plotLayout()->clear();
    m_channels.clear();
    m_cycleSampleCount = 0;
    m_hasPreviousCycle = false;
    m_plot->replot();
    if (m_fftChannelCombo)
        m_fftChannelCombo->clear();
}

void MonitoringPage::rebuildChannels(const QVector<quint8> &channels)
{
    if (!m_plot)
        return;
    m_plot->clearGraphs();
    m_plot->clearItems();
    m_plot->plotLayout()->clear();
    m_channels.clear();
    m_cycleSampleCount = 0;
    m_hasPreviousCycle = false;
    if (m_fftChannelCombo)
        m_fftChannelCombo->clear();

    for (int i = 0; i < channels.size(); ++i)
    {
        ChannelView cv;
        cv.physIndex = channels[i];
        cv.enabled = true;

        auto *rect = new QCPAxisRect(m_plot);
        rect->setupFullAxesBox(true);
        // Zoom and rubber-band drag act on the TIME axis only, so the live
        // amplitude auto-fit is never fought and every stacked channel stays
        // in lockstep (the horizontal ranges are mirrored in onXRangeChanged).
        rect->setRangeZoom(Qt::Horizontal);
        rect->setRangeDrag(Qt::Horizontal);
        rect->setRangeZoomAxes(rect->axis(QCPAxis::atBottom), nullptr);
        rect->setRangeDragAxes(rect->axis(QCPAxis::atBottom), nullptr);
        rect->axis(QCPAxis::atLeft)
            ->setLabel(QStringLiteral("CH%1 (%2)").arg(channels[i]).arg(m_unitSuffix));
        rect->axis(QCPAxis::atBottom)->setRange(0.0, m_timeWindowSec);

        // Only the bottom-most rect shows the time axis label to save space.
        if (i == channels.size() - 1)
            rect->axis(QCPAxis::atBottom)->setLabel(tr("Time (s)"));
        const int row = i * 2;
        m_plot->plotLayout()->addElement(row, 0, rect);

        auto *noise = new QCPTextElement(m_plot, QString(), QFont(QStringLiteral("Sans"), 9));
        noise->setTextColor(Qt::gray);
        noise->setTextFlags(Qt::AlignCenter);
        m_plot->plotLayout()->addElement(row + 1, 0, noise);

        auto *current = m_plot->addGraph(rect->axis(QCPAxis::atBottom),
                                         rect->axis(QCPAxis::atLeft));
        const QColor color = channelColor(channels[i]);
        current->setPen(QPen(color));

        auto *divider = new QCPItemLine(m_plot);
        divider->setClipAxisRect(rect);
        divider->start->setType(QCPItemPosition::ptPlotCoords);
        divider->end->setType(QCPItemPosition::ptPlotCoords);
        divider->start->setAxes(rect->axis(QCPAxis::atBottom), rect->axis(QCPAxis::atLeft));
        divider->end->setAxes(rect->axis(QCPAxis::atBottom), rect->axis(QCPAxis::atLeft));
        QPen dividerPen(Qt::gray);
        dividerPen.setStyle(Qt::DashLine);
        divider->setPen(dividerPen);
        divider->setVisible(false);

        cv.axisRect = rect;
        cv.currentGraph = current;
        cv.cycleDivider = divider;
        cv.noiseLabel = noise;
        m_channels.push_back(cv);

        // Synchronize horizontal ranges across all channel rects.
        connect(rect->axis(QCPAxis::atBottom),
                qOverload<const QCPRange &>(&QCPAxis::rangeChanged),
                this, &MonitoringPage::onXRangeChanged);

        if (m_fftChannelCombo)
            m_fftChannelCombo->addItem(QStringLiteral("CH%1").arg(channels[i]), i);
    }

    // Give each channel a comfortable height; the scroll area handles overflow.
    m_plot->setMinimumHeight(std::max(1, static_cast<int>(channels.size())) * 180);

    m_followLatest = true;
    m_viewSpanSec = m_timeWindowSec;
    updateFilterChains();
    m_plot->replot();
}


void MonitoringPage::onSamples(const QtDeviceSessionAdapter::SampleBlock &block)
{
    // (Re)build channel views if the active set changed.
    bool channelSetChanged = false;
    if (m_channels.size() != block.channels.size())
    {
        rebuildChannels(block.channels);
        channelSetChanged = true;
    }
    else
    {
        for (int i = 0; i < block.channels.size(); ++i)
        {
            if (m_channels[i].physIndex != block.channels[i])
            {
                rebuildChannels(block.channels);
                channelSetChanged = true;
                break;
            }
        }
    }

    if (channelSetChanged && !m_channelChecks.isEmpty())
    {
        m_updatingChannelChecks = true;
        for (int i = 0; i < m_channelChecks.size(); ++i)
            m_channelChecks[i]->setChecked(block.channels.contains(static_cast<quint8>(i)));
        m_updatingChannelChecks = false;
    }

    const int fs = currentSampleRate() > 0 ? currentSampleRate() : 250;
    const uint64_t cycleSamples = std::max<uint64_t>(1, std::llround(m_timeWindowSec * fs));

    // All channels share this position. Device blocks are expected to contain
    // the same number of samples per channel; advancing once per sample keeps
    // the divider and cycle transition aligned across every trace.
    const int sampleCount = block.samples.isEmpty() ? 0 : block.samples.first().size();
    for (int sample = 0; sample < sampleCount; ++sample)
    {
        if (m_cycleSampleCount >= cycleSamples)
        {
            for (ChannelView &cv : m_channels)
            {
                // Keep the completed cycle for analysis (FFT/filter
                // reprocessing), but do not render it.  The display always
                // contains only the samples collected in the active cycle.
                cv.previousRaw = std::move(cv.currentRaw);
                cv.currentRaw.clear();
                cv.previousFiltered = std::move(cv.currentFiltered);
                cv.currentFiltered.clear();
                if (cv.cycleDivider)
                    cv.cycleDivider->setVisible(true);
            }
            m_hasPreviousCycle = true;
            m_cycleSampleCount = 0;
        }

        for (int i = 0; i < block.samples.size() && i < m_channels.size(); ++i)
        {
            ChannelView &cv = m_channels[i];
            if (!cv.enabled || sample >= block.samples[i].size())
                continue;
            const double value = static_cast<double>(block.samples[i][sample]);
            cv.currentRaw.push_back(value);
            if (m_filterEnabled)
                cv.currentFiltered.push_back(cv.filter.process(value));
        }
        ++m_cycleSampleCount;
    }

    // RMS noise estimate over the latest second of the current cycle.
    for (ChannelView &cv : m_channels)
    {
        const int n = std::min<int>(cv.currentRaw.size(), std::max(1, fs));
        if (n <= 0)
            continue;
        double mean = 0.0, sq = 0.0;
        for (int k = cv.currentRaw.size() - n; k < cv.currentRaw.size(); ++k)
            mean += cv.currentRaw[k];
        mean /= n;
        for (int k = cv.currentRaw.size() - n; k < cv.currentRaw.size(); ++k)
        {
            const double d = cv.currentRaw[k] - mean;
            sq += d * d;
        }
        cv.noiseRms = std::sqrt(sq / n);
        if (cv.noiseLabel && m_session)
            cv.noiseLabel->setText(m_session->testModeEnabled()
                                       ? tr("Noise RMS: %1 %2").arg(cv.noiseRms * m_unitScale, 0, 'f', 1).arg(m_unitSuffix)
                                       : QString());
    }
}


void MonitoringPage::redrawChannel(ChannelView &cv)
{
    if (!cv.currentGraph || !cv.enabled)
        return;

    const int fs = currentSampleRate() > 0 ? currentSampleRate() : 250;
    // Decimation: draw at most m_pointsPerSec points per second of data.
    int stride = 1;
    if (m_pointsPerSec > 0 && fs > m_pointsPerSec)
        stride = fs / m_pointsPerSec;
    if (stride < 1)
        stride = 1;

    // Draw the filtered trace when filters are on (and in sync with raw),
    // otherwise the pristine signal.
    auto draw = [&](QCPGraph *graph, const QVector<double> &raw,
                    const QVector<double> &filtered)
    {
        if (!graph)
            return;
        const bool useFiltered = m_filterEnabled && filtered.size() == raw.size();
        const QVector<double> &src = useFiltered ? filtered : raw;
        QVector<double> keys, values;
        keys.reserve(src.size() / stride + 1);
        values.reserve(src.size() / stride + 1);
        for (int i = 0; i < src.size(); i += stride)
        {
            keys.push_back(static_cast<double>(i) / fs);
            values.push_back(src[i] * m_unitScale);
        }
        graph->setData(keys, values, true);
    };

    draw(cv.currentGraph, cv.currentRaw, cv.currentFiltered);

    // Amplitude: auto-fit only until the user zooms Y (m_autoScaleY == false).
    if (m_autoScaleY)
    {
        if (cv.currentGraph && !cv.currentRaw.isEmpty())
            cv.currentGraph->rescaleValueAxis(false, true);
    }

    if (cv.cycleDivider)
    {
        const double x = static_cast<double>(m_cycleSampleCount) / fs;
        const QCPRange yr = cv.axisRect->axis(QCPAxis::atLeft)->range();
        cv.cycleDivider->start->setCoords(std::min(x, m_timeWindowSec), yr.lower);
        cv.cycleDivider->end->setCoords(std::min(x, m_timeWindowSec), yr.upper);
        cv.cycleDivider->setVisible(m_hasPreviousCycle);
    }

}



void MonitoringPage::onRedraw()
{
    if (m_channels.isEmpty())
        return;
    for (ChannelView &cv : m_channels)
        redrawChannel(cv);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void MonitoringPage::onTimeWindowChanged(double seconds)
{
    Q_UNUSED(seconds);
    m_timeWindowSec = 10.0;
    if (m_followLatest)
        m_viewSpanSec = m_timeWindowSec;
}


void MonitoringPage::onDecimationChanged(int pointsPerSec)
{
    m_pointsPerSec = pointsPerSec;
}

void MonitoringPage::applyUnitLabels()
{
    for (ChannelView &cv : m_channels)
    {
        if (cv.axisRect)
            cv.axisRect->axis(QCPAxis::atLeft)
                ->setLabel(QStringLiteral("CH%1 (%2)").arg(cv.physIndex).arg(m_unitSuffix));
        if (cv.noiseLabel && m_session && m_session->testModeEnabled())
            cv.noiseLabel->setText(tr("Noise RMS: %1 %2").arg(cv.noiseRms * m_unitScale, 0, 'f', 1).arg(m_unitSuffix));
    }
    if (m_fftPlot)
        m_fftPlot->yAxis->setLabel(tr("Magnitude (%1)").arg(m_unitSuffix));
    if (m_harmonicsTable)
        m_harmonicsTable->setHorizontalHeaderLabels(
            {tr("Frequency (Hz)"), tr("Magnitude (%1)").arg(m_unitSuffix)});
}

void MonitoringPage::onUnitChanged(int index)
{
    m_unitScale = m_unitCombo->itemData(index).toDouble();
    m_unitSuffix = m_unitCombo->itemText(index);
    applyUnitLabels();
    m_plot->replot();
    computeFft();
}

void MonitoringPage::onXRangeChanged()
{
    // Mirror the sender's new X range to every other channel rect.
    if (m_syncing)
        return;
    auto *senderAxis = qobject_cast<QCPAxis *>(sender());
    if (!senderAxis)
        return;

    m_syncing = true;
    QCPRange r = senderAxis->range();
    const double maxSpan = m_timeWindowSec;
    double span = r.size();
    if (!(span > 0.0))
    {
        m_syncing = false;
        return;
    }
    if (span > maxSpan)
    {
        span = maxSpan;
        const double center = (r.lower + r.upper) * 0.5;
        r = QCPRange(center - span * 0.5, center + span * 0.5);
    }
    // Keep the logical cycle in view; zooming in is still unrestricted.
    if (span <= maxSpan)
    {
        if (r.lower < 0.0)
            r = QCPRange(0.0, span);
        if (r.upper > maxSpan)
            r = QCPRange(maxSpan - span, maxSpan);
    }

    // A user gesture (wheel zoom or rubber-band) changed the visible span.
    // Adopt it as the follow span so redrawChannel keeps the right edge pinned
    // to the newest sample at this zoom instead of snapping back to the old
    // window. Programmatic range updates from redrawChannel are ignored here.
    if (!m_programmaticRange)
    {
        if (r.size() > 1e-6)
            m_viewSpanSec = std::min(r.size(), maxSpan);
    }

    for (ChannelView &cv : m_channels)
    {
        auto *ax = cv.axisRect->axis(QCPAxis::atBottom);
        if (ax != senderAxis)
            ax->setRange(r);
    }
    if (senderAxis->range() != r)
        senderAxis->setRange(r);
    m_syncing = false;
}


void MonitoringPage::updateFilterChains()
{
    const double fs = currentSampleRate();

    // Collect enabled stages and sort by the user-specified order.
    QVector<const FilterStage *> ordered;
    for (const FilterStage &s : m_filterStages)
        if (s.enable && s.enable->isChecked())
            ordered.push_back(&s);
    std::sort(ordered.begin(), ordered.end(),
              [](const FilterStage *a, const FilterStage *b)
              { return a->order->value() < b->order->value(); });

    for (ChannelView &cv : m_channels)
    {
        cv.filter.clear();
        for (const FilterStage *s : ordered)
        {
            dsp::Biquad bq;
            bq.configure(s->type, fs, s->freq->value(), s->q->value());
            cv.filter.add(bq);
        }
        cv.filter.reset();
    }
}

void MonitoringPage::reprocessFilters()
{
    // Rebuild each channel's filtered buffer by running its (freshly reset)
    // chain over the whole raw history, so a filter change is visible on the
    // already-captured trace immediately, not only on future samples.
    for (ChannelView &cv : m_channels)
    {
        cv.previousFiltered.clear();
        cv.currentFiltered.clear();
        if (!m_filterEnabled)
            continue;
        cv.filter.reset();
        cv.previousFiltered.reserve(cv.previousRaw.size());
        for (double v : cv.previousRaw)
            cv.previousFiltered.push_back(cv.filter.process(v));
        cv.currentFiltered.reserve(cv.currentRaw.size());
        for (double v : cv.currentRaw)
            cv.currentFiltered.push_back(cv.filter.process(v));
    }
    if (m_plot)
        m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void MonitoringPage::applyFilters()
{
    // Any enabled stage implies the user wants filtering; auto-tick the master
    // toggle so pressing Apply visibly does something even if they forgot it.
    bool anyStage = false;
    for (const FilterStage &s : m_filterStages)
        if (s.enable && s.enable->isChecked())
            anyStage = true;
    if (anyStage && !m_filterEnableCheck->isChecked())
        m_filterEnableCheck->setChecked(true);

    m_filterEnabled = m_filterEnableCheck->isChecked();
    updateFilterChains();
    reprocessFilters();
}

void MonitoringPage::clearFilters()
{
    m_filterEnabled = false;
    m_filterEnableCheck->setChecked(false);
    for (FilterStage &s : m_filterStages)
        if (s.enable)
            s.enable->setChecked(false);
    for (ChannelView &cv : m_channels)
    {
        cv.filter.clear();
        cv.previousFiltered.clear();
        cv.currentFiltered.clear();
    }
    if (m_plot)
        m_plot->replot(QCustomPlot::rpQueuedReplot);
}


void MonitoringPage::computeFft()
{
    if (!m_fftChannelCombo || m_fftChannelCombo->count() == 0)
        return;
    const int idx = m_fftChannelCombo->currentData().toInt();
    if (idx < 0 || idx >= m_channels.size())
        return;

    const ChannelView &cv = m_channels[idx];
    const QVector<double> &signalBuffer = !cv.currentRaw.isEmpty() ? cv.currentRaw : cv.previousRaw;
    if (signalBuffer.size() < 16)
        return;

    std::vector<double> signal(signalBuffer.begin(), signalBuffer.end());
    const auto window = static_cast<dsp::Window>(m_fftWindowCombo->currentData().toInt());
    auto result = dsp::SpectrumAnalyzer::analyze(signal, currentSampleRate(), window, 8);

    QVector<double> freqs(result.frequencies.begin(), result.frequencies.end());
    QVector<double> mags;
    mags.reserve(static_cast<int>(result.magnitude.size()));
    for (double m : result.magnitude)
        mags.push_back(m * m_unitScale);
    m_fftPlot->graph(0)->setData(freqs, mags, true);
    m_fftPlot->graph(0)->setPen(QPen(channelColor(cv.physIndex)));
    // Only auto-fit while the user has not zoomed; otherwise hold their view.
    if (m_fftAutoScale)
        m_fftPlot->rescaleAxes();
    m_fftPlot->replot();


    m_harmonicsTable->setRowCount(static_cast<int>(result.peaks.size()));
    for (int i = 0; i < static_cast<int>(result.peaks.size()); ++i)
    {
        m_harmonicsTable->setItem(i, 0,
                                  new QTableWidgetItem(QString::number(result.peaks[i].frequencyHz, 'f', 2)));
        m_harmonicsTable->setItem(i, 1,
                                  new QTableWidgetItem(QString::number(result.peaks[i].magnitude * m_unitScale, 'f', 4)));
    }
}

void MonitoringPage::onOpenRecording()
{
    if (!m_context)
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open recording"), QString(), tr("Biosignal recordings (*.bsig)"));
    if (path.isEmpty())
        return;

    auto *session = new QtDeviceSessionAdapter();
    if (!session->setupPlayback(path))
    {
        delete session;
        return;
    }
    const QVector<quint8> channels = session->playbackChannels();
    m_context->setSession(session);
    session->connectDevice();
    // Stream the recording's channels so the graphs populate, then start
    // playback. The transport strip lets the user pause/seek from here.
    if (!channels.isEmpty())
        session->startStream(channels);
    session->play();
    if (m_playButton)
        m_playButton->setText(tr("Pause"));
}


void MonitoringPage::toggleRecording()
{
    if (!m_session)
    {
        m_recordButton->setChecked(false);
        return;
    }

    if (m_recording)
    {
        m_session->stopRecording();
        m_recording = false;
        m_recordButton->setChecked(false);
        m_recordButton->setText(tr("Record"));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save recording"), QStringLiteral("session.bsig"),
        tr("Biosignal recordings (*.bsig)"));
    if (path.isEmpty())
    {
        m_recordButton->setChecked(false);
        return;
    }

    // Prompt for a free-text description stored in the .bsig header, so the
    // user can annotate what the recording is (subject, montage, notes...).
    bool ok = false;
    const QString description = QInputDialog::getMultiLineText(
        this, tr("Recording description"),
        tr("Describe this recording (optional):"),
        tr("Monitoring session"), &ok);
    if (!ok)
    {
        // User cancelled the description dialog: abort the whole operation.
        m_recordButton->setChecked(false);
        return;
    }

    // Record every currently active channel; all enabled here (a disabled
    // display channel could be filled with a placeholder before recording).
    QVector<quint8> channels;
    QVector<bool> enabled;
    QVector<QString> labels;
    for (const ChannelView &cv : m_channels)
    {
        channels.push_back(cv.physIndex);
        enabled.push_back(cv.enabled);
        labels.push_back(QStringLiteral("CH%1").arg(cv.physIndex));
    }

    m_session->startRecording(path, description, channels, enabled, labels);

    m_recording = true;
    m_recordButton->setChecked(true);
    m_recordButton->setText(tr("Stop recording"));
}

void MonitoringPage::onPlayPause()
{
    if (!m_session || !m_isPlayback)
        return;
    // The play button toggles; infer intent from its text.
    if (m_playButton->text() == tr("Play"))
    {
        m_session->play();
        m_playButton->setText(tr("Pause"));
    }
    else
    {
        m_session->pause();
        m_playButton->setText(tr("Play"));
    }
}

void MonitoringPage::onSeek(int sliderValue)
{
    if (!m_session || !m_isPlayback)
        return;
    const quint64 total = m_session->playbackTotalSamples();
    if (total == 0)
        return;
    const quint64 target = static_cast<quint64>(
        (static_cast<double>(sliderValue) / m_seekSlider->maximum()) * total);
    m_session->seekToSample(target);
    // Re-anchor the display: drop the old trace and restart drawing from the
    // seek point so the time axis begins at the new position and grows right.
    resetTimeBaseTo(target);
}

void MonitoringPage::resetTimeBaseTo(quint64 sampleIndex)
{
    Q_UNUSED(sampleIndex);

    for (ChannelView &cv : m_channels)
    {
        cv.previousRaw.clear();
        cv.currentRaw.clear();
        cv.previousFiltered.clear();
        cv.currentFiltered.clear();
        cv.filter.reset();
        if (cv.cycleDivider)
            cv.cycleDivider->setVisible(false);
    }
    m_cycleSampleCount = 0;
    m_hasPreviousCycle = false;
    m_followLatest = true;
    m_programmaticRange = true;

    for (ChannelView &cv : m_channels)
    {
        if (cv.axisRect)
            cv.axisRect->axis(QCPAxis::atBottom)->setRange(0.0, m_viewSpanSec);
    }
    m_programmaticRange = false;
    if (m_plot)
        m_plot->replot(QCustomPlot::rpQueuedReplot);
}


void MonitoringPage::onSpeedChanged(int index)
{
    if (!m_session || !m_isPlayback)
        return;
    m_session->setPlaybackSpeed(m_speedCombo->itemData(index).toDouble());
}

void MonitoringPage::onPlaybackPosition(quint64 sampleIndex)
{
    if (!m_isPlayback || !m_session)
        return;
    const quint64 total = m_session->playbackTotalSamples();
    m_positionLabel->setText(QStringLiteral("%1 / %2").arg(sampleIndex).arg(total));
    if (!m_seekSliderHeld && total > 0)
    {
        const int v = static_cast<int>(
            (static_cast<double>(sampleIndex) / total) * m_seekSlider->maximum());
        QSignalBlocker block(m_seekSlider);
        m_seekSlider->setValue(v);
    }
}
