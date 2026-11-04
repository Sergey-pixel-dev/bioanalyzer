#include "exchange/applicationprotocol.h"
#include "exchange/transportprotocol.h"

#include <algorithm>

constexpr int ApplicationProtocol::kSampleRateHz[5];
constexpr int ApplicationProtocol::kGainValues[7];
constexpr uint8_t ApplicationProtocol::kTestFrequencies[2];

const char *ApplicationProtocol::errorText(uint8_t code)
{
    switch (static_cast<Error>(code))
    {
    case Error::None:
        return "OK";
    case Error::UnknownCommand:
        return "Unknown command";
    case Error::BadParams:
        return "Bad parameters";
    case Error::NotReady:
        return "Device not ready";
    case Error::HardwareFault:
        return "Hardware fault";
    case Error::CrcMismatch:
        return "CRC mismatch";
    case Error::BadPayloadSize:
        return "Bad payload size";
    default:
        return "Unknown error";
    }
}

ApplicationProtocol::ApplicationProtocol(TransportProtocol *transport)
{
    setTransport(transport);
}

void ApplicationProtocol::setTransport(TransportProtocol *transport)
{
    m_transport = transport;
    if (m_transport)
    {
        m_transport->setPushHandler(
            [this](const uint8_t *payload, int len, uint8_t seq)
            { onPush(payload, len, seq); });
    }
}

int32_t ApplicationProtocol::decodeSample(const uint8_t *p)
{
    int32_t raw = static_cast<int32_t>(p[0]) |
                  (static_cast<int32_t>(p[1]) << 8) |
                  (static_cast<int32_t>(p[2]) << 16);
    if (raw & 0x800000)
        raw -= 0x1000000;
    return raw;
}

namespace
{
    // Adapt a typed AckHandler to a transport ResponseHandler.
    TransportProtocol::ResponseHandler makeResultAdapter(ApplicationProtocol::AckHandler onAck)
    {
        if (!onAck)
            return {};
        return [onAck = std::move(onAck)](bool ok, const uint8_t *payload, int len)
        {
            if (ok)
            {
                onAck(true, ApplicationProtocol::Error::None);
            }
            else
            {
                ApplicationProtocol::Error err = ApplicationProtocol::Error::None;
                if (len >= 1)
                    err = static_cast<ApplicationProtocol::Error>(payload[0]);
                onAck(false, err);
            }
        };
    }
} // namespace

int ApplicationProtocol::setChannels(const std::vector<uint8_t> &channels, AckHandler onAck)
{
    if (m_transport == nullptr)
        return -1;
    if (channels.empty() || channels.size() > kMaxChannels)
        return -1;

    // Validate: sorted ascending, unique, within 0..7.
    for (size_t i = 0; i < channels.size(); ++i)
    {
        if (channels[i] > 7)
            return -1;
        if (i > 0 && channels[i] <= channels[i - 1])
            return -1;
    }

    std::vector<uint8_t> payload;
    payload.reserve(2 + channels.size());
    payload.push_back(static_cast<uint8_t>(Cmd::SetChannels));
    payload.push_back(static_cast<uint8_t>(channels.size()));
    for (uint8_t ch : channels)
        payload.push_back(ch);

    auto inner = makeResultAdapter(std::move(onAck));
    auto self = this;
    auto selected = channels;
    return m_transport->sendCommand(
        payload.data(), static_cast<int>(payload.size()),
        [self, selected, inner](bool ok, const uint8_t *p, int len)
        {
            // Набор запоминается только по подтверждению устройства: иначе
            // разбор Push-пакетов шёл бы по набору, который не применён.
            if (ok)
                self->m_selectedChannels = selected;
            if (inner)
                inner(ok, p, len);
        });
}

int ApplicationProtocol::startStream(AckHandler onAck)
{
    if (m_transport == nullptr)
        return -1;
    if (m_selectedChannels.empty())
        return -1;

    uint8_t payload = static_cast<uint8_t>(Cmd::StartStream);
    auto inner = makeResultAdapter(std::move(onAck));
    auto self = this;
    return m_transport->sendCommand(
        &payload, 1,
        [self, inner](bool ok, const uint8_t *p, int len)
        {
            if (ok)
                self->m_activeChannels = self->m_selectedChannels;
            if (inner)
                inner(ok, p, len);
        });
}

