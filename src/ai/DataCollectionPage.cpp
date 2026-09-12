#include "ai/DataCollectionPage.h"
#include "ai/CueCaptureDialog.h"
#include "ai/qtdatasetcaptureadapter.h"
#include "device/acquisitionservice.h"
#include "device/qtdevicesessionadapter.h"
#include "navigation/AppContext.h"
#include "notifications/Notifications.h"
#include "ui_datacollectionpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <exception>
#include <random>

namespace
{
    bool isSupportedCaptureTask(const TaskDescriptor &descriptor)
    {
        return descriptor.taskType == TaskType::Classification &&
               descriptor.targetType == TargetType::Label &&
               descriptor.paradigm.kind == ParadigmSpec::Kind::CueSchedule;
    }
}

DataCollectionPage::DataCollectionPage(AppContext *c, QWidget *p)
    : AiPageBase(c, "Data Collection", p)
{
    setObjectName(QStringLiteral("data_collection"));
    if (c && c->notifications())
        c->notifications()->attachStatusLabel(objectName(), m_status);
    auto *root = qobject_cast<QVBoxLayout *>(layout());
    auto *content = new QWidget(this);
    Ui::DataCollectionPageForm form;
    form.setupUi(content);
    root->insertWidget(2, content, 1);

    m_path = form.datasetPath;
    m_taskCombo = form.taskCombo;
    m_taskId = form.taskId;
    m_taskName = form.taskName;
    m_prep = form.preparationSeconds;
    m_duration = form.recordingSeconds;
    m_sampleRateLabel = form.sampleRateLabel;
    m_windowSamples = form.windowSamples;
    m_strideSamples = form.strideSamples;
    m_units = form.unitsCombo;
    m_units->setEditable(true);
    m_scale = form.scaleSpin;
    m_channelWidget = form.channelWidget;
    m_labels = form.labelsTable;
    m_targetPlaceholder = form.targetPlaceholder;
    m_start = form.startButton;
    m_stop = form.stopButton;
    m_addLabelButton = form.addLabelButton;
    m_removeLabelButton = form.removeLabelButton;
    m_stop->setEnabled(false);
    m_form = form.settingsForm;

    connect(form.browseButton, &QPushButton::clicked, this, &DataCollectionPage::choosePath);
    /*TODO, индекс при запуске получается меняется? чтобы вызывался chooseTask, это необходимое условие */
    connect(m_taskCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &DataCollectionPage::chooseTask);
    connect(form.addLabelButton, &QPushButton::clicked, this, &DataCollectionPage::addLabel);
    connect(form.removeLabelButton, &QPushButton::clicked, this, &DataCollectionPage::removeLabel);
    connect(m_start, &QPushButton::clicked, this, &DataCollectionPage::start);
    connect(m_stop, &QPushButton::clicked, this, &DataCollectionPage::stop);
    loadTasks();
    if (m_taskCombo->count() == 0)
        addLabel();
    if (c)
        m_capture = std::make_unique<QtDatasetCaptureAdapter>(c->dataHub(), this);
    updateChannelWidgets();
    if (c)
    {
        connect(c, &AppContext::sessionChanged,
                this, &DataCollectionPage::onSessionChanged);
        if (c->acquisition())
            connect(c->acquisition(), &AcquisitionService::ownerChanged,
                    this, [this](AcquisitionOwner)
                    {
                        updateChannelWidgets();
                        updateCaptureRateUi(); });
        onSessionChanged(c->session());
    }
}

