#include "core/devicesession.h"
#include "core/itransport.h"
#include "core/device.h"

#include <algorithm>
#include <chrono>
#include <dirent.h>
#include <thread>
#include <stdexcept>
#include <cstddef>

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

DeviceSession::ProbeResult DeviceSession::probePortDetailed(const std::string &portPath,
                                                            int baud, int timeoutMs,
                                                            int maxAttempts)
{
    DeviceSession::ProbeResult result;
    maxAttempts = std::max(1, maxAttempts);
    timeoutMs = std::max(1, timeoutMs);

    Device dev(portPath, baud);
    if (dev.open() < 0)
    {
        result.status = DeviceSession::ProbeStatus::OpenFailed;
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

        const uint8_t command = static_cast<uint8_t>(ApplicationProtocol::Cmd::GetDeviceInfo);
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
                                                         ApplicationProtocol::DeviceInfo info;
                                                         if (ApplicationProtocol::decodeDeviceInfo(payload, len, info) &&
                                                             info.channelCount > 0 &&
                                                             info.channelCount <= ApplicationProtocol::kMaxChannels)
                                                         {
                                                             result.info = std::move(info);
                                                             valid = true;
                                                         }
                                                     });
        if (sendResult < 0)
        {
            transportFailure = true;
            result.status = DeviceSession::ProbeStatus::TransportError;
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
            result.status = DeviceSession::ProbeStatus::Success;
            result.errorCode = 0;
            dev.close();
            return result;
        }

        if (protocolError)
        {
            result.status = DeviceSession::ProbeStatus::ProtocolError;
            result.errorCode = errorCode;
            dev.close();
            return result;
        }

        result.status = transportFailure || crcError ? DeviceSession::ProbeStatus::TransportError
                                                     : (completed ? DeviceSession::ProbeStatus::InvalidResponse
                                                                  : DeviceSession::ProbeStatus::Timeout);
    }

    dev.close();
    return result;
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

DeviceSession::DeviceSession(ITransport *transport, DataHub *dataHub, bool ownTransport)
    : m_transport(transport),
      m_ownTransport(ownTransport),
      m_transportProto(transport),
      m_app(&m_transportProto),
      m_dataHub(dataHub)
{
    if (!m_dataHub)
        throw std::invalid_argument("DeviceSession requires a DataHub");
    m_app.setSampleHandler([this](const ApplicationProtocol::SampleFrame &frame)
                           { onSamples(frame); });
}

