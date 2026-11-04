#ifndef DSP_CHANNELCYCLEBUFFER_H
#define DSP_CHANNELCYCLEBUFFER_H

#include "monitoring/biquad.h"

#include <cstddef>
#include <vector>

namespace dsp
{

    // Хранилище отсчётов каналов, организованное циклами фиксированной
    // длительности. По достижении длительности цикла текущие отсчёты
    // становятся предыдущими, а накопление начинается заново — так на
    // осциллограмме видны и текущая развёртка, и предшествующая ей.
    //
    // Копия сигнала одна: если фильтрация включена, отсчёт проходит цепочку
    // фильтров до записи в цикл. По ней строятся осциллограмма, спектр и
    // оценка шума; для оценки качества регистрации фильтры отключают. Смена
    // фильтров действует на новые отсчёты, уже накопленные остаются как были.
    // В запись сеанса и в набор данных поступают сырые отсчёты: они берутся
    // из потока, а не отсюда.
    //
    // Qt не используется, поэтому ротация цикла и расчёт среднеквадратичного
    // уровня шума проверяются без запуска графического интерфейса.
    class ChannelCycleBuffer
    {
    public:
        struct Channel
        {
            std::vector<double> previous;
            std::vector<double> current;
            FilterChain filter;
            double noiseRms = 0.0;
        };

        // Задаёт число каналов и длительность цикла в отсчётах. Накопленные
        // данные сбрасываются.
        void configure(std::size_t channelCount, std::size_t cycleSamples);

        // Изменяет длительность цикла, сохраняя накопленные отсчёты.
        void setCycleSamples(std::size_t cycleSamples);

        void reset();

        // Добавляет блок отсчётов, упорядоченный как [канал][отсчёт], пропуская
        // его через фильтры, если filtered истинно. Возвращает истину, если при
        // добавлении произошла ротация цикла.
        bool append(const std::vector<std::vector<double>> &block, bool filtered);

        // Пересчитывает среднеквадратичный уровень шума по последним
        // windowSamples отсчётам каждого канала.
        void updateNoise(std::size_t windowSamples);

        std::size_t channelCount() const noexcept { return m_channels.size(); }
        std::size_t cycleSamples() const noexcept { return m_cycleSamples; }
        std::size_t cycleSampleCount() const noexcept { return m_cycleSampleCount; }
        bool hasPreviousCycle() const noexcept { return m_hasPreviousCycle; }

        const Channel &channel(std::size_t index) const { return m_channels[index]; }
        Channel &channel(std::size_t index) { return m_channels[index]; }
        double noiseRms(std::size_t index) const { return m_channels[index].noiseRms; }

    private:
        void rotate();

        std::vector<Channel> m_channels;
        std::size_t m_cycleSamples = 0;
        std::size_t m_cycleSampleCount = 0;
        bool m_hasPreviousCycle = false;
    };

} // namespace dsp

#endif // DSP_CHANNELCYCLEBUFFER_H
