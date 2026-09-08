#include "core/slidingwindowaccumulator.h"

#include <algorithm>
#include <stdexcept>

void SlidingWindowAccumulator::configure(std::size_t channels,
                                          std::size_t windowSamples,
                                          std::size_t strideSamples)
{
    if (channels == 0 || windowSamples == 0 || strideSamples == 0)
        throw std::invalid_argument("sliding window dimensions must be positive");
    m_channels = channels;
    m_windowSamples = windowSamples;
    m_strideSamples = strideSamples;
    reset();
}

void SlidingWindowAccumulator::reset()
{
    m_buffer.clear();
    m_skipSamples = 0;
}

std::vector<SlidingWindowAccumulator::Window>
SlidingWindowAccumulator::append(const std::vector<float> &sampleMajor,
                                 std::size_t sampleCount)
{
    if (m_channels == 0 || m_windowSamples == 0 || m_strideSamples == 0)
        throw std::logic_error("sliding window is not configured");
    if (sampleCount * m_channels != sampleMajor.size())
        throw std::invalid_argument("sample block size does not match channel count");

    std::vector<Window> result;
    for (std::size_t sample = 0; sample < sampleCount; ++sample)
    {
        if (m_skipSamples > 0)
        {
            --m_skipSamples;
            continue;
        }

        const auto begin = sampleMajor.begin() + static_cast<std::ptrdiff_t>(sample * m_channels);
        m_buffer.insert(m_buffer.end(), begin, begin + static_cast<std::ptrdiff_t>(m_channels));
        if (m_buffer.size() < m_windowSamples * m_channels)
            continue;

        result.emplace_back(m_buffer.begin(),
                            m_buffer.begin() + static_cast<std::ptrdiff_t>(m_windowSamples * m_channels));
        if (m_strideSamples < m_windowSamples)
        {
            const std::size_t removeValues = m_strideSamples * m_channels;
            m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(removeValues));
        }
        else
        {
            m_buffer.clear();
            m_skipSamples = m_strideSamples - m_windowSamples;
        }
    }
    return result;
}
