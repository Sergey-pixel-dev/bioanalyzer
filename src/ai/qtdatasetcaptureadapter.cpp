#include "ai/qtdatasetcaptureadapter.h"
#include "ai/datasetindex.h"

QtDatasetCaptureAdapter::QtDatasetCaptureAdapter(DataHub *hub, QObject *parent)
    : QObject(parent), m_engine(hub)
{
    // Called from the packet-receiving thread; the signal reaches the page
    // through its queue.
    m_engine.setProgressCallback([this](int cycle, uint64_t examples)
                                 { emit progress(cycle, static_cast<quint64>(examples)); });
    // The check filters the whole trial: it goes to the GUI thread, so the
    // receiving thread only copies the samples.
    m_engine.setExampleCallback([this](int cycle, uint64_t id, const ExampleRecord &record)
                                {
                                    std::vector<std::vector<double>> samples;
                                    samples.reserve(record.samples.size());
                                    for (const auto &channel : record.samples)
                                        samples.emplace_back(channel.begin(), channel.end());
                                    QMetaObject::invokeMethod(
                                        this,
                                        [this, cycle, id, label = record.target.label, samples = std::move(samples)]
                                        { checkTrial(cycle, static_cast<quint64>(id), label, samples); },
                                        Qt::QueuedConnection);
                                });

    connect(&m_cueTimer, &CueTimer::phaseChanged, this,
            [this](int cycle, bool recording, int seconds)
            {
                // The label change is made by the engine, between blocks.
                m_engine.setPhase(cycle, recording);
                const QString label = cycle >= 0 && static_cast<size_t>(cycle) < m_labels.size()
                                          ? QString::fromStdString(m_labels[static_cast<size_t>(cycle)])
                                          : QString();
                emit cueChanged(cycle, label, recording, seconds);
            });
    connect(&m_cueTimer, &CueTimer::countdown, this, &QtDatasetCaptureAdapter::countdown);
    connect(&m_cueTimer, &CueTimer::paused, this, &QtDatasetCaptureAdapter::paused);
    // The capture ends once the last trial is checked: the cue timer runs on
    // the wall clock, the stream on the device clock, so its window may still
    // be completing. Should the stream stop, the capture ends after a while.
    m_finishDelay.setSingleShot(true);
    m_finishDelay.setInterval(1000);
    connect(&m_finishDelay, &QTimer::timeout, this, [this] { complete(false); });
    connect(&m_cueTimer, &CueTimer::finished, this, [this]
            {
                if (m_checked + 1 >= static_cast<int>(m_labels.size()))
                    complete(false);
                else
                    m_finishDelay.start();
            });
}

QtDatasetCaptureAdapter::~QtDatasetCaptureAdapter()
{
    // Unsubscribe while this object is whole: the progress callback emits
    // its signal.
    m_cueTimer.stop();
    m_finishDelay.stop();
    m_engine.finish(true);
}

bool QtDatasetCaptureAdapter::start(const std::string &directory, const DatasetSpec &spec,
                                    const std::vector<std::string> &labels, bool append,
                                    std::string *error)
{
    if (!m_engine.start(directory, spec, labels, append, error))
        return false;
    m_labels = labels;
    m_paradigm = spec.task.paradigm;
    m_sampleRate = static_cast<double>(spec.sampleRate);
    m_fullScale = fullScaleMicrovolts(spec.gain);
    m_quality = quality::configFromTask(spec.task.quality);
    m_groups.clear();
    m_measurements.clear();
    m_checked = -1;
    // A continued dataset keeps its own checks, and its trials are the
    // reference from the first new trial on.
    if (append)
    {
        DatasetIndex index;
        if (index.open(directory) && index.evaluate())
        {
            m_quality = index.config();
            for (const auto &trial : index.trials())
            {
                m_groups.push_back(quality::groupKey(trial.target, m_quality.groupBy));
                m_measurements.push_back(trial.measurements);
            }
        }
    }
    return true;
}

void QtDatasetCaptureAdapter::checkTrial(int cycle, quint64 id, const std::string &label,
                                         const std::vector<std::vector<double>> &samples)
{
    const quality::TrialData data = quality::trialData(m_quality, samples, m_sampleRate, m_fullScale);
    TargetValue target;
    target.type = TargetType::Label;
    target.label = label;
    m_groups.push_back(quality::groupKey(target, m_quality.groupBy));
    m_measurements.push_back(quality::measureAll(m_quality, data));
    const auto judgements = quality::judgeLast(m_quality, m_groups, m_measurements);
    QStringList flagged;
    for (size_t c = 0; c < m_quality.checks.size(); ++c)
        if (judgements[c].verdict == quality::Verdict::Flagged)
            flagged << QString::fromStdString(m_quality.checks[c].check->id());
    m_checked = cycle;
    emit trialChecked(cycle, id, QString::fromStdString(label), flagged);
    // A suspicious trial stops the schedule until the operator decides, the
    // last one included; otherwise the last trial's check ends the capture.
    if (!flagged.isEmpty())
    {
        m_finishDelay.stop();
        m_cueTimer.pause();
    }
    else if (m_finishDelay.isActive() && m_checked + 1 >= static_cast<int>(m_labels.size()))
    {
        complete(false);
    }
}

bool QtDatasetCaptureAdapter::repeatTrial(int cycle, quint64 id)
{
    if (!m_engine.running() || cycle < 0 || static_cast<size_t>(cycle) >= m_labels.size() || !m_cueTimer.extend(1))
        return false;
    const std::string label = m_labels[static_cast<size_t>(cycle)];
    m_labels.push_back(label);
    m_engine.appendLabel(label);
    m_engine.markRepeated(static_cast<uint64_t>(id));
    return true;
}

void QtDatasetCaptureAdapter::resume()
{
    if (m_engine.running())
        m_cueTimer.resume();
}

void QtDatasetCaptureAdapter::startCues()
{
    if (m_engine.running())
        m_cueTimer.start(m_paradigm, static_cast<int>(m_labels.size()));
}

void QtDatasetCaptureAdapter::stop()
{
    complete(true);
}

void QtDatasetCaptureAdapter::complete(bool stopped)
{
    if (!m_engine.running())
        return;
    m_cueTimer.stop();
    m_finishDelay.stop();
    m_engine.finish(stopped);
    emit completed(stopped);
}
