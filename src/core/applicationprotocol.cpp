#include "core/applicationprotocol.h"

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
