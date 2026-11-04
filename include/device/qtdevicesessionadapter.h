#ifndef QTDEVICESESSIONADAPTER_H
#define QTDEVICESESSIONADAPTER_H

#include <QObject>
#include <array>
#include <QVector>
#include <QString>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

#include "exchange/applicationprotocol.h"
#include "device/sessionrecord.h"

class DeviceSession;
class ITransport;
class PhantomDevice;
class DataHub;
class QTimer;
class QThread;

// Qt bridge over a Qt-free DeviceSession.
//
// The core session (transport + protocol + buffer) knows nothing about Qt.
// This adapter runs the session's poll loop on a dedicated worker thread.
// Decoded samples leave the core through the shared DataHub; the adapter
// subscribes to it and re-emits each block as samplesReady(), so pages consume
// the stream through ordinary Qt signals.
//
// Threads. While the poll loop runs, every call into the session is posted to
// the session's command queue and executed in the polling thread before the
// next poll. Acknowledgements arrive in the polling thread and are carried
// back to the GUI thread, so the adapter's own fields are touched by the GUI
// thread only.
//
// The active channel set is a standing setting, applied by setActiveChannels()
// the same way as gain or sample rate. startStream() then runs the stream over
// that selection. Only device controls issue these commands: the device page
// and, for the virtual device, the playback bar. Both show the same state,
// reported by streamingChanged().
//
// One adapter == one device (a live serial device or the virtual device that
// plays a recording back).
class QtDeviceSessionAdapter : public QObject
{

    Q_OBJECT
public:
    // A decoded block marshalled for the UI: one inner vector per active
    // channel, values in microvolts. The channel numbers are activeChannels().
    struct SampleBlock
    {
        QVector<QVector<qint32>> samples; // [channel][sample], µV
    };

    explicit QtDeviceSessionAdapter(DataHub *hub, QObject *parent = nullptr);
    ~QtDeviceSessionAdapter() override;

    // Build a live serial session on `portPath`. Replaces any current session.
    void setupSerial(const QString &portPath, int baud = 921600);

    // Build a session on the virtual device that plays a recording back in a
    // loop. Returns false if the recording cannot be loaded. Replaces any
    // current session.
    bool setupPlayback(const QString &recordingPath);

    bool isPlayback() const { return m_phantom != nullptr; }
    bool isConnected() const { return m_connected; }
    int sampleRateHz() const { return m_sampleRateHz; }
    // Amplifier gain of a physical channel: the one the device confirmed (the
    // device's default x6 until a gain is set), or, for a recording played
    // back, the one stored in it.
    int gain(int channel) const;
    int channelCount() const { return m_channelCount; }
    // Whether the device's internal test signal was last acknowledged as
    // enabled. Monitoring uses this to decide whether to show noise metrics.
    bool testSignalEnabled() const { return m_testSignalEnabled; }
    bool testModeEnabled() const { return m_testModeEnabled; }
    // MUX input-short test state (SetShortInput command), which is the mode
    // for which Monitoring displays the per-channel noise estimate.
    bool muxTestEnabled() const { return m_shortInputEnabled; }

    // Channel selection the device has acknowledged (SetChannels, 0x34). It
    // survives stopStream(), so it is the geometry the next stream will use;
    // isStreaming() tells whether samples are flowing right now.
    QVector<quint8> activeChannels() const { return m_activeChannels; }
    bool isStreaming() const { return m_streaming; }
    // StartStream is sent and not yet acknowledged.
    bool isStartPending() const { return m_startPending; }

    // Recording geometry of the virtual device (valid after setupPlayback).
    quint64 playbackTotalSamples() const;
    int playbackSampleRate() const;
    int playbackChannelCount() const;
    // Description the device reported in GetDeviceInfo.
    QString deviceDescription() const { return m_description; }

public slots:
    // Open the transport and start the worker poll loop.
    void connectDevice();
    // Stop streaming, close the transport, stop the worker.
    void disconnectDevice();

    // Device configuration (live or phantom both honour these).
    void setSampleRateIndex(int idx);
    void setGain(int channel, int gainCode);
    void setShortInput();
    void setNormalInput();
    void setTestSignal(int amplitude, int frequency);
    // Channel selection is a separate device command (SetChannels, 0x34),
    // like gain or sample rate: it may be issued only while the stream is
    // stopped, and the selection is kept by the device afterwards. The
    // selection is recorded here only once the device acknowledges it,
    // reported by activeChannelsChanged().
    void setActiveChannels(const QVector<quint8> &channels);
    // Runs the stream over the acknowledged selection; takes no parameters.
    void startStream();
    void stopStream();

    // Recording of the live/played stream into a .bsig file.
    void startRecording(const QString &path, const QString &description,
                        const QVector<quint8> &channels,
                        const QVector<bool> &enabled,
                        const QVector<QString> &labels);
    void stopRecording();

