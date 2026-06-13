#ifndef DEVICE_H
#define DEVICE_H

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstdint>
typedef struct
{
    char name[32];
    char description[128];
} DeviceInfo;
class Device
{
public:
    int fd = -1;
    char sysname[32] = {0};
    DeviceInfo dinfo;
    int DeviceOpen();
    int DeviceClose();
    int DeviceWrite(uint8_t *buf, int n);
    int DeviceRead(uint8_t *buf, int n, int timeout = 50); // timeout в мс
};
#endif