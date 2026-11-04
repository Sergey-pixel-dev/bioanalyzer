#include "navigation/MonitoringPage.h"

#include "navigation/AppContext.h"
#include "settings/thememanager.h"
#include "device/qtdevicesessionadapter.h"
#include "notifications/Notifications.h"
#include "exchange/applicationprotocol.h"
#include "monitoring/spectrum.h"
#include "qcustomplot.h"
#include "ui_monitoringpage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QShowEvent>
#include <QHideEvent>
#include <QGroupBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QSettings>
#include <QSignalBlocker>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QScrollArea>
#include <cmath>
#include <utility>

#include <QTimer>
#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDropEvent>
#include <QDrag>
#include <QMimeData>
#include <QPainter>
#include <QFont>
#include <QtAlgorithms>
#include <algorithm>
#include <functional>

namespace
{
    // A small palette so stacked channels are visually distinct.
    QColor channelColor(int i, bool dark)
    {
        static const QColor darkPalette[] = {
            QColor(80, 180, 255), QColor(255, 130, 80), QColor(255, 205, 70),
            QColor(190, 125, 235), QColor(145, 220, 90), QColor(100, 215, 240),
            QColor(245, 100, 125), QColor(70, 220, 180)};
        static const QColor lightPalette[] = {
            QColor(0, 90, 160), QColor(190, 65, 15), QColor(170, 115, 0),
            QColor(105, 35, 125), QColor(55, 125, 20), QColor(0, 120, 165),
            QColor(145, 15, 40), QColor(0, 110, 95)};
        const QColor *palette = dark ? darkPalette : lightPalette;
        return palette[i % 8];
    }

    class FilterTableWidget final : public QTableWidget
    {
    public:
        using QTableWidget::QTableWidget;

    protected:
        void startDrag(Qt::DropActions supportedActions) override
        {
            m_dragRow = currentRow();
            if (m_dragRow < 0 || m_dragRow >= rowCount())
                return;

            QModelIndexList indexes = selectedIndexes();
            // Cell widgets can leave the selection model empty even though
            // the user started dragging a visible row. The name item is the
            // canonical drag payload in that case.
            if (indexes.isEmpty())
                indexes.push_back(model()->index(m_dragRow, 1));

            auto *drag = new QDrag(this);
            drag->setMimeData(model()->mimeData(indexes));

            // QTableWidget's default drag pixmap omits text/items when rows
            // contain cell widgets. Build a small, opaque row preview instead
            // of rendering the viewport: this keeps the filter name visible
            // even while Qt hides the source row during the drag.
            const int rowHeight = std::max(28, this->rowHeight(m_dragRow));
            const int previewWidth = std::max(240, columnWidth(1) + 24);
            QPixmap pixmap(previewWidth, rowHeight);
            const QColor background = palette().color(QPalette::Base);
            pixmap.fill(background);
            QPainter painter(&pixmap);
            painter.setPen(palette().color(QPalette::Mid));
            painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
            if (auto *item = this->item(m_dragRow, 1))
            {
                const QRect textRect(12, 0, pixmap.width() - 24, rowHeight);
                QColor textColor = palette().color(QPalette::Text);
                if (qAbs(textColor.lightnessF() - background.lightnessF()) < 0.25)
                    textColor = background.lightnessF() < 0.5 ? Qt::white : Qt::black;
                painter.setPen(textColor);
                QFont font = painter.font();
                font.setBold(true);
                painter.setFont(font);
                painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                                 item->text());
            }
            painter.end();
            drag->setPixmap(pixmap);
            drag->setHotSpot(QPoint(12, rowHeight / 2));
            drag->exec(supportedActions & Qt::MoveAction ? Qt::MoveAction
                                                         : supportedActions);
        }

