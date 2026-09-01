#ifndef DEVICESESSION_H
#define DEVICESESSION_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "core/transportprotocol.h"
#include "core/ecgadcprotocol.h"
#include "core/samplebuffer.h"
#include "core/sessionrecord.h"
#include "core/datahub.h"

class ITransport;

// One connected device: owns its transport, protocol stack, rolling sample
// buffer, and (optionally) a session recorder.
//
// This is the core-side session object. It is Qt-free and single-threaded
// from its own perspective: poll() is meant to be pumped by an owner (a Qt
// worker thread via the adapter, or a test loop). Decoded sample frames land
// in the SampleBuffer and, when recording, in the SessionWriter; an optional
// callback also forwards them for live consumers.
//
// The transport is injected, so the same session works over a real serial
// Device or a PhantomDevice for playback.
class DeviceSession
{
public:
    // Information returned by serial-port discovery. Discovery is stateless:
    // it never owns or keeps sessions. The application can have only one
    // active session, which is owned by AppContext.
    struct PortInfo
    {
        std::string path;
        std::string description;
    };

    struct DiscoveredDevice
    {
        std::string port;
        int baud = 921600;
        EcgAdcProtocol::DeviceInfo info;
    };

    enum class ProbeStatus
    {
        Success,
        OpenFailed,
        Timeout,
        InvalidResponse,
        ProtocolError,
        TransportError,
    };

    struct ProbeResult
    {
        ProbeStatus status = ProbeStatus::Timeout;
        EcgAdcProtocol::DeviceInfo info;
        uint8_t errorCode = 0;
        int attempts = 0;

        bool ok() const { return status == ProbeStatus::Success; }
    };

    // Enumerate candidate serial ports and probe them with GetDeviceInfo.
    // These helpers are synchronous and Qt-free; the Qt adapter runs them on
    // a worker thread so the GUI remains responsive.
    static std::vector<PortInfo> discoverPorts();
    static ProbeResult probePortDetailed(const std::string &portPath, int baud = 921600,
                                         int timeoutMs = 650, int maxAttempts = 3);
    // Compatibility wrapper for callers that need a single probe attempt.
    static bool probePort(const std::string &portPath, int baud, int timeoutMs,
                          EcgAdcProtocol::DeviceInfo &out);
    static std::vector<DiscoveredDevice> scanDevices(int baud = 921600,
                                                     int timeoutMs = 650,
                                                     int maxAttempts = 3);

    // Called (on the poll thread) whenever a fresh sample frame is decoded.
    using SampleCallback = std::function<void(const EcgAdcProtocol::SampleFrame &)>;

    // `transport` is borrowed (not owned) unless ownTransport is true.
    DeviceSession(ITransport *transport, bool ownTransport = false);
    ~DeviceSession();

    // Rolling buffer sizing (samples kept per channel). Default ~60 s @ 4 kHz.
    void setBufferCapacity(size_t samplesPerChannel);

    bool open();
    void close();
    bool isOpen() const;

    // Pump the transport parser. Returns bytes read (>=0) or <0 on error.
    int poll();

    // Protocol commands (thin pass-through to EcgAdcProtocol).
    int startStream(const std::vector<uint8_t> &channels, EcgAdcProtocol::AckHandler onAck = {});
    int stopStream(EcgAdcProtocol::AckHandler onAck = {});
    // Compatibility command only; ADS1298 reference remains fixed at 2.4 V.
    int setVref(uint16_t mV, EcgAdcProtocol::AckHandler onAck = {});
    int setSamplerate(uint8_t idx, EcgAdcProtocol::AckHandler onAck = {});
    int setGain(uint8_t channel, uint8_t gainCode, EcgAdcProtocol::AckHandler onAck = {});
    // Global, mutually-exclusive ADS1298 input modes. setShortInput(false)
    // remains a compatibility alias for setNormalInput().
    int setShortInput(bool enable, EcgAdcProtocol::AckHandler onAck = {});
    int setNormalInput(EcgAdcProtocol::AckHandler onAck = {});
    int setTestSignal(uint8_t amplitude, uint8_t frequency, EcgAdcProtocol::AckHandler onAck = {});
    int getDeviceInfo(EcgAdcProtocol::DeviceInfoHandler onInfo = {});

    // Current geometry.
    const std::vector<uint8_t> &activeChannels() const { return m_app.activeChannels(); }
    int sampleRateHz() const { return m_sampleRateHz; }
    void setSampleRateHz(int hz) { m_sampleRateHz = hz; }
    // ADS1298 uses the internal, fixed 2.4 V reference.
    uint16_t vrefMv() const { return EcgAdcProtocol::kFixedReferenceVoltageMv; }

    SampleBuffer &buffer() { return m_buffer; }
    const SampleBuffer &buffer() const { return m_buffer; }

    void setSampleCallback(SampleCallback cb) { m_sampleCb = std::move(cb); }
    void setDataHub(DataHub *hub) { m_dataHub = hub; }

    // Recording. Records every active channel; disabled channels are the
    // caller's concern (they fill placeholder data before recording). Returns
    // false if a recording is already active or the file can't be opened.
    bool startRecording(const std::string &path, const std::string &description,
                        const std::vector<ChannelInfo> &channels);
    void stopRecording();
    bool isRecording() const { return m_writer && m_writer->isOpen(); }
    uint64_t recordedSamples() const { return m_writer ? m_writer->sampleCount() : 0; }

private:
    void onSamples(const EcgAdcProtocol::SampleFrame &frame);

    ITransport *m_transport = nullptr;
    bool m_ownTransport = false;
    TransportProtocol m_transportProto;
    EcgAdcProtocol m_app;
    SampleBuffer m_buffer;
    std::unique_ptr<SessionWriter> m_writer;
    std::vector<ChannelInfo> m_recordingChannels;

    SampleCallback m_sampleCb;
    DataHub *m_dataHub = nullptr; // borrowed
    uint64_t m_totalSamples = 0;
    size_t m_bufferCapacity = 4000 * 60; // 60 s @ 4 kHz
    int m_sampleRateHz = EcgAdcProtocol::kSampleRateHz[EcgAdcProtocol::kDefaultSampleRateIndex];
};

#endif // DEVICESESSION_H
