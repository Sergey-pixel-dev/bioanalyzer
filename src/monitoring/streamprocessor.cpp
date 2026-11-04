#include "monitoring/streamprocessor.h"

#include <algorithm>
#include <cmath>

namespace dsp
{

    StreamProcessor::~StreamProcessor()
    {
        detach();
    }

    std::size_t StreamProcessor::cycleSamples() const
    {
        const double samples = m_cycleSeconds * static_cast<double>(m_sampleRateHz);
        const long long rounded = std::llround(samples);
        return static_cast<std::size_t>(rounded > 1 ? rounded : 1);
    }

    void StreamProcessor::attach(DataHub *hub)
    {
        if (m_hub == hub)
            return;
        detach();
        if (!hub)
            return;
        m_hub = hub;
        m_token = hub->subscribe([this](const DataHub::Block &block) { onBlock(block); });
    }

    void StreamProcessor::detach()
    {
        if (!m_hub)
            return;
        // unsubscribe() ждёт завершения идущей обработки блока; после неё
        // обработчик принадлежит этому потоку, и оставшиеся команды можно
        // исполнить сразу.
        m_hub->unsubscribe(m_token);
        m_hub = nullptr;
        m_token = 0;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.drain();
    }

    void StreamProcessor::run(CommandQueue::Command command)
    {
        if (m_hub)
        {
            m_queue.post(std::move(command));
            return;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        command();
    }

    void StreamProcessor::rebuildChains()
    {
        const double fs = static_cast<double>(m_sampleRateHz);
        for (std::size_t c = 0; c < m_buffer.channelCount(); ++c)
        {
            auto &chain = m_buffer.channel(c).filter;
            chain.clear();
            for (const FilterSpec &stage : m_stages)
                chain.add(stage.type, fs, stage.frequency, stage.q,
                          stage.digitalOrder);
            chain.reset();
        }
    }

    void StreamProcessor::applyGeometry(const std::vector<uint8_t> &channels, int sampleRateHz)
    {
        m_channels = channels;
        m_sampleRateHz = sampleRateHz > 0 ? sampleRateHz : 250;
        m_buffer.configure(channels.size(), cycleSamples());
        // Коэффициенты ступеней зависят от частоты дискретизации.
        rebuildChains();
    }

    void StreamProcessor::setStream(const std::vector<uint8_t> &channels, int sampleRateHz)
    {
        run([this, channels, sampleRateHz] {
            // Перезапуск с той же геометрией продолжает накопленный цикл.
            if (channels != m_channels || sampleRateHz != m_sampleRateHz)
                applyGeometry(channels, sampleRateHz);
            m_streamSet = !channels.empty();
        });
    }

    void StreamProcessor::endStream()
    {
        run([this] { m_streamSet = false; });
    }

    void StreamProcessor::setFilters(const std::vector<FilterSpec> &stages)
    {
        run([this, stages] {
            m_stages = stages;
            // Новый фильтр действует на новые отсчёты; накопленные остаются
            // такими, какими были отображены.
            rebuildChains();
        });
    }

    void StreamProcessor::setFilterEnabled(bool enabled)
    {
        run([this, enabled] {
            if (enabled && !m_filterEnabled)
                for (std::size_t c = 0; c < m_buffer.channelCount(); ++c)
                    m_buffer.channel(c).filter.reset();
            m_filterEnabled = enabled;
        });
    }

    void StreamProcessor::setCycleSeconds(double seconds)
    {
        if (seconds <= 0.0)
            return;
        run([this, seconds] {
            m_cycleSeconds = seconds;
            m_buffer.setCycleSamples(cycleSamples());
        });
    }

    void StreamProcessor::reset()
    {
        run([this] {
            m_buffer.reset();
            rebuildChains();
        });
    }

    void StreamProcessor::onBlock(const DataHub::Block &block)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.drain();

        if (!m_streamSet || block.samples.empty() || block.samples.size() != m_channels.size())
            return;

        std::vector<std::vector<double>> samples(block.samples.size());
        for (std::size_t c = 0; c < block.samples.size(); ++c)
            samples[c].assign(block.samples[c].begin(), block.samples[c].end());

        m_buffer.append(samples, m_filterEnabled && !m_stages.empty());
        // Оценка шума берётся по последней секунде текущего цикла.
        m_buffer.updateNoise(static_cast<std::size_t>(std::max(1, m_sampleRateHz)));
    }

    void StreamProcessor::read(const std::function<void(const View &)> &reader) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        reader(View{m_buffer, m_channels, m_sampleRateHz});
    }

    SpectrumResult StreamProcessor::spectrum(std::size_t channel, Window window,
                                             int maxPeaks) const
    {
        std::vector<double> signal;
        double fs = 0.0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (channel >= m_buffer.channelCount())
                return {};
            const auto &ch = m_buffer.channel(channel);
            signal = ch.current.size() >= 16 ? ch.current : ch.previous;
            fs = static_cast<double>(m_sampleRateHz);
        }
        if (signal.size() < 16)
            return {};
        return SpectrumAnalyzer::analyze(signal, fs, window, maxPeaks);
    }

    double StreamProcessor::noiseRms(std::size_t channel) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (channel >= m_buffer.channelCount())
            return 0.0;
        return m_buffer.noiseRms(channel);
    }

} // namespace dsp