        void dropEvent(QDropEvent *event) override
        {
            const int source = m_dragRow >= 0 ? m_dragRow : currentRow();
            int destination = indexAt(event->position().toPoint()).row();
            if (source < 0 || source >= rowCount() || rowCount() == 0)
            {
                event->ignore();
                m_dragRow = -1;
                return;
            }
            if (destination < 0)
                destination = rowCount();
            if (destination == source || destination == source + 1)
            {
                event->acceptProposedAction();
                m_dragRow = -1;
                return;
            }

            if (destination > source)
                --destination;
            destination = std::clamp(destination, 0, rowCount() - 1);

            // Keep the row widgets alive. Removing rows/cell widgets during a
            // drag lets QTableWidget delete them, which used to corrupt the
            // filter labels and could crash while the table was being rebuilt.
            struct RowData
            {
                int type = 0;
                QString name;
                bool enabled = false;
                int digitalOrder = 1;
                double frequency = 0.0;
                double q = 0.0;
            };
            QVector<RowData> rows;
            rows.reserve(rowCount());
            for (int row = 0; row < rowCount(); ++row)
            {
                RowData data;
                if (auto *item = this->item(row, 1))
                {
                    data.type = item->data(Qt::UserRole).toInt();
                    data.name = item->text();
                }
                if (data.name.isEmpty())
                {
                    switch (static_cast<dsp::Biquad::Type>(data.type))
                    {
                    case dsp::Biquad::Type::HighPass:
                        data.name = tr("High-pass");
                        break;
                    case dsp::Biquad::Type::LowPass:
                        data.name = tr("Low-pass");
                        break;
                    case dsp::Biquad::Type::Notch:
                        data.name = tr("Mains notch");
                        break;
                    default:
                        break;
                    }
                }
                if (auto *widget = qobject_cast<QCheckBox *>(cellWidget(row, 0)))
                    data.enabled = widget->isChecked();
                if (auto *widget = qobject_cast<QSpinBox *>(cellWidget(row, 2)))
                    data.digitalOrder = widget->value();
                if (auto *widget = qobject_cast<QDoubleSpinBox *>(cellWidget(row, 3)))
                    data.frequency = widget->value();
                if (auto *widget = qobject_cast<QDoubleSpinBox *>(cellWidget(row, 4)))
                    data.q = widget->value();
                rows.push_back(data);
            }
            const RowData moved = rows.takeAt(source);
            rows.insert(destination, moved);

            for (int row = 0; row < rows.size(); ++row)
            {
                const RowData &data = rows.at(row);
                auto *item = this->item(row, 1);
                if (!item)
                {
                    item = new QTableWidgetItem;
                    setItem(row, 1, item);
                }
                item->setText(data.name);
                item->setData(Qt::UserRole, data.type);
                if (auto *widget = qobject_cast<QCheckBox *>(cellWidget(row, 0)))
                {
                    const QSignalBlocker blocker(widget);
                    widget->setChecked(data.enabled);
                }
                if (auto *widget = qobject_cast<QSpinBox *>(cellWidget(row, 2)))
                {
                    const QSignalBlocker blocker(widget);
                    widget->setValue(data.digitalOrder);
                }
                if (auto *widget = qobject_cast<QDoubleSpinBox *>(cellWidget(row, 3)))
                {
                    const QSignalBlocker blocker(widget);
                    widget->setValue(data.frequency);
                }
                if (auto *widget = qobject_cast<QDoubleSpinBox *>(cellWidget(row, 4)))
                {
                    const QSignalBlocker blocker(widget);
                    widget->setValue(data.q);
                }
            }
            setCurrentCell(destination, 1);
            event->acceptProposedAction();
            m_dragRow = -1;
        }

    private:
        int m_dragRow = -1;
    };
}

MonitoringPage::MonitoringPage(AppContext *context, QWidget *parent)
    : QWidget(parent), m_context(context)
{
    buildUi();

    // Обработчик потока сам подписан на поток отсчётов и работает в потоке
    // приёма; форма лишь читает накопленное по таймеру перерисовки.
    m_processor.setCycleSeconds(m_timeWindowSec);
    if (m_context)
        m_processor.attach(m_context->dataHub());

    m_redrawTimer = new QTimer(this);
    m_redrawTimer->setInterval(33); // ~30 fps
    connect(m_redrawTimer, &QTimer::timeout, this, &MonitoringPage::onRedraw);

    // FFT auto-recomputes once per second.
    m_fftTimer = new QTimer(this);
    m_fftTimer->setInterval(1000);
    connect(m_fftTimer, &QTimer::timeout, this, &MonitoringPage::computeFft);

    // Таймеры запускаются при показе страницы и смене вкладки.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int)
            { updateRefreshTimers(isVisible()); });
    updateRefreshTimers(isVisible());

    if (m_context)
    {
        connect(m_context, &AppContext::sessionChanged,
                this, &MonitoringPage::onSessionChanged);
        if (m_context->themeManager())
            connect(m_context->themeManager(), &ThemeManager::themeChanged,
                    this, [this](AppTheme)
                    { applyThemeColors(); });
        onSessionChanged(m_context->session());
    }
    // Графики без каналов тоже в цветах темы: иначе до первого потока или
    // смены темы они остаются белыми.
    applyThemeColors();
}

MonitoringPage::~MonitoringPage()
{
    m_processor.detach();
    if (m_fftSplitter)
    {
        QSettings settings;
        settings.setValue(QStringLiteral("monitoring/fftSplitterState"),
                          m_fftSplitter->saveState());
    }
}

