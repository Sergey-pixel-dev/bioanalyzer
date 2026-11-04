#include "device/device.h"
#include "common/translatable.h"

#include <cstring>
#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <cerrno>
#endif

namespace
{
#ifdef _WIN32
    // Text of a Windows error in UTF-8, the encoding of the core's messages.
    std::string systemMessage(DWORD code)
    {
        wchar_t *text = nullptr;
        const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                                FORMAT_MESSAGE_IGNORE_INSERTS,
                                            nullptr, code, 0, reinterpret_cast<wchar_t *>(&text), 0, nullptr);
        std::string message;
        if (length > 0 && text != nullptr)
        {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0,
                                                  nullptr, nullptr);
            message.resize(static_cast<std::size_t>(bytes));
            WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), &message[0], bytes, nullptr, nullptr);
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r' ||
                                        message.back() == ' ' || message.back() == '.'))
                message.pop_back();
        }
        if (text != nullptr)
            LocalFree(text);
        return message.empty() ? "error " + std::to_string(code) : message;
    }
#else
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
#endif
} // namespace

Device::Device() = default;

Device::Device(const std::string &portName, int baud)
    : m_portName(portName), m_baud(baud)
{
}

Device::~Device()
{
    close();
}

void Device::setPortName(const std::string &portName)
{
    m_portName = portName;
}

void Device::setBaudRate(int baud)
{
    m_baud = baud;
}

#ifdef _WIN32
int Device::open()
{
    if (m_handle != nullptr)
        return 0;
    if (m_portName.empty())
        return -1;

    m_lastError.clear();
    // COM10 and higher are reachable only through the device namespace.
    const std::string path = m_portName.rfind("\\\\.\\", 0) == 0 ? m_portName : "\\\\.\\" + m_portName;
    // No sharing: a port held by another program or by another copy of this
    // one is refused at once.
    HANDLE handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        const DWORD code = GetLastError();
        m_lastError = code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION
                          ? QT_TRANSLATE_NOOP("Errors", "the port is busy: it is used by another program")
                          : systemMessage(code);
        return -1;
    }
    auto fail = [&]
    {
        m_lastError = systemMessage(GetLastError());
        CloseHandle(handle);
        return -1;
    };

    // 8 data bits, no parity, one stop bit, no flow control; DTR and RTS are
    // raised, as a tty does on POSIX systems when it is opened.
    DCB dcb;
    std::memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb))
        return fail();
    dcb.BaudRate = static_cast<DWORD>(m_baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(handle, &dcb))
        return fail();

    // Non-blocking read: ReadFile returns at once with whatever is waiting.
    COMMTIMEOUTS timeouts;
    std::memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.WriteTotalTimeoutConstant = 100;
    if (!SetCommTimeouts(handle, &timeouts))
        return fail();
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    m_handle = handle;
    return 0;
}

int Device::close()
{
    if (m_handle == nullptr)
        return 0;
    const BOOL closed = CloseHandle(static_cast<HANDLE>(m_handle));
    m_handle = nullptr;
    return closed ? 0 : -1;
}

int Device::write(const uint8_t *buf, int length)
{
    if (m_handle == nullptr || buf == nullptr || length < 0)
        return -1;

    int total = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(100);
    while (total < length)
    {
        DWORD written = 0;
        if (!WriteFile(static_cast<HANDLE>(m_handle), buf + total, static_cast<DWORD>(length - total),
                       &written, nullptr))
            return -1;
        total += static_cast<int>(written);
        if (written == 0)
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return -1;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return total;
}

int Device::read(uint8_t *buf, int length)
{
    if (m_handle == nullptr || buf == nullptr || length < 0)
        return -1;
    DWORD received = 0;
    if (!ReadFile(static_cast<HANDLE>(m_handle), buf, static_cast<DWORD>(length), &received, nullptr))
        return -1;
    return static_cast<int>(received);
}

bool Device::flushInput()
{
    return m_handle != nullptr && PurgeComm(static_cast<HANDLE>(m_handle), PURGE_RXCLEAR) != 0;
}
#else
int Device::open()
{
    if (m_fd >= 0)
        return m_fd;
    if (m_portName.empty())
        return -1;

    m_lastError.clear();
    m_fd = ::open(m_portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0)
    {
        m_lastError = errno == EBUSY ? QT_TRANSLATE_NOOP("Errors", "the port is busy: it is used by another program")
                                     : std::strerror(errno);
        return -1;
    }
    // Exclusive use: a lock held by someone else means the port is taken.
    if (::flock(m_fd, LOCK_EX | LOCK_NB) != 0)
    {
        m_lastError = errno == EWOULDBLOCK ? QT_TRANSLATE_NOOP("Errors", "the port is busy: it is used by another program")
                                           : std::strerror(errno);
        ::close(m_fd);
        m_fd = -1;
        return -1;
    }
    ::ioctl(m_fd, TIOCEXCL);

    struct termios tty;
    std::memset(&tty, 0, sizeof(tty));
    if (tcgetattr(m_fd, &tty) != 0)
    {
        m_lastError = std::strerror(errno);
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
        m_lastError = std::strerror(errno);
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
    // The exclusive flag belongs to the tty and outlives the descriptor:
    // without clearing it the port would stay closed to everyone.
    ::ioctl(m_fd, TIOCNXCL);
    ::flock(m_fd, LOCK_UN);
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
#endif
