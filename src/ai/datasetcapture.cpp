#include "ai/datasetcapture.h"
#include "common/translatable.h"

#include <algorithm>
#include <ctime>
#include <exception>

DatasetCaptureEngine::DatasetCaptureEngine(DataHub *hub)
    : m_hub(hub) {}

DatasetCaptureEngine::~DatasetCaptureEngine()
{
    // Stop receiving and keep what was captured; finish() makes no callbacks.
    finish(true);
}

bool DatasetCaptureEngine::start(const std::string &directory, const DatasetSpec &spec,
                                 const std::vector<std::string> &labels, bool append,
                                 std::string *error)
{
    auto fail = [&](const char *reason)
    {
        if (error)
            *error = reason;
        return false;
    };
    if (m_running || !m_hub)
        return fail(QT_TRANSLATE_NOOP("Errors", "capture is already running"));
    if (!isSupportedTask(spec.task))
        return fail(QT_TRANSLATE_NOOP("Errors", "only classification by a cue schedule can be captured"));
    if (labels.empty())
        return fail(QT_TRANSLATE_NOOP("Errors", "the schedule is empty"));
    if (spec.channels.empty() || spec.sampleRate == 0 || spec.windowSamples == 0)
        return fail(QT_TRANSLATE_NOOP("Errors", "the stream parameters are not set"));
    DatasetSpec normalized = spec;
    normalized.task.paradigm.prepSeconds = std::max(0, normalized.task.paradigm.prepSeconds);
    normalized.task.paradigm.recordSeconds = std::max(1, normalized.task.paradigm.recordSeconds);
    // One window per trial: the stride equals the window.
    normalized.strideSamples = normalized.windowSamples;
    try
    {
        m_accumulator.configure(normalized.channels.size(), normalized.windowSamples,
                                normalized.windowSamples);
    }
    catch (const std::exception &exception)
    {
        if (error)
            *error = exception.what();
        return false;
    }
    auto dataset = append ? Dataset::openForAppend(directory, normalized, error)
                          : Dataset::create(directory, normalized, error);
    if (!dataset)
        return false;

    m_spec = normalized;
    m_dataset = std::move(dataset);
    m_labels = labels;
    m_examples = 0;
    m_cycle = -1;
    m_recording = false;
    m_windowDone = false;
    m_pendingPhases.clear();
    m_queue.clear();
    m_pending = {};
    m_running = true;
    // Subscribe last: everything a block needs is already in place.
    m_token = m_hub->subscribe([this](const DataHub::Block &block) { onBlock(block); });
    return true;
}

void DatasetCaptureEngine::setPhase(int cycle, bool recording)
{
    if (!m_running)
        return;
    m_queue.post([this, cycle, recording] { applyPhase(cycle, recording); });
}

void DatasetCaptureEngine::applyPhase(int cycle, bool recording)
{
    if (cycle < 0 || static_cast<size_t>(cycle) >= m_labels.size())
        return;
    // The trial's window is not complete yet: finish it first.
    if (m_recording && !m_windowDone)
    {
        m_pendingPhases.emplace_back(cycle, recording);
        return;
    }
    m_accumulator.reset();
    m_windowDone = false;
    m_cycle = cycle;
    m_recording = recording;
}

void DatasetCaptureEngine::unsubscribe()
{
    if (m_hub && m_token)
        m_hub->unsubscribe(m_token);
    m_token = 0;
}

void DatasetCaptureEngine::finish(bool stopped)
{
    if (!m_running)
        return;
    // Unsubscribing waits for a block in progress; afterwards nothing else
    // touches the dataset.
    unsubscribe();
    m_queue.clear();
    m_pendingPhases.clear();
    if (m_dataset)
        m_dataset->finish(stopped ? "stopped" : "complete", m_pending);
    m_pending = {};
    m_dataset.reset();
    m_accumulator.reset();
    m_running = false;
}

void DatasetCaptureEngine::markRepeated(uint64_t id)
{
    if (m_running)
        m_pending.repeated.insert(id);
}

void DatasetCaptureEngine::appendLabel(const std::string &label)
{
    if (!m_running)
        return;
    // The schedule is read by the receiving thread: it grows between blocks,
    // before the cue timer reaches the new trial.
    m_queue.post([this, label] { m_labels.push_back(label); });
}

void DatasetCaptureEngine::onBlock(const DataHub::Block &block)
{
    // Phase changes posted by the cue timer take effect between blocks.
    m_queue.drain();

    // The specification took the channels from the device session, and they
    // cannot change while the stream runs: capture ends when the stream stops.
    if (block.samples.size() != m_spec.channels.size())
        return;
    const size_t count = block.samples.empty() ? 0 : block.samples.front().size();
    for (const auto &channel : block.samples)
        if (channel.size() != count)
            return;

    // Preparation, or the rest of a trial whose window is already written.
    if (!m_recording || m_cycle < 0 || m_windowDone)
        return;

    std::vector<float> sampleMajor;
    sampleMajor.reserve(count * block.samples.size());
    for (size_t i = 0; i < count; ++i)
        for (const auto &channel : block.samples)
            sampleMajor.push_back(static_cast<float>(channel[i]));

    std::vector<SlidingWindowAccumulator::Window> windows;
    try
    {
        windows = m_accumulator.append(sampleMajor, count);
    }
    catch (const std::exception &)
    {
        return;
    }
    if (windows.empty())
        return;

    writeWindow(windows.front());
    m_windowDone = true;
    if (m_progress)
        m_progress(m_cycle, m_examples);
    // Phase changes that waited for this window take effect now, in order.
    std::deque<std::pair<int, bool>> pending;
    pending.swap(m_pendingPhases);
    for (const auto &phase : pending)
        applyPhase(phase.first, phase.second);
}

void DatasetCaptureEngine::writeWindow(const SlidingWindowAccumulator::Window &window)
{
    const size_t channels = m_spec.channels.size();
    ExampleRecord record;
    record.target.type = TargetType::Label;
    record.target.label = m_labels[static_cast<size_t>(m_cycle)];
    record.cue = record.target;
    char stamp[32] = {};
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", &local);
    record.recordedAt = stamp;
    // The accumulator is sample-major; the dataset stores [channel][sample].
    record.samples.assign(channels, std::vector<int32_t>(m_spec.windowSamples));
    for (size_t s = 0; s < m_spec.windowSamples; ++s)
        for (size_t c = 0; c < channels; ++c)
            record.samples[c][s] = static_cast<int32_t>(window[s * channels + c]);
    if (m_dataset && m_dataset->append(record))
    {
        ++m_examples;
        if (m_example)
            m_example(m_cycle, m_dataset->exampleCount() - 1, record);
    }
}
