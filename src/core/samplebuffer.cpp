#include "core/samplebuffer.h"

#include <algorithm>

void SampleBuffer::configure(int channelCount, size_t capacityPerChannel)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_channels = channelCount > 0 ? channelCount : 0;
    m_capacity = capacityPerChannel;
    m_data.assign(m_channels, std::vector<int32_t>(m_capacity, 0));
    m_head.assign(m_channels, 0);
    m_total.assign(m_channels, 0);
}

int SampleBuffer::channelCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_channels;
}

uint64_t SampleBuffer::totalPushed() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_channels > 0 ? m_total[0] : 0;
}

void SampleBuffer::pushSampleSet(const std::vector<int32_t> &values)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_capacity == 0 || static_cast<int>(values.size()) != m_channels)
        return;
    for (int c = 0; c < m_channels; ++c)
    {
        m_data[c][m_head[c]] = values[c];
        m_head[c] = (m_head[c] + 1) % m_capacity;
        ++m_total[c];
    }
}

void SampleBuffer::pushBlock(const std::vector<std::vector<int32_t>> &samples)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_capacity == 0 || static_cast<int>(samples.size()) != m_channels || m_channels == 0)
        return;

    const size_t n = samples[0].size();
    for (int c = 1; c < m_channels; ++c)
    {
        if (samples[c].size() != n)
            return; // ragged block; reject
    }

    for (size_t s = 0; s < n; ++s)
    {
        for (int c = 0; c < m_channels; ++c)
        {
            m_data[c][m_head[c]] = samples[c][s];
            m_head[c] = (m_head[c] + 1) % m_capacity;
            ++m_total[c];
        }
    }
}

size_t SampleBuffer::readLatest(int channel, size_t count, std::vector<int32_t> &out) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    out.clear();
    if (channel < 0 || channel >= m_channels || m_capacity == 0)
        return 0;

    const uint64_t total = m_total[channel];
    const size_t available = static_cast<size_t>(std::min<uint64_t>(total, m_capacity));
    const size_t n = std::min(count, available);
    if (n == 0)
        return 0;

    out.resize(n);
    // head points at the next write slot; the newest sample is at head-1.
    // Oldest of the n samples is at head - n (mod capacity).
    size_t start = (m_head[channel] + m_capacity - n) % m_capacity;
    for (size_t i = 0; i < n; ++i)
        out[i] = m_data[channel][(start + i) % m_capacity];
    return n;
}

void SampleBuffer::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int c = 0; c < m_channels; ++c)
    {
        std::fill(m_data[c].begin(), m_data[c].end(), 0);
        m_head[c] = 0;
        m_total[c] = 0;
    }
}
