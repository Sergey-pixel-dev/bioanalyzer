#include "pages/AiPages.h"
#include "pages/CueCaptureDialog.h"
#include "app/AppContext.h"
#include "app/acquisitionservice.h"
#include "adapters/qtaiworkeradapter.h"
#include "adapters/qtdevicesessionadapter.h"
#include "core/aimodel.h"
#include "ui_aipages.h"
#include "ui_datacollectionpage.h"
#include "ui_inferencepage.h"
#include "ui_trainingpage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QProgressBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QCheckBox>
#include <QGridLayout>
#include <QScrollArea>
#include <QTableWidgetItem>
#include <QAbstractItemView>
#include <QSignalBlocker>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <random>
#include <limits>
#include <cmath>
#include <exception>

namespace
{
    bool isSupportedCaptureTask(const TaskDescriptor &descriptor)
    {
        return descriptor.taskType == TaskType::Classification &&
               descriptor.targetType == TargetType::Label &&
               descriptor.paradigm.kind == ParadigmSpec::Kind::CueSchedule;
    }
}

AiPageBase::AiPageBase(AppContext *c, const QString &t, QWidget *p) : QWidget(p), m_context(c)
{
    Ui::AiPagesForm form;
    form.setupUi(this);
    auto *l = qobject_cast<QVBoxLayout *>(layout());
    if (!l)
        l = new QVBoxLayout(this);
    l->insertWidget(0, new QLabel(tr("AI / %1").arg(t), this));
    m_status = new QLabel(tr("Ready"), this);
    l->insertWidget(1, m_status);
}

DataCollectionPage::DataCollectionPage(AppContext *c, QWidget *p)
    : AiPageBase(c, "Data Collection", p)
{
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
        m_status->setText(tr("Task error: %1").arg(QString::fromStdString(err)));
        m_descriptorLoaded = false;
        return;
    }
    if (!isSupportedCaptureTask(d))
    {
        m_status->setText(tr("Unsupported task: this page supports classification cue schedules only"));
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
        m_status->setText(tr("Connect a device to configure capture settings"));
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
        m_status->setText(tr("Connect a device first"));
        return;
    }
    if (!m_descriptorLoaded)
    {
        m_status->setText(tr("Select a valid task descriptor"));
        return;
    }
    if (!isSupportedCaptureTask(m_descriptor))
    {
        m_status->setText(tr("This capture page supports classification cue schedules with label targets only"));
        return;
    }
    if (m_path->text().trimmed().isEmpty())
    {
        const QString message = tr("Choose a dataset folder first.");
        m_status->setText(message);
        QMessageBox::warning(this, tr("Capture settings"), message);
        return;
    }
    const QVector<quint8> selected = selectedChannels();
    DatasetSpec s = buildSpec(selected, liveSession->sampleRateHz());
    QString validationError;
    if (!validateCapture(s, &validationError))
    {
        m_status->setText(validationError);
        QMessageBox::warning(this, tr("Capture settings"), validationError);
        return;
    }
    /* TODO: подумай еще раз над необходимостью acquisition service, может лучше просто дать возможность запускать поток только Device Page? */
    if (m_context->acquisition() && m_context->acquisition()->hasOwner() &&
        !m_context->acquisition()->isOwner(AcquisitionOwner::DatasetCapture))
    {
        m_status->setText(tr("Stop the current stream on the Devices page, then start capture again"));
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
        m_status->setText(tr("Add at least one gesture label"));
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
            m_status->setText(tr("Unable to start device stream"));
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
        m_status->setText(message);
        QMessageBox::warning(this, tr("Capture settings"), message);
        return;
    }
    if (!m_capture->start(datasetPath, s, labels, 1))
    {
        if (m_context->acquisition())
            m_context->acquisition()->release(AcquisitionOwner::DatasetCapture);
        m_status->setText(tr("Unable to open dataset"));
        return;
    }
    m_start->setEnabled(false);
    m_stop->setEnabled(true);
    m_status->setText(tr("Cycle started"));
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
    m_status->setText(tr("Recording %1 (%2 s)").arg(label).arg(seconds, 0, 'f', 1));
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
    m_status->setText(stopped ? tr("Capture stopped") : tr("Capture complete"));
}

