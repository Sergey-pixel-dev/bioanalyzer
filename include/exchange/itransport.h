#ifndef ITRANSPORT_H
#define ITRANSPORT_H

#include <cstdint>

// Low-level byte transport abstraction.
//
// Pure POSIX/C++ interface — no Qt, no protocol knowledge. Concrete
// implementations move raw bytes to/from a physical or virtual endpoint
// (real serial port, phantom file-backed device, socket, etc.).
//
// read()/write() follow POSIX semantics: they return the number of bytes
// transferred, or a negative value on error. A return of 0 from read()
// means "no data available right now" (non-blocking) or EOF, depending on
// the implementation.
class ITransport
{
public:
    virtual ~ITransport() = default;

    // Open the endpoint. Returns >= 0 on success, negative on error.
    virtual int open() = 0;

    // Close the endpoint. Returns 0 on success, negative on error.
    virtual int close() = 0;

    // Write exactly up to `length` bytes. Returns bytes written or < 0.
    virtual int write(const uint8_t *buf, int length) = 0;

    // Read up to `length` bytes. Returns bytes read, 0 if none, or < 0.
    virtual int read(uint8_t *buf, int length) = 0;

    // True while the endpoint is usable.
    virtual bool isOpen() const = 0;
};

#endif // ITRANSPORT_H
