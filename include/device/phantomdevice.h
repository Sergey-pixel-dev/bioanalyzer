#ifndef PHANTOMDEVICE_H
#define PHANTOMDEVICE_H

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <memory>

#include "exchange/itransport.h"
#include "device/sessionrecord.h"

// Virtual device: plays a recorded .bsig session back as if it were the real
// ECG/ADC MCU over SerProt.
//
// It implements ITransport, so the exact same TransportProtocol +
// ApplicationProtocol stack drives it — nothing above the transport layer knows
// whether it is talking to real hardware or to the virtual device. That keeps
// the playback path identical to the live path.
//
// It parses the Command frames the master writes, answers with Response
// frames and, while streaming, emits Push frames with the recording's samples.
// The device configuration is the recording's:
//   * GetDeviceInfo reports the recording's channel count and description;
//   * channels are numbered 0..N-1 by their position in the recording, not by
//     the physical ADC inputs they were captured from;
//   * SetSamplerate accepts only the recording's rate, gain is accepted and
//     ignored, and only the normal input exists (no short or test signal).
//
// StartStream plays from the current position and loops at the end of the
// recording; StopStream stops and keeps the position. seekToSample() may be
// called at any time. pump() advances playback by elapsed wall-clock time.
//
// All calls come from the thread that polls the transport. Pure C++: no Qt.
class PhantomDevice : public ITransport
{
public:
    PhantomDevice() = default;
    ~PhantomDevice() override = default;

    // Load a recording to play back. Must be called before open()/streaming.
    bool loadRecording(const std::string &path, std::string *error = nullptr);

    // ITransport.
    int open() override;
    int close() override;
    int write(const uint8_t *buf, int length) override; // master -> phantom
    int read(uint8_t *buf, int length) override;        // phantom -> master
    bool isOpen() const override { return m_open; }

    // Advance playback by `elapsedMs` of wall-clock time: generates Push
    // frames for the samples that fall in that interval, wrapping to the
    // start of the recording at its end. No-op unless streaming.
    void pump(double elapsedMs);
    bool isStreaming() const { return m_streaming; }

    // Continue playback from an absolute sample index of the recording.
    void seekToSample(uint64_t index);
    uint64_t positionSample() const { return m_playIndex; }
    uint64_t totalSamples() const { return m_reader.sampleCount(); }

    const SessionHeader &recordingHeader() const { return m_reader.header(); }
    bool hasRecording() const { return m_reader.isOpen(); }

private:
    void handleCommand(const uint8_t *payload, int len);
    void queueResponseOk(uint8_t seq);
    void queueError(uint8_t seq, uint8_t code);
    void queueFrame(uint8_t type, uint8_t seq, const std::vector<uint8_t> &payload);
    void emitPushForSamples(uint64_t startIndex, size_t count);

    static uint16_t crc16Modbus(const uint8_t *data, int len);

    bool m_open = false;

    // Parser for Command frames coming from the master (mirrors the transport
    // receiver, but only needs to extract [type][seq][payload]).
    enum class RxState
    {
        WaitStart,
        Type,
        Seq,
        LenLow,
        LenHigh,
        Payload,
        CrcLow,
        CrcHigh
    };
    RxState m_state = RxState::WaitStart;
    bool m_escape = false;
    uint8_t m_rxType = 0;
    uint8_t m_rxSeq = 0;
    uint16_t m_rxLen = 0;
    uint16_t m_rxCrc = 0;
    std::vector<uint8_t> m_rxPayload;
    std::vector<uint8_t> m_crcBuf;
    void feedByte(uint8_t b);

    // Bytes queued for the master to read().
    std::deque<uint8_t> m_txQueue;
    mutable std::mutex m_txMutex;

    // Streaming state.
    bool m_streaming = false;
    std::vector<uint8_t> m_selectedChannels; // set by SetChannels
    std::vector<uint8_t> m_activeChannels;   // streaming now
    uint8_t m_pushSeq = 0;

    // Playback source.
    SessionReader m_reader;
    uint64_t m_playIndex = 0;
    double m_sampleAccumulator = 0.0; // fractional samples pending
    int m_sampleRate = 250;
};

#endif // PHANTOMDEVICE_H
