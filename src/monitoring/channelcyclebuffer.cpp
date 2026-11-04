#include "monitoring/channelcyclebuffer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace dsp
{

    void ChannelCycleBuffer::configure(std::size_t channelCount, std::size_t cycleSamples)
    {
        m_channels.assign(channelCount, Channel{});
        m_cycleSamples = cycleSamples;
        m_cycleSampleCount = 0;
        m_hasPreviousCycle = false;
    }

    void ChannelCycleBuffer::setCycleSamples(std::size_t cycleSamples)
    {
        m_cycleSamples = cycleSamples;
    }

    void ChannelCycleBuffer::reset()
    {
        for (auto &channel : m_channels)
        {
            channel.previous.clear();
            channel.current.clear();
            channel.filter.reset();
            channel.noiseRms = 0.0;
        }
        m_cycleSampleCount = 0;
        m_hasPreviousCycle = false;
    }

    void ChannelCycleBuffer::rotate()
    {
        for (auto &channel : m_channels)
        {
            channel.previous = std::move(channel.current);
            channel.current.clear();
        }
        m_cycleSampleCount = 0;
        m_hasPreviousCycle = true;
    }

    bool ChannelCycleBuffer::append(const std::vector<std::vector<double>> &block, bool filtered)
    {
        if (m_channels.empty() || block.empty() || m_cycleSamples == 0)
            return false;

        const std::size_t channels = std::min(m_channels.size(), block.size());
        std::size_t sampleCount = block[0].size();
        for (std::size_t c = 0; c < channels; ++c)
            sampleCount = std::min(sampleCount, block[c].size());

        bool rotated = false;
        for (std::size_t sample = 0; sample < sampleCount; ++sample)
        {
            // Ротация выполняется до записи очередного отсчёта, чтобы он попал
            // уже в новый цикл, а завершённый цикл остался неизменным.
            if (m_cycleSampleCount >= m_cycleSamples)
            {
                rotate();
                rotated = true;
            }
            for (std::size_t c = 0; c < channels; ++c)
            {
                const double value = block[c][sample];
                m_channels[c].current.push_back(filtered ? m_channels[c].filter.process(value)
                                                         : value);
            }
            ++m_cycleSampleCount;
        }
        return rotated;
    }

    void ChannelCycleBuffer::updateNoise(std::size_t windowSamples)
    {
        for (auto &channel : m_channels)
        {
            const auto &values = channel.current;
            const std::size_t n = std::min(values.size(), std::max<std::size_t>(windowSamples, 1));
            if (n == 0)
            {
                channel.noiseRms = 0.0;
                continue;
            }
            const auto first = values.end() - static_cast<std::ptrdiff_t>(n);
            double mean = 0.0;
            for (auto it = first; it != values.end(); ++it)
                mean += *it;
            mean /= static_cast<double>(n);
            double sq = 0.0;
            for (auto it = first; it != values.end(); ++it)
            {
                const double d = *it - mean;
                sq += d * d;
            }
            channel.noiseRms = std::sqrt(sq / static_cast<double>(n));
        }
    }

} // namespace dsp
