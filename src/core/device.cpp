#include "core/device.h"

int Device::open()
{
    if (fd >= 0)
        return fd;
    fd = ::open(sysname, O_RDWR | O_NOCTTY);
    if (fd < 0)
        return -1;

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0)
    {
        ::close(fd);
        return -1;
    }

    cfmakeraw(&tty);
    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);

    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CLOCAL | CREAD;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_oflag &= ~OPOST;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        ::close(fd);
        return -1;
    }
    return fd;
}

int Device::close()
{
    return ::close(fd);
}

int Device::write(const uint8_t *buf, int length)
{
    return ::write(fd, buf, length);
}

int Device::read(uint8_t *buf, int length)
{
    return ::read(fd, buf, length);
}

Device::~Device()
{
    if (fd >= 0)
        ::close(fd);
}
