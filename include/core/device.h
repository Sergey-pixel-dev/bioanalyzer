#ifndef DEVICE_H
#define DEVICE_H

#include <cstdint>
#include <string>
#include "core/itransport.h"

// POSIX serial-port transport.
//
// Wraps a termios-configured tty (e.g. /dev/ttyUSB0). Reads are
// non-blocking with a short timeout so a polling loop can pump bytes into
// the transport-protocol parser without hanging.
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
    bool isOpen() const override { return m_fd >= 0; }

    // Discard bytes already waiting in the kernel RX queue. Useful before a
    // fresh discovery probe so stale/partial frames cannot affect the retry.
    bool flushInput();

private:
    int m_fd = -1;
    std::string m_portName;
    int m_baud = 921600;
};

#endif // DEVICE_H
