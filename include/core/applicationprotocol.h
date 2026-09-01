#ifndef APPLICATIONPROTOCOL_H
#define APPLICATIONPROTOCOL_H

#include <cstdint>
#include <string>

// Base class for a device's application-layer protocol.
//
// The application protocol sits on top of TransportProtocol and gives each
// device family a typed command surface plus a way to decode its Push
// payloads. Concrete devices (e.g. the ECG/ADC MCU) subclass this.
//
// Pure C++: no Qt. Higher layers (DeviceSession + Qt adapters) marshal the
// decoded data to the UI.
class ApplicationProtocol
{
public:
    virtual ~ApplicationProtocol() = default;

    // Human-readable identifier for the device family this protocol targets.
    virtual std::string name() const = 0;

    // SerProt application-layer error codes (shared across devices).
    enum class Error : uint8_t
    {
        None = 0x00,
        UnknownCommand = 0x01,
        BadParams = 0x02,
        NotReady = 0x03,
        HardwareFault = 0x04,
        CrcMismatch = 0x05,
        BadPayloadSize = 0x06,
    };

    static const char *errorText(uint8_t code);
};

#endif // APPLICATIONPROTOCOL_H
