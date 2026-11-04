#include "device/qtdevicesessionadapter.h"

#include <QCoreApplication>

#include "device/devicesession.h"
#include "device/device.h"
#include "device/phantomdevice.h"
#include "datahub/datahub.h"

#include <QThread>
#include <QTimer>
#include <QMetaType>

namespace
{
    // Register the sample block type so it can cross thread boundaries via
    // queued connections. Done once, lazily.
    void ensureMetaTypes()
    {
        static bool done = false;
        if (!done)
        {
            qRegisterMetaType<QtDeviceSessionAdapter::SampleBlock>("QtDeviceSessionAdapter::SampleBlock");
            done = true;
        }
    }

    // Position reports for the playback bar: often enough for a smooth
    // slider, rarely enough not to flood the GUI thread.
    constexpr auto kPositionReportInterval = std::chrono::milliseconds(66);
    // Link supervision thresholds. The requirement is to detect a lost
    // link within 5 s; a streaming device sends data every few tens of
    // milliseconds and answers a command within 100 ms.
    constexpr auto kSilenceTimeout = std::chrono::milliseconds(2000);
    constexpr auto kReplyTimeout = std::chrono::milliseconds(2000);
}

QtDeviceSessionAdapter::QtDeviceSessionAdapter(DataHub *hub, QObject *parent)
    : QObject(parent), m_dataHub(hub)
{
    Q_ASSERT(m_dataHub);
    ensureMetaTypes();
    subscribeToHub();
}

void QtDeviceSessionAdapter::subscribeToHub()
{
    if (!m_dataHub || m_hubToken)
        return;
    // Блоки публикуются из рабочего потока; сигнал доставляется подписчикам
    // очередью, поэтому страницы получают их в потоке графического интерфейса.
    m_hubToken = m_dataHub->subscribe([this](const DataHub::Block &block)
                                      {
        SampleBlock out;
        out.samples.reserve(static_cast<int>(block.samples.size()));
        for (const auto &channelSamples : block.samples)
        {
            QVector<qint32> values;
            values.reserve(static_cast<int>(channelSamples.size()));
            for (auto value : channelSamples)
                values.push_back(static_cast<qint32>(value));
            out.samples.push_back(std::move(values));
        }
        emit samplesReady(out); });
}

void QtDeviceSessionAdapter::unsubscribeFromHub()
{
    if (m_dataHub && m_hubToken)
        m_dataHub->unsubscribe(m_hubToken);
    m_hubToken = 0;
}

QtDeviceSessionAdapter::~QtDeviceSessionAdapter()
{
    unsubscribeFromHub();
    disconnectDevice();
    teardownSession();
}

void QtDeviceSessionAdapter::runOnSession(std::function<void(DeviceSession &)> action)
{
    if (!m_session || !action)
        return;
    DeviceSession *session = m_session.get();
    // The command lives in the session's own queue, so it cannot outlive the
    // session it refers to.
    if (m_polling)
        session->post([session, action = std::move(action)] { action(*session); });
    else
        action(*session);
}

void QtDeviceSessionAdapter::inGuiThread(std::function<void()> fn)
{
    // Queued even when already in the GUI thread: acknowledgements are then
    // always handled after the call that issued the command has returned.
    QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection);
}

void QtDeviceSessionAdapter::sendCommand(int cmdId, Sender send, std::function<void(bool ok)> apply)
{
    ++m_commandsInFlight;
    ApplicationProtocol::AckHandler ack =
        [this, cmdId, apply](bool ok, ApplicationProtocol::Error err)
    {
        replyReceived();
        inGuiThread([this, cmdId, apply, ok, err]
                    {
            m_commandsInFlight = std::max(0, m_commandsInFlight - 1);
            if (apply)
                apply(ok);
            emit commandAck(cmdId, ok, static_cast<int>(err)); });
    };
    runOnSession([this, cmdId, send = std::move(send), ack = std::move(ack)](DeviceSession &session)
                 {
        if (send(session, ack) < 0)
        {
            inGuiThread([this, cmdId]
                        {
                m_commandsInFlight = std::max(0, m_commandsInFlight - 1);
                emit commandAck(cmdId, false, -1); });
            // A frame that cannot be written means the port has failed.
            if (m_polling)
                reportLinkLost(tr("a command could not be sent to the port"));
        }
        else
            awaitReply(); });
}

void QtDeviceSessionAdapter::setStreaming(bool streaming)
{
    if (m_streaming == streaming)
        return;
    m_streaming = streaming;
    m_expectData = streaming;
    emit streamingChanged(streaming);
}

