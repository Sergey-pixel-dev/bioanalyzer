#include "core/transportprotocol.h"
#include "transportprotocol.h"

static uint16_t crc16_modbus(uint8_t *data, int n)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < n; ++i)
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
int TransportProtocol::SendFrame(uint8_t *frame, int n)
{
    if (dev == nullptr)
        return -1;
    return 0;
}
int TransportProtocol::SendCommand(uint8_t *cmd, int n, void (*func_ptr)(uint8_t *response, int n))
{
    uint8_t buf[255];
    buf[0] = 0xAA;
    buf[1] = 0xCC;
    buf[2] = cur_seq;
    int i = 0;
    int j = 5;
    while (i < n)
    {
        if (cmd[i] == 0xAA || cmd[i] == 0xBB)
            buf[j++] = 0xBB;
        buf[j++] = cmd[i++];
    }
    buf[3] = (j - 5) & 0xFF;
    buf[4] = (j - 5) >> 8;
    uint16_t crc16 = crc16_modbus(buf + 1, j + 5);
    buf[5 + j] = crc16 & 0xFF;
    buf[6 + j] = crc16 >> 8;
    if (cur_seq >= 255)
        cur_seq = 0;
    seq_array[++cur_seq] = {.valid = 1, .func_ptr = func_ptr};
    dev->write(buf, j + 7);
    return 0;
}
int TransportProtocol::StartStream()
{

    return 0;
}
int TransportProtocol::StopStream()
{
    return 0;
}