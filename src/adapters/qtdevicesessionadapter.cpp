#include "adapters/qtdevicesessionadapter.h"

#include "core/devicesession.h"
#include "core/device.h"
#include "core/phantomdevice.h"
#include "core/datahub.h"

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
}

QtDeviceSessionAdapter::QtDeviceSessionAdapter(DataHub *hub, QObject *parent)
    : QObject(parent), m_dataHub(hub)
{
    Q_ASSERT(m_dataHub);
    ensureMetaTypes();
}

QtDeviceSessionAdapter::~QtDeviceSessionAdapter()
{
    disconnectDevice();
    teardownSession();
}

void QtDeviceSessionAdapter::teardownSession()
{
    // Session owns the transport; phantom alias is cleared alongside it.
    m_session.reset();
    m_transport = nullptr;
    m_phantom = nullptr;
}

void QtDeviceSessionAdapter::setupSerial(const QString &portPath, int baud)
{
    disconnectDevice();
    teardownSession();

    Device *dev = new Device(portPath.toStdString(), baud);
    m_transport = dev;
    m_phantom = nullptr;
    m_testSignalEnabled = false;
    m_shortInputEnabled = false;
    m_testModeEnabled = false;
    m_streaming = false;
    m_sampleRateHz = ApplicationProtocol::kSampleRateHz[ApplicationProtocol::kDefaultSampleRateIndex];
    m_session = std::make_unique<DeviceSession>(dev, m_dataHub, /*ownTransport=*/true);
}

bool QtDeviceSessionAdapter::setupPlayback(const QString &recordingPath)
{
    disconnectDevice();
    teardownSession();

    auto *phantom = new PhantomDevice();
    if (!phantom->loadRecording(recordingPath.toStdString()))
    {
        delete phantom;
        emit errorOccurred(QStringLiteral("Failed to load recording: %1").arg(recordingPath));
        return false;
    }

    m_transport = phantom;
    m_phantom = phantom;
    m_testSignalEnabled = false;
    m_shortInputEnabled = false;
    m_testModeEnabled = false;
    m_streaming = false;
    m_sampleRateHz = phantom->recordingHeader().sampleRate;
    m_channelCount = static_cast<int>(phantom->recordingHeader().channels.size());
    m_session = std::make_unique<DeviceSession>(phantom, m_dataHub, /*ownTransport=*/true);
    m_session->setSampleRateHz(m_sampleRateHz);
    return true;
}

quint64 QtDeviceSessionAdapter::playbackTotalSamples() const
{
    return m_phantom ? m_phantom->totalSamples() : 0;
}

int QtDeviceSessionAdapter::playbackSampleRate() const
{
    return m_phantom ? static_cast<int>(m_phantom->recordingHeader().sampleRate) : m_sampleRateHz;
}

QVector<quint8> QtDeviceSessionAdapter::playbackChannels() const
{
    QVector<quint8> chans;
    if (m_phantom)
        for (const ChannelInfo &ci : m_phantom->recordingHeader().channels)
            chans.push_back(ci.physIndex);
    return chans;
}

void QtDeviceSessionAdapter::connectDevice()
{
    if (!m_session || m_connected)
        return;

    if (!m_session->open())
    {
        emit errorOccurred(QStringLiteral("Failed to open device"));
        return;
    }

    // Query device capabilities once the transport is open. The result is
    // forwarded to pages so channel selectors can match the actual hardware.
    m_session->getDeviceInfo([this](bool ok, const ApplicationProtocol::DeviceInfo &info)
                             {
        if (ok && info.channelCount > 0)
        {
            m_channelCount = info.channelCount;
            emit deviceInfoChanged(m_channelCount);
        } });

    // Spin up a worker thread that pumps poll() (and phantom playback).
    m_worker = new QThread(this);
    m_pollTimer = new QTimer();
    m_pollTimer->setInterval(m_pollIntervalMs);
    m_pollTimer->moveToThread(m_worker);

    connect(m_worker, &QThread::started, m_pollTimer, qOverload<>(&QTimer::start));
    connect(m_worker, &QThread::finished, m_pollTimer, &QTimer::stop);
    connect(m_pollTimer, &QTimer::timeout, this, &QtDeviceSessionAdapter::pollOnce, Qt::DirectConnection);

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

    if (m_session)
    {
        m_session->stopStream();
        m_session->close();
    }

    m_connected = false;
    m_streaming = false;
    emit connectionChanged(false);
}

void QtDeviceSessionAdapter::pollOnce()
{
    if (!m_session)
        return;

    // Advance phantom playback in step with the poll interval.
    if (m_phantom && m_phantom->isPlaying())
        m_phantom->pump(m_pollIntervalMs);

    m_session->poll();

    if (m_phantom)
        emit playbackPositionChanged(m_phantom->positionSample());
}

void QtDeviceSessionAdapter::setSampleRateIndex(int idx)
{
    if (!m_session || m_streaming)
    {
        if (m_streaming)
            emit errorOccurred(QStringLiteral("Stop streaming before changing sample rate"));
        return;
    }
    const int result = m_session->setSamplerate(
        static_cast<uint8_t>(idx),
        [this, idx](bool ok, ApplicationProtocol::Error err)
        {
            if (ok && idx >= 0 && idx < ApplicationProtocol::kSampleRateCount)
                m_sampleRateHz = ApplicationProtocol::kSampleRateHz[idx];
            emit commandAck(0x11, ok, static_cast<int>(err));
        });
    if (result < 0)
        emit commandAck(0x11, false, -1);
}

