#ifndef SAMPLEBUFFER_H
#define SAMPLEBUFFER_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <mutex>

// Fixed-capacity, per-channel ring buffer of samples (in µV).
//
// Producer (device read thread) appends decoded sample frames; consumer (UI /
// analysis) reads recent windows. Thread-safe via an internal mutex — the
// producer and consumer live on different threads.
//
// Each channel keeps `capacity` samples; when full, the oldest are
// overwritten. A monotonically increasing global sample index lets consumers
// reason about absolute time (index / sampleRate = seconds since start).
class SampleBuffer
{
public:
    SampleBuffer() = default;

    // Configure channel count and per-channel capacity. Clears existing data.
    void configure(int channelCount, size_t capacityPerChannel);

    int channelCount() const;
    size_t capacity() const { return m_capacity; }

    // Total samples ever pushed per channel (monotonic, not capped).
    uint64_t totalPushed() const;

    // Append one sample per channel. `values.size()` must equal channelCount.
    void pushSampleSet(const std::vector<int32_t> &values);

    // Append a block: samples[c] holds N samples for channel c (all channels
    // must have the same N).
    void pushBlock(const std::vector<std::vector<int32_t>> &samples);

    // Copy the most recent `count` samples of `channel` into `out` (oldest
    // first). Returns the number actually copied.
    size_t readLatest(int channel, size_t count, std::vector<int32_t> &out) const;

    // Clear all samples but keep the configured geometry.
    void clear();

private:
    mutable std::mutex m_mutex;
    int m_channels = 0;
    size_t m_capacity = 0;
    std::vector<std::vector<int32_t>> m_data; // [channel][ring slot]
    std::vector<size_t> m_head;               // next write slot per channel
    std::vector<uint64_t> m_total;            // total pushed per channel
};

#endif // SAMPLEBUFFER_H
