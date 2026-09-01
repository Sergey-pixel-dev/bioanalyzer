#include "core/devicesession.h"
#include "core/itransport.h"
#include "core/device.h"

#include <algorithm>
#include <chrono>
#include <dirent.h>
#include <thread>

std::vector<DeviceSession::PortInfo> DeviceSession::discoverPorts()
{
    std::vector<PortInfo> ports;

    DIR *dir = ::opendir("/dev");
    if (dir == nullptr)
        return ports;

    struct dirent *entry;
    while ((entry = ::readdir(dir)) != nullptr)
    {
        const std::string name = entry->d_name;
        const bool isUsb = name.rfind("ttyUSB", 0) == 0;
        const bool isAcm = name.rfind("ttyACM", 0) == 0;
        if (!isUsb && !isAcm)
            continue;

        PortInfo info;
        info.path = "/dev/" + name;
        info.description = isUsb ? "USB serial" : "USB CDC (ACM)";
        ports.push_back(std::move(info));
    }
    ::closedir(dir);

    std::sort(ports.begin(), ports.end(),
              [](const PortInfo &a, const PortInfo &b)
              { return a.path < b.path; });
    return ports;
}

DeviceSession::ProbeResult DeviceSessionfprobePortDetailed(const std::string &portPath,
                                                           int baud, int timeoutMs,
                                                           int maxAttempts)
{
    ProbeResult result;
    maxAttempts = std::max(1, maxAttempts);
    timeoutMs = std::max(1, timeoutMs);

    Device dev(portPath, baud);
    if (dev.open() < 0)
    {
        result.status = ProbeStatus::OpenFailed;
        return result;
    }

    for (int attempt = 1; attempt <= maxAttempts; ++attempt)
    {
        result.attempts = attempt;
        dev.flushInput();

        // A fresh parser/pending-command table isolates each retry from late
        // responses or a partial frame left by the previous attempt.
        TransportProtocol transport(&dev);
        bool completed = false;
        bool valid = false;
        bool protocolError = false;
        bool transportFailure = false;
        uint8_t errorCode = 0;
        bool crcError = false;
        transport.setParseErrorHandler([&](TransportProtocol::ParseError error)
                                       {
                                           if (error == TransportProtocol::ParseError::CrcMismatch)
                                               crcError = true; });

        const uint8_t command = static_cast<uint8_t>(EcgAdcProtocol::Cmd::GetDeviceInfo);
        const int sendResult = transport.sendCommand(&command, 1,
                                                     [&](bool ok, const uint8_t *payload, int len)
                                                     {
                                                         completed = true;
                                                         if (!ok)
                                                         {
                                                             protocolError = true;
                                                             if (len > 0)
                                                                 errorCode = payload[0];
                                                             return;
                                                         }
                                                         EcgAdcProtocol::DeviceInfo info;
                                                         if (EcgAdcProtocol::decodeDeviceInfo(payload, len, info) &&
                                                             info.channelCount > 0 &&
                                                             info.channelCount <= EcgAdcProtocol::kMaxChannels)
                                                         {
                                                             result.info = std::move(info);
                                                             valid = true;
                                                         }
                                                     });
        if (sendResult < 0)
        {
            transportFailure = true;
            result.status = ProbeStatus::TransportError;
            break;
        }

        const auto start = std::chrono::steady_clock::now();
        while (!completed && std::chrono::steady_clock::now() - start <
                                 std::chrono::milliseconds(timeoutMs))
        {
            const int polled = transport.poll();
            if (polled < 0)
            {
                transportFailure = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        if (valid)
        {
            result.status = ProbeStatus::Success;
            result.errorCode = 0;
            dev.close();
            return result;
        }

        if (protocolError)
        {
            result.status = ProbeStatus::ProtocolError;
            result.errorCode = errorCode;
            dev.close();
            return result;
        }

        result.status = transportFailure || crcError ? ProbeStatus::TransportError
                                                     : (completed ? ProbeStatus::InvalidResponse
                                                                  : ProbeStatus::Timeout);
    }

    dev.close();
    return result;
}

bool DeviceSession::probePort(const std::string &portPath, int baud, int timeoutMs,
                              EcgAdcProtocol::DeviceInfo &out)
{
    const ProbeResult result = probePortDetailed(portPath, baud, timeoutMs, 1);
    if (!result.ok())
        return false;
    out = result.info;
    return true;
}

std::vector<DeviceSession::DiscoveredDevice>
DeviceSession::scanDevices(int baud, int timeoutMs, int maxAttempts)
{
    std::vector<DiscoveredDevice> found;
    for (const PortInfo &port : discoverPorts())
    {
        const ProbeResult result = probePortDetailed(port.path, baud, timeoutMs, maxAttempts);
        if (!result.ok())
            continue;

        DiscoveredDevice device;
        device.port = port.path;
        device.baud = baud;
        device.info = result.info;
        found.push_back(std::move(device));
    }
    return found;
}

DeviceSession::DeviceSession(ITransport *transport, bool ownTransport)
    : m_transport(transport),
      m_ownTransport(ownTransport),
      m_transportProto(transport),
      m_app(&m_transportProto)
{
    m_app.setSampleHandler(
        [this](const EcgAdcProtocol::SampleFrame &frame)
        { onSamples(frame); });
}

DeviceSession::~DeviceSession()
{
    stopRecording();
    if (m_ownTransport)
        delete m_transport;
}

void DeviceSession::setBufferCapacity(size_t samplesPerChannel)
{
    m_bufferCapacity = samplesPerChannel;
}

bool DeviceSession::open()
{
    if (m_transport == nullptr)
        return false;
    return m_transport->open() >= 0;
}

void DeviceSession::close()
{
    if (m_transport)
        m_transport->close();
}

bool DeviceSession::isOpen() const
{
    return m_transport && m_transport->isOpen();
}

int DeviceSession::poll()
{
    return m_transportProto.poll();
}

int DeviceSession::startStream(const std::vector<uint8_t> &channels, EcgAdcProtocol::AckHandler onAck)
{
    // Size the rolling buffer to the active channel count before streaming.
    m_buffer.configure(static_cast<int>(channels.size()), m_bufferCapacity);
    return m_app.startStream(channels, std::move(onAck));
}

int DeviceSession::stopStream(EcgAdcProtocol::AckHandler onAck)
{
    return m_app.stopStream(std::move(onAck));
}

int DeviceSession::setVref(uint16_t mV, EcgAdcProtocol::AckHandler onAck)
{
    // Kept as a compatibility pass-through for legacy callers.  The ADS1298
    // reference is fixed internally; normal UI code never calls this method.
    return m_app.setVref(mV, std::move(onAck));
}

int DeviceSession::setSamplerate(uint8_t idx, EcgAdcProtocol::AckHandler onAck)
{
    auto ack = [this, idx, onAck = std::move(onAck)](bool ok, EcgAdcProtocol::Error err)
    {
        if (ok && idx < 4)
            m_sampleRateHz = EcgAdcProtocol::kSampleRateHz[idx];
        if (onAck)
            onAck(ok, err);
    };
    return m_app.setSamplerate(idx, std::move(ack));
}

int DeviceSession::setGain(uint8_t channel, uint8_t gainCode, EcgAdcProtocol::AckHandler onAck)
{
    return m_app.setGain(channel, gainCode, std::move(onAck));
}

int DeviceSession::setShortInput(bool enable, EcgAdcProtocol::AckHandler onAck)
{
    return m_app.setShortInput(enable, std::move(onAck));
}

int DeviceSession::setNormalInput(EcgAdcProtocol::AckHandler onAck)
{
    return m_app.setNormalInput(std::move(onAck));
}

int DeviceSession::setTestSignal(uint8_t amplitude, uint8_t frequency,
                                 EcgAdcProtocol::AckHandler onAck)
{
    return m_app.setTestSignal(amplitude, frequency, std::move(onAck));
}

int DeviceSession::getDeviceInfo(EcgAdcProtocol::DeviceInfoHandler onInfo)
{
    return m_app.getDeviceInfo(std::move(onInfo));
}

bool DeviceSession::startRecording(const std::string &path, const std::string &description,
                                   const std::vector<ChannelInfo> &channels)
{
    if (isRecording())
        return false;

    m_writer = std::make_unique<SessionWriter>();
    SessionHeader header;
    header.version = 1;
    header.sampleRate = static_cast<uint32_t>(m_sampleRateHz);
    header.vrefMv = EcgAdcProtocol::kFixedReferenceVoltageMv;
    header.description = description;
    header.channels = channels;

    if (!m_writer->open(path, header))
    {
        m_writer.reset();
        return false;
    }
    m_recordingChannels = channels;
    return true;
}

void DeviceSession::stopRecording()
{
    if (m_writer)
    {
        m_writer->close();
        m_writer.reset();
    }
    m_recordingChannels.clear();
}

void DeviceSession::onSamples(const EcgAdcProtocol::SampleFrame &frame)
{
    // Push into the rolling buffer (channel-major -> per sample set).
    m_buffer.pushBlock(frame.samples);

    // Persist if recording. Channel selection can change while a recording is
    // active, so map each incoming physical channel into the fixed recording
    // geometry instead of dropping the block on a size mismatch.
    if (m_writer && m_writer->isOpen())
    {
        std::vector<std::vector<int32_t>> recorded(m_recordingChannels.size());
        for (size_t rc = 0; rc < m_recordingChannels.size(); ++rc)
        {
            const uint8_t phys = m_recordingChannels[rc].physIndex;
            for (size_t fc = 0; fc < frame.channels.size(); ++fc)
                if (frame.channels[fc] == phys)
                {
                    recorded[rc] = frame.samples[fc];
                    break;
                }
            if (recorded[rc].empty() && !frame.samples.empty())
                recorded[rc].assign(frame.samples.front().size(), 0);
        }
        m_writer->writeBlock(recorded);
    }

    if (m_dataHub)
    {
        DataHub::Block block;
        block.firstSample = m_totalSamples;
        block.pushSequence = frame.pushSequence;
        block.sampleRateHz = m_sampleRateHz;
        block.channels = frame.channels;
        block.samples = frame.samples;
        m_dataHub->publish(std::move(block));
    }
    if (!frame.samples.empty())
        m_totalSamples += frame.samples.front().size();

    if (m_sampleCb)
        m_sampleCb(frame);
}
