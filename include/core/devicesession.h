#ifndef DEVICESESSION_H
#define DEVICESESSION_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/transportprotocol.h"
#include "core/applicationprotocol.h"
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
// in the SampleBuffer and, when recording, in the SessionWriter. Live samples
// are published through the required DataHub in short time-based batches.
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
        ApplicationProtocol::DeviceInfo info;
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
        ApplicationProtocol::DeviceInfo info;
        uint8_t errorCode = 0;
        int attempts = 0;

        bool ok() const { return status == ProbeStatus::Success; }
    };

    // Enumerate candidate serial ports and probe them with GetDeviceInfo.
    // These helpers are synchronous and Qt-free; the Qt adapter runs them on
    // a worker thread so the GUI remains responsive.
    static std::vector<PortInfo> discoverPorts();
    static ProbeResult probePortDetailed(const std::string &portPath, int baud = 921600,
                                         int timeoutMs = 1200, int maxAttempts = 3);
    static std::vector<DiscoveredDevice> scanDevices(int baud = 921600,
                                                     int timeoutMs = 1200,
                                                     int maxAttempts = 3);

    // `transport` is borrowed (not owned) unless ownTransport is true.
    // `dataHub` is required: all decoded samples leave the session through
    // this bus.
    DeviceSession(ITransport *transport, DataHub *dataHub, bool ownTransport = false);
    ~DeviceSession();

    // Rolling buffer sizing (samples kept per channel). Default ~60 s @ 4 kHz.
    void setBufferCapacity(size_t samplesPerChannel);

    bool open();
    void close();
    bool isOpen() const;

    // Pump the transport parser. Returns bytes read (>=0) or <0 on error.
    int poll();

    // Protocol commands (thin pass-through to ApplicationProtocol).
    int startStream(const std::vector<uint8_t> &channels, ApplicationProtocol::AckHandler onAck = {});
    int stopStream(ApplicationProtocol::AckHandler onAck = {});
    int setSamplerate(uint8_t idx, ApplicationProtocol::AckHandler onAck = {});
    int setGain(uint8_t channel, uint8_t gainCode, ApplicationProtocol::AckHandler onAck = {});
    // Global, mutually-exclusive ADS1298 input modes.
    int setShortInput(ApplicationProtocol::AckHandler onAck = {});
    int setNormalInput(ApplicationProtocol::AckHandler onAck = {});
    int setTestSignal(uint8_t amplitude, uint8_t frequency, ApplicationProtocol::AckHandler onAck = {});
    int getDeviceInfo(ApplicationProtocol::DeviceInfoHandler onInfo = {});

    // Current geometry.
    const std::vector<uint8_t> &activeChannels() const { return m_app.activeChannels(); }
    int sampleRateHz() const { return m_sampleRateHz; }
    void setSampleRateHz(int hz) { m_sampleRateHz = hz; }
    // ADS1298 uses the internal, fixed 2.4 V reference.
    uint16_t vrefMv() const { return ApplicationProtocol::kFixedReferenceVoltageMv; }

    SampleBuffer &buffer() { return m_buffer; }
    const SampleBuffer &buffer() const { return m_buffer; }

    // Recording. The active physical channel set comes from the successful
    // StartStream command; the supplied descriptors only provide optional
    // labels/metadata for those channels. Returns false if no stream is
    // active, a recording is already open, or the file cannot be opened.
    bool startRecording(const std::string &path, const std::string &description,
                        const std::vector<ChannelInfo> &channels);
    void stopRecording();
    bool isRecording() const { return m_writer && m_writer->isOpen(); }
    uint64_t recordedSamples() const { return m_writer ? m_writer->sampleCount() : 0; }

private:
    void onSamples(const ApplicationProtocol::SampleFrame &frame);
    void resetPublishBatch();
    void publishPending(size_t count);
    void flushPublishedSamples();

    ITransport *m_transport = nullptr;
    bool m_ownTransport = false;
    TransportProtocol m_transportProto;
    ApplicationProtocol m_app;
    SampleBuffer m_buffer;
    std::unique_ptr<SessionWriter> m_writer;

    DataHub *m_dataHub = nullptr; // required, borrowed
    std::vector<std::vector<int32_t>> m_publishPending;
    std::vector<uint8_t> m_publishChannels;
    uint64_t m_publishFirstSample = 0;
    size_t m_publishPendingSamples = 0;
    uint8_t m_publishLastSequence = 0;
    uint64_t m_totalSamples = 0;
    size_t m_bufferCapacity = 4000 * 60; // 60 s @ 4 kHz
    int m_sampleRateHz = ApplicationProtocol::kSampleRateHz[ApplicationProtocol::kDefaultSampleRateIndex];
};

#endif // DEVICESESSION_H
