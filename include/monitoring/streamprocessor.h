#ifndef DSP_STREAMPROCESSOR_H
#define DSP_STREAMPROCESSOR_H

#include "datahub/datahub.h"
#include "datahub/commandqueue.h"
#include "monitoring/biquad.h"
#include "monitoring/channelcyclebuffer.h"
#include "monitoring/spectrum.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace dsp
{

    // Описание одной ступени цепочки фильтров отображения. Порядок ступеней
    // в векторе есть порядок их применения.
    struct FilterSpec
    {
        Biquad::Type type = Biquad::Type::HighPass;
        double frequency = 0.0;  // частота среза или центра, Гц
        double q = 0.707;        // добротность
        int digitalOrder = 2;    // цифровой порядок ступени
    };

    // Обработчик потока отсчётов — управляющий объект варианта использования
    // «Проверить качество регистрации». Подписан на поток отсчётов и задаёт
    // порядок обработки блока: принять, отфильтровать, накопить, оценить шум.
    //
    // Потоки исполнения. onBlock() исполняется в потоке приёма пакетов.
    // Команды из потока интерфейса (фильтры, длительность цикла, сброс) ставятся
    // в очередь и исполняются в начале обработки очередного блока, поэтому
    // обработка блока никогда не застаёт изменение настроек на середине. Пока
    // обработчик не подписан, команды исполняются сразу. Форма читает
    // накопленное через read() под мьютексом, который onBlock() держит на всё
    // время обработки блока.
    //
    // Геометрию потока — каналы и частоту дискретизации — задаёт форма через
    // setStream(), взяв её у сеанса устройства при запуске потока; при смене
    // геометрии накопление начинается заново. Обработчик подписан всё время
    // работы формы, поэтому блоки до setStream() и после endStream()
    // пропускаются: иначе первые блоки нового потока прошли бы через фильтр,
    // рассчитанный на прежнюю частоту.
    //
    // Фильтр применяется только к отображаемой копии. В запись сеанса и в набор
    // данных поступают сырые отсчёты: иначе параметры фильтра стали бы скрытой
    // частью формата, а модель, обученная на отфильтрованных данных, отказала
    // бы при другой настройке фильтра.
    //
    // Qt не используется, поэтому порядок обработки проверяется без запуска
    // графического интерфейса.
    class StreamProcessor
    {
    public:
        // То, что форма читает за один раз.
        struct View
        {
            const ChannelCycleBuffer &buffer;
            const std::vector<uint8_t> &channels; // номера каналов потока
            int sampleRateHz;
        };

        StreamProcessor() = default;
        ~StreamProcessor();
        StreamProcessor(const StreamProcessor &) = delete;
        StreamProcessor &operator=(const StreamProcessor &) = delete;

        // Подписка на поток отсчётов. detach() возвращается после завершения
        // обработки блока, если она шла, и исполняет оставшиеся команды.
        void attach(DataHub *hub);
        void detach();
        bool attached() const noexcept { return m_hub != nullptr; }

        // Команды.
        void setStream(const std::vector<uint8_t> &channels, int sampleRateHz);
        void endStream();
        void setFilters(const std::vector<FilterSpec> &stages);
        void setFilterEnabled(bool enabled);
        void setCycleSeconds(double seconds);
        void reset();

        // Чтение накопленного под мьютексом.
        void read(const std::function<void(const View &)> &reader) const;

        // Спектр по той же единственной копии, что и осциллограмма: текущий
        // цикл, а при его недостаточной длине — предыдущий.
        SpectrumResult spectrum(std::size_t channel, Window window,
                                int maxPeaks = 8) const;

        double noiseRms(std::size_t channel) const;

        // Обработка блока; вызывается подпиской, открыта для проверок без
        // потока отсчётов.
        void onBlock(const DataHub::Block &block);

    private:
        // Вызываются при захваченном m_mutex.
        void run(CommandQueue::Command command);
        void applyGeometry(const std::vector<uint8_t> &channels, int sampleRateHz);
        void rebuildChains();
        std::size_t cycleSamples() const;

        mutable std::mutex m_mutex;
        CommandQueue m_queue;
        DataHub *m_hub = nullptr;
        uint64_t m_token = 0;

        ChannelCycleBuffer m_buffer;
        std::vector<uint8_t> m_channels;
        std::vector<FilterSpec> m_stages;
        int m_sampleRateHz = 250;
        bool m_streamSet = false;
        double m_cycleSeconds = 10.0;
        bool m_filterEnabled = false;
    };

} // namespace dsp

#endif // DSP_STREAMPROCESSOR_H