    // Virtual device only: continue playback from `index` (it keeps playing
    // if the stream runs). No-op for a live device.
    void seekToSample(quint64 index);

signals:
    // One decoded block per DataHub publication, marshalled to the GUI thread.
    void samplesReady(const QtDeviceSessionAdapter::SampleBlock &block);
    void activeChannelsChanged(const QVector<quint8> &channels);

    // Command acknowledgements. `ok` false => `error` holds the SerProt code.
    void commandAck(int cmdId, bool ok, int error);
    void connectionChanged(bool connected);
    void testSignalChanged(bool enabled);
    void testModeChanged(bool enabled);
    void muxTestChanged(bool enabled);
    void deviceInfoChanged(int channelCount);
    void deviceDescriptionChanged(const QString &description);
    // The stream started or stopped, whichever control issued the command.
    void streamingChanged(bool streaming);
    // Writing the stream to a file started, failed to start, or stopped.
    void recordingChanged(bool recording);
    // Virtual device only; emitted from the polling thread at most ~15 times
    // per second.
    void playbackPositionChanged(quint64 position, quint64 total);
    void errorOccurred(const QString &message);
    // The link to the device is lost: the port failed, the stream fell
    // silent or a command went unanswered. The adapter has already
    // disconnected (connectionChanged(false) came first).
    void connectionLost(const QString &reason);

private slots:
    void pollOnce();

private:
    void teardownSession();
    void subscribeToHub();
    void unsubscribeFromHub();

    // Run an action on the core session: through its command queue while
    // the poll loop runs, directly otherwise.
    void runOnSession(std::function<void(DeviceSession &)> action);
    // Carry a continuation into the GUI thread.
    void inGuiThread(std::function<void()> fn);
    // Send one protocol command. `send` issues it on the session; `apply`
    // runs in the GUI thread with the acknowledgement result, before
    // commandAck() is emitted.
    using Sender = std::function<int(DeviceSession &, ApplicationProtocol::AckHandler)>;
    void sendCommand(int cmdId, Sender send, std::function<void(bool ok)> apply = {});
    void setStreaming(bool streaming);
    // Settings commands are refused while the stream runs or is starting.
    bool streamBusy() const { return m_streaming || m_startPending; }

    // Link supervision. The first three run in the polling thread and
    // see only its state; handleLinkLost() runs in the GUI thread.
    void superviseLink(int polled);
    void awaitReply();
    void replyReceived();
    void reportLinkLost(const QString &reason);
    void handleLinkLost(const QString &reason);

    DataHub *m_dataHub = nullptr; // required for every DeviceSession
    std::unique_ptr<DeviceSession> m_session;
    ITransport *m_transport = nullptr;  // owned by m_session
    PhantomDevice *m_phantom = nullptr; // non-owning alias when playback

    QThread *m_worker = nullptr;
    QTimer *m_pollTimer = nullptr;

    bool m_connected = false;
    bool m_polling = false;
    bool m_testSignalEnabled = false;
    bool m_shortInputEnabled = false;
    bool m_testModeEnabled = false;
    bool m_streaming = false;
    bool m_startPending = false;  // GUI thread
    int m_commandsInFlight = 0;   // GUI thread: sent, not yet acknowledged
    QString m_portPath;
    int m_pollIntervalMs = 5;
    // Polling-thread only: wall-clock pacing of the virtual device and
    // thinning of position reports.
    std::chrono::steady_clock::time_point m_lastPump;
    std::chrono::steady_clock::time_point m_lastPositionReport;
    // Polling-thread only: when bytes last came in, and how many commands
    // wait for a reply since when.
    std::chrono::steady_clock::time_point m_lastRx;
    std::chrono::steady_clock::time_point m_lastValid; // last growth of valid samples
    uint64_t m_lastSamples = 0;
    uint64_t m_rejectedAtValid = 0;
    std::chrono::steady_clock::time_point m_awaitingSince;
    int m_awaitingReplies = 0;
    bool m_wasExpectingData = false;
    // Set by the GUI thread while the device confirms streaming: only then
    // does silence mean a lost link.
    std::atomic<bool> m_expectData{false};
    // The loss is reported once per connection.
    std::atomic<bool> m_linkLost{false};
    int m_sampleRateHz = ApplicationProtocol::kSampleRateHz[ApplicationProtocol::kDefaultSampleRateIndex];
    // Gain code confirmed by the device per physical channel; -1 if not set.
    // Gain codes the device confirmed; code 0 (x6) is its default after power-on.
    std::array<int, 8> m_gainCodes{};
    int m_channelCount = 8;
    QVector<quint8> m_activeChannels;
    QString m_description;
    quint64 m_hubToken = 0;
};

#endif // QTDEVICESESSIONADAPTER_H
