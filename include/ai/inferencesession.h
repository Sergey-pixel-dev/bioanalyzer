#ifndef AI_INFERENCESESSION_H
#define AI_INFERENCESESSION_H

#include "ai/aimodel.h"
#include "ai/dataset.h"
#include "ai/qtaiworkeradapter.h"
#include "ai/resultsmoother.h"
#include "ai/slidingwindowaccumulator.h"
#include "datahub/datahub.h"
#include "device/qtdevicesessionadapter.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>

// Управляющий класс варианта использования «Распознавать жесты в реальном
// времени». Владеет накопителем скользящего окна и своим рабочим процессом,
// присоединяется к уже идущему потоку отсчётов, передаёт накопленные окна
// модели и сам принимает её ответы: сглаживает их и сообщает странице, что
// показ обновлён. Поток
// запускают только органы управления устройством, поэтому сеанс распознавания
// его не захватывает, а лишь требует. Форма распознавания к потоку не
// обращается.
//
// Потоки исполнения. Сеанс подписывается на поток отсчётов при запуске и
// отписывается при остановке; onBlock() исполняется в потоке приёма пакетов.
// Состояние, нужное обработке блока, задаётся до подписки, а сбрасывается
// после отписки, которая дожидается конца обработки идущего блока, — поэтому
// команды оператора с обработкой блока не пересекаются. Других команд во
// время распознавания нет. Окно передаётся рабочему процессу в потоке
// интерфейса: его QProcess принадлежит этому потоку.
//
// Пакеты. Поток отсчётов сеанс получает при создании (глобальный пакет
// «Шина данных»), а частоту и каналы идущего потока берёт у
// текущего сеанса работы с устройством через переданную функцию: сеанс
// устройства может смениться, а страниц и общего состояния приложения
// сеанс распознавания не знает.
class InferenceSession : public QObject
{
    Q_OBJECT
public:
    // Returns the current device session, or nullptr when there is none.
    using DeviceSessionSource = std::function<QtDeviceSessionAdapter *()>;
    InferenceSession(DataHub *hub, DeviceSessionSource device, QObject *parent = nullptr);
    ~InferenceSession() override;

    bool loadModel(const QString &path, QString *error = nullptr);
    bool start(std::size_t strideSamples, QString *error = nullptr);
    void stop();

    bool running() const { return m_running; }
    bool modelLoaded() const { return m_modelLoaded; }
    // The chosen model: what it takes and returns.
    const Model &model() const { return m_model; }
    quint64 windowsSent() const { return m_windowsSent.load(); }
    quint64 resultsReceived() const { return m_resultsReceived; }
    // The smoothed answers of the model, for the display.
    const ResultSmoother &smoother() const { return m_smoother; }
    // Time constant of the smoothing, seconds; 0 turns it off. May change
    // while recognition runs.
    void setSmoothing(double seconds);

signals:
    void started(std::size_t windowSamples, std::size_t strideSamples);
    void stopped();
    // Накоплено buffered отсчётов из windowSamples; отправлено windowsSent окон.
    void progressed(quint64 buffered, quint64 windowSamples, quint64 windowsSent);
    // The worker started and takes windows.
    void workerReady();
    // The model answered a window; the smoothed answer is updated.
    void resultReceived();
    void failed(const QString &reason);

private:
    // Каналы идущего потока — набор, подтверждённый устройством (в том числе
    // виртуальным).
    QVector<quint8> resolveChannels() const;
    void onBlock(const DataHub::Block &block);
    void unsubscribe();
    // A model's answer for one window; it is smoothed for the display.
    void addResult(const ResultSmoother::Probabilities &probabilities);
    void onWorkerFinished(bool ok);

    DeviceSessionSource m_device;
    QtAiWorkerAdapter m_worker;
    SlidingWindowAccumulator m_accumulator;
    Model m_model;
    bool m_modelLoaded = false;
    bool m_running = false;
    // Поток отсчётов и подписка на него; пока подписка есть (m_token != 0),
    // накопитель и число каналов принадлежат потоку приёма.
    DataHub *m_hub = nullptr;
    quint64 m_token = 0;
    std::size_t m_channelCount = 0;
    std::atomic<quint64> m_windowsSent{0};
    quint64 m_resultsReceived = 0;
    ResultSmoother m_smoother;
    double m_smoothingSeconds = 0.5;
};

#endif // AI_INFERENCESESSION_H
