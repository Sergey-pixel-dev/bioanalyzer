#ifndef DEVICE_H
#define DEVICE_H

#include <cstdint>
#include <string>
#include "exchange/itransport.h"

// Serial-port transport.
//
// On POSIX systems wraps a termios-configured tty (e.g. /dev/ttyUSB0), on
// Windows a COM port (e.g. COM3) through the Win32 API. Reads are
// non-blocking so a polling loop can pump bytes into the transport-protocol
// parser without hanging.
//
// The port is opened exclusively. On POSIX TIOCEXCL keeps other processes
// from opening it, and an advisory flock() detects another copy of this
// program (or any program that locks the port); on Windows the port is opened
// without sharing. A busy port fails open() at once with a reason in
// lastError(), instead of two readers splitting the bytes.
class Device : public ITransport
{
public:
    Device();
    explicit Device(const std::string &portName, int baud = 921600);
    ~Device() override;

    // Configure before open(). Baud is a raw value in bits/s (default 921600);
    // it is mapped to the closest supported termios speed.
    void setPortName(const std::string &portName);
    void setBaudRate(int baud);
    const std::string &portName() const { return m_portName; }

    int open() override;
    int close() override;
    int write(const uint8_t *buf, int length) override;
    int read(uint8_t *buf, int length) override;
#ifdef _WIN32
    bool isOpen() const override { return m_handle != nullptr; }
#else
    bool isOpen() const override { return m_fd >= 0; }
#endif
    std::string lastError() const override { return m_lastError; }

    // Discard bytes already waiting in the kernel RX queue. Useful before a
    // fresh discovery probe so stale/partial frames cannot affect the retry.
    bool flushInput();

private:
#ifdef _WIN32
    void *m_handle = nullptr; // HANDLE of the open COM port
#else
    int m_fd = -1;
#endif
    std::string m_portName;
    std::string m_lastError;
    int m_baud = 921600;
};

#endif // DEVICE_H