DataCollectionPage::~DataCollectionPage() = default;
void DataCollectionPage::loadTasks()
{
    m_taskCombo->clear();
    const QDir dir(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tasks")));
    for (const QString &file : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
    {
        TaskDescriptor d;
        std::string err;
        if (loadTaskDescriptor(dir.filePath(file).toStdString(), d, &err) &&
            isSupportedCaptureTask(d))
            m_taskCombo->addItem(QString::fromStdString(d.displayName), dir.filePath(file));
    }
    if (m_taskCombo->count() > 0)
        chooseTask(0);
}
void DataCollectionPage::chooseTask(int index)
{
    if (index < 0 || index >= m_taskCombo->count())
        return;
    TaskDescriptor d;
    std::string err;
    if (!loadTaskDescriptor(m_taskCombo->itemData(index).toString().toStdString(), d, &err))
    {
        setStatus(tr("Task error: %1").arg(QString::fromStdString(err)));
        m_descriptorLoaded = false;
        return;
    }
    if (!isSupportedCaptureTask(d))
    {
        setStatus(tr("Unsupported task: this page supports classification cue schedules only"));
        m_descriptorLoaded = false;
        updateTargetModeUi();
        return;
    }

    m_descriptor = d;
    m_descriptorLoaded = true;
    m_taskId->setText(QString::fromStdString(d.taskId));
    m_taskName->setText(QString::fromStdString(d.displayName));
    m_prep->setValue(d.paradigm.prepSeconds);
    m_duration->setValue(d.paradigm.recordSeconds);
    const int rate = m_context && m_context->session() ? m_context->session()->sampleRateHz() : 250;
    m_sampleRateLabel->setText(tr("%1 Hz").arg(rate));
    const int defaultWindow = std::max(1, rate * std::max(1, d.paradigm.recordSeconds));
    m_windowSamples->setValue(d.capture.windowSamples > 0 ? int(d.capture.windowSamples) : defaultWindow);
    m_strideSamples->setValue(d.capture.strideSamples > 0 ? int(d.capture.strideSamples) : m_windowSamples->value());
    const int unitIndex = m_units->findText(QString::fromStdString(d.capture.units));
    const int defaultUnitIndex = m_units->findText(QStringLiteral("uV"));
    m_units->setCurrentIndex(unitIndex >= 0 ? unitIndex : std::max(0, defaultUnitIndex));
    m_scale->setValue(unitIndex >= 0 && d.capture.scale > 0.0 ? d.capture.scale : 1e-6);
    m_labels->setRowCount(0);
    for (const auto &e : d.events)
    {
        if (e.target.type != TargetType::Label)
            continue;
        const int r = m_labels->rowCount();
        m_labels->insertRow(r);
        m_labels->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(e.target.label)));
        m_labels->setItem(r, 1, new QTableWidgetItem(QStringLiteral("1")));
    }
    updateTargetModeUi();
    updateChannelWidgets();
    if (m_labels->rowCount() == 0 && d.targetType == TargetType::Label)
        addLabel();
}
void DataCollectionPage::choosePath()
{
    auto d = QFileDialog::getExistingDirectory(this, tr("Select dataset folder"), m_path->text());
    if (!d.isEmpty())
        m_path->setText(d);
}
void DataCollectionPage::addLabel()
{
    int r = m_labels->rowCount();
    m_labels->insertRow(r);
    m_labels->setItem(r, 0, new QTableWidgetItem(QString("gesture_%1").arg(r + 1)));
    m_labels->setItem(r, 1, new QTableWidgetItem("1"));
}
void DataCollectionPage::removeLabel()
{
    if (m_labels->currentRow() >= 0)
        m_labels->removeRow(m_labels->currentRow());
}
void DataCollectionPage::refreshSpec() { chooseTask(m_taskCombo ? m_taskCombo->currentIndex() : -1); }

void DataCollectionPage::onSessionChanged(QtDeviceSessionAdapter *session)
{
    if (session)
    {
        connect(session, &QtDeviceSessionAdapter::connectionChanged,
                this, [this](bool)
                {
                    updateChannelWidgets();
                    updateCaptureRateUi(); });
        connect(session, &QtDeviceSessionAdapter::commandAck,
                this, [this](int, bool, int)
                { updateCaptureRateUi(); });
        updateCaptureRateUi();
    }
    else
    {
        m_sampleRateLabel->setText(tr("Not connected"));
    }
    updateChannelWidgets();
}

