#ifndef DEVICE_H
#define DEVICE_H
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
class Device
{
public:
    int fd;
    char sysname[32];
    int open();
    int close();
    int write(const uint8_t *buf, int length);
    int read(uint8_t *buf, int length);
    Device();
    ~Device();
};
#endif // DEVICE_H