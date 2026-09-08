#include "core/device.h"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <thread>

namespace
{
    // Map a raw baud value to the nearest termios speed constant.
    speed_t toSpeed(int baud)
    {
        switch (baud)
        {
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        case 230400:
            return B230400;
        case 460800:
            return B460800;
        case 921600:
            return B921600;
        default:
            return B921600;
        }
    }
} // namespace

Device::Device() = default;

Device::Device(const std::string &portName, int baud)
    : m_portName(portName), m_baud(baud)
{
}

Device::~Device()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

void Device::setPortName(const std::string &portName)
{
    m_portName = portName;
}

void Device::setBaudRate(int baud)
{
    m_baud = baud;
}

int Device::open()
{
    if (m_fd >= 0)
        return m_fd;
    if (m_portName.empty())
        return -1;

    m_fd = ::open(m_portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0)
        return -1;

    struct termios tty;
    std::memset(&tty, 0, sizeof(tty));
    if (tcgetattr(m_fd, &tty) != 0)
    {
        ::close(m_fd);
        m_fd = -1;
        return -1;
    }

    cfmakeraw(&tty);
    const speed_t sp = toSpeed(m_baud);
    cfsetospeed(&tty, sp);
    cfsetispeed(&tty, sp);

    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CLOCAL | CREAD;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_oflag &= ~OPOST;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    // Non-blocking read: return immediately with whatever is available.
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(m_fd, TCSANOW, &tty) != 0)
    {
        ::close(m_fd);
        m_fd = -1;
        return -1;
    }
    return m_fd;
}

int Device::close()
{
    if (m_fd < 0)
        return 0;
    int r = ::close(m_fd);
    m_fd = -1;
    return r;
}

int Device::write(const uint8_t *buf, int length)
{
    if (m_fd < 0 || buf == nullptr || length < 0)
        return -1;

    int total = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(100);
    ssize_t n;
    while (total < length)
    {
        n = ::write(m_fd, buf + total, static_cast<size_t>(length - total));
        if (n > 0)
        {
            total += static_cast<int>(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
            std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        return -1;
    }
    return total;
}

int Device::read(uint8_t *buf, int length)
{
    if (m_fd < 0)
        return -1;
    int r = static_cast<int>(::read(m_fd, buf, length));
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return 0;
    return r;
}

bool Device::flushInput()
{
    return m_fd >= 0 && ::tcflush(m_fd, TCIFLUSH) == 0;
}
