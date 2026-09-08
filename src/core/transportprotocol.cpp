#include "core/transportprotocol.h"
#include "core/itransport.h"

TransportProtocol::TransportProtocol(ITransport *transport)
    : m_transport(transport)
{
}

uint16_t TransportProtocol::crc16Modbus(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

int TransportProtocol::sendCommand(const uint8_t *payload, int len, ResponseHandler onResult)
{
    if (m_transport == nullptr || len < 0 || len > 0xFFFF)
        return -1;

    const uint8_t seq = m_curSeq;

    /* TODO: наверное можно обойтись без crc_Region и считать сразу по vector frame*/
    // CRC covers the unescaped [type][seq][len_L][len_H][payload].
    std::vector<uint8_t> crcRegion;
    crcRegion.reserve(4 + len);
    crcRegion.push_back(static_cast<uint8_t>(FrameType::Command));
    crcRegion.push_back(seq);
    crcRegion.push_back(static_cast<uint8_t>(len & 0xFF));
    crcRegion.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    for (int i = 0; i < len; ++i)
        crcRegion.push_back(payload[i]);

    const uint16_t crc = crc16Modbus(crcRegion.data(), static_cast<int>(crcRegion.size()));

    // Build the wire frame. Header (type, seq, len) is never escaped; payload
    // and CRC are escaped.
    std::vector<uint8_t> frame;
    frame.reserve(crcRegion.size() * 2 + 8);
    frame.push_back(kStartMarker);
    frame.push_back(static_cast<uint8_t>(FrameType::Command));
    frame.push_back(seq);
    frame.push_back(static_cast<uint8_t>(len & 0xFF));
    frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));

    auto pushEscaped = [&frame](uint8_t b)
    {
        if (b == kStartMarker || b == kEscMarker)
            frame.push_back(kEscMarker);
        frame.push_back(b);
    };

    for (int i = 0; i < len; ++i)
        pushEscaped(payload[i]);
    pushEscaped(static_cast<uint8_t>(crc & 0xFF));
    pushEscaped(static_cast<uint8_t>((crc >> 8) & 0xFF));

    m_pending[seq] = std::move(onResult);
    m_curSeq = static_cast<uint8_t>(m_curSeq + 1);

    const int written = m_transport->write(frame.data(), static_cast<int>(frame.size()));
    if (written != static_cast<int>(frame.size()))
    {
        m_pending[seq] = nullptr;
        return -1;
    }
    return seq;
}

int TransportProtocol::poll()
{
    if (m_transport == nullptr)
        return -1;

    uint8_t buf[512];
    int total = 0;
    for (;;)
    {
        int n = m_transport->read(buf, sizeof(buf));
        if (n < 0)
            return n;
        if (n == 0)
            break;
        feed(buf, n);
        total += n;
        if (n < static_cast<int>(sizeof(buf)))
            break;
    }
    return total;
}

void TransportProtocol::feed(const uint8_t *data, int len)
{
    for (int i = 0; i < len; ++i)
        handleByte(data[i]);
}

void TransportProtocol::resetParser()
{
    m_state = RxState::WaitStart;
    m_escape = false;
    m_rxPayload.clear();
    m_crcBuf.clear();
}

void TransportProtocol::handleByte(uint8_t byte)
{
    // A start marker always resynchronises the parser, regardless of state.
    // The header fields are never escaped, so an unescaped 0xAA outside a
    // payload/CRC field is unambiguously a frame boundary.
    if (m_state == RxState::WaitStart)
    {
        if (byte == kStartMarker)
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
        // Payload is escape-encoded.
        if (m_escape)
        {
            m_escape = false;
            m_rxPayload.push_back(byte);
            m_crcBuf.push_back(byte);
        }
        else if (byte == kEscMarker)
        {
            m_escape = true;
        }
        else if (byte == kStartMarker)
        {
            // Unexpected marker mid-payload: resync from this start.
            resetParser();
            m_state = RxState::Type;
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
        else if (byte == kEscMarker)
        {
            m_escape = true;
        }
        else if (byte == kStartMarker)
        {
            resetParser();
            m_state = RxState::Type;
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
            dispatchFrame();
            resetParser();
        }
        else if (byte == kEscMarker)
        {
            m_escape = true;
        }
        else if (byte == kStartMarker)
        {
            resetParser();
            m_state = RxState::Type;
        }
        else
        {
            m_rxCrc |= static_cast<uint16_t>(byte) << 8;
            dispatchFrame();
            resetParser();
        }
        break;

    default:
        resetParser();
        break;
    }
}

void TransportProtocol::dispatchFrame()
{
    const uint16_t expected = crc16Modbus(m_crcBuf.data(), static_cast<int>(m_crcBuf.size()));
    if (expected != m_rxCrc)
    {
        if (m_parseErrorHandler)
            m_parseErrorHandler(ParseError::CrcMismatch);
        return; // CRC mismatch: drop frame, parser resyncs on next 0xAA.
    }

    const uint8_t *payload = m_rxPayload.data();
    const int len = static_cast<int>(m_rxPayload.size());

    switch (static_cast<FrameType>(m_rxType))
    {
    case FrameType::Response:
        if (m_pending[m_rxSeq])
        {
            auto handler = m_pending[m_rxSeq];
            m_pending[m_rxSeq] = nullptr;
            handler(true, payload, len);
        }
        break;

    case FrameType::Error:
        if (m_pending[m_rxSeq])
        {
            auto handler = m_pending[m_rxSeq];
            m_pending[m_rxSeq] = nullptr;
            handler(false, payload, len);
        }
        break;

    case FrameType::Push:
        if (m_pushHandler)
            m_pushHandler(payload, len, m_rxSeq);
        break;

    default:
        break;
    }
}