void DataCollectionPage::updateCaptureRateUi()
{
    const int rate = m_context && m_context->session() ? m_context->session()->sampleRateHz() : 0;
    m_sampleRateLabel->setText(rate > 0 ? tr("%1 Hz").arg(rate) : tr("Not connected"));
    const bool connected = m_context && m_context->session() && m_context->session()->isConnected();
    if (!connected && (!m_capture || !m_capture->running()))
        setStatus(tr("Connect a device to configure capture settings"));
    for (QWidget *widget : {static_cast<QWidget *>(m_prep), static_cast<QWidget *>(m_duration),
                            static_cast<QWidget *>(m_windowSamples), static_cast<QWidget *>(m_strideSamples),
                            static_cast<QWidget *>(m_units), static_cast<QWidget *>(m_scale),
                            static_cast<QWidget *>(m_channelWidget)})
        if (widget)
            widget->setEnabled(connected && (!m_capture || !m_capture->running()));
    if (m_start)
        m_start->setEnabled(connected && m_descriptorLoaded && isSupportedCaptureTask(m_descriptor) &&
                            (!m_capture || !m_capture->running()));
    if (rate <= 0 || !m_descriptorLoaded)
        return;
    if (m_descriptor.capture.windowSamples == 0)
    {
        const int window = std::max(1, rate * std::max(1, m_duration->value()));
        m_windowSamples->setValue(window);
        if (m_descriptor.capture.strideSamples == 0)
            m_strideSamples->setValue(window);
    }
    else if (m_descriptor.capture.strideSamples == 0)
    {
        m_strideSamples->setValue(m_windowSamples->value());
    }
}

QVector<quint8> DataCollectionPage::selectedChannels() const
{
    QVector<quint8> result;
    /* TODO: мы же берем каналы из devicesession (или acquisition) */
    for (int i = 0; i < m_channels.size(); ++i)
        if (m_channels[i]->isChecked())
            result.push_back(static_cast<quint8>(i));
    return result;
}

