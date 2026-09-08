#ifndef PHANTOMDEVICE_H
#define PHANTOMDEVICE_H

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <memory>

#include "core/itransport.h"
#include "core/sessionrecord.h"

// Virtual device that behaves like the real ECG/ADC MCU over SerProt.
//
// It implements ITransport, so the exact same TransportProtocol +
// ApplicationProtocol stack drives it — nothing above the transport layer knows
// whether it is talking to real hardware or this phantom. That keeps the
// playback path identical to the live path.
//
// The phantom parses Command frames the master writes (stream control,
// configuration, and the global InputShort/TestSignal/NormalInput modes),
// answers with Response frames, and — while streaming — emits Push frames.
// The Push payload comes
// from a recorded .bsig session (playback) advanced by pump(), which the
// owner calls on a timer at the recording's sample rate.
//
// Playback controls (play/pause/seek/speed) let the UI scrub a recording.
//
// Pure C++/POSIX: no Qt.
class PhantomDevice : public ITransport
{
public:
    PhantomDevice() = default;
    ~PhantomDevice() override = default;

    // Load a recording to play back. Must be called before open()/streaming.
    bool loadRecording(const std::string &path);

    // ITransport.
    int open() override;
    int close() override;
    int write(const uint8_t *buf, int length) override; // master -> phantom
    int read(uint8_t *buf, int length) override;        // phantom -> master
    bool isOpen() const override { return m_open; }

    // Advance playback by `elapsedMs` of wall-clock time. Generates Push
    // frames for the samples that fall in that interval (scaled by speed).
    // Call from a timer. No-op unless streaming and playing.
    void pump(double elapsedMs);

    // Playback transport controls.
    void play() { m_playing = true; }
    void pause() { m_playing = false; }
    bool isPlaying() const { return m_playing; }
    void setSpeed(double factor) { m_speed = factor > 0 ? factor : 1.0; }
    double speed() const { return m_speed; }

    // Seek to an absolute sample index in the recording.
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
    bool m_playing = false;
    // Global ADS1298 input MUX mode (0x31, 0x32 or 0x33).
    uint8_t m_inputMode = 0x33;
    double m_speed = 1.0;
    std::vector<uint8_t> m_activeChannels; // physical indices requested
    uint8_t m_pushSeq = 0;

    // Playback source.
    SessionReader m_reader;
    uint64_t m_playIndex = 0;
    double m_sampleAccumulator = 0.0; // fractional samples pending
    int m_sampleRate = 250;
};

#endif // PHANTOMDEVICE_H