void MonitoringPage::buildUi()
{
    Ui::MonitoringPageForm form;
    form.setupUi(this);
    auto *root = qobject_cast<QVBoxLayout *>(layout());
    if (!root)
        root = new QVBoxLayout(this);

    // --- Top control bar (full page width) ---
    auto *controlBar = new QGroupBox(this);
    auto *controls = new QHBoxLayout(controlBar);

    controls->addWidget(new QLabel(tr("Time window:")));
    // Ширина развёртки 1–60 с; новое значение применяется по Enter или при
    // уходе из поля, а не на каждой набранной цифре: смена ширины начинает
    // цикл развёртки заново.
    m_timeWindowSpin = new QDoubleSpinBox(this);
    m_timeWindowSpin->setRange(1.0, 60.0);
    m_timeWindowSpin->setDecimals(0);
    m_timeWindowSpin->setSingleStep(1.0);
    m_timeWindowSpin->setValue(m_timeWindowSec);
    m_timeWindowSpin->setKeyboardTracking(false);
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
    m_unitCombo->addItem(QStringLiteral("V"), 1.0e-6);
    controls->addWidget(m_unitCombo);

    controls->addStretch();

    root->addWidget(controlBar);

    // --- Tabs: Graphs / Filters / FFT ---
    m_tabs = new QTabWidget(this);
    m_graphTab = buildGraphTab();
    m_tabs->addTab(m_graphTab, tr("Graphs"));
    m_tabs->addTab(buildFilterTab(), tr("Filters"));
    m_fftTab = buildFftTab();
    m_tabs->addTab(m_fftTab, tr("FFT"));
    root->addWidget(m_tabs, 1);

    // Wire control signals.
    connect(m_timeWindowSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &MonitoringPage::onTimeWindowChanged);
    connect(m_decimationSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &MonitoringPage::onDecimationChanged);
    connect(m_unitCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MonitoringPage::onUnitChanged);
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

    // Channel visibility only. Checkboxes are created from the immutable
    // active stream geometry selected on the Devices page.
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

    // Правая кнопка открывает меню масштаба оси амплитуд. Пункты взаимно
    // исключают друг друга; отмена окна ввода возвращает отметку прежнему.
    m_plot->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_plot, &QWidget::customContextMenuRequested, this, &MonitoringPage::onPlotContextMenu);
    m_plotMenu = new QMenu(this);
    auto *yGroup = new QActionGroup(m_plotMenu);
    m_yAutoAction = m_plotMenu->addAction(tr("Auto-scale amplitude"));
    m_yAutoAction->setObjectName(QStringLiteral("yAutoScaleAction"));
    m_yRangeAction = m_plotMenu->addAction(tr("Fixed amplitude range…"));
    m_yRangeAction->setObjectName(QStringLiteral("yFixedRangeAction"));
    m_ySpanAction = m_plotMenu->addAction(tr("Fixed amplitude span…"));
    m_ySpanAction->setObjectName(QStringLiteral("yFixedSpanAction"));
    for (QAction *action : {m_yAutoAction, m_yRangeAction, m_ySpanAction})
    {
        action->setCheckable(true);
        yGroup->addAction(action);
    }
    m_yAutoAction->setChecked(true);
    connect(m_yAutoAction, &QAction::triggered, this, [this]
            { setYScaleMode(YAuto); });
    connect(m_yRangeAction, &QAction::triggered, this, &MonitoringPage::askFixedRange);
    connect(m_ySpanAction, &QAction::triggered, this, &MonitoringPage::askFixedSpan);

    return tab;
}

