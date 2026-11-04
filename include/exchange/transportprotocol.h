#ifndef TRANSPORTPROTOCOL_H
#define TRANSPORTPROTOCOL_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

class ITransport;

// SerProt v0.2 transport layer.
//
// Responsibilities:
//   - Frame a command payload (start marker, type, seq, len, escaping, CRC-16)
//     and send it over an ITransport.
//   - Parse an incoming byte stream with a resynchronising state machine that
//     handles escape sequences and validates CRC.
//   - Dispatch Response / Error frames back to the caller that issued the
//     matching command (matched by seq), and route Push frames to a stream
//     handler.
//
// Pure C++/POSIX-friendly: no Qt. Ownership of the transport stays with the
// caller. poll() is expected to be driven by an external loop/timer.
class TransportProtocol
{
public:
    // SerProt frame types.
    enum class FrameType : uint8_t
    {
        Command = 0xCC,
        Response = 0xDD,
        Error = 0xEE,
        Push = 0xFF,
    };

    static constexpr uint8_t kStartMarker = 0xAA;
    static constexpr uint8_t kEscMarker = 0xBB;

    // Callback for a command's terminal result. `ok` is true for Response,
    // false for Error. `payload`/`len` point at the (de-escaped) payload.
    using ResponseHandler = std::function<void(bool ok, const uint8_t *payload, int len)>;

    // Callback for streaming Push frames.
    using PushHandler = std::function<void(const uint8_t *payload, int len, uint8_t seq)>;

    enum class ParseError
    {
        CrcMismatch,
    };
    using ParseErrorHandler = std::function<void(ParseError)>;

    explicit TransportProtocol(ITransport *transport = nullptr);

    void setTransport(ITransport *transport) { m_transport = transport; }
    void setPushHandler(PushHandler handler) { m_pushHandler = std::move(handler); }
    void setParseErrorHandler(ParseErrorHandler handler) { m_parseErrorHandler = std::move(handler); }

    // Build and send a Command frame. `payload` is the application payload
    // (starting with cmd_id). `onResult` is invoked when the matching
    // Response/Error arrives. Returns the seq used, or -1 on error.
    int sendCommand(const uint8_t *payload, int len, ResponseHandler onResult = {});

    // Feed bytes from the transport into the parser and dispatch any complete
    // frames. Returns the number of bytes read (>= 0), or < 0 on transport
    // error. Call this regularly from an external loop.
    int poll();

    // Feed an explicit buffer into the parser (useful for tests / phantom).
    void feed(const uint8_t *data, int len);

    // Reset the parser back to waiting for a start marker.
    void resetParser();

private:
    // Receiver state machine states.
    enum class RxState
    {
        WaitStart,
        Type,
        Seq,
        LenLow,
        LenHigh,
        Payload,
        CrcLow,
        CrcHigh,
    };

    void handleByte(uint8_t byte);
    void dispatchFrame();

    static uint16_t crc16Modbus(const uint8_t *data, int len);

    ITransport *m_transport = nullptr;
    PushHandler m_pushHandler;
    ParseErrorHandler m_parseErrorHandler;

    // Pending command result handlers, indexed by seq (0-255).
    ResponseHandler m_pending[256];
    uint8_t m_curSeq = 0;

    // Receiver state.
    RxState m_state = RxState::WaitStart;
    bool m_escape = false;
    uint8_t m_rxType = 0;
    uint8_t m_rxSeq = 0;
    uint16_t m_rxLen = 0;
    uint16_t m_rxCrc = 0;
    std::vector<uint8_t> m_rxPayload;
    // Unescaped bytes covered by CRC: [type][seq][len_L][len_H][payload].
    std::vector<uint8_t> m_crcBuf;
};

#endif // TRANSPORTPROTOCOL_H
