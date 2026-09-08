#include "core/phantomdevice.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr uint8_t kStart = 0xAA;
    constexpr uint8_t kEsc = 0xBB;

    // Application command IDs (mirrors ApplicationProtocol::Cmd).
    constexpr uint8_t kStartStream = 0x01;
    constexpr uint8_t kStopStream = 0x02;
    constexpr uint8_t kSetVref = 0x10;
    constexpr uint8_t kSetSamplerate = 0x11;
    constexpr uint8_t kGetDeviceInfo = 0x20;
    constexpr uint8_t kSetGain = 0x30;
    constexpr uint8_t kSetShortInput = 0x31;
    constexpr uint8_t kSetTestSignal = 0x32;
    constexpr uint8_t kNormalInput = 0x33;

    // Frame types.
    constexpr uint8_t kResponse = 0xDD;
    constexpr uint8_t kError = 0xEE;
    constexpr uint8_t kPush = 0xFF;

    constexpr int kSampleRateHz[5] = {250, 500, 1000, 4000, 2000};

    void encodeI24(std::vector<uint8_t> &out, int32_t v)
    {
        const uint32_t u = static_cast<uint32_t>(v) & 0xFFFFFF;
        out.push_back(static_cast<uint8_t>(u & 0xFF));
        out.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
    }
} // namespace