TrainingPage::TrainingPage(AppContext *c, QWidget *p) : AiPageBase(c, "Training", p)
{
    auto *l = qobject_cast<QVBoxLayout *>(layout());
    auto *content = new QWidget(this);
    Ui::TrainingPageForm form;
    form.setupUi(content);
    m_dataset = form.datasetPath;
    m_datasetBrowse = form.datasetBrowse;
    m_familyCombo = form.familyCombo;
    m_output = form.outputPath;
    m_outputBrowse = form.outputBrowse;
    m_hyperparameters = form.hyperparameters;
    m_hyperparameterForm = form.hyperparameterForm;
    m_progress = form.progress;
    m_metrics = form.metrics;
    m_train = form.startButton;
    m_trainingStop = form.stopButton;
    m_output->setText(QStringLiteral("models"));
    loadModelFamilies();
    m_metrics->horizontalHeader()->setStretchLastSection(true);
    m_metrics->setEditTriggers(QAbstractItemView::NoEditTriggers);
    l->insertWidget(2, content, 1);
    connect(m_datasetBrowse, &QPushButton::clicked, this, &TrainingPage::chooseDataset);
    connect(m_outputBrowse, &QPushButton::clicked, this, &TrainingPage::chooseOutput);
    connect(m_familyCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int)
            {if(m_context) m_context->aiWorker()->describeFamily(m_familyCombo->currentText()); });
    connect(m_train, &QPushButton::clicked, this, &TrainingPage::train);
    connect(m_trainingStop, &QPushButton::clicked, this, &TrainingPage::stop);
    setTrainingControlsEnabled(false);
    if (c)
    {
        connect(c->aiWorker(), &QtAiWorkerAdapter::metric, this, &TrainingPage::onMetric);
        connect(c->aiWorker(), &QtAiWorkerAdapter::familyMeta, this, &TrainingPage::onFamilyMeta);
        connect(c->aiWorker(), &QtAiWorkerAdapter::modelReady, this, [this](const QString &p)
                {
                    if (!m_training) return;
                    const QString w = QFileInfo(QDir(p).filePath(QStringLiteral("weights.pt"))).exists()
                                          ? QStringLiteral("weights.pt") : QStringLiteral("weights.json");
                    writeModelManifest(p.toStdString(), m_spec, m_dataset->text().toStdString(), w.toStdString());
                    m_training = false;
                    setTrainingControlsEnabled(m_datasetValid);
                    m_trainingStop->setEnabled(false);
                    m_status->setText(tr("Model saved: %1").arg(p)); });
        connect(c->aiWorker(), &QtAiWorkerAdapter::errorOccurred, this, [this](const QString &e)
                { if (m_training) { m_training = false; setTrainingControlsEnabled(m_datasetValid); m_status->setText(tr("Error: %1").arg(e)); } });
        connect(c->aiWorker(), &QtAiWorkerAdapter::finished, this, [this](bool)
                { if (m_training) { m_training = false; setTrainingControlsEnabled(m_datasetValid); m_status->setText(tr("Training finished")); } });
    }
}

void TrainingPage::setTrainingControlsEnabled(bool enabled)
{
    if (m_datasetBrowse)
        m_datasetBrowse->setEnabled(!m_training);
    if (m_familyCombo)
        m_familyCombo->setEnabled(enabled);
    if (m_hyperparameters)
        m_hyperparameters->setEnabled(enabled);
    if (m_output)
        m_output->setEnabled(enabled);
    if (m_outputBrowse)
        m_outputBrowse->setEnabled(enabled);
    if (m_train)
        m_train->setEnabled(enabled && m_datasetValid && !m_training);
    if (m_trainingStop)
        m_trainingStop->setEnabled(m_training);
}

