#ifndef APPLICATIONPROTOCOL_H
#define APPLICATIONPROTOCOL_H

#include <cstdint>
#include <vector>
#include <functional>
#include <string>
#include <utility>

class TransportProtocol;

// Application protocol for the ECG/EEG/EMG ADC device (MCU + external ADC).
//
// Implements the command surface from Protocols/Application_Layer_Protocol.md:
//   0x01 StartStream, 0x02 StopStream,
//   0x11 SetSamplerate, 0x20 GetDeviceInfo, 0x30 SetGain,
//   0x31 InputShort, 0x32 TestSignal, 0x33 NormalInput.
//
// It also decodes Push payloads (24-bit signed, little-endian, interleaved by
// active channel) into per-channel µV samples.
//
// Pure C++: it drives a TransportProtocol but knows nothing about Qt or the
// UI. Result callbacks report success plus (for errors) the SerProt error
// code.
class ApplicationProtocol
{
public:
    virtual ~ApplicationProtocol() = default;

    // SerProt application-layer error codes.
    enum class Error : uint8_t
    {
        None = 0x00,
        UnknownCommand = 0x01,
        BadParams = 0x02,
        NotReady = 0x03,
        HardwareFault = 0x04,
        CrcMismatch = 0x05,
        BadPayloadSize = 0x06,
    };

    static const char *errorText(uint8_t code);

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
    // Protocol indices are stable: index 3 has always been 4000 Hz.
    static constexpr int kSampleRateHz[5] = {250, 500, 1000, 4000, 2000};
    static constexpr int kSampleRateCount = 5;
    static constexpr int kMaxChannels = 8;
    static constexpr int kDefaultSampleRateIndex = 2; // 1000 Hz
    static constexpr uint16_t kFixedReferenceVoltageMv = 2400;
    static constexpr int kGainValues[7] = {6, 1, 2, 3, 4, 8, 12};
    static constexpr uint8_t kTestFrequencies[2] = {0, 1};

    // ok == true  -> Response received (err is None).
    // ok == false -> Error frame (err holds the code) or transport failure.
    using AckHandler = std::function<void(bool ok, Error err)>;

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
    // in microvolts. `channels[channelIndex]` is the physical ADC index for
    // that column; the vector is exactly the acknowledged active channel set.
    struct SampleFrame
    {
        uint8_t pushSequence = 0;
        std::vector<uint8_t> channels;             // physical channel indices
        std::vector<std::vector<int32_t>> samples; // per active channel, µV
    };
    using SampleHandler = std::function<void(const SampleFrame &)>;

    explicit ApplicationProtocol(TransportProtocol *transport = nullptr);

    void setTransport(TransportProtocol *transport);
    void setSampleHandler(SampleHandler handler) { m_sampleHandler = std::move(handler); }

    std::string name() const { return "EMG/ADC"; }

    // Commands. `channels` must be sorted ascending, unique, within 0..7.
    int startStream(const std::vector<uint8_t> &channels, AckHandler onAck = {});
    int stopStream(AckHandler onAck = {});
    int setSamplerate(uint8_t idx, AckHandler onAck = {});
    int setGain(uint8_t channel, uint8_t gainCode, AckHandler onAck = {});
    // Select the input-short mode for all channels.
    int setShortInput(AckHandler onAck = {});
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

#endif // APPLICATIONPROTOCOL_H
