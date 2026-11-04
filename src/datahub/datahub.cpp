#include "datahub/datahub.h"

uint64_t DataHub::subscribe(Callback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const uint64_t token = m_nextToken++;
    m_subscribers.emplace(token, std::move(cb));
    return token;
}

void DataHub::unsubscribe(uint64_t token)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_subscribers.erase(token);
    // A publish already in progress holds a copy of the callback. Wait until
    // it finishes, otherwise the caller could destroy the subscriber while
    // its callback is still running. Inside a callback the publisher is this
    // very thread, and waiting would never end.
    if (m_activePublishes > 0 && m_publisher == std::this_thread::get_id())
        return;
    m_idle.wait(lock, [this] { return m_activePublishes == 0; });
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
        ++m_activePublishes;
        m_publisher = std::this_thread::get_id();
    }
    // The counter is released even if a callback throws; otherwise every later
    // unsubscribe() would wait forever.
    struct Release
    {
        DataHub *hub;
        ~Release()
        {
            std::lock_guard<std::mutex> lock(hub->m_mutex);
            if (--hub->m_activePublishes == 0)
            {
                hub->m_publisher = std::thread::id();
                hub->m_idle.notify_all();
            }
        }
    } release{this};
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
