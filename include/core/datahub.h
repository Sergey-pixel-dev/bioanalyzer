#ifndef DATAHUB_H
#define DATAHUB_H

#include "core/ecgadcprotocol.h"
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

// Thread-safe fan-out bus for decoded samples.  The producer is the device
// worker; consumers may be GUI, recorders, DSP or AI adapters.
class DataHub
{
public:
    struct Block
    {
        uint64_t firstSample = 0;
        uint64_t pushSequence = 0;
        uint64_t lostPushes = 0;
        int sampleRateHz = 250;
        std::vector<uint8_t> channels;
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
    std::unordered_map<uint64_t, Callback> m_subscribers;
    uint64_t m_nextToken = 1;
    uint64_t m_totalSamples = 0;
};

#endif