void MonitoringPage::rebuildChannelChecks(const QVector<quint8> &channels)
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
    if (channels.isEmpty())
    {
        m_channelSelector->setVisible(false);
        m_updatingChannelChecks = false;
        return;
    }
    m_channelSelector->setVisible(true);
    for (quint8 channel : channels)
    {
        auto *cb = new QCheckBox(QStringLiteral("CH%1").arg(channel), m_channelSelector);
        cb->setProperty("physicalChannel", channel);
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

    auto *filterTable = new FilterTableWidget(tab);
    m_filterTable = filterTable;
    m_filterTable->setColumnCount(5);
    m_filterTable->setHorizontalHeaderLabels({tr("Enabled"), tr("Filter"),
                                              tr("Digital order"), tr("Frequency (Hz)"), tr("Q")});
    m_filterTable->horizontalHeaderItem(2)->setToolTip(
        tr("Number of cascaded biquad sections; the effective filter order is twice this value."));
    m_filterTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_filterTable->setColumnWidth(0, 58);
    m_filterTable->setColumnWidth(1, 150);
    m_filterTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_filterTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_filterTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_filterTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_filterTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_filterTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_filterTable->setDragDropMode(QAbstractItemView::InternalMove);
    m_filterTable->setDefaultDropAction(Qt::MoveAction);
    m_filterTable->setDragEnabled(true);
    m_filterTable->setAcceptDrops(true);
    m_filterTable->setDropIndicatorShown(true);
    layout->addWidget(m_filterTable, 1);

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

    int row = 0;
    for (const auto &p : presets)
    {
        FilterStage stage;
        stage.type = p.type;
        m_filterTable->insertRow(row);
        stage.enable = new QCheckBox(m_filterTable);
        stage.enable->setChecked(false);
        auto *nameLabel = new QTableWidgetItem(tr(p.name));
        nameLabel->setData(Qt::UserRole, static_cast<int>(p.type));
        nameLabel->setFlags(nameLabel->flags() & ~Qt::ItemIsEditable);
        stage.digitalOrder = new QSpinBox(m_filterTable);
        stage.digitalOrder->setRange(1, 8);
        stage.digitalOrder->setValue(1);
        stage.freq = new QDoubleSpinBox(m_filterTable);
        stage.freq->setRange(0.1, 2000.0);
        stage.freq->setValue(p.freq);
        stage.q = new QDoubleSpinBox(m_filterTable);
        stage.q->setRange(0.1, 100.0);
        stage.q->setValue(p.q);

        m_filterTable->setCellWidget(row, 0, stage.enable);
        m_filterTable->setItem(row, 1, nameLabel);
        m_filterTable->setCellWidget(row, 2, stage.digitalOrder);
        m_filterTable->setCellWidget(row, 3, stage.freq);
        m_filterTable->setCellWidget(row, 4, stage.q);

        m_filterStages.push_back(stage);
        ++row;
    }
    updateFilterRanges();

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

    m_fftPlot = new QCustomPlot(tab);
    m_fftPlot->addGraph();
    m_fftPlot->xAxis->setLabel(tr("Frequency (Hz)"));
    m_fftPlot->yAxis->setLabel(tr("Magnitude (µV)"));
    m_fftPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_fftPlot->setSelectionRectMode(QCP::srmZoom);
    // Same interaction model as the channel plot: wheel/drag zoom pins the
    // view, middle-click restores auto-fit.
    connect(m_fftPlot, &QCustomPlot::mousePress, this, &MonitoringPage::onFftMousePress);
    connect(m_fftPlot, &QCustomPlot::mouseWheel, this, &MonitoringPage::onFftWheel);

    m_harmonicsTable = new QTableWidget(0, 2, tab);
    m_harmonicsTable->setHorizontalHeaderLabels({tr("Frequency (Hz)"), tr("Magnitude (µV)")});
    m_harmonicsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_harmonicsTable->setMinimumHeight(80);
    m_fftSplitter = new QSplitter(Qt::Vertical, tab);
    m_fftSplitter->addWidget(m_fftPlot);
    m_fftSplitter->addWidget(m_harmonicsTable);
    m_fftSplitter->setStretchFactor(0, 3);
    m_fftSplitter->setStretchFactor(1, 1);
    layout->addWidget(m_fftSplitter, 1);
    QSettings settings;
    const QByteArray state = settings.value(QStringLiteral("monitoring/fftSplitterState")).toByteArray();
    if (!state.isEmpty())
        m_fftSplitter->restoreState(state);

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
    // Restore the full logical cycle. The amplitude axis keeps the scale
    // chosen in the plot menu: only the menu changes it.
    m_followLatest = true;
    m_viewSpanSec = m_timeWindowSec;
    m_programmaticRange = true;

    for (ChannelView &cv : m_channels)
    {
        if (!cv.axisRect)
            continue;
        cv.axisRect->axis(QCPAxis::atBottom)->setRange(0.0, m_viewSpanSec);
        applyValueScale(cv);
    }
    m_programmaticRange = false;
    m_plot->replot();
}

void MonitoringPage::applyValueScale(ChannelView &cv)
{
    if (!cv.axisRect || !cv.currentGraph)
        return;
    QCPAxis *valueAxis = cv.axisRect->axis(QCPAxis::atLeft);
    switch (m_yScaleMode)
    {
    case YFixedRange:
        valueAxis->setRange(m_yLowUv * m_unitScale, m_yHighUv * m_unitScale);
        break;
    case YFixedSpan:
    {
        // Размах постоянный, а середина следует за сигналом видимой части
        // развёртки: масштаб не теряется, а постоянное смещение не уводит
        // сигнал за край.
        bool found = false;
        const QCPRange data = cv.currentGraph->getValueRange(
            found, QCP::sdBoth, cv.axisRect->axis(QCPAxis::atBottom)->range());
        const double center = found ? data.center() : valueAxis->range().center();
        const double half = m_ySpanUv * m_unitScale / 2.0;
        valueAxis->setRange(center - half, center + half);
        break;
    }
    default:
        if (cv.currentGraph->dataCount() > 0)
            cv.currentGraph->rescaleValueAxis(false, true);
        break;
    }
}

void MonitoringPage::setYScaleMode(int mode)
{
    m_yScaleMode = mode;
    syncYScaleActions();
    for (ChannelView &cv : m_channels)
        applyValueScale(cv);
    if (m_plot)
        m_plot->replot();
}

void MonitoringPage::syncYScaleActions()
{
    if (!m_yAutoAction)
        return;
    QAction *current = m_yScaleMode == YFixedRange  ? m_yRangeAction
                       : m_yScaleMode == YFixedSpan ? m_ySpanAction
                                                    : m_yAutoAction;
    current->setChecked(true);
}