int ApplicationProtocol::stopStream(AckHandler onAck)
{
    if (m_transport == nullptr)
        return -1;
    uint8_t payload = static_cast<uint8_t>(Cmd::StopStream);
    auto inner = makeResultAdapter(std::move(onAck));
    auto self = this;
    return m_transport->sendCommand(
        &payload, 1,
        [self, inner](bool ok, const uint8_t *p, int len)
        {
            // Выбранный набор — постоянная настройка устройства и остановку
            // потока переживает; очищается только текущий активный набор.
            if (ok)
                self->m_activeChannels.clear();
            if (inner)
                inner(ok, p, len);
        });
}

int ApplicationProtocol::setSamplerate(uint8_t idx, AckHandler onAck)
{
    if (m_transport == nullptr)
        return -1;
    if (idx >= kSampleRateCount)
        return -1;
    uint8_t payload[2] = {static_cast<uint8_t>(Cmd::SetSamplerate), idx};
    return m_transport->sendCommand(payload, 2, makeResultAdapter(std::move(onAck)));
}

int ApplicationProtocol::setGain(uint8_t channel, uint8_t gainCode, AckHandler onAck)
{
    if (!m_transport || channel > 7 || gainCode > 6)
        return -1;
    uint8_t payload[3] = {static_cast<uint8_t>(Cmd::SetGain), channel, gainCode};
    return m_transport->sendCommand(payload, 3, makeResultAdapter(std::move(onAck)));
}

int ApplicationProtocol::setShortInput(AckHandler onAck)
{
    if (!m_transport)
        return -1;
    const uint8_t payload = static_cast<uint8_t>(Cmd::SetShortInput);
    return m_transport->sendCommand(&payload, 1, makeResultAdapter(std::move(onAck)));
}

int ApplicationProtocol::setNormalInput(AckHandler onAck)
{
    if (!m_transport)
        return -1;
    const uint8_t payload = static_cast<uint8_t>(Cmd::NormalInput);
    return m_transport->sendCommand(&payload, 1, makeResultAdapter(std::move(onAck)));
}

int ApplicationProtocol::setTestSignal(uint8_t amplitude, uint8_t frequency, AckHandler onAck)
{
    if (!m_transport || amplitude > 1 || (frequency != 0 && frequency != 1))
        return -1;
    uint8_t payload[3] = {static_cast<uint8_t>(Cmd::SetTestSignal), amplitude, frequency};
    return m_transport->sendCommand(payload, 3, makeResultAdapter(std::move(onAck)));
}

bool ApplicationProtocol::decodeDeviceInfo(const uint8_t *payload, int len, DeviceInfo &out)
{
    // [id_L][id_H][fw_major][fw_minor][n_channels][desc_len][desc...]
    if (len < 6)
        return false;
    out.deviceId = static_cast<uint16_t>(payload[0]) |
                   (static_cast<uint16_t>(payload[1]) << 8);
    out.fwMajor = payload[2];
    out.fwMinor = payload[3];
    out.channelCount = payload[4];
    const int descLen = payload[5];
    if (len < 6 + descLen)
        return false;
    out.description.assign(reinterpret_cast<const char *>(payload + 6), descLen);
    return true;
}

int ApplicationProtocol::getDeviceInfo(DeviceInfoHandler onInfo)
{
    if (m_transport == nullptr)
        return -1;
    uint8_t payload = static_cast<uint8_t>(Cmd::GetDeviceInfo);
    return m_transport->sendCommand(
        &payload, 1,
        [onInfo = std::move(onInfo)](bool ok, const uint8_t *p, int len)
        {
            if (!onInfo)
                return;
            DeviceInfo info;
            if (ok && decodeDeviceInfo(p, len, info))
                onInfo(true, info);
            else
                onInfo(false, info);
        });
}

void ApplicationProtocol::onPush(const uint8_t *payload, int len, uint8_t seq)
{
    if (!m_sampleHandler || m_activeChannels.empty())
        return;

    const int n = static_cast<int>(m_activeChannels.size());
    const int bytesPerSampleSet = n * 3;
    // Firmware emits exactly one complete ADC conversion per Push.
    if (bytesPerSampleSet == 0 || len != bytesPerSampleSet)
    {
        // Malformed: dropped, but counted so that a stream of nothing but
        // bad frames is not taken for a live link.
        if (m_malformedPushHandler)
            m_malformedPushHandler();
        return;
    }

    SampleFrame frame;
    frame.pushSequence = seq;
    frame.channels = m_activeChannels;
    frame.samples.assign(n, std::vector<int32_t>());
    for (int c = 0; c < n; ++c)
        frame.samples[c].reserve(1);

    int offset = 0;
    for (int c = 0; c < n; ++c)
    {
        frame.samples[c].push_back(decodeSample(payload + offset));
        offset += 3;
    }

    m_sampleHandler(frame);
}
