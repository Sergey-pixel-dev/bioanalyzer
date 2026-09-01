#ifndef DEVICEPAGE_H
#define DEVICEPAGE_H

#include <QWidget>
#include <QVector>
#include "core/ecgadcprotocol.h"

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
// Presents a LIST of devices discovered by an auto-scan. A device only appears
// once it has answered GetDeviceInfo at both the transport and application
// layer, so the list reflects real, reachable hardware attached over a
// UART-USB converter. Each entry shows its human description, firmware and
// channel count, and can be connected with a single click.
//
// Once connected, the ECG/ADC extended settings (fixed internal reference,
// sample rate, active channels) become available. Opening recorded .bsig files is
// handled on the Monitoring page, not here.
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
    void onCommandAck(int cmdId, bool ok, int error);
    void applySettings();
    void toggleStreaming();

private:
    void buildUi();
    void wireSession(QtDeviceSessionAdapter *session);
    QVector<quint8> selectedChannels() const;
    void setControlsEnabled(bool connected);
    void sendNextApplyCommand();

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

    bool m_connected = false;
    bool m_streaming = false;
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
    int m_channelCount = EcgAdcProtocol::kMaxChannels;
};

#endif // DEVICEPAGE_H