void MonitoringPage::onPlotContextMenu(const QPoint &pos)
{
    m_menuChannel = 0;
    if (QCPAxisRect *rect = m_plot->axisRectAt(pos))
    {
        for (int i = 0; i < m_channels.size(); ++i)
        {
            if (m_channels[i].axisRect == rect)
                m_menuChannel = i;
        }
    }
    syncYScaleActions();
    m_plotMenu->popup(m_plot->mapToGlobal(pos));
}

QDoubleSpinBox *MonitoringPage::makeAmplitudeSpin(QWidget *parent) const
{
    // Предел поля --- 10 В в выбранных единицах, точность --- 0,1 мкВ.
    auto *spin = new QDoubleSpinBox(parent);
    const double limit = 1.0e7 * m_unitScale;
    spin->setRange(-limit, limit);
    spin->setDecimals(std::max(1, static_cast<int>(std::lround(1.0 - std::log10(m_unitScale)))));
    spin->setSuffix(QStringLiteral(" ") + m_unitSuffix);
    return spin;
}

void MonitoringPage::askFixedRange()
{
    QCPRange current(m_yLowUv * m_unitScale, m_yHighUv * m_unitScale);
    if (m_menuChannel >= 0 && m_menuChannel < m_channels.size() && m_channels[m_menuChannel].axisRect)
        current = m_channels[m_menuChannel].axisRect->axis(QCPAxis::atLeft)->range();

    // Окно открывается без вложенного цикла событий, как окна сообщений:
    // графики продолжают обновляться, пока оператор вводит значения.
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(QStringLiteral("yFixedRangeDialog"));
    dialog->setWindowTitle(tr("Amplitude range"));
    auto *form = new QFormLayout(dialog);
    QDoubleSpinBox *low = makeAmplitudeSpin(dialog);
    QDoubleSpinBox *high = makeAmplitudeSpin(dialog);
    low->setObjectName(QStringLiteral("low"));
    high->setObjectName(QStringLiteral("high"));
    low->setValue(current.lower);
    high->setValue(current.upper);
    form->addRow(tr("From:"), low);
    form->addRow(tr("To:"), high);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    form->addRow(buttons);
    auto updateOk = [buttons, low, high]
    { buttons->button(QDialogButtonBox::Ok)->setEnabled(high->value() > low->value()); };
    connect(low, qOverload<double>(&QDoubleSpinBox::valueChanged), dialog, updateOk);
    connect(high, qOverload<double>(&QDoubleSpinBox::valueChanged), dialog, updateOk);
    updateOk();
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, low, high]
            {
                m_yLowUv = low->value() / m_unitScale;
                m_yHighUv = high->value() / m_unitScale;
                setYScaleMode(YFixedRange); });
    connect(dialog, &QDialog::rejected, this, &MonitoringPage::syncYScaleActions);
    dialog->open();
}

void MonitoringPage::askFixedSpan()
{
    double current = m_ySpanUv * m_unitScale;
    if (m_menuChannel >= 0 && m_menuChannel < m_channels.size() && m_channels[m_menuChannel].axisRect)
        current = m_channels[m_menuChannel].axisRect->axis(QCPAxis::atLeft)->range().size();

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(QStringLiteral("yFixedSpanDialog"));
    dialog->setWindowTitle(tr("Amplitude span"));
    auto *form = new QFormLayout(dialog);
    QDoubleSpinBox *span = makeAmplitudeSpin(dialog);
    span->setObjectName(QStringLiteral("span"));
    span->setMinimum(0.1 * m_unitScale);
    span->setValue(current);
    form->addRow(tr("Span (Y max − Y min):"), span);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, span]
            {
                m_ySpanUv = span->value() / m_unitScale;
                setYScaleMode(YFixedSpan); });
    connect(dialog, &QDialog::rejected, this, &MonitoringPage::syncYScaleActions);
    dialog->open();
}

