#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include "core/device.h"

class DeviceManager
{
public:
    Device **GetAllDevices();
    int ScanAndConnectDevices();
    int WriteDevice(Device *dev, uint8_t *buf, int n);
    int ReadDevice(Device *dev, uint8_t *buf, int n);
    int SendCommand(Device *dev, uint8_t *cmd, int n);
    DeviceInfo GetDeviceInfo(Device *dev);

    Device **DeviceArray = nullptr;
    int n = 0;

    DeviceManager();
    ~DeviceManager();
};

#endif // DEVICEMANAGER_H