void DataCollectionPage::updateChannelWidgets()
{
    if (!m_channelWidget)
        return;
    auto *grid = qobject_cast<QGridLayout *>(m_channelWidget->layout());
    if (!grid)
    {
        grid = new QGridLayout(m_channelWidget);
        grid->setContentsMargins(0, 0, 0, 0);
    }
    while (QLayoutItem *item = grid->takeAt(0))
    {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_channels.clear();
    const int count = m_context && m_context->session() ? m_context->session()->channelCount() : 0;
    QVector<quint8> configured = m_context && m_context->acquisition()
                                     ? m_context->acquisition()->configuredChannels()
                                     : QVector<quint8>();
    for (int i = 0; i < count; ++i)
    {
        auto *check = new QCheckBox(QStringLiteral("CH%1").arg(i + 1), m_channelWidget);
        check->setChecked(configured.contains(static_cast<quint8>(i)));
        m_channels.push_back(check);
        grid->addWidget(check, i / 4, i % 4);
    }
}

void DataCollectionPage::updateTargetModeUi()
{
    const bool labels = m_descriptorLoaded && isSupportedCaptureTask(m_descriptor);
    m_labels->setEnabled(labels);
    if (m_addLabelButton)
        m_addLabelButton->setEnabled(labels);
    if (m_removeLabelButton)
        m_removeLabelButton->setEnabled(labels);
    m_targetPlaceholder->setVisible(!labels);
    m_targetPlaceholder->setText(labels ? QString() : tr("This capture workflow supports classification cue schedules with label targets only"));
    const bool connected = m_context && m_context->session() && m_context->session()->isConnected();
    m_start->setEnabled(connected && labels && (!m_capture || !m_capture->running()));
}

DatasetSpec DataCollectionPage::buildSpec(const QVector<quint8> &channels, int sampleRate) const
{
    DatasetSpec spec;
    spec.task = m_descriptor;
    spec.task.paradigm.prepSeconds = m_prep->value();
    spec.task.paradigm.recordSeconds = m_duration->value();
    spec.sampleRate = static_cast<uint32_t>(sampleRate);
    spec.channels.reserve(channels.size());
    for (quint8 channel : channels)
        spec.channels.push_back(channel);
    spec.windowSamples = static_cast<size_t>(m_windowSamples->value());
    spec.strideSamples = static_cast<size_t>(m_strideSamples->value());
    spec.units = m_units->currentText().toStdString();
    spec.scale = m_scale->value();
    spec.task.capture.channels = spec.channels;
    spec.task.capture.sampleRate = spec.sampleRate;
    spec.task.capture.windowSamples = spec.windowSamples;
    spec.task.capture.strideSamples = spec.strideSamples;
    spec.task.capture.units = spec.units;
    spec.task.capture.scale = spec.scale;
    return spec;
}

bool DataCollectionPage::validateCapture(const DatasetSpec &spec, QString *error) const
{
    const int actualRate = static_cast<int>(spec.sampleRate);
    if (m_descriptor.capture.sampleRate > 0 &&
        m_descriptor.capture.sampleRate != static_cast<uint32_t>(actualRate))
    {
        if (error)
            *error = tr("The descriptor requires %1 Hz, but the device is running at %2 Hz. Change it on the Devices page.")
                         .arg(m_descriptor.capture.sampleRate)
                         .arg(actualRate);
        return false;
    }
    if (!m_descriptor.capture.channels.empty())
    {
        std::vector<uint8_t> selected;
        for (quint8 channel : selectedChannels())
            selected.push_back(channel);
        if (selected != m_descriptor.capture.channels)
        {
            if (error)
                *error = tr("The descriptor requires a different channel set. Select the required channels on this page.");
            return false;
        }
    }
    if (spec.channels.empty())
    {
        if (error)
            *error = tr("Select at least one channel.");
        return false;
    }
    if (spec.windowSamples == 0 || spec.strideSamples == 0 || spec.strideSamples > spec.windowSamples)
    {
        if (error)
            *error = tr("Stride must be positive and no greater than window size.");
        return false;
    }
    const size_t maxWindow = static_cast<size_t>(std::max(1, m_duration->value())) * spec.sampleRate;
    if (spec.windowSamples > maxWindow)
    {
        if (error)
            *error = tr("Window size must not exceed the recording duration (%1 samples).").arg(maxWindow);
        return false;
    }
    if (!std::isfinite(spec.scale) || spec.scale <= 0.0)
    {
        if (error)
            *error = tr("Scale must be a positive finite number.");
        return false;
    }
    return true;
}

void DataCollectionPage::start()
{
    if (!m_context || !m_capture)
        return;
    auto *liveSession = m_context->session();
    if (!liveSession || !liveSession->isConnected())
    {
        setStatus(tr("Connect a device first"));
        return;
    }
    if (!m_descriptorLoaded)
    {
        setStatus(tr("Select a valid task descriptor"));
        return;
    }
    if (!isSupportedCaptureTask(m_descriptor))
    {
        setStatus(tr("This capture page supports classification cue schedules with label targets only"));
        return;
    }
    if (m_path->text().trimmed().isEmpty())
    {
        const QString message = tr("Choose a dataset folder first.");
        setStatus(message);
        m_context->notifications()->warning(this, tr("Capture settings"), message);
        return;
    }
    const QVector<quint8> selected = selectedChannels();
    DatasetSpec s = buildSpec(selected, liveSession->sampleRateHz());
    QString validationError;
    if (!validateCapture(s, &validationError))
    {
        setStatus(validationError);
        m_context->notifications()->warning(this, tr("Capture settings"), validationError);
        return;
    }
    /* TODO: подумай еще раз над необходимостью acquisition service, может лучше просто дать возможность запускать поток только Device Page? */
    if (m_context->acquisition() && m_context->acquisition()->hasOwner() &&
        !m_context->acquisition()->isOwner(AcquisitionOwner::DatasetCapture))
    {
        setStatus(tr("Stop the current stream on the Devices page, then start capture again"));
        return;
    }
    // Build a randomized schedule while avoiding consecutive repetitions whenever
    // another gesture still has samples to capture. If only one gesture remains,
    // consecutive entries are unavoidable and are emitted as-is.
    std::vector<std::pair<std::string, int>> remaining;
    size_t total = 0;
    for (int r = 0; r < m_labels->rowCount(); ++r)
        if (m_labels->item(r, 0) && !m_labels->item(r, 0)->text().isEmpty())
        {
            int reps = m_labels->item(r, 1) ? m_labels->item(r, 1)->text().toInt() : 1;
            int count = std::max(1, reps);
            remaining.emplace_back(m_labels->item(r, 0)->text().toStdString(), count);
            total += size_t(count);
        }
    std::vector<std::string> labels;
    labels.reserve(total);
    std::mt19937 rng(std::random_device{}());
    if (!m_descriptor.paradigm.randomize)
    {
        for (const auto &entry : remaining)
            for (int i = 0; i < entry.second; ++i)
                labels.push_back(entry.first);
    }
    std::string previous;
    while (m_descriptor.paradigm.randomize && labels.size() < total)
    {
        // Prefer the most frequent remaining gesture. This is the standard
        // reorganize-string strategy: it keeps the largest class distributed
        // across the schedule and avoids repeats whenever a valid arrangement
        // exists, while randomizing ties for a different order on each run.
        int bestCount = 0;
        for (const auto &entry : remaining)
            if (entry.second > bestCount && entry.first != previous)
                bestCount = entry.second;
        std::vector<size_t> candidates;
        for (size_t i = 0; i < remaining.size(); ++i)
            if (remaining[i].second > 0 && remaining[i].first != previous && remaining[i].second == bestCount)
                candidates.push_back(i);
        if (candidates.empty())
            for (size_t i = 0; i < remaining.size(); ++i)
                if (remaining[i].second > 0)
                    candidates.push_back(i);
        if (candidates.empty())
            break;
        std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
        auto &entry = remaining[candidates[pick(rng)]];
        labels.push_back(entry.first);
        previous = entry.first;
        --entry.second;
    }
    if (labels.empty())
    {
        setStatus(tr("Add at least one gesture label"));
        return;
    }
    // Persist the edited vocabulary in the descriptor. The randomized schedule
    // may contain repetitions, while events are kept once per label.
    s.task.events.clear();
    for (int row = 0; row < m_labels->rowCount(); ++row)
    {
        const auto *item = m_labels->item(row, 0);
        if (!item || item->text().trimmed().isEmpty())
            continue;
        const std::string label = item->text().trimmed().toStdString();
        const auto found = std::find_if(s.task.events.begin(), s.task.events.end(), [&](const EventSpec &event)
                                        { return event.target.type == TargetType::Label && event.target.label == label; });
        if (found != s.task.events.end())
            continue;
        EventSpec event;
        event.id = label;
        event.target.type = TargetType::Label;
        event.target.label = label;
        const auto descriptorEvent = std::find_if(m_descriptor.events.begin(), m_descriptor.events.end(),
                                                  [&](const EventSpec &candidate)
                                                  {
                                                      return candidate.target.type == TargetType::Label &&
                                                             candidate.target.label == label;
                                                  });
        event.cueText = descriptorEvent != m_descriptor.events.end() && !descriptorEvent->cueText.empty()
                            ? descriptorEvent->cueText
                            : label;
        s.task.events.push_back(std::move(event));
    }
    // The capture controller consumes the shared DataHub. Acquire the fixed
    // stream geometry before creating the dataset, so a failed request cannot
    // leave a partially initialized dataset behind.
    if (auto *session = m_context->session())
    {
        QVector<quint8> channels;
        for (auto c : s.channels)
            channels.push_back(c);
        // Firmware emits one complete sample set (all selected channels) per
        // Push; payload size is derived from this channel list.
        if (!m_context->acquisition() ||
            !m_context->acquisition()->request({AcquisitionOwner::DatasetCapture, channels}))
        {
            m_capture->stop();
            setStatus(tr("Unable to start device stream"));
            return;
        }
    }
    std::string datasetPath;
    try
    {
        datasetPath = nextDatasetBundlePath(m_path->text().trimmed().toStdString());
    }
    catch (const std::exception &e)
    {
        if (m_context->acquisition())
            m_context->acquisition()->release(AcquisitionOwner::DatasetCapture);
        const QString message = tr("Unable to create dataset folder: %1").arg(QString::fromUtf8(e.what()));
        setStatus(message);
        m_context->notifications()->warning(this, tr("Capture settings"), message);
        return;
    }
    if (!m_capture->start(datasetPath, s, labels, 1))
    {
        if (m_context->acquisition())
            m_context->acquisition()->release(AcquisitionOwner::DatasetCapture);
        setStatus(tr("Unable to open dataset"));
        return;
    }
    m_start->setEnabled(false);
    m_stop->setEnabled(true);
    setStatus(tr("Cycle started"));
    disconnect(m_capture.get(), nullptr, this, nullptr);
    connect(m_capture.get(), &QtDatasetCaptureAdapter::progress,
            this, &DataCollectionPage::onCaptureProgress);
    connect(m_capture.get(), &QtDatasetCaptureAdapter::completed,
            this, &DataCollectionPage::onCaptureCompleted);

    QVector<CueCaptureDialog::CueEntry> cueEntries;
    for (int row = 0; row < m_labels->rowCount(); ++row)
    {
        if (!m_labels->item(row, 0) || m_labels->item(row, 0)->text().trimmed().isEmpty())
            continue;
        CueCaptureDialog::CueEntry entry;
        entry.label = m_labels->item(row, 0)->text().trimmed();
        entry.cueText = entry.label;
        entry.totalRepetitions = std::max(1, m_labels->item(row, 1) ? m_labels->item(row, 1)->text().toInt() : 1);
        for (const auto &event : s.task.events)
            if (event.target.label == entry.label && !event.cueText.empty())
                entry.cueText = QString::fromStdString(event.cueText);
        cueEntries.push_back(std::move(entry));
    }
    QVector<QString> cycleLabels;
    for (const auto &label : labels)
        cycleLabels.push_back(QString::fromStdString(label));
    m_cueDialog = std::make_unique<CueCaptureDialog>(this);
    m_cueDialog->setSchedule(cueEntries, cycleLabels, s.task.paradigm.prepSeconds,
                             s.task.paradigm.recordSeconds);
    connect(m_cueDialog.get(), &CueCaptureDialog::stopRequested,
            this, &DataCollectionPage::stop);
    m_cueDialog->show();
    m_cueDialog->raise();
    m_cueDialog->activateWindow();
}
void DataCollectionPage::stop()
{
    if (m_capture)
        m_capture->stop();
}

void DataCollectionPage::onCaptureProgress(int labelIndex, int repetition,
                                           double seconds, const QString &label)
{
    setStatus(tr("Recording %1 (%2 s)").arg(label).arg(seconds, 0, 'f', 1));
    if (m_cueDialog)
        m_cueDialog->updateProgress(labelIndex, repetition, seconds, label);
}

void DataCollectionPage::onCaptureCompleted(bool stopped)
{
    if (m_context && m_context->acquisition())
        m_context->acquisition()->release(AcquisitionOwner::DatasetCapture);
    updateCaptureRateUi();
    m_stop->setEnabled(false);
    if (m_cueDialog)
    {
        m_cueDialog->finish(stopped);
        m_cueDialog.reset();
    }
    setStatus(stopped ? tr("Capture stopped") : tr("Capture complete"));
}