void MonitoringPage::onSessionChanged(QtDeviceSessionAdapter *session)
{
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);

    m_session = session;
    clearChannels();

    if (!m_session)
    {
        rebuildChannelChecks(QVector<quint8>());
        return;
    }

    // Страница устройства могла пустить поток до открытия этой страницы,
    // поэтому геометрия графиков перестраивается по смене набора каналов,
    // а не только по действию оператора.
    connect(m_session, &QtDeviceSessionAdapter::activeChannelsChanged,
            this, [this](const QVector<quint8> &channels)
            {
                rebuildChannelChecks(channels);
                if (!channels.isEmpty())
                    rebuildChannels(channels); });
    connect(m_session, &QtDeviceSessionAdapter::deviceInfoChanged,
            this, &MonitoringPage::onDeviceInfoChanged);
    // Частота дискретизации и каналы известны к запуску потока и до его
    // остановки не меняются.
    connect(m_session, &QtDeviceSessionAdapter::streamingChanged,
            this, [this](bool streaming)
            {
                if (streaming)
                {
                    updateFilterRanges();
                    setProcessorStream();
                }
                else
                    m_processor.endStream(); });
    // Noise is meaningful only while a controller MUX test input is active
    // (short-to-ground or internal test signal), never for normal electrodes.
    connect(m_session, &QtDeviceSessionAdapter::testModeChanged,
            this, &MonitoringPage::onMuxTestChanged);
    connect(m_session, &QtDeviceSessionAdapter::muxTestChanged,
            this, &MonitoringPage::onMuxTestChanged);

    QVector<quint8> channels = m_session ? m_session->activeChannels()
                                         : QVector<quint8>();
    rebuildChannelChecks(channels);
    if (!channels.isEmpty())
        rebuildChannels(channels);
    // Поток мог быть запущен до открытия этой страницы.
    if (m_session->isStreaming())
        setProcessorStream();
    onMuxTestChanged(m_session->testModeEnabled());
}

void MonitoringPage::setProcessorStream()
{
    if (!m_session)
        return;
    const QVector<quint8> active = m_session->activeChannels();
    m_processor.setStream(std::vector<uint8_t>(active.begin(), active.end()),
                          m_session->sampleRateHz());
}

void MonitoringPage::onDeviceInfoChanged(int channelCount)
{
    Q_UNUSED(channelCount);
    if (!m_session)
        return;
    const auto active = m_session->activeChannels();
    if (!active.isEmpty())
    {
        rebuildChannelChecks(active);
        rebuildChannels(active);
    }
    else
    {
        rebuildChannelChecks(QVector<quint8>());
    }
}

void MonitoringPage::onMuxTestChanged(bool enabled)
{
    Q_UNUSED(enabled);
    updateNoiseLabels();
    if (m_plot)
        m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void MonitoringPage::onChannelToggled(bool checked)
{
    Q_UNUSED(checked);
    if (m_updatingChannelChecks || !m_session)
        return;
    // Channel checkboxes control visibility only. Physical acquisition
    // channels are selected on the Devices page, which is the sole owner of
    // the stream.
    for (ChannelView &cv : m_channels)
    {
        cv.enabled = true;
        for (auto *cb : m_channelChecks)
        {
            if (cb->property("physicalChannel").toInt() == cv.physIndex)
            {
                cv.enabled = cb->isChecked();
                break;
            }
        }
    }
    if (m_plot)
        m_plot->replot(QCustomPlot::rpQueuedReplot);
}

int MonitoringPage::currentSampleRate() const
{
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
    // Накопленное прежним сеансом отбрасывается: отписка дожидается конца
    // обработки блока, после чего сброс исполняется сразу.
    m_processor.detach();
    m_processor.reset();
    m_processor.endStream();
    if (m_context)
        m_processor.attach(m_context->dataHub());
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
        const QColor color = channelColor(channels[i],
                                          m_context && m_context->themeManager()
                                              ? m_context->themeManager()->isDark()
                                              : true);
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
    // Геометрию потока обработчик берёт из самих блоков; цепочка — принятая
    // «Применить», а не текущая правка таблицы.
    m_processor.setFilters(m_appliedFilters);
    applyThemeColors();
    m_plot->replot();
}

bool MonitoringPage::noiseMeasurable() const
{
    return m_session && (m_session->testModeEnabled() || m_session->muxTestEnabled());
}

void MonitoringPage::updateNoiseLabels()
{
    const bool show = noiseMeasurable();
    std::size_t processed = 0;
    m_processor.read([&](const dsp::StreamProcessor::View &view)
                     { processed = view.buffer.channelCount(); });
    const int count = std::min<int>(m_channels.size(), static_cast<int>(processed));
    for (int i = 0; i < count; ++i)
    {
        if (!m_channels[i].noiseLabel)
            continue;
        m_channels[i].noiseLabel->setText(
            show ? tr("Noise RMS: %1 %2")
                       .arg(m_processor.noiseRms(static_cast<std::size_t>(i)) * m_unitScale, 0, 'f', 1)
                       .arg(m_unitSuffix)
                 : QString());
    }
}

void MonitoringPage::redrawChannel(int index)
{
    if (index < 0 || index >= m_channels.size())
        return;
    ChannelView &cv = m_channels[index];
    if (!cv.currentGraph || !cv.enabled)
        return;

    QVector<double> keys, values;
    int fs = 250;
    std::size_t cycleSampleCount = 0;
    bool hasPrevious = false;
    // Прореженная трасса снимается под мьютексом обработчика одним чтением:
    // поток приёма не дописывает цикл, пока форма его копирует.
    m_processor.read([&](const dsp::StreamProcessor::View &view)
                     {
        if (static_cast<std::size_t>(index) >= view.buffer.channelCount())
            return;
        fs = view.sampleRateHz > 0 ? view.sampleRateHz : 250;
        // Прореживание: не более m_pointsPerSec точек на секунду данных.
        std::size_t stride = 1;
        if (m_pointsPerSec > 0 && fs > m_pointsPerSec)
            stride = static_cast<std::size_t>(fs / m_pointsPerSec);
        const std::vector<double> &src = view.buffer.channel(static_cast<std::size_t>(index)).current;
        const int points = static_cast<int>(src.size() / stride) + 1;
        keys.reserve(points);
        values.reserve(points);
        for (std::size_t i = 0; i < src.size(); i += stride)
        {
            keys.push_back(static_cast<double>(i) / fs);
            values.push_back(src[i] * m_unitScale);
        }
        cycleSampleCount = view.buffer.cycleSampleCount();
        hasPrevious = view.buffer.hasPreviousCycle(); });
    cv.currentGraph->setData(keys, values, true);

    // Ось амплитуд --- по способу, выбранному в меню графика.
    applyValueScale(cv);

    if (cv.cycleDivider && cv.axisRect)
    {
        const double x = static_cast<double>(cycleSampleCount) / fs;
        const QCPRange yr = cv.axisRect->axis(QCPAxis::atLeft)->range();
        cv.cycleDivider->start->setCoords(std::min(x, m_timeWindowSec), yr.lower);
        cv.cycleDivider->end->setCoords(std::min(x, m_timeWindowSec), yr.upper);
        cv.cycleDivider->setVisible(hasPrevious);
    }
}

void MonitoringPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateRefreshTimers(true);
}

void MonitoringPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    updateRefreshTimers(false);
}

void MonitoringPage::updateRefreshTimers(bool pageVisible)
{
    if (!m_redrawTimer || !m_fftTimer || !m_tabs)
        return;
    const bool graphs = pageVisible && m_tabs->currentWidget() == m_graphTab;
    const bool fft = pageVisible && m_tabs->currentWidget() == m_fftTab;
    if (graphs && !m_redrawTimer->isActive())
    {
        m_redrawTimer->start();
        onRedraw(); // накопленное за время паузы показывается сразу
    }
    else if (!graphs)
        m_redrawTimer->stop();
    if (fft && !m_fftTimer->isActive())
    {
        m_fftTimer->start();
        computeFft();
    }
    else if (!fft)
        m_fftTimer->stop();
}

void MonitoringPage::onRedraw()
{
    // Регистрация могла начаться до открытия страницы: графики строятся по
    // геометрии, которую обработчику передала страница из сеанса устройства.
    if (m_channels.isEmpty())
    {
        QVector<quint8> streamed;
        m_processor.read([&](const dsp::StreamProcessor::View &view)
                         {
            for (uint8_t channel : view.channels)
                streamed.push_back(channel); });
        if (streamed.isEmpty())
            return;
        rebuildChannelChecks(streamed);
        rebuildChannels(streamed);
    }
    for (int i = 0; i < m_channels.size(); ++i)
        redrawChannel(i);
    updateNoiseLabels();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void MonitoringPage::onTimeWindowChanged(double seconds)
{
    if (!(seconds > 0.0))
        return;
    // Длину цикла меняет обработчик потока командой в потоке приёма; цикл,
    // уже накопивший больше отсчётов, заканчивается на следующем блоке.
    m_timeWindowSec = seconds;
    m_processor.setCycleSeconds(seconds);
    resetPlotScale();
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
    }
    updateNoiseLabels();
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

void MonitoringPage::updateFilterRanges()
{
    const double maximum = std::max(0.2, currentSampleRate() / 2.0 - 0.1);
    for (int row = 0; m_filterTable && row < m_filterTable->rowCount(); ++row)
    {
        auto *freq = qobject_cast<QDoubleSpinBox *>(m_filterTable->cellWidget(row, 3));
        if (!freq)
            continue;
        freq->setMaximum(maximum); // большее значение поле приводит к границе
        freq->setToolTip(tr("0.1 to %1 Hz: below half the sample rate").arg(maximum, 0, 'f', 1));
    }
    // Принятая цепочка тоже приводится к новой границе, иначе она
    // действовала бы с частотой, которой поле уже не показывает.
    bool clamped = false;
    for (dsp::FilterSpec &stage : m_appliedFilters)
    {
        if (stage.frequency > maximum)
        {
            stage.frequency = maximum;
            clamped = true;
        }
    }
    if (clamped)
        m_processor.setFilters(m_appliedFilters);
}

std::vector<dsp::FilterSpec> MonitoringPage::filterChainFromTable() const
{
    // Порядок строк таблицы есть порядок применения, верхняя строка первая.
    std::vector<dsp::FilterSpec> stages;
    for (int row = 0; m_filterTable && row < m_filterTable->rowCount(); ++row)
    {
        auto *enable = qobject_cast<QCheckBox *>(m_filterTable->cellWidget(row, 0));
        auto *digitalOrder = qobject_cast<QSpinBox *>(m_filterTable->cellWidget(row, 2));
        auto *freq = qobject_cast<QDoubleSpinBox *>(m_filterTable->cellWidget(row, 3));
        auto *q = qobject_cast<QDoubleSpinBox *>(m_filterTable->cellWidget(row, 4));
        auto *item = m_filterTable->item(row, 1);
        if (!enable || !enable->isChecked() || !digitalOrder || !freq || !q || !item)
            continue;
        dsp::FilterSpec stage;
        stage.type = static_cast<dsp::Biquad::Type>(item->data(Qt::UserRole).toInt());
        stage.frequency = freq->value();
        stage.q = q->value();
        stage.digitalOrder = digitalOrder->value();
        stages.push_back(stage);
    }
    return stages;
}

void MonitoringPage::applyFilters()
{
    // Копия сигнала одна: новый фильтр действует на новые отсчёты, а уже
    // нарисованная часть развёртки остаётся такой, какой была.
    updateFilterRanges();
    m_appliedFilters = filterChainFromTable();
    m_processor.setFilterEnabled(!m_appliedFilters.empty());
    m_processor.setFilters(m_appliedFilters);
}

void MonitoringPage::clearFilters()
{
    for (int row = 0; m_filterTable && row < m_filterTable->rowCount(); ++row)
    {
        auto *enable = qobject_cast<QCheckBox *>(m_filterTable->cellWidget(row, 0));
        if (enable)
            enable->setChecked(false);
    }
    m_appliedFilters.clear();
    m_processor.setFilterEnabled(false);
    m_processor.setFilters({});
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

    const auto window = static_cast<dsp::Window>(m_fftWindowCombo->currentData().toInt());
    auto result = m_processor.spectrum(static_cast<std::size_t>(idx), window, 8);
    if (result.frequencies.empty())
        return;

    QVector<double> freqs(result.frequencies.begin(), result.frequencies.end());
    QVector<double> mags;
    mags.reserve(static_cast<int>(result.magnitude.size()));
    for (double m : result.magnitude)
        mags.push_back(m * m_unitScale);
    m_fftPlot->graph(0)->setData(freqs, mags, true);
    m_fftPlot->graph(0)->setPen(QPen(channelColor(m_channels[idx].physIndex,
                                                  m_context && m_context->themeManager()
                                                      ? m_context->themeManager()->isDark()
                                                      : true)));
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

void MonitoringPage::applyThemeColors()
{
    const bool dark = !m_context || !m_context->themeManager() || m_context->themeManager()->isDark();
    const QColor background = dark ? QColor(24, 26, 31) : QColor(255, 255, 255);
    const QColor foreground = dark ? QColor(210, 214, 222) : QColor(45, 48, 55);
    const QColor grid = dark ? QColor(65, 70, 80) : QColor(205, 210, 218);
    auto styleAxis = [&](QCPAxis *axis)
    {
        if (!axis)
            return;
        axis->setBasePen(QPen(foreground));
        axis->setTickPen(QPen(foreground));
        axis->setSubTickPen(QPen(foreground));
        axis->setTickLabelColor(foreground);
        axis->setLabelColor(foreground);
        axis->grid()->setPen(QPen(grid));
        axis->grid()->setSubGridPen(QPen(grid));
    };
    if (m_plot)
    {
        m_plot->setBackground(background);
        for (const ChannelView &cv : m_channels)
        {
            if (cv.axisRect)
            {
                cv.axisRect->setBackground(background);
                styleAxis(cv.axisRect->axis(QCPAxis::atLeft));
                styleAxis(cv.axisRect->axis(QCPAxis::atBottom));
            }
        }
    }
    if (m_fftPlot)
    {
        m_fftPlot->setBackground(background);
        styleAxis(m_fftPlot->xAxis);
        styleAxis(m_fftPlot->yAxis);
    }
    for (const ChannelView &cv : m_channels)
    {
        if (cv.currentGraph)
            cv.currentGraph->setPen(QPen(channelColor(cv.physIndex, dark)));
        if (cv.noiseLabel)
            cv.noiseLabel->setTextColor(dark ? QColor(160, 165, 175) : QColor(100, 105, 115));
    }
    if (m_fftPlot && m_fftPlot->graphCount() > 0)
    {
        const int idx = m_fftChannelCombo ? m_fftChannelCombo->currentData().toInt() : -1;
        if (idx >= 0 && idx < m_channels.size())
            m_fftPlot->graph(0)->setPen(QPen(channelColor(m_channels[idx].physIndex, dark)));
    }
    if (m_plot)
        m_plot->replot(QCustomPlot::rpQueuedReplot);
    if (m_fftPlot)
        m_fftPlot->replot(QCustomPlot::rpQueuedReplot);
}