void QtDeviceSessionAdapter::teardownSession()
{
    // Session owns the transport; phantom alias is cleared alongside it.
    m_session.reset();
    m_transport = nullptr;
    m_phantom = nullptr;
    m_gainCodes.fill(0);
}

void QtDeviceSessionAdapter::setupSerial(const QString &portPath, int baud)
{
    disconnectDevice();
    teardownSession();
    m_portPath = portPath;

    auto device = std::make_unique<Device>(portPath.toStdString(), baud);
    m_transport = device.get();
    m_phantom = nullptr;
    m_testSignalEnabled = false;
    m_shortInputEnabled = false;
    m_testModeEnabled = false;
    m_streaming = false;
    m_activeChannels.clear();
    m_description.clear();
    m_sampleRateHz = ApplicationProtocol::kSampleRateHz[ApplicationProtocol::kDefaultSampleRateIndex];
    m_session = std::make_unique<DeviceSession>(std::move(device), m_dataHub);
}

bool QtDeviceSessionAdapter::setupPlayback(const QString &recordingPath)
{
    disconnectDevice();
    teardownSession();

    auto phantom = std::make_unique<PhantomDevice>();
    std::string reason;
    if (!phantom->loadRecording(recordingPath.toStdString(), &reason))
    {
        if (reason.empty())
            emit errorOccurred(tr("Failed to load recording: %1").arg(recordingPath));
        else
            emit errorOccurred(tr("Failed to load recording %1: %2")
                                   .arg(recordingPath, QCoreApplication::translate("Errors", reason.c_str())));
        return false;
    }

    m_transport = phantom.get();
    m_phantom = phantom.get();
    m_testSignalEnabled = false;
    m_shortInputEnabled = false;
    m_testModeEnabled = false;
    m_streaming = false;
    m_activeChannels.clear();
    m_description.clear();
    m_sampleRateHz = static_cast<int>(phantom->recordingHeader().sampleRate);
    m_channelCount = static_cast<int>(phantom->recordingHeader().channels.size());
    m_session = std::make_unique<DeviceSession>(std::move(phantom), m_dataHub);
    m_session->setSampleRateHz(m_sampleRateHz);
    return true;
}

// The recording header does not change after loading, so these reads are
// safe from the GUI thread while the polling thread plays the recording.
quint64 QtDeviceSessionAdapter::playbackTotalSamples() const
{
    return m_phantom ? m_phantom->totalSamples() : 0;
}

int QtDeviceSessionAdapter::playbackSampleRate() const
{
    return m_phantom ? static_cast<int>(m_phantom->recordingHeader().sampleRate) : m_sampleRateHz;
}

int QtDeviceSessionAdapter::playbackChannelCount() const
{
    return m_phantom ? static_cast<int>(m_phantom->recordingHeader().channels.size()) : 0;
}

void QtDeviceSessionAdapter::connectDevice()
{
    if (!m_session || m_connected)
        return;

    if (!m_session->open())
    {
        const QString what = m_phantom ? tr("the recording") : m_portPath;
        emit errorOccurred(tr("Cannot open %1: %2")
                               .arg(what, QCoreApplication::translate(
                                              "Errors", m_session->lastOpenError().c_str())));
        return;
    }

    // Query device capabilities once the transport is open. The result is
    // forwarded to pages so channel selectors can match the actual hardware.
    // The poll loop is not running yet, so the request is sent directly; the
    // answer arrives in the polling thread.
    // The polling thread is not running yet, so its supervision state is
    // set here without a race.
    m_linkLost = false;
    m_expectData = false;
    m_wasExpectingData = false;
    m_lastRx = std::chrono::steady_clock::now();
    m_lastValid = m_lastRx;
    m_lastSamples = m_session->totalSamples();
    m_rejectedAtValid = m_session->rejectedFrames();
    m_awaitingReplies = 0;
    m_startPending = false;
    m_commandsInFlight = 0;
    m_session->getDeviceInfo([this](bool ok, const ApplicationProtocol::DeviceInfo &info)
                             {
        replyReceived();
        if (!ok)
            return;
        const int count = info.channelCount;
        const QString description = QString::fromStdString(info.description);
        inGuiThread([this, count, description]
                    {
            m_description = description;
            emit deviceDescriptionChanged(m_description);
            if (count > 0)
            {
                m_channelCount = count;
                emit deviceInfoChanged(m_channelCount);
            } }); });

    // Spin up a worker thread that pumps poll() (and the virtual device).
    m_worker = new QThread(this);
    m_pollTimer = new QTimer();
    m_pollTimer->setInterval(m_pollIntervalMs);
    m_pollTimer->moveToThread(m_worker);

    connect(m_worker, &QThread::started, m_pollTimer, qOverload<>(&QTimer::start));
    connect(m_worker, &QThread::finished, m_pollTimer, &QTimer::stop);
    connect(m_pollTimer, &QTimer::timeout, this, &QtDeviceSessionAdapter::pollOnce, Qt::DirectConnection);

    awaitReply(); // the device-info request above
    m_lastPump = std::chrono::steady_clock::now();
    m_lastPositionReport = m_lastPump;
    m_polling = true;
    m_worker->start();
    m_connected = true;
    emit connectionChanged(true);
}