uint16_t PhantomDevice::crc16Modbus(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

bool PhantomDevice::loadRecording(const std::string &path)
{
    if (!m_reader.open(path))
        return false;
    m_sampleRate = static_cast<int>(m_reader.header().sampleRate);
    m_playIndex = 0;
    m_sampleAccumulator = 0.0;
    return true;
}

int PhantomDevice::open()
{
    m_open = true;
    return 0;
}

int PhantomDevice::close()
{
    m_open = false;
    m_streaming = false;
    m_playing = false;
    return 0;
}

int PhantomDevice::write(const uint8_t *buf, int length)
{
    if (!m_open)
        return -1;
    for (int i = 0; i < length; ++i)
        feedByte(buf[i]);
    return length;
}

int PhantomDevice::read(uint8_t *buf, int length)
{
    if (!m_open)
        return -1;
    std::lock_guard<std::mutex> lock(m_txMutex);
    int n = 0;
    while (!m_txQueue.empty() && n < length)
    {
        buf[n++] = m_txQueue.front();
        m_txQueue.pop_front();
    }
    return n;
}

void PhantomDevice::feedByte(uint8_t byte)
{
    if (m_state == RxState::WaitStart)
    {
        if (byte == kStart)
        {
            m_state = RxState::Type;
            m_escape = false;
            m_rxPayload.clear();
            m_crcBuf.clear();
        }
        return;
    }

    switch (m_state)
    {
    case RxState::Type:
        m_rxType = byte;
        m_crcBuf.clear();
        m_crcBuf.push_back(byte);
        m_state = RxState::Seq;
        break;
    case RxState::Seq:
        m_rxSeq = byte;
        m_crcBuf.push_back(byte);
        m_state = RxState::LenLow;
        break;
    case RxState::LenLow:
        m_rxLen = byte;
        m_crcBuf.push_back(byte);
        m_state = RxState::LenHigh;
        break;
    case RxState::LenHigh:
        m_rxLen |= static_cast<uint16_t>(byte) << 8;
        m_crcBuf.push_back(byte);
        m_rxPayload.clear();
        m_state = (m_rxLen == 0) ? RxState::CrcLow : RxState::Payload;
        break;
    case RxState::Payload:
        if (m_escape)
        {
            m_escape = false;
            m_rxPayload.push_back(byte);
            m_crcBuf.push_back(byte);
        }
        else if (byte == kEsc)
        {
            m_escape = true;
        }
        else if (byte == kStart)
        {
            m_state = RxState::Type;
            m_escape = false;
            m_rxPayload.clear();
            m_crcBuf.clear();
            return;
        }
        else
        {
            m_rxPayload.push_back(byte);
            m_crcBuf.push_back(byte);
        }
        if (!m_escape && m_rxPayload.size() == m_rxLen)
            m_state = RxState::CrcLow;
        break;
    case RxState::CrcLow:
        if (m_escape)
        {
            m_escape = false;
            m_rxCrc = byte;
            m_state = RxState::CrcHigh;
        }
        else if (byte == kEsc)
        {
            m_escape = true;
        }
        else
        {
            m_rxCrc = byte;
            m_state = RxState::CrcHigh;
        }
        break;
    case RxState::CrcHigh:
        if (m_escape)
        {
            m_escape = false;
            m_rxCrc |= static_cast<uint16_t>(byte) << 8;
        }
        else if (byte == kEsc)
        {
            m_escape = true;
            break;
        }
        else
        {
            m_rxCrc |= static_cast<uint16_t>(byte) << 8;
        }
        {
            const uint16_t expected = crc16Modbus(m_crcBuf.data(), static_cast<int>(m_crcBuf.size()));
            if (expected == m_rxCrc && m_rxType == 0xCC)
                handleCommand(m_rxPayload.data(), static_cast<int>(m_rxPayload.size()));
        }
        m_state = RxState::WaitStart;
        m_escape = false;
        break;
    default:
        m_state = RxState::WaitStart;
        break;
    }
}

void PhantomDevice::handleCommand(const uint8_t *payload, int len)
{
    if (len < 1)
    {
        queueError(m_rxSeq, 0x06); // bad payload size
        return;
    }

    const uint8_t cmd = payload[0];
    switch (cmd)
    {
    case kStartStream:
    {
        if (len < 2)
        {
            queueError(m_rxSeq, 0x06);
            return;
        }
        const bool wasPlaying = m_playing;
        const bool wasStreaming = m_streaming;
        const uint8_t n = payload[1];
        if (n == 0 || n > 8 || len != 2 + n)
        {
            queueError(m_rxSeq, 0x02);
            return;
        }
        std::vector<uint8_t> chans;
        for (int i = 0; i < n; ++i)
        {
            const uint8_t ch = payload[2 + i];
            if (ch > 7 || (!chans.empty() && ch <= chans.back()))
            {
                queueError(m_rxSeq, 0x02);
                return;
            }
            chans.push_back(ch);
        }
        m_activeChannels = chans;
        m_streaming = true;
        // The first stream command starts a loaded recording. Re-applying a
        // channel selection while already streaming preserves an explicit
        // paused state (important when the user toggles channels during
        // playback).
        m_playing = wasStreaming ? wasPlaying : true;
        m_sampleAccumulator = 0.0;
        queueResponseOk(m_rxSeq);
        break;
    }
    case kStopStream:
        m_streaming = false;
        queueResponseOk(m_rxSeq);
        break;
    case kSetVref:
    {
        if (len != 3)
        {
            queueError(m_rxSeq, 0x06);
            return;
        }
        const uint16_t mV = payload[1] | (static_cast<uint16_t>(payload[2]) << 8);
        if (mV > 3300)
        {
            queueError(m_rxSeq, 0x02);
            return;
        }
        queueResponseOk(m_rxSeq);
        break;
    }
    case kSetSamplerate:
    {
        if (len != 2)
        {
            queueError(m_rxSeq, 0x06);
            return;
        }
        if (payload[1] >= 5)
        {
            queueError(m_rxSeq, 0x02);
            return;
        }
        m_sampleRate = kSampleRateHz[payload[1]];
        queueResponseOk(m_rxSeq);
        break;
    }
    case kSetGain:
        if (m_streaming || len != 3 || payload[1] > 7 || payload[2] > 6)
            queueError(m_rxSeq, 0x02);
        else
            queueResponseOk(m_rxSeq);
        break;
    case kSetShortInput:
        if (len != 1)
            queueError(m_rxSeq, 0x06);
        else if (m_streaming)
            queueError(m_rxSeq, 0x02);
        else
        {
            m_inputMode = kSetShortInput;
            queueResponseOk(m_rxSeq);
        }
        break;
    case kSetTestSignal:
        if (len != 3)
            queueError(m_rxSeq, 0x06);
        else if (m_streaming || payload[1] > 1 ||
                 (payload[2] != 0 && payload[2] != 1))
            queueError(m_rxSeq, 0x02);
        else
        {
            m_inputMode = kSetTestSignal;
            queueResponseOk(m_rxSeq);
        }
        break;
    case kNormalInput:
        if (len != 1)
            queueError(m_rxSeq, 0x06);
        else if (m_streaming)
            queueError(m_rxSeq, 0x02);
        else
        {
            m_inputMode = kNormalInput;
            queueResponseOk(m_rxSeq);
        }
        break;
    case kGetDeviceInfo:
    {
        // Report the recording's geometry so the app treats the phantom like
        // the real device it was captured from.
        const auto &hdr = m_reader.header();
        const std::string desc = "Playback";
        std::vector<uint8_t> payload;
        payload.push_back(0x01); // device_id L (ECG/ADC front-end)
        payload.push_back(0x00); // device_id H
        payload.push_back(0x01); // fw major
        payload.push_back(0x00); // fw minor
        payload.push_back(static_cast<uint8_t>(hdr.channels.size()));
        payload.push_back(static_cast<uint8_t>(desc.size()));
        for (char c : desc)
            payload.push_back(static_cast<uint8_t>(c));
        queueFrame(kResponse, m_rxSeq, payload);
        break;
    }

    default:
        queueError(m_rxSeq, 0x01); // unknown command
        break;
    }
}

void PhantomDevice::queueResponseOk(uint8_t seq)
{
    queueFrame(kResponse, seq, {});
}

void PhantomDevice::queueError(uint8_t seq, uint8_t code)
{
    queueFrame(kError, seq, {code});
}

void PhantomDevice::queueFrame(uint8_t type, uint8_t seq, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> crcRegion;
    crcRegion.push_back(type);
    crcRegion.push_back(seq);
    crcRegion.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
    crcRegion.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
    for (uint8_t b : payload)
        crcRegion.push_back(b);
    const uint16_t crc = crc16Modbus(crcRegion.data(), static_cast<int>(crcRegion.size()));

    std::vector<uint8_t> frame;
    frame.push_back(kStart);
    frame.push_back(type);
    frame.push_back(seq);
    frame.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
    frame.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
    auto esc = [&](uint8_t b)
    {
        if (b == kStart || b == kEsc)
            frame.push_back(kEsc);
        frame.push_back(b);
    };
    for (uint8_t b : payload)
        esc(b);
    esc(static_cast<uint8_t>(crc & 0xFF));
    esc(static_cast<uint8_t>((crc >> 8) & 0xFF));

    std::lock_guard<std::mutex> lock(m_txMutex);
    for (uint8_t b : frame)
        m_txQueue.push_back(b);
}

void PhantomDevice::seekToSample(uint64_t index)
{
    m_playIndex = std::min<uint64_t>(index, totalSamples());
    m_sampleAccumulator = 0.0;
}

void PhantomDevice::pump(double elapsedMs)
{
    if (!m_streaming || !m_playing || !m_reader.isOpen() || m_activeChannels.empty())
        return;

    const uint64_t total = totalSamples();
    if (m_playIndex >= total)
    {
        m_playing = false; // reached end
        return;
    }

    // How many samples of recorded time to advance.
    m_sampleAccumulator += (elapsedMs / 1000.0) * m_sampleRate * m_speed;
    uint64_t toEmit = static_cast<uint64_t>(m_sampleAccumulator);
    if (toEmit == 0)
        return;
    m_sampleAccumulator -= static_cast<double>(toEmit);

    if (m_playIndex + toEmit > total)
        toEmit = total - m_playIndex;

    // Firmware contract: one Push carries exactly one complete sample set.
    uint64_t remaining = toEmit;
    while (remaining > 0)
    {
        emitPushForSamples(m_playIndex, 1);
        ++m_playIndex;
        --remaining;
    }
}

void PhantomDevice::emitPushForSamples(uint64_t startIndex, size_t count)
{
    // Read `count` interleaved sample sets from the recording.
    std::vector<int32_t> interleaved;
    const size_t got = m_reader.readSampleSets(startIndex, count, interleaved);
    if (got == 0)
        return;

    const auto &recChannels = m_reader.header().channels;
    const int recCh = static_cast<int>(recChannels.size());

    // Map each active (requested) physical channel to its column in the
    // recording. If the recording lacks that physical channel, emit zeros.
    std::vector<int> colOf(m_activeChannels.size(), -1);
    for (size_t a = 0; a < m_activeChannels.size(); ++a)
    {
        for (int c = 0; c < recCh; ++c)
        {
            if (recChannels[c].physIndex == m_activeChannels[a])
            {
                colOf[a] = c;
                break;
            }
        }
    }

    std::vector<uint8_t> payload;
    payload.reserve(got * m_activeChannels.size() * 3);
    for (size_t s = 0; s < got; ++s)
    {
        for (size_t a = 0; a < m_activeChannels.size(); ++a)
        {
            int32_t v = 0;
            if (colOf[a] >= 0)
                v = interleaved[s * recCh + colOf[a]];
            encodeI24(payload, v);
        }
    }

    queueFrame(kPush, m_pushSeq++, payload);
}
