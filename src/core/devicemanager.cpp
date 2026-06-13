#include "core/devicemanager.h"
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

static uint16_t crc16_modbus(uint8_t *data, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}
static bool matches_prefix(const char *str, const char *prefix)
{
    while (*prefix)
    {
        if (*str++ != *prefix++)
            return false;
    }
    return true;
}

DeviceManager::DeviceManager()
{
    DeviceArray = nullptr;
    n = 0;
}

DeviceManager::~DeviceManager()
{
    if (DeviceArray != nullptr)
    {
        for (int i = 0; i < n; i++)
        {
            DeviceArray[i]->DeviceClose();
            delete DeviceArray[i];
        }
        delete[] DeviceArray;
    }
}

Device **DeviceManager::GetAllDevices()
{
    return DeviceArray;
}

int DeviceManager::ScanAndConnectDevices()
{
    if (DeviceArray != nullptr)
    {
        for (int i = 0; i < n; i++)
        {
            DeviceArray[i]->DeviceClose();
            delete DeviceArray[i];
        }
        delete[] DeviceArray;
        DeviceArray = nullptr;
        n = 0;
    }

    DIR *dir = opendir("/sys/class/tty");
    if (!dir)
        return -1;

    struct dirent *entry;
    Device *found[32];
    int found_count = 0;

    while ((entry = readdir(dir)) != nullptr && found_count < 32)
    {
        if (matches_prefix(entry->d_name, "ttyUSB") ||
            matches_prefix(entry->d_name, "ttyACM"))
        {
            Device *dev = new Device();

            std::snprintf(dev->sysname, sizeof(dev->sysname), "/dev/%s", entry->d_name);
            if (dev->DeviceOpen() == 0)
            {
                // === МЕСТО ДЛЯ КОМАНДЫ ===
                // TODO: заменить на реальную команду
                uint8_t cmd[] = {0x00};
                int written = dev->DeviceWrite(cmd, sizeof(cmd));

                if (written == sizeof(cmd))
                {
                    // TODO: добавить select()/poll() с таймаутом, иначе read() может повиснуть
                    uint8_t resp[64] = {0};
                    int r = dev->DeviceRead(resp, sizeof(resp));

                    if (r > 0)
                    {
                        // TODO: проверить корректность ответа
                        // Если всё ок — сохраняем
                        found[found_count++] = dev;
                        continue; // устройство добавлено, не удаляем
                    }
                }

                // Не ответил или ответ кривой — закрываем и удаляем
                dev->DeviceClose();
            }

            delete dev;
        }
    }

    closedir(dir);

    if (found_count > 0)
    {
        n = found_count;
        DeviceArray = new Device *[n];
        for (int i = 0; i < n; i++)
            DeviceArray[i] = found[i];
    }

    return n;
}

int DeviceManager::WriteDevice(Device *dev, uint8_t *buf, int n)
{
    if (!dev)
        return -1;
    return dev->DeviceWrite(buf, n);
}

int DeviceManager::ReadDevice(Device *dev, uint8_t *buf, int n)
{
    if (!dev)
        return -1;
    return dev->DeviceRead(buf, n);
}

int DeviceManager::SendCommand(Device *dev, uint8_t *cmd, int n)
{
    uint8_t buf[64];
    buf[0] = 0xAA;
    buf[1] = 0xCC;
    buf[2] = 0x00;
    int i = 0;
    int j = 5;
    while (i < n)
    {
        if (cmd[i] == 0xAA || cmd[i] == 0xBB)
            buf[j++] = 0xBB;
        buf[j++] == cmd[i++];
    }
    buf[3] = (j - 5) & 0xFF;
    buf[4] = (j - 5) >> 8;
    uint16_t crc16 = crc16_modbus(buf + 1, j + 5);
    buf[5 + j] = crc16 & 0xFF;
    buf[6 + j] = crc16 >> 8;
    WriteDevice(dev, buf, j + 7);
    ReadDevice(dev, buf, 64);
    return 0;
}

DeviceInfo DeviceManager::GetDeviceInfo(Device *dev)
{
    if (!dev)
    {
        DeviceInfo empty = {0};
        return empty;
    }
    return dev->dinfo;
}