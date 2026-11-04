#include "datahub/commandqueue.h"

#include <utility>

void CommandQueue::post(Command command)
{
    if (!command)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_commands.push_back(std::move(command));
}

void CommandQueue::drain()
{
    // Команды забираются под мьютексом, а исполняются без него: команда
    // может сама поставить в очередь следующую, не заблокировав себя.
    std::deque<Command> pending;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        pending.swap(m_commands);
    }
    for (auto &command : pending)
        command();
}

bool CommandQueue::empty() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_commands.empty();
}

void CommandQueue::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_commands.clear();
}
