#ifndef TRANSPORTPROTOCOL_H
#define TRANSPORTPROTOCOL_H
#include <cstdint>

typedef struct
{
    int valid;
    void (*func_ptr)(uint8_t *response, int n);
} PacketCallback_t;
class TransportProtocol
{
public:
    int cur_seq = 1;
    PacketCallback_t seq_array[255];
    Device *dev = nullptr;
    int SendFrame(uint8_t *frame, int n);
    int SendCommand(uint8_t *cmd, int n, void (*func_ptr)(uint8_t *response, int n));
    int StartStream();
    int StopStream();
};
#endif // TRANSPORTPROTOCOL_H