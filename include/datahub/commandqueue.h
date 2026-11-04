#ifndef COMMANDQUEUE_H
#define COMMANDQUEUE_H

#include <deque>
#include <functional>
#include <mutex>

// Очередь отложенных вызовов между потоками исполнения.
//
// post() вызывается из потока интерфейса, drain() — из потока приёма
// пакетов: в начале очередного опроса устройства или обработки очередного
// блока отсчётов. Состояние управляющего класса, опустошающего очередь,
// принадлежит потоку приёма, поэтому команда оператора никогда не совпадает
// по времени с обработкой блока и не застаёт её на середине. Мьютекс
// защищает только саму очередь, а не состояние управляющего класса.
//
// Qt не используется.
class CommandQueue
{
public:
    using Command = std::function<void()>;

    void post(Command command);

    // Исполняет накопленные команды в порядке поступления. Команды,
    // поставленные во время исполнения, дожидаются следующего вызова.
    void drain();

    bool empty() const;
    void clear();

private:
    mutable std::mutex m_mutex;
    std::deque<Command> m_commands;
};

#endif // COMMANDQUEUE_H
