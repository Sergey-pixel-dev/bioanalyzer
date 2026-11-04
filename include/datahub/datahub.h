#ifndef DATAHUB_H
#define DATAHUB_H

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

// Thread-safe fan-out bus for decoded samples.  The producer is the device
// worker; consumers may be GUI, recorders, DSP or AI adapters. Part of the
// global data bus package: it knows no class of the packages that use it,
// a subscriber hands it only a callback.
//
// unsubscribe() returns only after every callback already in flight has
// finished, so a subscriber may be destroyed right after it unsubscribes.
// Called from inside a callback (the publishing thread), it does not wait.
class DataHub
{
public:
    // A block carries the samples only: one inner vector per active channel,
    // in the order of the device's channel selection. The sample rate and the
    // channel numbers do not change while a stream runs, so a consumer takes
    // them from the device session when it starts to receive the stream.
    struct Block
    {
        uint64_t firstSample = 0;
        uint64_t pushSequence = 0;
        uint64_t lostPushes = 0;
        std::vector<std::vector<int32_t>> samples;
    };
    using Callback = std::function<void(const Block &)>;

    uint64_t subscribe(Callback cb);
    void unsubscribe(uint64_t token);
    void publish(Block block);
    void clear();
    uint64_t totalSamples() const;

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_idle;
    int m_activePublishes = 0;
    std::thread::id m_publisher;
    std::unordered_map<uint64_t, Callback> m_subscribers;
    uint64_t m_nextToken = 1;
    uint64_t m_totalSamples = 0;
};

#endif
