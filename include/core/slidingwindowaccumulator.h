#ifndef SLIDINGWINDOWACCUMULATOR_H
#define SLIDINGWINDOWACCUMULATOR_H

#include <cstddef>
#include <vector>

// Builds fixed-size sample-major windows from arbitrarily sized input blocks.
// Stride is independent from dataset capture settings.
class SlidingWindowAccumulator
{
public:
    using Window = std::vector<float>;

    void configure(std::size_t channels, std::size_t windowSamples,
                   std::size_t strideSamples);
    void reset();
    std::vector<Window> append(const std::vector<float> &sampleMajor,
                               std::size_t sampleCount);

    std::size_t channels() const noexcept { return m_channels; }
    std::size_t windowSamples() const noexcept { return m_windowSamples; }
    std::size_t strideSamples() const noexcept { return m_strideSamples; }
    std::size_t bufferedSamples() const noexcept { return m_buffer.size() / m_channels; }
    std::size_t skippedSamples() const noexcept { return m_skipSamples; }

private:
    std::size_t m_channels = 0;
    std::size_t m_windowSamples = 0;
    std::size_t m_strideSamples = 0;
    std::size_t m_skipSamples = 0;
    std::vector<float> m_buffer;
};

#endif
