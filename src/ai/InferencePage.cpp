#include "ai/InferencePage.h"
#include "ai/aimodel.h"
#include "ai/qtaiworkeradapter.h"
#include "device/acquisitionservice.h"
#include "device/qtdevicesessionadapter.h"
#include "navigation/AppContext.h"
#include "settings/SettingsService.h"
#include "notifications/Notifications.h"
#include "ui_inferencepage.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
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
#include <limits>

InferencePage::InferencePage(AppContext *c, QWidget *p) : AiPageBase(c, "Inference", p)
{
    setObjectName(QStringLiteral("inference"));
    if (c && c->notifications())
        c->notifications()->attachStatusLabel(objectName(), m_status);
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
                        setStatus(tr("Inference worker ready; collecting data")); });
        connect(c->aiWorker(), &QtAiWorkerAdapter::errorOccurred, this, [this](const QString &message)
                {
                    if (m_running)
                        setStatus(tr("Inference error: %1").arg(message)); });
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
                        setStatus(ok ? tr("Inference worker stopped")
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
                    setStatus(tr("Inference result received (%1 window%2)")
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
        setStatus(tr("Stop inference before changing the model"));
        return;
    }
    auto d = QFileDialog::getExistingDirectory(this, tr("Select model bundle"), m_model->text());
    if (d.isEmpty())
        return;
    ModelBundleInfo info;
    std::string err;
    if (!loadModelManifest(d.toStdString(), info, m_spec, &err))
    {
        m_context->notifications()->warning(this, tr("Model"), QString::fromStdString(err));
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
        m_context->settings()->rememberModel(d);
    m_inferenceStride->setEnabled(true);
    m_model->setReadOnly(true);
    m_descriptor->setText(tr("Task: %1  |  Family: %2  |  Window: %3 samples").arg(QString::fromStdString(info.taskId)).arg(QString::fromStdString(info.modelFamily)).arg(m_spec.windowSamples));
}
void InferencePage::start()
{
    if (m_running)
    {
        setStatus(tr("Inference is already running"));
        return;
    }
    if (!m_context || m_model->text().isEmpty() || !m_modelLoaded)
    {
        setStatus(tr("Select a model first"));
        return;
    }
    if (m_context->aiWorker()->isRunning())
    {
        setStatus(tr("AI worker is busy with another operation"));
        return;
    }
    ModelBundleInfo info;
    std::string err;
    if (!loadModelManifest(m_model->text().toStdString(), info, m_spec, &err))
    {
        setStatus(QString::fromStdString(err));
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
            setStatus(tr("Select channels and start streaming on the Devices page first"));
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
            setStatus(tr("Model is incompatible with device: %1").arg(QString::fromStdString(reason)));
            return;
        }
    }
    else if (session && session->isPlayback())
    {
        channels = session->playbackChannels();
    }
    if (channels.isEmpty())
    {
        setStatus(tr("No active channels are available"));
        return;
    }
    auto *acquisition = m_context->acquisition();
    if (!acquisition)
    {
        setStatus(tr("Acquisition service is unavailable"));
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
            setStatus(tr("The running stream uses a different channel selection"));
            return;
        }
        if (acquisition->isOwner(AcquisitionOwner::DatasetCapture))
        {
            setStatus(tr("Device is busy with data collection"));
            return;
        }
    }
    else if (!acquisition->request({AcquisitionOwner::Inference, channels}))
    {
        setStatus(tr("Unable to start device stream"));
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
        setStatus(tr("The model has an invalid inference window"));
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
        setStatus(tr("Inference setup error: %1").arg(QString::fromUtf8(e.what())));
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
    setStatus(tr("Inference started; waiting for a %1-sample window (stride %2)")
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
    setStatus(tr("Inference stopped"));
}
void InferencePage::onBlock(const QtDeviceSessionAdapter::SampleBlock &b)
{
    if (!m_running || b.samples.isEmpty() || m_spec.windowSamples == 0)
        return;
    if (b.samples.size() != m_spec.channels.size())
    {
        setStatus(tr("Inference error: received %1 channels, model expects %2")
                              .arg(b.samples.size())
                              .arg(m_spec.channels.size()));
        return;
    }
    const int sampleCount = b.samples[0].size();
    for (const auto &channel : b.samples)
    {
        if (channel.size() != sampleCount)
        {
            setStatus(tr("Inference error: inconsistent sample block"));
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
        setStatus(tr("Inference error: %1").arg(QString::fromUtf8(e.what())));
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
            setStatus(tr("Waiting for next inference window: %1 samples")
                                  .arg(m_windowAccumulator.skippedSamples()));
        else
            setStatus(tr("Collecting inference window: %1 / %2 samples")
                                  .arg(m_seen)
                                  .arg(m_spec.windowSamples));
    }
    else if (m_running && !windows.empty())
        setStatus(tr("Inference windows queued: %1; results received: %2")
                              .arg(m_windowsSent)
                              .arg(m_resultsReceived));
}

void InferencePage::onStrideChanged(int samples)
{
    if (m_running)
    {
        setStatus(tr("Stop inference before changing the stride"));
        return;
    }
    if (samples <= 0 || m_spec.windowSamples == 0 || !m_modelLoaded)
        return;
    if (m_windowAccumulator.channels() == 0)
    {
        setStatus(tr("Inference stride will be used when inference starts"));
        return;
    }
    try
    {
        m_windowAccumulator.configure(m_windowAccumulator.channels(), m_spec.windowSamples,
                                      static_cast<std::size_t>(samples));
        m_seen = 0;
        setStatus(tr("Inference stride changed to %1 samples; buffer reset").arg(samples));
    }
    catch (const std::exception &e)
    {
        setStatus(tr("Inference stride error: %1").arg(QString::fromUtf8(e.what())));
    }
}
