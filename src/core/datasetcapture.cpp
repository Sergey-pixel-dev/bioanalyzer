#include "core/datasetcapture.h"

#include <algorithm>

DatasetCaptureEngine::DatasetCaptureEngine(DataHub *hub)
    : m_hub(hub) {}

DatasetCaptureEngine::~DatasetCaptureEngine() { stop(); }

bool DatasetCaptureEngine::start(const std::string &directory, const DatasetSpec &spec,
                                 const std::vector<std::string> &labels,
                                 int repetitions)
{
    if (!m_hub || labels.empty() || spec.channels.empty() || spec.sampleRate == 0 ||
        spec.windowSamples == 0 || repetitions < 1)
        return false;
    m_spec = spec;
    m_spec.task.paradigm.prepSeconds = std::max(0, m_spec.task.paradigm.prepSeconds);
    m_spec.task.paradigm.recordSeconds = std::max(1, m_spec.task.paradigm.recordSeconds);
    std::string error;
    auto dataset = Dataset::create(directory, m_spec, &error);
    if (!dataset)
        return false;
    m_dataset = std::move(dataset);
    m_labels = labels;
    m_repetitions = repetitions;
    m_prep = m_spec.task.paradigm.prepSeconds;
    m_duration = m_spec.task.paradigm.recordSeconds;
    m_seen = 0;
    m_running = true;
    m_history.assign(spec.channels.size(), {});
    m_token = m_hub->subscribe([this](const DataHub::Block &block)
                               { onBlock(block); });
    return true;
}

void DatasetCaptureEngine::stop()
{
    if (m_token && m_hub)
        m_hub->unsubscribe(m_token);
    m_token = 0;
    if (m_running)
    {
        if (m_dataset)
            m_dataset->finish("stopped");
        m_dataset.reset();
        m_running = false;
        m_history.clear();
        if (m_completed)
            m_completed(true);
    }
}

void DatasetCaptureEngine::onBlock(const DataHub::Block &block)
{
    if (!m_running || block.channels.size() != m_spec.channels.size() ||
        block.samples.size() != m_spec.channels.size())
        return;
    for (size_t c = 0; c < m_spec.channels.size(); ++c)
        if (block.channels[c] != m_spec.channels[c])
            return;

    const uint64_t prep = uint64_t(m_prep) * m_spec.sampleRate;
    const uint64_t duration = uint64_t(m_duration) * m_spec.sampleRate;
    const uint64_t cycle = prep + duration;
    const uint64_t totalCycles = uint64_t(m_labels.size()) * uint64_t(m_repetitions);
    const size_t window = m_spec.windowSamples;
    const size_t stride = m_spec.strideSamples ? m_spec.strideSamples : window;
    const size_t count = block.samples.empty() ? 0 : block.samples.front().size();

    for (size_t i = 0; i < count && m_running; ++i)
    {
        const uint64_t absolute = m_seen++;
        const uint64_t cycleNo = cycle ? absolute / cycle : 0;
        if (cycleNo >= totalCycles)
        {
            if (m_dataset)
                m_dataset->finish("complete");
            m_dataset.reset();
            m_running = false;
            m_history.clear();
            if (m_completed)
                m_completed(false);
            break;
        }
        const uint64_t inCycle = cycle ? absolute % cycle : 0;
        if (inCycle < prep)
        {
            for (auto &history : m_history)
                history.clear();
            if (inCycle == 0)
            {
                const size_t labelIndex = static_cast<size_t>(cycleNo % m_labels.size());
                const int repetition = static_cast<int>(cycleNo / m_labels.size());
                TargetValue eventTarget;
                eventTarget.type = TargetType::Label;
                eventTarget.label = m_labels[labelIndex];
                if (m_dataset)
                    m_dataset->appendEvent(absolute, m_labels[labelIndex], eventTarget);
                if (m_progress)
                    m_progress(static_cast<int>(labelIndex), repetition, 0.0,
                               m_labels[labelIndex]);
            }
            continue;
        }
        const uint64_t recorded = inCycle - prep;
        for (size_t c = 0; c < m_history.size(); ++c)
        {
            if (i >= block.samples[c].size())
                continue;
            auto &history = m_history[c];
            history.push_back(block.samples[c][i]);
            while (history.size() > window)
                history.pop_front();
        }
        if (recorded + 1 < window || ((recorded + 1 - window) % stride) != 0)
            continue;

        std::vector<std::vector<int32_t>> example(m_history.size());
        bool complete = true;
        for (size_t c = 0; c < m_history.size(); ++c)
        {
            if (m_history[c].size() != window)
            {
                complete = false;
                break;
            }
            example[c].assign(m_history[c].begin(), m_history[c].end());
        }
        if (!complete)
            continue;
        const size_t labelIndex = static_cast<size_t>(cycleNo % m_labels.size());
        const int repetition = static_cast<int>(cycleNo / m_labels.size());
        // `absolute` is the sample position in the capture timeline; unlike
        // the index within this push block it cannot underflow when a window
        // spans two or more blocks.
        ExampleRecord record;
        record.sourceSample = absolute + 1 - window;
        record.target.type = TargetType::Label;
        record.target.label = m_labels[labelIndex];
        record.samples = std::move(example);
        if (m_dataset)
            m_dataset->append(record);
        if (m_progress)
            m_progress(static_cast<int>(labelIndex), repetition,
                       double(recorded + 1) / double(m_spec.sampleRate),
                       m_labels[labelIndex]);
    }
}
