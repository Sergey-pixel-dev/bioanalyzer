#ifndef DEVICEPAGE_H
#define DEVICEPAGE_H

#include <QWidget>
#include <QVector>
#include "exchange/applicationprotocol.h"

class AppContext;
class QtDeviceSessionAdapter;
class QComboBox;
class QPushButton;
class QCheckBox;
class QLabel;
class QGroupBox;
class QListWidget;

// Device management page ("Devices").
//
// Presents a LIST of devices. The first entry is always the virtual device,
// which plays a recorded .bsig session back in a loop; for it the Connect
// button becomes "Choose recording…". The other entries are discovered by a
// scan: a device only appears once it has answered GetDeviceInfo at both the
// transport and application layer, so the list reflects real, reachable
// hardware attached over a UART-USB converter.
//
// Once connected, the ECG/ADC settings (sample rate, gain, input mode, active
// channels) become available. For the virtual device they come from the
// recording and are frozen, except the channel selection among the recorded
// channels; they are applied automatically once the recording is chosen.
//
// The stream button reflects the session's stream state, which the playback
// bar may change too (streamingChanged). Next to it the Record button writes
// the stream to a .bsig file; recording belongs to the device, not to any one
// mode, and is not offered for the virtual device, whose stream already is a
// recording.
class DevicePage : public QWidget
{
    Q_OBJECT
public:
    explicit DevicePage(AppContext *context, QWidget *parent = nullptr);
    ~DevicePage() override;

private slots:
    void onScanClicked();
    void onScanStarted();
    void onScanFinished();
    void onDevicesChanged();
    void onConnectSelected();
    void onConnectionChanged(bool connected);
    void onDeviceInfoChanged(int channelCount);
    void onDeviceDescriptionChanged(const QString &description);
    void onStreamingChanged(bool streaming);
    void onCommandAck(int cmdId, bool ok, int error);
    void applySettings();
    void toggleStreaming();
    void toggleRecording();

private:
    // Ошибки и отказы — окном службы сообщений; строка — только состояние.
    void fail(const QString &text);
    void settingsFailed();
    void buildUi();
    void wireSession(QtDeviceSessionAdapter *session);
    QVector<quint8> selectedChannels() const;
    void setControlsEnabled(bool connected);
    void updateChannelChecks();
    void sendNextApplyCommand();

    // Virtual device.
    void populateDeviceList();
    bool virtualSelected() const;
    QString virtualItemText() const;
    void refreshVirtualItem();
    void updateConnectButton();
    void chooseRecording();
    void applyRecordingSettings();

    AppContext *m_context = nullptr;

    QPushButton *m_scanButton = nullptr;
    QListWidget *m_deviceList = nullptr;
    QPushButton *m_connectButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    QGroupBox *m_settingsBox = nullptr;
    QComboBox *m_gainCombo = nullptr;
    QComboBox *m_muxModeCombo = nullptr;
    QComboBox *m_testAmplitudeCombo = nullptr;
    QComboBox *m_testFrequencyCombo = nullptr;
    QComboBox *m_sampleRateCombo = nullptr;
    QVector<QCheckBox *> m_channelChecks;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_streamButton = nullptr;
    QPushButton *m_recordButton = nullptr;

    bool m_connected = false;
    bool m_streaming = false;
    bool m_virtual = false;          // the connected device is the virtual one
    bool m_recording = false;        // the stream is being written to a file
    bool m_autoApplyPending = false; // apply the recording's settings once known
    QString m_description;
    bool m_applyInProgress = false;
    bool m_applyFailed = false;
    int m_applyPendingAcks = 0;
    int m_applyNextCommand = 0;
    int m_applyExpectedCommand = -1;
    QVector<quint8> m_applyChannels;
    int m_applyGainCode = 0;
    int m_applyMuxMode = 0;
    int m_applyTestAmplitude = 0;
    int m_applyTestFrequency = 0;
    int m_applySampleRate = 0;
    quint64 m_applyGeneration = 0;
    QString m_applyError;
    int m_channelCount = ApplicationProtocol::kMaxChannels;
};

#endif // DEVICEPAGE_H