void TrainingPage::loadModelFamilies()
{
    if (!m_familyCombo)
        return;

    m_familyCombo->clear();
    const QDir modelsDir(QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("ai_worker/models")));
    const QStringList files = modelsDir.entryList({QStringLiteral("*.py")},
                                                  QDir::Files, QDir::Name);
    for (const QString &file : files)
    {
        const QString family = QFileInfo(file).completeBaseName();
        if (family.isEmpty() || family == QStringLiteral("__init__") ||
            family.startsWith(QLatin1Char('_')) ||
            m_familyCombo->findText(family) >= 0)
            continue;
        m_familyCombo->addItem(family);
    }

    // Keep the built-in reference family available even if the runtime model
    // directory is temporarily unavailable (for example before deployment).
    if (m_familyCombo->findText(QStringLiteral("custom")) < 0)
        m_familyCombo->addItem(QStringLiteral("custom"));

    const int customIndex = m_familyCombo->findText(QStringLiteral("custom"));
    m_familyCombo->setCurrentIndex(customIndex >= 0 ? customIndex : 0);
}
void TrainingPage::chooseDataset()
{
    if (m_training)
        return;
    auto d = QFileDialog::getExistingDirectory(this, tr("Select dataset"), m_dataset->text());
    if (d.isEmpty())
        return;
    std::string err;
    auto dataset = Dataset::open(d.toStdString(), &err);
    if (!dataset)
    {
        QMessageBox::warning(this, tr("Dataset"), QString::fromStdString(err));
        return;
    }
    m_spec = dataset->spec();
    m_dataset->setText(d);
    m_datasetValid = true;
    setTrainingControlsEnabled(true);
    const QString family = m_familyCombo->currentText();
    if (m_context)
    {
        m_context->rememberDataset(d);
        m_context->aiWorker()->describeFamily(family);
    }
    m_status->setText(tr("Loaded: %1 (%2 channels, %3 samples)").arg(QString::fromStdString(m_spec.task.displayName)).arg(m_spec.channels.size()).arg(m_spec.windowSamples));
}
void TrainingPage::chooseOutput()
{
    auto d = QFileDialog::getExistingDirectory(this, tr("Select output folder"), m_output->text());
    if (!d.isEmpty())
        m_output->setText(d);
}
void TrainingPage::rebuildHyperparameters(const QJsonObject &meta)
{
    while (m_hyperparameterForm->count())
    {
        auto *item = m_hyperparameterForm->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_hyperparameterWidgets.clear();
    const QJsonArray params = meta.value(QStringLiteral("hyperparameters")).toArray();
    for (const QJsonValue &value : params)
    {
        const QJsonObject p = value.toObject();
        const QString name = p.value(QStringLiteral("name")).toString();
        const QString type = p.value(QStringLiteral("type")).toString();
        if (name.isEmpty())
            continue;
        QWidget *w = nullptr;
        if (type == QStringLiteral("int"))
        {
            auto *s = new QSpinBox(m_hyperparameters);
            s->setRange(p.value("min").toInt(std::numeric_limits<int>::min()), p.value("max").toInt(std::numeric_limits<int>::max()));
            s->setValue(p.value("default").toInt());
            w = s;
        }
        else if (type == QStringLiteral("float"))
        {
            auto *s = new QDoubleSpinBox(m_hyperparameters);
            s->setDecimals(8);
            s->setRange(p.value("min").toDouble(-1e9), p.value("max").toDouble(1e9));
            s->setValue(p.value("default").toDouble());
            w = s;
        }
        else if (type == QStringLiteral("enum"))
        {
            auto *s = new QComboBox(m_hyperparameters);
            for (const auto &c : p.value("choices").toArray())
                s->addItem(c.toString(), c.toVariant());
            const QString def = p.value("default").toString();
            if (!def.isEmpty())
                s->setCurrentText(def);
            w = s;
        }
        else if (type == QStringLiteral("bool"))
        {
            auto *s = new QCheckBox(m_hyperparameters);
            s->setChecked(p.value("default").toBool());
            w = s;
        }
        else
        {
            auto *s = new QLineEdit(m_hyperparameters);
            s->setText(p.value("default").toString());
            w = s;
        }
        if (w)
        {
            if (name.compare(QStringLiteral("seed"), Qt::CaseInsensitive) == 0)
                w->setToolTip(tr("Seed fixes random generators used by training, making splits and initialization reproducible."));
            m_hyperparameterWidgets.insert(name, w);
            m_hyperparameterForm->addRow(name + QStringLiteral(":"), w);
        }
    }
}
QJsonObject TrainingPage::trainingConfig() const
{
    QJsonObject config;
    for (auto it = m_hyperparameterWidgets.cbegin(); it != m_hyperparameterWidgets.cend(); ++it)
    {
        if (auto *s = qobject_cast<QSpinBox *>(it.value()))
            config[it.key()] = s->value();
        else if (auto *d = qobject_cast<QDoubleSpinBox *>(it.value()))
            config[it.key()] = d->value();
        else if (auto *c = qobject_cast<QComboBox *>(it.value()))
            config[it.key()] = c->currentData().isValid() ? QJsonValue::fromVariant(c->currentData()) : QJsonValue(c->currentText());
        else if (auto *b = qobject_cast<QCheckBox *>(it.value()))
            config[it.key()] = b->isChecked();
        else if (auto *e = qobject_cast<QLineEdit *>(it.value()))
            config[it.key()] = e->text();
    }
    return config;
}
void TrainingPage::onFamilyMeta(const QJsonObject &meta)
{
    if (!m_datasetValid)
        return;
    m_familyMeta = meta;
    rebuildHyperparameters(meta);
    m_status->setText(tr("Family ready: %1").arg(meta.value(QStringLiteral("name")).toString()));
}
void TrainingPage::train()
{
    if (!m_context || m_dataset->text().isEmpty() || !m_datasetValid)
    {
        m_status->setText(tr("Select a dataset first"));
        return;
    }
    if (m_spec.windowSamples == 0)
    {
        std::string err;
        auto dataset = Dataset::open(m_dataset->text().toStdString(), &err);
        if (!dataset)
        {
            m_status->setText(QString::fromStdString(err));
            return;
        }
        m_spec = dataset->spec();
    }
    const QString taskType = QString::fromStdString(toString(m_spec.task.taskType)), targetType = QString::fromStdString(toString(m_spec.task.targetType));
    if (!m_familyMeta.isEmpty() && (m_familyMeta.value(QStringLiteral("task_type")).toString() != taskType || m_familyMeta.value(QStringLiteral("target_type")).toString() != targetType))
    {
        m_status->setText(tr("Model family is incompatible with this dataset"));
        return;
    }
    // The selected family is a property of the model bundle, not of the
    // dataset. Store it before the worker reports modelReady so the manifest
    // points to the loader that produced the weights.
    m_spec.modelFamily = m_familyCombo->currentText().toStdString();
    auto out = QString::fromStdString(nextModelBundlePath(m_output->text().toStdString()));
    m_metrics->setRowCount(0);
    m_progress->setValue(0);
    m_training = true;
    setTrainingControlsEnabled(false);
    m_spec.extra["output_kind"] = m_familyMeta.value(QStringLiteral("output")).toObject().value(QStringLiteral("kind")).toString().toStdString();
    m_context->aiWorker()->startTrainingConfig(m_dataset->text(), out, m_familyCombo->currentText(), trainingConfig());
    m_status->setText(tr("Training started"));
}
void TrainingPage::stop()
{
    if (m_context)
        m_context->aiWorker()->stop();
    m_training = false;
    setTrainingControlsEnabled(m_datasetValid);
    m_status->setText(tr("Training stopped"));
}
void TrainingPage::onMetric(int epoch, int epochs, double tl, double vl, double acc)
{
    if (!m_training)
        return;
    m_progress->setValue(int(100.0 * epoch / std::max(1, epochs)));
    int r = m_metrics->rowCount();
    m_metrics->insertRow(r);
    for (int c = 0; c < 5; ++c)
        m_metrics->setItem(r, c, new QTableWidgetItem(c == 0 ? QString::number(epoch) : c == 1 ? QString::number(tl, 'f', 5)
                                                                                    : c == 2   ? QString::number(vl, 'f', 5)
                                                                                    : c == 3   ? QString::number(acc, 'f', 3)
                                                                                               : QString::number(100.0 * epoch / std::max(1, epochs), 'f', 1) + "%"));
}

InferencePage::InferencePage(AppContext *c, QWidget *p) : AiPageBase(c, "Inference", p)
{
    auto *l = qobject_cast<QVBoxLayout *>(layout());
    auto *content = new QWidget(this);
    Ui::InferencePageForm form;
    form.setupUi(content);
    m_model = form.modelPath;
    m_modelBrowse = form.browseButton;
    m_descriptor = form.descriptorLabel;
    m_inferenceStride = form.inferenceStride;
    m_top5 = form.resultsTable;
    m_start = form.startButton;
    m_stop = form.stopButton;
    m_top5->setHorizontalHeaderLabels({tr("Label"), tr("Probability")});
    m_top5->horizontalHeader()->setStretchLastSection(true);
    m_top5->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_model->setReadOnly(true);
    m_inferenceStride->setEnabled(false);
    m_stop->setEnabled(false);
    l->insertWidget(2, content, 1);
    connect(form.browseButton, &QPushButton::clicked, this, &InferencePage::chooseModel);
    connect(m_start, &QPushButton::clicked, this, &InferencePage::start);
    connect(m_stop, &QPushButton::clicked, this, &InferencePage::stop);
    connect(m_inferenceStride, qOverload<int>(&QSpinBox::valueChanged),
            this, &InferencePage::onStrideChanged);
    if (c)
    {
        // The worker is shared by AI pages. Keep one set of connections and
        // only update this page while an inference session is active.
        connect(c->aiWorker(), &QtAiWorkerAdapter::started, this, [this]
                {
                    if (m_running)
                        m_status->setText(tr("Inference worker ready; collecting data")); });
        connect(c->aiWorker(), &QtAiWorkerAdapter::errorOccurred, this, [this](const QString &message)
                {
                    if (m_running)
                        m_status->setText(tr("Inference error: %1").arg(message)); });
        connect(c->aiWorker(), &QtAiWorkerAdapter::finished, this, [this](bool ok)
                {
                    if (m_running)
                    {
                        m_running = false;
                        if (m_acquisitionRequested && m_context && m_context->acquisition())
                            m_context->acquisition()->release(AcquisitionOwner::Inference);
                        m_acquisitionRequested = false;
                        m_windowAccumulator.reset();
                        m_start->setEnabled(true);
                        m_stop->setEnabled(false);
                        m_modelBrowse->setEnabled(true);
                        m_inferenceStride->setEnabled(m_modelLoaded);
                        m_status->setText(ok ? tr("Inference worker stopped")
                                             : tr("Inference worker exited with an error"));
                    } });
        connect(c->aiWorker(), &QtAiWorkerAdapter::inferenceOutput, this, [this](const QJsonObject &out)
                {
                    if (!m_running)
                        return;
                    ++m_resultsReceived;
                    const QString kind = out.value("kind").toString();
                    m_top5->setRowCount(0);
                    if (kind == QStringLiteral("top_k"))
                    {
                        for (const auto &v : out.value("items").toArray())
                        {
                            const auto o = v.toObject();
                            const int row = m_top5->rowCount();
                            m_top5->insertRow(row);
                            m_top5->setItem(row, 0, new QTableWidgetItem(o.value("label").toString()));
                            m_top5->setItem(row, 1, new QTableWidgetItem(
                                                          QString::number(o.value("probability").toDouble(), 'f', 4)));
                        }
                    }
                    else if (kind == QStringLiteral("value"))
                    {
                        m_top5->insertRow(0);
                        m_top5->setItem(0, 0, new QTableWidgetItem(tr("Value")));
                        m_top5->setItem(0, 1, new QTableWidgetItem(
                                                      QString::number(out.value("value").toDouble(), 'f', 4)));
                    }
                    else if (kind == QStringLiteral("spans"))
                    {
                        for (const auto &v : out.value("spans").toArray())
                        {
                            const auto a = v.toArray();
                            const int row = m_top5->rowCount();
                            m_top5->insertRow(row);
                            m_top5->setItem(row, 0, new QTableWidgetItem(tr("Span")));
                            m_top5->setItem(row, 1, new QTableWidgetItem(
                                                          a.size() >= 2 ? QStringLiteral("%1-%2").arg(a[0].toInteger()).arg(a[1].toInteger())
                                                                        : QString()));
                        }
                    }
                    m_status->setText(tr("Inference result received (%1 window%2)")
                                          .arg(m_windowsSent)
                                          .arg(m_windowsSent == 1 ? QString() : QStringLiteral("s"))); });
        connect(c->acquisition(), &AcquisitionService::samplesReady,
                this, &InferencePage::onBlock, Qt::UniqueConnection);
    }
}
void InferencePage::chooseModel()
{
    if (m_running)
    {
        m_status->setText(tr("Stop inference before changing the model"));
        return;
    }
    auto d = QFileDialog::getExistingDirectory(this, tr("Select model bundle"), m_model->text());
    if (d.isEmpty())
        return;
    ModelBundleInfo info;
    std::string err;
    if (!loadModelManifest(d.toStdString(), info, m_spec, &err))
    {
        QMessageBox::warning(this, tr("Model"), QString::fromStdString(err));
        return;
    }
    m_model->setText(d);
    m_modelLoaded = true;
    if (m_inferenceStride && m_spec.windowSamples > 0)
    {
        const QSignalBlocker blocker(m_inferenceStride);
        m_inferenceStride->setValue(static_cast<int>(std::min<std::size_t>(m_spec.windowSamples, 100000000)));
    }
    if (m_context)
        m_context->rememberModel(d);
    m_inferenceStride->setEnabled(true);
    m_model->setReadOnly(true);
    m_descriptor->setText(tr("Task: %1  |  Family: %2  |  Window: %3 samples").arg(QString::fromStdString(info.taskId)).arg(QString::fromStdString(info.modelFamily)).arg(m_spec.windowSamples));
}
void InferencePage::start()
{
    if (m_running)
    {
        m_status->setText(tr("Inference is already running"));
        return;
    }
    if (!m_context || m_model->text().isEmpty() || !m_modelLoaded)
    {
        m_status->setText(tr("Select a model first"));
        return;
    }
    if (m_context->aiWorker()->isRunning())
    {
        m_status->setText(tr("AI worker is busy with another operation"));
        return;
    }
    ModelBundleInfo info;
    std::string err;
    if (!loadModelManifest(m_model->text().toStdString(), info, m_spec, &err))
    {
        m_status->setText(QString::fromStdString(err));
        return;
    }
    QVector<quint8> channels;
    auto *session = m_context->session();
    if (session && !session->isPlayback())
    {
        const auto active = m_context->acquisition()
                                ? m_context->acquisition()->activeChannels()
                                : QVector<quint8>();
        channels = !active.isEmpty()
                       ? active
                       : (m_context->acquisition()
                              ? m_context->acquisition()->configuredChannels()
                              : QVector<quint8>());
        if (channels.isEmpty())
        {
            m_status->setText(tr("Select channels and start streaming on the Devices page first"));
            return;
        }
        DatasetSpec live = m_spec;
        live.sampleRate = static_cast<uint32_t>(session->sampleRateHz());
        live.channels.clear();
        for (auto c : channels)
            live.channels.push_back(c);
        std::string reason;
        if (!modelCompatible(m_spec, live, &reason))
        {
            m_status->setText(tr("Model is incompatible with device: %1").arg(QString::fromStdString(reason)));
            return;
        }
    }
    else if (session && session->isPlayback())
    {
        channels = session->playbackChannels();
    }
    if (channels.isEmpty())
    {
        m_status->setText(tr("No active channels are available"));
        return;
    }
    auto *acquisition = m_context->acquisition();
    if (!acquisition)
    {
        m_status->setText(tr("Acquisition service is unavailable"));
        return;
    }
    m_acquisitionRequested = false;
    if (acquisition->hasOwner())
    {
        // A stream has immutable physical channels, but its samples can be
        // consumed by several pages. Reuse an existing monitoring/playback
        // stream instead of attempting to start a second one.
        if (acquisition->activeChannels() != channels)
        {
            m_status->setText(tr("The running stream uses a different channel selection"));
            return;
        }
        if (acquisition->isOwner(AcquisitionOwner::DatasetCapture))
        {
            m_status->setText(tr("Device is busy with data collection"));
            return;
        }
    }
    else if (!acquisition->request({AcquisitionOwner::Inference, channels}))
    {
        m_status->setText(tr("Unable to start device stream"));
        return;
    }
    else
    {
        m_acquisitionRequested = true;
    }
    const std::size_t stride = static_cast<std::size_t>(m_inferenceStride->value());
    if (stride == 0 || m_spec.windowSamples == 0)
    {
        if (m_acquisitionRequested && m_context->acquisition())
            m_context->acquisition()->release(AcquisitionOwner::Inference);
        m_acquisitionRequested = false;
        m_status->setText(tr("The model has an invalid inference window"));
        return;
    }
    try
    {
        m_windowAccumulator.configure(static_cast<std::size_t>(channels.size()),
                                      m_spec.windowSamples, stride);
    }
    catch (const std::exception &e)
    {
        if (m_acquisitionRequested && m_context->acquisition())
            m_context->acquisition()->release(AcquisitionOwner::Inference);
        m_acquisitionRequested = false;
        m_status->setText(tr("Inference setup error: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    m_seen = 0;
    m_windowsSent = 0;
    m_resultsReceived = 0;
    m_running = true;
    m_start->setEnabled(false);
    m_stop->setEnabled(true);
    m_modelBrowse->setEnabled(false);
    m_inferenceStride->setEnabled(false);
    m_context->aiWorker()->startInference(m_model->text(), QString::fromStdString(info.modelFamily));
    m_status->setText(tr("Inference started; waiting for a %1-sample window (stride %2)")
                          .arg(m_spec.windowSamples)
                          .arg(stride));
}
void InferencePage::stop()
{
    m_running = false;
    if (m_context)
    {
        if (m_acquisitionRequested && m_context->acquisition())
            m_context->acquisition()->release(AcquisitionOwner::Inference);
        m_context->aiWorker()->stop();
    }
    m_acquisitionRequested = false;
    m_windowAccumulator.reset();
    m_start->setEnabled(true);
    m_stop->setEnabled(false);
    m_modelBrowse->setEnabled(true);
    m_inferenceStride->setEnabled(m_modelLoaded);
    m_status->setText(tr("Inference stopped"));
}
void InferencePage::onBlock(const QtDeviceSessionAdapter::SampleBlock &b)
{
    if (!m_running || b.samples.isEmpty() || m_spec.windowSamples == 0)
        return;
    if (b.samples.size() != m_spec.channels.size())
    {
        m_status->setText(tr("Inference error: received %1 channels, model expects %2")
                              .arg(b.samples.size())
                              .arg(m_spec.channels.size()));
        return;
    }
    const int sampleCount = b.samples[0].size();
    for (const auto &channel : b.samples)
    {
        if (channel.size() != sampleCount)
        {
            m_status->setText(tr("Inference error: inconsistent sample block"));
            return;
        }
    }
    std::vector<float> sampleMajor;
    sampleMajor.reserve(static_cast<std::size_t>(sampleCount) * static_cast<std::size_t>(b.samples.size()));
    for (int i = 0; i < sampleCount; ++i)
        for (int c = 0; c < b.samples.size(); ++c)
            sampleMajor.push_back(static_cast<float>(b.samples[c][i]));

    std::vector<SlidingWindowAccumulator::Window> windows;
    try
    {
        windows = m_windowAccumulator.append(sampleMajor, static_cast<std::size_t>(sampleCount));
    }
    catch (const std::exception &e)
    {
        m_status->setText(tr("Inference error: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    const int rows = static_cast<int>(m_spec.windowSamples);
    const int channels = b.samples.size();
    for (const auto &window : windows)
    {
        QVector<float> flat;
        flat.reserve(static_cast<int>(window.size()));
        for (float value : window)
            flat.push_back(value);
        QMetaObject::invokeMethod(this, [this, flat, rows, channels]
                                  {
                                      if (m_context && m_running)
                                          m_context->aiWorker()->sendWindow(flat, rows, channels); }, Qt::QueuedConnection);
        ++m_windowsSent;
    }
    m_seen = m_windowAccumulator.bufferedSamples();
    if (m_running && windows.empty())
    {
        if (m_windowAccumulator.skippedSamples() > 0)
            m_status->setText(tr("Waiting for next inference window: %1 samples")
                                  .arg(m_windowAccumulator.skippedSamples()));
        else
            m_status->setText(tr("Collecting inference window: %1 / %2 samples")
                                  .arg(m_seen)
                                  .arg(m_spec.windowSamples));
    }
    else if (m_running && !windows.empty())
        m_status->setText(tr("Inference windows queued: %1; results received: %2")
                              .arg(m_windowsSent)
                              .arg(m_resultsReceived));
}

void InferencePage::onStrideChanged(int samples)
{
    if (m_running)
    {
        m_status->setText(tr("Stop inference before changing the stride"));
        return;
    }
    if (samples <= 0 || m_spec.windowSamples == 0 || !m_modelLoaded)
        return;
    if (m_windowAccumulator.channels() == 0)
    {
        m_status->setText(tr("Inference stride will be used when inference starts"));
        return;
    }
    try
    {
        m_windowAccumulator.configure(m_windowAccumulator.channels(), m_spec.windowSamples,
                                      static_cast<std::size_t>(samples));
        m_seen = 0;
        m_status->setText(tr("Inference stride changed to %1 samples; buffer reset").arg(samples));
    }
    catch (const std::exception &e)
    {
        m_status->setText(tr("Inference stride error: %1").arg(QString::fromUtf8(e.what())));
    }
}
