#include "ai/inferencesession.h"
#include "ai/errortext.h"


#include <QMetaObject>
#include <algorithm>
#include <exception>

InferenceSession::InferenceSession(DataHub *hub, DeviceSessionSource device, QObject *parent)
    : QObject(parent), m_device(std::move(device)), m_hub(hub)
{
    // Процесс принадлежит сеансу: ответы модели и отказы процесса приходят
    // сюда, а страница получает от сеанса готовый сглаженный ответ.
    connect(&m_worker, &QtAiWorkerAdapter::started, this, [this]
            {
                if (m_running)
                    emit workerReady(); });
    connect(&m_worker, &QtAiWorkerAdapter::classificationResult, this, [this](const ClassificationResult &result)
            {
                if (!m_running)
                    return;
                addResult(result);
                emit resultReceived(); });
    // Отказ процесса останавливает распознавание: ошибка при обработке окна
    // повторялась бы на каждом следующем.
    connect(&m_worker, &QtAiWorkerAdapter::errorOccurred, this, [this](const QString &message)
            {
                if (m_running)
                    emit failed(message); });
    connect(&m_worker, &QtAiWorkerAdapter::finished, this, &InferenceSession::onWorkerFinished);
}

void InferenceSession::onWorkerFinished(bool ok)
{
    if (!m_running)
        return;
    if (ok)
        stop();
    else
        emit failed(tr("The recognition process exited with an error"));
}

InferenceSession::~InferenceSession()
{
    unsubscribe();
}

void InferenceSession::unsubscribe()
{
    // Отписка дожидается конца обработки идущего блока.
    if (m_hub && m_token)
        m_hub->unsubscribe(m_token);
    m_token = 0;
}

bool InferenceSession::loadModel(const QString &path, QString *error)
{
    if (m_running)
    {
        if (error)
            *error = tr("Stop inference before changing the model");
        return false;
    }
    Model model;
    std::string err;
    if (!model.open(path.toStdString(), &err))
    {
        if (error)
            *error = tr("Cannot open the model: %1").arg(errorText(err));
        return false;
    }
    const ModelBundleInfo &info = model.info();
    // Вид ответа модели объявлен её описанием и записан в манифест. Страница
    // показывает пока только результат классификации --- метки с
    // вероятностями; модель с иным ответом отвергается сразу, а не после
    // запуска рабочего процесса.
    if (info.outputKind != "top_k")
    {
        if (error)
            *error = tr("The model gives results of the kind \"%1\", which this program cannot show")
                         .arg(QString::fromStdString(info.outputKind));
        return false;
    }
    m_model = std::move(model);
    m_modelLoaded = true;
    return true;
}

QVector<quint8> InferenceSession::resolveChannels() const
{
    auto *session = m_device ? m_device() : nullptr;
    if (!session)
        return {};
    // The virtual device behaves like a real one: its channels are the
    // selection it acknowledged.
    return session->activeChannels();
}

void InferenceSession::addResult(const ResultSmoother::Probabilities &probabilities)
{
    ++m_resultsReceived;
    m_smoother.add(probabilities);
}

void InferenceSession::setSmoothing(double seconds)
{
    m_smoothingSeconds = seconds;
    // The windows follow each other by the stride, in the device's time.
    const uint32_t sampleRate = m_model.spec().sampleRate;
    const double step = sampleRate > 0 ? static_cast<double>(m_accumulator.strideSamples()) / sampleRate : 0.0;
    m_smoother.configure(seconds, step);
}