void QtDeviceSessionAdapter::disconnectDevice()
{
    if (!m_connected)
        return;

    if (m_worker)
    {
        m_worker->quit();
        m_worker->wait();
        delete m_pollTimer;
        m_pollTimer = nullptr;
        delete m_worker;
        m_worker = nullptr;
    }
    // Nothing polls any more: the session is now touched by this thread only.
    m_polling = false;

    if (m_session)
    {
        m_session->discardPosted();
        m_session->stopStream();
        m_session->close();
    }

    m_connected = false;
    m_startPending = false;
    m_commandsInFlight = 0;
    setStreaming(false);
    emit connectionChanged(false);
}

void QtDeviceSessionAdapter::pollOnce()
{
    // Polling thread. Commands posted from the GUI thread run inside poll()
    // before the transport is read.
    if (!m_session)
        return;

    superviseLink(m_session->poll());

    if (m_phantom)
    {
        // Pace the virtual device by wall-clock time rather than by the timer
        // period, which the scheduler does not guarantee.
        const auto now = std::chrono::steady_clock::now();
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(now - m_lastPump).count();
        m_lastPump = now;
        m_phantom->pump(elapsedMs);

        if (now - m_lastPositionReport >= kPositionReportInterval)
        {
            m_lastPositionReport = now;
            emit playbackPositionChanged(m_phantom->positionSample(), m_phantom->totalSamples());
        }
    }
}

void QtDeviceSessionAdapter::superviseLink(int polled)
{
    if (m_linkLost.load())
        return;
    const auto now = std::chrono::steady_clock::now();
    if (polled < 0)
    {
        reportLinkLost(tr("the serial port reported an error"));
        return;
    }
    if (polled > 0)
        m_lastRx = now;
    // Only valid samples keep the stream alive: frames with a bad checksum
    // or a wrong length are bytes too, but carry no data.
    const uint64_t samples = m_session->totalSamples();
    if (samples != m_lastSamples)
    {
        m_lastSamples = samples;
        m_lastValid = now;
        m_rejectedAtValid = m_session->rejectedFrames();
    }
    // Silence is counted from the moment the stream was confirmed.
    const bool expecting = m_expectData.load();
    if (expecting && !m_wasExpectingData)
    {
        m_lastRx = now;
        m_lastValid = now;
        m_rejectedAtValid = m_session->rejectedFrames();
    }
    m_wasExpectingData = expecting;
    const double silence = std::chrono::duration<double>(kSilenceTimeout).count();
    if (expecting && now - m_lastValid > kSilenceTimeout)
    {
        const uint64_t rejected = m_session->rejectedFrames() - m_rejectedAtValid;
        if (now - m_lastRx <= kSilenceTimeout && rejected > 0)
            reportLinkLost(tr("the device sends only invalid frames (%1 rejected in %2 s)")
                               .arg(rejected)
                               .arg(silence));
        else if (now - m_lastRx <= kSilenceTimeout)
            reportLinkLost(tr("the device sends bytes that are not frames for %1 s").arg(silence));
        else
            reportLinkLost(tr("no data came from the device for %1 s").arg(silence));
        return;
    }
    if (m_awaitingReplies > 0 && now - m_awaitingSince > kReplyTimeout)
        reportLinkLost(tr("the device did not answer a command within %1 s")
                           .arg(std::chrono::duration<double>(kReplyTimeout).count()));
}

void QtDeviceSessionAdapter::awaitReply()
{
    if (m_awaitingReplies++ == 0)
        m_awaitingSince = std::chrono::steady_clock::now();
}

void QtDeviceSessionAdapter::replyReceived()
{
    if (m_awaitingReplies > 0)
        --m_awaitingReplies;
    // The next unanswered command is timed from this reply.
    m_awaitingSince = std::chrono::steady_clock::now();
}

