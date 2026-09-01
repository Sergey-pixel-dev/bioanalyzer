#ifndef QTDEVICESESSIONADAPTER_H
#define QTDEVICESESSIONADAPTER_H

#include <QObject>
#include <QVector>
#include <QString>
#include <memory>
#include <vector>
#include <cstdint>

#include "core/ecgadcprotocol.h"
#include "core/sessionrecord.h"

class DeviceSession;
class ITransport;
class PhantomDevice;
class QTimer;
class QThread;
class DataHub;

// Qt bridge over a Qt-free DeviceSession.
//
// The core session (transport + protocol + buffer) knows nothing about Qt.
// This adapter runs the session's poll loop on a dedicated worker thread and
// re-emits decoded sample frames as Qt signals on that thread; the UI connects
// with queued connections so data crosses to the GUI thread safely.
//
// It exposes command slots (connect, configure, stream, record) plus — when
// wrapping a PhantomDevice — playback controls (play/pause/seek/speed).
//
// One adapter == one device (live serial or phantom playback).
class QtDeviceSessionAdapter : public QObject
{
    Q_OBJECT
public:
    // A decoded block marshalled for the UI: one inner vector per active
    // channel, values in microvolts.
    struct SampleBlock
    {
        QVector<quint8> channels;         // physical channel indices
        QVector<QVector<qint32>> samples; // [channel][sample], µV
    };

    explicit QtDeviceSessionAdapter(QObject *parent = nullptr);
    ~QtDeviceSessionAdapter() override;

    // Build a live serial session on `portPath`. Replaces any current session.
    void setupSerial(const QString &portPath, int baud = 921600);
    void setDataHub(DataHub *hub);

    // Build a playback session backed by a recording file. Returns false if
    // the recording cannot be loaded. Replaces any current session.
    bool setupPlayback(const QString &recordingPath);

    bool isPlayback() const { return m_phantom != nullptr; }
    bool isConnected() const { return m_connected; }
    int sampleRateHz() const { return m_sampleRateHz; }
    int channelCount() const { return m_channelCount; }
    // Whether the device's internal test signal was last acknowledged as
    // enabled. Monitoring uses this to decide whether to show noise metrics.
    bool testSignalEnabled() const { return m_testSignalEnabled; }
    bool testModeEnabled() const { return m_testModeEnabled; }
    // MUX input-short test state (SetShortInput command), which is the mode
    // for which Monitoring displays the per-channel noise estimate.
    bool muxTestEnabled() const { return m_shortInputEnabled; }

    // Playback geometry (valid after setupPlayback).
    quint64 playbackTotalSamples() const;
    int playbackSampleRate() const;
    // Physical channel indices stored in the loaded recording.
    QVector<quint8> playbackChannels() const;

public slots:
    // Open the transport and start the worker poll loop.
    void connectDevice();
    // Stop streaming, close the transport, stop the worker.
    void disconnectDevice();

    // Device configuration (live or phantom both honour these).
    void setVref(int mV);
    void setSampleRateIndex(int idx);
    void setGain(int channel, int gainCode);
    void setShortInput(bool enable);
    void setNormalInput();
    void setTestSignal(int amplitude, int frequency);
    void startStream(const QVector<quint8> &channels);
    void stopStream();

    // Recording of the live/played stream into a .bsig file.
    void startRecording(const QString &path, const QString &description,
                        const QVector<quint8> &channels,
                        const QVector<bool> &enabled,
                        const QVector<QString> &labels);
    void stopRecording();

    // Playback transport controls (no-op for live sessions).
    void play();
    void pause();
    void seekToSample(quint64 index);
    void setPlaybackSpeed(double factor);

signals:
    // Emitted (queued) when a new sample block is decoded.
    void samplesReady(const QtDeviceSessionAdapter::SampleBlock &block);
    // Command acknowledgements. `ok` false => `error` holds the SerProt code.
    void commandAck(int cmdId, bool ok, int error);
    void connectionChanged(bool connected);
    void testSignalChanged(bool enabled);
    void testModeChanged(bool enabled);
    void muxTestChanged(bool enabled);
    void deviceInfoChanged(int channelCount);
    void playbackPositionChanged(quint64 sampleIndex);
    void errorOccurred(const QString &message);

private slots:
    void pollOnce();

private:
    void teardownSession();
    void emitSampleFrame(const EcgAdcProtocol::SampleFrame &frame);

    std::unique_ptr<DeviceSession> m_session;
    ITransport *m_transport = nullptr;  // owned by m_session
    PhantomDevice *m_phantom = nullptr; // non-owning alias when playback

    QThread *m_worker = nullptr;
    QTimer *m_pollTimer = nullptr;

    bool m_connected = false;
    bool m_testSignalEnabled = false;
    bool m_shortInputEnabled = false;
    bool m_testModeEnabled = false;
    bool m_streaming = false;
    int m_pollIntervalMs = 5;
    int m_sampleRateHz = EcgAdcProtocol::kSampleRateHz[EcgAdcProtocol::kDefaultSampleRateIndex];
    int m_channelCount = 8;
};

#endif // QTDEVICESESSIONADAPTER_H