void QtDeviceSessionAdapter::setGain(int channel, int gainCode)
{
    if (!m_session || m_streaming)
    {
        if (m_streaming)
            emit errorOccurred(QStringLiteral("Stop streaming before changing gain"));
        return;
    }
    const int result = m_session->setGain(
        uint8_t(channel), uint8_t(gainCode),
        [this](bool ok, ApplicationProtocol::Error e)
        { emit commandAck(0x30, ok, int(e)); });
    if (result < 0)
        emit commandAck(0x30, false, -1);
}
void QtDeviceSessionAdapter::setShortInput()
{
    if (!m_session || m_streaming)
    {
        if (m_streaming)
            emit errorOccurred(QStringLiteral("Stop streaming before changing MUX mode"));
        return;
    }
    const int result = m_session->setShortInput(
        [this](bool ok, ApplicationProtocol::Error e)
        {
            if (ok)
            {
                // InputShort applies globally and excludes the test signal.
                m_shortInputEnabled = true;
                m_testSignalEnabled = false;
                m_testModeEnabled = true;
                emit muxTestChanged(true);
                emit testSignalChanged(false);
                emit testModeChanged(true);
            }
            emit commandAck(0x31, ok, int(e));
        });
    if (result < 0)
        emit commandAck(0x31, false, -1);
}

void QtDeviceSessionAdapter::setNormalInput()
{
    if (!m_session || m_streaming)
    {
        if (m_streaming)
            emit errorOccurred(QStringLiteral("Stop streaming before changing MUX mode"));
        return;
    }
    const int result = m_session->setNormalInput(
        [this](bool ok, ApplicationProtocol::Error e)
        {
            if (ok)
            {
                m_shortInputEnabled = false;
                m_testSignalEnabled = false;
                m_testModeEnabled = false;
                emit muxTestChanged(false);
                emit testSignalChanged(false);
                emit testModeChanged(false);
            }
            emit commandAck(0x33, ok, int(e));
        });
    if (result < 0)
        emit commandAck(0x33, false, -1);
}
void QtDeviceSessionAdapter::setTestSignal(int amplitude, int frequency)
{
    if (!m_session || m_streaming)
    {
        if (m_streaming)
            emit errorOccurred(QStringLiteral("Stop streaming before changing MUX mode"));
        return;
    }
    const int result = m_session->setTestSignal(uint8_t(amplitude), uint8_t(frequency), [this](bool ok, ApplicationProtocol::Error e)
                                                {
        if(ok){
            // Both amplitudes select the internal test MUX; amplitude only
            // changes the generated signal level. This mode excludes short.
            m_testSignalEnabled = true;
            m_shortInputEnabled = false;
            m_testModeEnabled = true;
            emit testSignalChanged(true);
            emit muxTestChanged(false);
            emit testModeChanged(true);
        }
        emit commandAck(0x32,ok,int(e)); });
    if (result < 0)
        emit commandAck(0x32, false, -1);
}

void QtDeviceSessionAdapter::startStream(const QVector<quint8> &channels)
{
    if (!m_session)
        return;
    std::vector<uint8_t> ch;
    ch.reserve(channels.size());
    for (quint8 c : channels)
        ch.push_back(c);
    const bool wasPlaying = !m_phantom || m_phantom->isPlaying();
    m_session->startStream(ch,
                           [this, wasPlaying](bool ok, ApplicationProtocol::Error err)
                           {
                               // PhantomDevice starts playback whenever a new
                               // channel set is applied. Preserve an existing
                               // paused state when the user only changed the
                               // channel selection.
                               if (ok && m_phantom && !wasPlaying)
                                   m_phantom->pause();
                               if (ok)
                                   m_streaming = true;
                               emit commandAck(0x01, ok, static_cast<int>(err));
                           });
}

void QtDeviceSessionAdapter::stopStream()
{
    if (!m_session)
        return;
    m_session->stopStream(
        [this](bool ok, ApplicationProtocol::Error err)
        { if (ok) m_streaming = false; emit commandAck(0x02, ok, static_cast<int>(err)); });
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
        infos.push_back(ci);
    }
    if (!m_session->startRecording(path.toStdString(), description.toStdString(), infos))
    {
        emit errorOccurred(QStringLiteral("Failed to start recording: %1").arg(path));
        return;
    }
}

void QtDeviceSessionAdapter::stopRecording()
{
    if (m_session)
        m_session->stopRecording();
}

void QtDeviceSessionAdapter::play()
{
    if (m_phantom)
        m_phantom->play();
}

void QtDeviceSessionAdapter::pause()
{
    if (m_phantom)
        m_phantom->pause();
}

void QtDeviceSessionAdapter::seekToSample(quint64 index)
{
    if (m_phantom)
        m_phantom->seekToSample(index);
}

void QtDeviceSessionAdapter::setPlaybackSpeed(double factor)
{
    if (m_phantom)
        m_phantom->setSpeed(factor);
}