void QtDeviceSessionAdapter::reportLinkLost(const QString &reason)
{
    if (m_linkLost.exchange(true))
        return;
    inGuiThread([this, reason] { handleLinkLost(reason); });
}

void QtDeviceSessionAdapter::handleLinkLost(const QString &reason)
{
    // GUI thread. Disconnecting stops the polling thread, closes the port
    // and reports connectionChanged(false) and streamingChanged(false).
    if (!m_connected)
        return;
    disconnectDevice();
    emit connectionLost(reason);
}

void QtDeviceSessionAdapter::setSampleRateIndex(int idx)
{
    if (!m_session || streamBusy())
    {
        if (streamBusy())
            emit errorOccurred(tr("Stop streaming before changing sample rate"));
        return;
    }
    sendCommand(
        0x11,
        [idx](DeviceSession &session, ApplicationProtocol::AckHandler ack)
        { return session.setSamplerate(static_cast<uint8_t>(idx), std::move(ack)); },
        [this, idx](bool ok)
        {
            if (ok && idx >= 0 && idx < ApplicationProtocol::kSampleRateCount)
                m_sampleRateHz = ApplicationProtocol::kSampleRateHz[idx];
        });
}

void QtDeviceSessionAdapter::setGain(int channel, int gainCode)
{
    if (!m_session || streamBusy())
    {
        if (streamBusy())
            emit errorOccurred(tr("Stop streaming before changing gain"));
        return;
    }
    sendCommand(0x30,
                [channel, gainCode](DeviceSession &session, ApplicationProtocol::AckHandler ack)
                { return session.setGain(uint8_t(channel), uint8_t(gainCode), std::move(ack)); },
                [this, channel, gainCode](bool ok)
                {
                    // Only a gain the device confirmed defines the ADC range.
                    if (ok && channel >= 0 && channel < int(m_gainCodes.size()))
                        m_gainCodes[size_t(channel)] = gainCode;
                });
}

int QtDeviceSessionAdapter::gain(int channel) const
{
    if (isPlayback())
    {
        for (const ChannelInfo &info : m_phantom->recordingHeader().channels)
            if (info.physIndex == channel)
                return info.gain;
        return 0;
    }
    if (channel < 0 || channel >= int(m_gainCodes.size()))
        return 0;
    return ApplicationProtocol::kGainValues[m_gainCodes[size_t(channel)]];
}

void QtDeviceSessionAdapter::setShortInput()
{
    if (!m_session || streamBusy())
    {
        if (streamBusy())
            emit errorOccurred(tr("Stop streaming before changing the input switching"));
        return;
    }
    sendCommand(
        0x31,
        [](DeviceSession &session, ApplicationProtocol::AckHandler ack)
        { return session.setShortInput(std::move(ack)); },
        [this](bool ok)
        {
            if (!ok)
                return;
            // InputShort applies globally and excludes the test signal.
            m_shortInputEnabled = true;
            m_testSignalEnabled = false;
            m_testModeEnabled = true;
            emit muxTestChanged(true);
            emit testSignalChanged(false);
            emit testModeChanged(true);
        });
}

void QtDeviceSessionAdapter::setNormalInput()
{
    if (!m_session || streamBusy())
    {
        if (streamBusy())
            emit errorOccurred(tr("Stop streaming before changing the input switching"));
        return;
    }
    sendCommand(
        0x33,
        [](DeviceSession &session, ApplicationProtocol::AckHandler ack)
        { return session.setNormalInput(std::move(ack)); },
        [this](bool ok)
        {
            if (!ok)
                return;
            m_shortInputEnabled = false;
            m_testSignalEnabled = false;
            m_testModeEnabled = false;
            emit muxTestChanged(false);
            emit testSignalChanged(false);
            emit testModeChanged(false);
        });
}

void QtDeviceSessionAdapter::setTestSignal(int amplitude, int frequency)
{
    if (!m_session || streamBusy())
    {
        if (streamBusy())
            emit errorOccurred(tr("Stop streaming before changing the input switching"));
        return;
    }
    sendCommand(
        0x32,
        [amplitude, frequency](DeviceSession &session, ApplicationProtocol::AckHandler ack)
        { return session.setTestSignal(uint8_t(amplitude), uint8_t(frequency), std::move(ack)); },
        [this](bool ok)
        {
            if (!ok)
                return;
            // Both amplitudes select the internal test MUX; amplitude only
            // changes the generated signal level. This mode excludes short.
            m_testSignalEnabled = true;
            m_shortInputEnabled = false;
            m_testModeEnabled = true;
            emit testSignalChanged(true);
            emit muxTestChanged(false);
            emit testModeChanged(true);
        });
}