bool InferenceSession::start(std::size_t strideSamples, QString *error)
{
    auto fail = [&](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };

    if (m_running)
        return fail(tr("Inference is already running"));
    if (!m_modelLoaded)
        return fail(tr("Select a model first"));
    if (m_worker.isRunning())
        return fail(tr("The previous recognition process has not ended yet"));

    QString err;
    if (!loadModel(QString::fromStdString(m_model.folder()), &err))
        return fail(err);

    const QVector<quint8> channels = resolveChannels();
    if (channels.isEmpty())
        return fail(tr("Select channels and start streaming on the Devices page first"));

    auto *session = m_device ? m_device() : nullptr;
    if (session)
    {
        DatasetSpec live = m_model.spec();
        live.sampleRate = static_cast<uint32_t>(session->sampleRateHz());
        live.channels.clear();
        for (auto c : channels)
            live.channels.push_back(c);
        std::string reason;
        if (!m_model.suits(live, &reason))
            return fail(tr("Model is incompatible with device: %1")
                            .arg(errorText(reason)));
    }

    // Поток запускает только страница устройства: сеанс распознавания к нему
    // присоединяется и без идущей регистрации начат быть не может.
    if (session && !session->isStreaming())
        return fail(tr("Start the stream on the Devices page, then start recognition"));

    const std::size_t windowSamples = m_model.spec().windowSamples;
    if (strideSamples == 0 || windowSamples == 0)
        return fail(tr("The model has an invalid inference window"));
    try
    {
        m_accumulator.configure(static_cast<std::size_t>(channels.size()),
                                windowSamples, strideSamples);
    }
    catch (const std::exception &e)
    {
        return fail(tr("Inference setup error: %1").arg(QString::fromUtf8(e.what())));
    }

    m_windowsSent = 0;
    m_resultsReceived = 0;
    m_smoother.reset();
    setSmoothing(m_smoothingSeconds);
    m_channelCount = static_cast<std::size_t>(channels.size());
    m_running = true;
    m_worker.startInference(QString::fromStdString(m_model.folder()),
                            QString::fromStdString(m_model.info().modelFamily));
    // Подписка — последней: всё, что нужно обработке блока, уже задано.
    if (m_hub)
        m_token = m_hub->subscribe([this](const DataHub::Block &block) { onBlock(block); });
    emit started(windowSamples, strideSamples);
    return true;
}

void InferenceSession::stop()
{
    if (!m_running)
        return;
    unsubscribe();
    m_running = false;
    m_worker.stop();
    m_accumulator.reset();
    emit stopped();
}

// Алгоритм обработки блока отсчётов: проверка геометрии, добавление
// отсчётов в накопитель, извлечение накопленных окон и передача их модели.
// Исполняется в потоке приёма пакетов.
void InferenceSession::onBlock(const DataHub::Block &block)
{
    if (block.samples.empty() || m_channelCount == 0)
        return;
    if (block.samples.size() != m_channelCount)
    {
        emit failed(tr("Inference error: received %1 channels, model expects %2")
                        .arg(block.samples.size())
                        .arg(m_channelCount));
        return;
    }
    const std::size_t sampleCount = block.samples[0].size();
    for (const auto &channel : block.samples)
    {
        if (channel.size() != sampleCount)
        {
            emit failed(tr("Inference error: inconsistent sample block"));
            return;
        }
    }

    std::vector<float> sampleMajor;
    sampleMajor.reserve(sampleCount * block.samples.size());
    for (std::size_t i = 0; i < sampleCount; ++i)
        for (const auto &channel : block.samples)
            sampleMajor.push_back(static_cast<float>(channel[i]));

    std::vector<SlidingWindowAccumulator::Window> windows;
    try
    {
        windows = m_accumulator.append(sampleMajor, sampleCount);
    }
    catch (const std::exception &e)
    {
        emit failed(tr("Inference error: %1").arg(QString::fromUtf8(e.what())));
        return;
    }

    const int rows = static_cast<int>(m_accumulator.windowSamples());
    const int channels = static_cast<int>(m_channelCount);
    for (const auto &window : windows)
    {
        QVector<float> flat;
        flat.reserve(static_cast<int>(window.size()));
        for (float value : window)
            flat.push_back(value);
        // QProcess рабочего процесса принадлежит потоку интерфейса.
        QMetaObject::invokeMethod(
            this,
            [this, flat, rows, channels] {
                if (m_running)
                    m_worker.sendWindow(flat, rows, channels);
            },
            Qt::QueuedConnection);
        ++m_windowsSent;
    }

    emit progressed(static_cast<quint64>(m_accumulator.bufferedSamples()),
                    static_cast<quint64>(m_accumulator.windowSamples()), m_windowsSent.load());
}