DeviceSession::~DeviceSession()
{
    // Do not drop a trailing sub-25 ms block when the session is torn down.
    flushPublishedSamples();
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
    flushPublishedSamples();
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

int DeviceSession::startStream(const std::vector<uint8_t> &channels, ApplicationProtocol::AckHandler onAck)
{
    // Channel geometry is fixed for a stream. Any trailing data belongs to the
    // previous stream and must be emitted before a new geometry is installed.
    flushPublishedSamples();
    resetPublishBatch();
    // Size the rolling buffer to the active channel count before streaming.
    m_buffer.configure(static_cast<int>(channels.size()), m_bufferCapacity);
    return m_app.startStream(channels, std::move(onAck));
}

int DeviceSession::stopStream(ApplicationProtocol::AckHandler onAck)
{
    // Flush immediately so callers that close the transport before the async
    // stop ACK still receive the final partial batch. The wrapped callback
    // flushes again after a successful ACK in case a final push arrived first.
    flushPublishedSamples();
    auto ack = [this, onAck = std::move(onAck)](bool ok, ApplicationProtocol::Error err)
    {
        if (ok)
            flushPublishedSamples();
        if (onAck)
            onAck(ok, err);
    };
    return m_app.stopStream(std::move(ack));
}

int DeviceSession::setSamplerate(uint8_t idx, ApplicationProtocol::AckHandler onAck)
{
    auto ack = [this, idx, onAck = std::move(onAck)](bool ok, ApplicationProtocol::Error err)
    {
        if (ok && idx < ApplicationProtocol::kSampleRateCount)
            m_sampleRateHz = ApplicationProtocol::kSampleRateHz[idx];
        if (onAck)
            onAck(ok, err);
    };
    return m_app.setSamplerate(idx, std::move(ack));
}

int DeviceSession::setGain(uint8_t channel, uint8_t gainCode, ApplicationProtocol::AckHandler onAck)
{
    return m_app.setGain(channel, gainCode, std::move(onAck));
}

int DeviceSession::setShortInput(ApplicationProtocol::AckHandler onAck)
{
    return m_app.setShortInput(std::move(onAck));
}

int DeviceSession::setNormalInput(ApplicationProtocol::AckHandler onAck)
{
    return m_app.setNormalInput(std::move(onAck));
}

int DeviceSession::setTestSignal(uint8_t amplitude, uint8_t frequency,
                                 ApplicationProtocol::AckHandler onAck)
{
    return m_app.setTestSignal(amplitude, frequency, std::move(onAck));
}

int DeviceSession::getDeviceInfo(ApplicationProtocol::DeviceInfoHandler onInfo)
{
    return m_app.getDeviceInfo(std::move(onInfo));
}

bool DeviceSession::startRecording(const std::string &path, const std::string &description,
                                   const std::vector<ChannelInfo> &channels)
{
    if (isRecording())
        return false;

    // A recording is always tied to the immutable geometry accepted by the
    // controller. Do not trust a page-provided list: it may contain display
    // channels or stale device metadata. The physical indices in the
    // application protocol are the source of truth.
    const auto &active = m_app.activeChannels();
    if (active.empty())
        return false;

    std::vector<ChannelInfo> recordedChannels;
    recordedChannels.reserve(active.size());
    for (uint8_t physical : active)
    {
        ChannelInfo info;
        info.physIndex = physical;
        info.enabled = true;
        for (const ChannelInfo &candidate : channels)
        {
            if (candidate.physIndex == physical)
            {
                info = candidate;
                info.physIndex = physical;
                info.enabled = true;
                break;
            }
        }
        recordedChannels.push_back(std::move(info));
    }

    m_writer = std::make_unique<SessionWriter>();
    SessionHeader header;
    header.version = 2;
    header.sampleRate = static_cast<uint32_t>(m_sampleRateHz);
    header.vrefMv = ApplicationProtocol::kFixedReferenceVoltageMv;
    header.description = description;
    header.channels = std::move(recordedChannels);

    if (!m_writer->open(path, header))
    {
        m_writer.reset();
        return false;
    }
    return true;
}

void DeviceSession::stopRecording()
{
    if (m_writer)
    {
        m_writer->close();
        m_writer.reset();
    }
}

void DeviceSession::resetPublishBatch()
{
    m_publishPending.clear();
    m_publishChannels.clear();
    m_publishPendingSamples = 0;
    m_publishFirstSample = m_totalSamples;
    m_publishLastSequence = 0;
}

void DeviceSession::publishPending(size_t count)
{
    if (count == 0 || count > m_publishPendingSamples || m_publishPending.empty())
        return;

    DataHub::Block block;
    block.firstSample = m_publishFirstSample;
    block.pushSequence = m_publishLastSequence;
    block.sampleRateHz = m_sampleRateHz;
    block.channels = m_publishChannels;
    block.samples.resize(m_publishPending.size());
    for (size_t channel = 0; channel < m_publishPending.size(); ++channel)
    {
        block.samples[channel].assign(m_publishPending[channel].begin(),
                                      m_publishPending[channel].begin() +
                                          static_cast<std::ptrdiff_t>(count));
        m_publishPending[channel].erase(
            m_publishPending[channel].begin(),
            m_publishPending[channel].begin() + static_cast<std::ptrdiff_t>(count));
    }
    m_dataHub->publish(std::move(block));
    m_publishPendingSamples -= count;
    m_publishFirstSample += count;
}

void DeviceSession::flushPublishedSamples()
{
    if (m_publishPendingSamples > 0)
        publishPending(m_publishPendingSamples);
}

void DeviceSession::onSamples(const ApplicationProtocol::SampleFrame &frame)
{
    if (frame.samples.empty() || frame.channels.empty())
        return;

    const size_t sampleCount = frame.samples.front().size();
    if (sampleCount == 0 || frame.samples.size() != frame.channels.size())
        return;
    if (frame.channels != m_app.activeChannels())
        return;
    for (const auto &channel : frame.samples)
        if (channel.size() != sampleCount)
            return;

    // Push into the rolling buffer (channel-major -> per sample set).
    m_buffer.pushBlock(frame.samples);

    // Persist the raw stream in the exact physical order selected by
    // StartStream. Display checkboxes never alter this data path.
    if (m_writer && m_writer->isOpen())
        m_writer->writeBlock(frame.channels, frame.samples);

    if (m_publishPending.empty())
    {
        m_publishChannels = frame.channels;
        m_publishPending.assign(frame.samples.size(), {});
        m_publishFirstSample = m_totalSamples;
    }
    // The stream geometry is immutable, so a changed frame shape is malformed
    // and must not be mixed into the pending DataHub block.
    if (frame.channels != m_publishChannels || frame.samples.size() != m_publishPending.size())
        return;

    for (size_t channel = 0; channel < frame.samples.size(); ++channel)
        m_publishPending[channel].insert(m_publishPending[channel].end(),
                                         frame.samples[channel].begin(),
                                         frame.samples[channel].end());
    m_publishLastSequence = frame.pushSequence;
    m_publishPendingSamples += sampleCount;
    m_totalSamples += sampleCount;

    const size_t target = std::max<size_t>(1,
                                           (static_cast<size_t>(std::max(1, m_sampleRateHz)) * 25 + 999) / 1000);
    while (m_publishPendingSamples >= target)
        publishPending(target);
}