void QtDeviceSessionAdapter::setActiveChannels(const QVector<quint8> &channels)
{
    if (!m_session)
        return;
    // Геометрия потока неизменна, пока он идёт: смена набора каналов меняет
    // размер кадра Push, который устройство уже передаёт.
    if (streamBusy())
    {
        emit errorOccurred(tr("Stop streaming before changing the channel selection"));
        return;
    }
    std::vector<uint8_t> ch;
    ch.reserve(channels.size());
    for (quint8 c : channels)
        ch.push_back(c);
    sendCommand(
        0x34,
        [ch](DeviceSession &session, ApplicationProtocol::AckHandler ack)
        { return session.setChannels(ch, std::move(ack)); },
        [this, requested = channels](bool ok)
        {
            // Набор запоминается только после подтверждения устройством:
            // иначе страницы строили бы графики по неприменённой настройке.
            if (!ok)
                return;
            m_activeChannels = requested;
            emit activeChannelsChanged(m_activeChannels);
        });
}

void QtDeviceSessionAdapter::startStream()
{
    // A second press before the acknowledgement is ignored: the stream is
    // already being started.
    if (!m_session || m_streaming || m_startPending)
        return;
    if (m_activeChannels.isEmpty())
    {
        emit errorOccurred(tr("Select at least one channel before starting the stream"));
        return;
    }
    // Settings are still being applied (for example the automatic apply of a
    // recording's parameters): the stream would start under half-applied
    // settings, and the rest of the commands would arrive during it.
    if (m_commandsInFlight > 0)
    {
        emit errorOccurred(tr("Wait until the device settings are applied"));
        return;
    }
    // Until the acknowledgement no settings command may follow: the device
    // may already be streaming.
    m_startPending = true;
    sendCommand(
        0x01,
        [](DeviceSession &session, ApplicationProtocol::AckHandler ack)
        { return session.startStream(std::move(ack)); },
        [this](bool ok)
        {
            m_startPending = false;
            if (ok)
                setStreaming(true);
        });
}

void QtDeviceSessionAdapter::stopStream()
{
    if (!m_session)
        return;
    sendCommand(
        0x02,
        [](DeviceSession &session, ApplicationProtocol::AckHandler ack)
        { return session.stopStream(std::move(ack)); },
        [this](bool ok)
        {
            if (ok)
                setStreaming(false);
        });
}

void QtDeviceSessionAdapter::startRecording(const QString &path, const QString &description,
                                            const QVector<quint8> &channels,
                                            const QVector<bool> &enabled,
                                            const QVector<QString> &labels)
{
    if (!m_session)
        return;
    std::vector<ChannelInfo> infos;
    infos.reserve(channels.size());
    for (int i = 0; i < channels.size(); ++i)
    {
        ChannelInfo ci;
        ci.physIndex = channels[i];
        ci.enabled = (i < enabled.size()) ? enabled[i] : true;
        ci.label = (i < labels.size()) ? labels[i].toStdString() : std::string();
        // The recording keeps the gain: a dataset collected from it knows the
        // ADC range, and continuing a dataset compares gains.
        ci.gain = static_cast<uint8_t>(std::max(0, gain(channels[i])));
        infos.push_back(ci);
    }
    // The writer is fed from the polling thread, so it is opened there too.
    runOnSession([this, path, description = description.toStdString(),
                  infos = std::move(infos)](DeviceSession &session)
                 {
        const bool ok = session.startRecording(path.toStdString(), description, infos);
        inGuiThread([this, path, ok]
                    {
            if (!ok)
                emit errorOccurred(tr("Failed to start recording: %1").arg(path));
            emit recordingChanged(ok); }); });
}

void QtDeviceSessionAdapter::stopRecording()
{
    runOnSession([this](DeviceSession &session)
                 {
        session.stopRecording();
        inGuiThread([this] { emit recordingChanged(false); }); });
}

void QtDeviceSessionAdapter::seekToSample(quint64 index)
{
    if (!m_phantom)
        return;
    // The virtual device is pumped by the polling thread; the seek joins the
    // same queue as every other command.
    PhantomDevice *phantom = m_phantom;
    runOnSession([phantom, index](DeviceSession &)
                 { phantom->seekToSample(index); });
}
