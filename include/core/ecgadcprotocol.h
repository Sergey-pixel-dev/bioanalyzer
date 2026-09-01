#ifndef ECGADCPROTOCOL_H
#define ECGADCPROTOCOL_H

#include <cstdint>
#include <vector>
#include <functional>
#include <string>

#include "core/applicationprotocol.h"

class TransportProtocol;

// Application protocol for the ECG/EEG/EMG ADC device (MCU + external ADC).
//
// Implements the command surface from Protocols/Application_Layer_Protocol.md:
//   0x01 StartStream, 0x02 StopStream, 0x10 SetVref (compatibility stub),
//   0x11 SetSamplerate, 0x20 GetDeviceInfo, 0x30 SetGain,
//   0x31 InputShort, 0x32 TestSignal, 0x33 NormalInput.
//
// It also decodes Push payloads (24-bit signed, little-endian, interleaved by
// active channel) into per-channel µV samples.
//
// Pure C++: it drives a TransportProtocol but knows nothing about Qt or the
// UI. Result callbacks report success plus (for errors) the SerProt error
// code.
class EcgAdcProtocol : public ApplicationProtocol
{
public:
    // Command IDs (application layer).
    enum class Cmd : uint8_t
    {
        StartStream = 0x01,
        StopStream = 0x02,
        SetVref = 0x10,
        SetSamplerate = 0x11,
        GetDeviceInfo = 0x20,
        SetGain = 0x30,
        SetShortInput = 0x31,
        InputShort = SetShortInput,
        SetTestSignal = 0x32,
        TestSignal = SetTestSignal,
        NormalInput = 0x33,
    };

    // Controller capabilities/constants.
    static constexpr int kSampleRateHz[4] = {250, 500, 1000, 4000};
    static constexpr int kMaxChannels = 8;
    static constexpr int kDefaultSampleRateIndex = 2; // 1000 Hz
    static constexpr uint16_t kFixedReferenceVoltageMv = 2400;
    static constexpr int kGainValues[7] = {6, 1, 2, 3, 4, 8, 12};
    static constexpr uint8_t kTestFrequencies[2] = {0, 1};

    // ok == true  -> Response received (err is None).
    // ok == false -> Error frame (err holds the code) or transport failure.
    using AckHandler = std::function<void(bool ok, ApplicationProtocol::Error err)>;

    // Decoded GetDeviceInfo response.
    struct DeviceInfo
    {
        uint16_t deviceId = 0;
        uint8_t fwMajor = 0;
        uint8_t fwMinor = 0;
        uint8_t channelCount = 0;
        std::string description;
    };
    // ok == false => info is unspecified.
    using DeviceInfoHandler = std::function<void(bool ok, const DeviceInfo &info)>;

    // Decoded frame of interleaved samples: samples[channelIndex][sampleIndex]
    // in microvolts. channelIndex refers to the position within the active
    // channel set, not the physical ADC channel.
    struct SampleFrame
    {
        uint8_t pushSequence = 0;
        std::vector<uint8_t> channels;             // physical channel indices
        std::vector<std::vector<int32_t>> samples; // per active channel, µV
    };
    using SampleHandler = std::function<void(const SampleFrame &)>;

    explicit EcgAdcProtocol(TransportProtocol *transport = nullptr);

    void setTransport(TransportProtocol *transport);
    void setSampleHandler(SampleHandler handler) { m_sampleHandler = std::move(handler); }

    std::string name() const override { return "ECG/ADC"; }

    // Commands. `channels` must be sorted ascending, unique, within 0..7.
    int startStream(const std::vector<uint8_t> &channels, AckHandler onAck = {});
    int stopStream(AckHandler onAck = {});
    int setVref(uint16_t mV, AckHandler onAck = {});
    int setSamplerate(uint8_t idx, AckHandler onAck = {});
    int setGain(uint8_t channel, uint8_t gainCode, AckHandler onAck = {});
    // Select the input-short mode for all channels. The command has no
    // parameters; the bool overload remains as a source-compatible wrapper.
    int setShortInput(bool enable, AckHandler onAck = {});
    // Select ordinary electrode inputs for all channels.
    int setNormalInput(AckHandler onAck = {});
    int setTestSignal(uint8_t amplitude, uint8_t frequency, AckHandler onAck = {});
    int getDeviceInfo(DeviceInfoHandler onInfo = {});

    // Decode a GetDeviceInfo response payload. Returns false if malformed.
    static bool decodeDeviceInfo(const uint8_t *payload, int len, DeviceInfo &out);

    // Currently active channel set (set on a successful startStream).
    const std::vector<uint8_t> &activeChannels() const { return m_activeChannels; }

    // Decode raw 24-bit signed little-endian sample at data[0..2] into µV.
    static int32_t decodeSample(const uint8_t *p);

private:
    void onPush(const uint8_t *payload, int len, uint8_t seq);

    TransportProtocol *m_transport = nullptr;
    SampleHandler m_sampleHandler;
    std::vector<uint8_t> m_activeChannels;
    std::vector<uint8_t> m_pendingChannels; // channels awaiting StartStream ack
};

#endif // ECGADCPROTOCOL_H
