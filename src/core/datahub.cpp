#include "core/datahub.h"

uint64_t DataHub::subscribe(Callback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const uint64_t token = m_nextToken++;
    m_subscribers.emplace(token, std::move(cb));
    return token;
}

void DataHub::unsubscribe(uint64_t token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subscribers.erase(token);
}

void DataHub::publish(Block block)
{
    std::vector<Callback> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t n = block.samples.empty() ? 0 : block.samples.front().size();
        m_totalSamples += n;
        callbacks.reserve(m_subscribers.size());
        for (const auto &it : m_subscribers)
            callbacks.push_back(it.second);
    }
    for (auto &cb : callbacks)
        if (cb)
            cb(block);
}

void DataHub::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_totalSamples = 0;
}

uint64_t DataHub::totalSamples() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_totalSamples;
}
