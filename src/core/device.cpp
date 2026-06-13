#include "core/device.h"

int Device::DeviceOpen()
{
    fd = open(sysname, O_RDWR | O_NOCTTY);
    if (fd < 0)
        return 1;

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0)
    {
        close(fd);
        fd = -1;
        return 1;
    }

    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS | CSIZE);
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        close(fd);
        fd = -1;
        return 1;
    }
    return 0;
}

int Device::DeviceClose()
{
    if (fd < 0)
        return -1;

    int ret = close(fd);
    fd = -1;
    return ret;
}

int Device::DeviceWrite(uint8_t *buf, int n)
{
    return write(fd, buf, n);
}

int Device::DeviceRead(uint8_t *buf, int n, int timeout = 50)
{
    return read(fd, buf, n);
}