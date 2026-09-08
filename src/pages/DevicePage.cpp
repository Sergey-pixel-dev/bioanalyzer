#include "pages/DevicePage.h"

#include "app/AppContext.h"
#include "app/acquisitionservice.h"
#include "adapters/qtdevicediscoveryadapter.h"
#include "adapters/qtdevicesessionadapter.h"
#include "core/applicationprotocol.h"
#include "ui_devicepage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTimer>

namespace
{
    bool isSettingsCommand(int cmdId)
    {
        switch (cmdId)
        {
        case 0x11: // SetSamplerate
        case 0x30: // SetGain
        case 0x31: // SetShortInput
        case 0x32: // SetTestSignal
        case 0x33: // NormalInput
            return true;
        default:
            return false;
        }
    }
}

DevicePage::DevicePage(AppContext *context, QWidget *parent)
    : QWidget(parent), m_context(context)
{
    buildUi();

    if (m_context && m_context->discovery())
    {
        auto *discovery = m_context->discovery();
        connect(discovery, &QtDeviceDiscoveryAdapter::scanStarted,
                this, &DevicePage::onScanStarted);
        connect(discovery, &QtDeviceDiscoveryAdapter::scanFinished,
                this, &DevicePage::onScanFinished);
        connect(discovery, &QtDeviceDiscoveryAdapter::devicesChanged,
                this, &DevicePage::onDevicesChanged);
    }

    setControlsEnabled(false);
}

DevicePage::~DevicePage() = default;

void DevicePage::buildUi()
{
    Ui::DevicePageForm designerForm;
    designerForm.setupUi(this);
    auto *root = qobject_cast<QVBoxLayout *>(layout());
    if (!root) root = new QVBoxLayout(this);

    // --- Discovery ---
    auto *discoveryBox = new QGroupBox(tr("Available devices"), this);
    auto *discoveryLayout = new QVBoxLayout(discoveryBox);

    auto *scanRow = new QHBoxLayout();
    m_scanButton = new QPushButton(tr("Scan for devices"), discoveryBox);
    scanRow->addWidget(m_scanButton);
    scanRow->addStretch();
    discoveryLayout->addLayout(scanRow);

    m_deviceList = new QListWidget(discoveryBox);
    m_deviceList->setAlternatingRowColors(true);
    m_deviceList->setSelectionMode(QAbstractItemView::SingleSelection);
    discoveryLayout->addWidget(m_deviceList, 1);

    auto *connRow = new QHBoxLayout();
    m_connectButton = new QPushButton(tr("Connect"), discoveryBox);
    connRow->addStretch();
    connRow->addWidget(m_connectButton);
    discoveryLayout->addLayout(connRow);

    root->addWidget(discoveryBox, 1);

    m_statusLabel = new QLabel(tr("Disconnected"), this);
    root->addWidget(m_statusLabel);

    // --- Settings (enabled only when connected) ---
    m_settingsBox = new QGroupBox(tr("ADC settings"), this);
    auto *form = new QFormLayout(m_settingsBox);

    m_gainCombo = new QComboBox(m_settingsBox);
    for (int code = 0; code <= 6; ++code)
        m_gainCombo->addItem(QStringLiteral("×%1 V/V (code %2)")
                                 .arg(ApplicationProtocol::kGainValues[code]).arg(code), code);
    form->addRow(tr("Gain (all selected):"), m_gainCombo);
    m_muxModeCombo = new QComboBox(m_settingsBox);
    m_muxModeCombo->addItem(tr("Normal input"), 0);
    m_muxModeCombo->addItem(tr("Input short"), 1);
    m_muxModeCombo->addItem(tr("Internal test"), 2);
    form->addRow(tr("MUX mode:"), m_muxModeCombo);
    m_testAmplitudeCombo = new QComboBox(m_settingsBox);
    m_testAmplitudeCombo->addItem(tr("1× (1 mV nominal)"), 0);
    m_testAmplitudeCombo->addItem(tr("2× (2 mV nominal)"), 1);
    form->addRow(tr("Test amplitude:"), m_testAmplitudeCombo);
    m_testFrequencyCombo = new QComboBox(m_settingsBox);
    // ADS1298 CONFIG2.TEST_FREQ uses the internal 2.048 MHz clock.
    m_testFrequencyCombo->addItem(tr("0.9766 Hz (fCLK / 2^21)"), 0);
    m_testFrequencyCombo->addItem(tr("1.9531 Hz (fCLK / 2^20)"), 1);
    form->addRow(tr("Internal test frequency:"), m_testFrequencyCombo);

    m_sampleRateCombo = new QComboBox(m_settingsBox);
    // Keep the wire indices stable while presenting rates in ascending order.
    constexpr int sampleRateDisplayIndices[] = {0, 1, 2, 4, 3};
    for (const int i : sampleRateDisplayIndices)
        m_sampleRateCombo->addItem(QStringLiteral("%1 Hz").arg(ApplicationProtocol::kSampleRateHz[i]), i);
    form->addRow(tr("Sample rate:"), m_sampleRateCombo);

    auto *chWidget = new QWidget(m_settingsBox);
    auto *chGrid = new QGridLayout(chWidget);
    chGrid->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < ApplicationProtocol::kMaxChannels; ++i)
    {
        auto *cb = new QCheckBox(QStringLiteral("CH%1").arg(i), chWidget);
        if (i == 0)
            cb->setChecked(true);
        m_channelChecks.push_back(cb);
        chGrid->addWidget(cb, i / 4, i % 4);
    }
    form->addRow(tr("Active channels:"), chWidget);

    auto *btnRow = new QHBoxLayout();
    m_applyButton = new QPushButton(tr("Apply settings"), m_settingsBox);
    m_streamButton = new QPushButton(tr("Start streaming"), m_settingsBox);
    btnRow->addWidget(m_applyButton);
    btnRow->addWidget(m_streamButton);
    btnRow->addStretch();
    form->addRow(btnRow);

    root->addWidget(m_settingsBox);

    connect(m_scanButton, &QPushButton::clicked, this, &DevicePage::onScanClicked);
    connect(m_connectButton, &QPushButton::clicked, this, &DevicePage::onConnectSelected);
    connect(m_deviceList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { onConnectSelected(); });
    connect(m_applyButton, &QPushButton::clicked, this, &DevicePage::applySettings);
    connect(m_streamButton, &QPushButton::clicked, this, &DevicePage::toggleStreaming);
    connect(m_muxModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                const bool test = index == 2 && !m_streaming && !m_applyInProgress;
                m_testFrequencyCombo->setEnabled(test);
                m_testAmplitudeCombo->setEnabled(test);
            });
    m_testFrequencyCombo->setEnabled(false);
    m_testAmplitudeCombo->setEnabled(false);
}

void DevicePage::onScanClicked()
{
    if (m_connected)
    {
        m_statusLabel->setText(tr("Disconnect the device before scanning"));
        return;
    }
    if (m_context && m_context->discovery())
        m_context->discovery()->scan();
}

void DevicePage::onScanStarted()
{
    m_scanButton->setEnabled(false);
    m_scanButton->setText(tr("Scanning..."));
    m_statusLabel->setText(tr("Scanning for devices..."));
}

void DevicePage::onScanFinished()
{
    m_scanButton->setEnabled(true);
    m_scanButton->setText(tr("Scan for devices"));
}

void DevicePage::onDevicesChanged()
{
    if (!m_context || !m_context->discovery())
        return;

    m_deviceList->clear();
    const auto &devices = m_context->discovery()->devices();
    for (const auto &d : devices)
    {
        const QString title = d.description.isEmpty()
                                  ? tr("Device 0x%1").arg(d.deviceId, 4, 16, QChar('0'))
                                  : d.description;
        const QString detail = tr("%1  •  fw %2  •  %3 ch  •  %4")
                                   .arg(title)
                                   .arg(d.firmware)
                                   .arg(d.channelCount)
                                   .arg(d.port);
        auto *item = new QListWidgetItem(detail, m_deviceList);
        item->setData(Qt::UserRole, d.port);
        item->setData(Qt::UserRole + 1, d.baud);
    }

    if (devices.isEmpty())
        m_statusLabel->setText(tr("No devices found"));
    else
        m_statusLabel->setText(tr("Found %1 device(s)").arg(devices.size()));
}

QVector<quint8> DevicePage::selectedChannels() const
{
    QVector<quint8> chans;
    for (int i = 0; i < m_channelChecks.size(); ++i)
        if (m_channelChecks[i]->isChecked())
            chans.push_back(static_cast<quint8>(i));
    return chans;
}

void DevicePage::wireSession(QtDeviceSessionAdapter *session)
{
    if (!session)
        return;
    // A lambda cannot be used with Qt::UniqueConnection (Qt asserts at
    // runtime). Remove any previous wiring to this page before reconnecting.
    disconnect(session, nullptr, this, nullptr);
    connect(session, &QtDeviceSessionAdapter::connectionChanged,
            this, &DevicePage::onConnectionChanged);
    connect(session, &QtDeviceSessionAdapter::commandAck,
            this, &DevicePage::onCommandAck);
    connect(session, &QtDeviceSessionAdapter::deviceInfoChanged,
            this, &DevicePage::onDeviceInfoChanged);
    connect(session, &QtDeviceSessionAdapter::errorOccurred, this, [this](const QString &msg)
            {
                // A local adapter validation error may not produce an ACK.
                // Complete the pending Apply operation immediately instead of
                // leaving the controls disabled waiting for a response that
                // cannot arrive.
                if (m_applyInProgress)
                {
                    m_applyInProgress = false;
                    m_applyFailed = true;
                    m_applyPendingAcks = 0;
                    m_applyError = msg;
                    setControlsEnabled(m_connected);
                    m_statusLabel->setText(tr("Settings failed: %1").arg(msg));
                    return;
                }
                m_statusLabel->setText(tr("Error: %1").arg(msg));
            });
}

void DevicePage::onConnectSelected()
{
    if (!m_context)
        return;

    if (m_connected)
    {
        if (m_context->session())
            m_context->session()->disconnectDevice();
        return;
    }

    QListWidgetItem *item = m_deviceList->currentItem();
    if (!item)
    {
        m_statusLabel->setText(tr("Select a device first"));
        return;
    }

    const QString port = item->data(Qt::UserRole).toString();
    const int baud = item->data(Qt::UserRole + 1).toInt();
    if (port.isEmpty())
        return;

    auto *session = new QtDeviceSessionAdapter(m_context ? m_context->dataHub() : nullptr);
    Q_UNUSED(baud);
    session->setupSerial(port, 921600);
    m_context->setSession(session);
    wireSession(session);
    session->connectDevice();
}

void DevicePage::onConnectionChanged(bool connected)
{
    m_connected = connected;
    m_connectButton->setText(connected ? tr("Disconnect") : tr("Connect"));
    if (!connected)
    {
        // A disconnect invalidates any outstanding settings transaction; its
        // ACKs can no longer be used to determine the result.
        m_applyInProgress = false;
        m_applyPendingAcks = 0;
        ++m_applyGeneration;
        if (m_context && m_context->acquisition())
            m_context->acquisition()->release(AcquisitionOwner::Monitoring);
        m_streaming = false;
        m_streamButton->setText(tr("Start streaming"));
        m_statusLabel->setText(tr("Disconnected"));
    }
    else
    {
        m_statusLabel->setText(tr("Connected"));
    }
    setControlsEnabled(connected);
}

void DevicePage::onDeviceInfoChanged(int channelCount)
{
    m_channelCount = qBound(1, channelCount, ApplicationProtocol::kMaxChannels);
    for (int i = 0; i < m_channelChecks.size(); ++i)
    {
        const bool available = i < m_channelCount;
        m_channelChecks[i]->setVisible(available);
        m_channelChecks[i]->setEnabled(available && !m_streaming && !m_applyInProgress);
        if (!available)
            m_channelChecks[i]->setChecked(false);
    }
    if (selectedChannels().isEmpty() && !m_channelChecks.isEmpty())
        m_channelChecks.first()->setChecked(true);
}

void DevicePage::setControlsEnabled(bool connected)
{
    m_settingsBox->setEnabled(connected);
    const bool editable = connected && !m_streaming && !m_applyInProgress;
    m_gainCombo->setEnabled(editable);
    m_muxModeCombo->setEnabled(editable);
    m_testAmplitudeCombo->setEnabled(editable && m_muxModeCombo->currentData().toInt() == 2);
    m_sampleRateCombo->setEnabled(editable);
    m_applyButton->setEnabled(editable);
    // Do not allow a stream transition while a settings sequence is awaiting
    // acknowledgements; the stream stop/start control remains available in
    // the normal connected state.
    m_streamButton->setEnabled(connected && !m_applyInProgress);
    onDeviceInfoChanged(m_channelCount);
    m_testFrequencyCombo->setEnabled(editable && m_muxModeCombo->currentData().toInt() == 2);
}

void DevicePage::applySettings()
{
    auto *session = m_context ? m_context->session() : nullptr;
    if (!session)
    {
        m_statusLabel->setText(tr("Settings failed: no device connected"));
        return;
    }
    const QVector<quint8> chans = selectedChannels();
    if (chans.isEmpty())
    {
        m_statusLabel->setText(tr("Settings failed: select at least one channel"));
        return;
    }

    // Commands must be sent strictly one at a time.  The MCU has a single
    // command slot (UART RX is intentionally not a command queue), so sending
    // a burst makes all but the first command indistinguishable from a lost
    // response and results in a timeout.
    m_applyInProgress = true;
    m_applyFailed = false;
    m_applyError.clear();
    ++m_applyGeneration;
    m_applyChannels = chans;
    m_applyGainCode = m_gainCombo->currentData().toInt();
    m_applyMuxMode = m_muxModeCombo->currentData().toInt();
    m_applyTestAmplitude = m_testAmplitudeCombo->currentData().toInt();
    m_applyTestFrequency = m_testFrequencyCombo->currentData().toInt();
    m_applySampleRate = m_sampleRateCombo->currentData().toInt();
    // One command is in flight at a time.  The count is only a completion
    // counter; it is not a command queue.
    m_applyPendingAcks = m_applyChannels.size() + 2; // gains + input mode + rate
    m_applyNextCommand = 0;
    m_applyExpectedCommand = -1;
    m_statusLabel->setText(tr("Applying settings..."));
    setControlsEnabled(m_connected);

    sendNextApplyCommand();
}

void DevicePage::sendNextApplyCommand()
{
    if (!m_applyInProgress)
        return;
    const int channelCount = m_applyChannels.size();
    if (m_applyNextCommand >= channelCount + 2)
        return;
    auto *session = m_context ? m_context->session() : nullptr;
    if (!session)
        return;
    const quint64 generation = m_applyGeneration;
    const int commandNumber = m_applyNextCommand++;
    if (commandNumber < channelCount)
    {
        m_applyExpectedCommand = 0x30;
        session->setGain(m_applyChannels[commandNumber], m_applyGainCode);
    }
    else if (commandNumber == channelCount)
    {
        if (m_applyMuxMode == 1)
        {
            m_applyExpectedCommand = 0x31;
            session->setShortInput();
        }
        else if (m_applyMuxMode == 2)
        {
            m_applyExpectedCommand = 0x32;
            session->setTestSignal(m_applyTestAmplitude, m_applyTestFrequency);
        }
        else
        {
            m_applyExpectedCommand = 0x33;
            session->setNormalInput();
        }
    }
    else
    {
        m_applyExpectedCommand = 0x11;
        session->setSampleRateIndex(m_applySampleRate);
    }
    QTimer::singleShot(1200, this, [this, generation, commandNumber]
                       {
        // A later ACK advances m_applyNextCommand. Only the still outstanding
        // command may trip this watchdog.
        if (!m_applyInProgress || m_applyGeneration != generation ||
            m_applyNextCommand != commandNumber + 1)
            return;
        m_applyInProgress = false;
        m_applyPendingAcks = 0;
        ++m_applyGeneration;
        m_applyFailed = true;
        m_applyError = tr("timed out waiting for device acknowledgement");
        setControlsEnabled(m_connected);
        m_statusLabel->setText(tr("Settings failed: %1").arg(m_applyError));
    });
}

void DevicePage::toggleStreaming()
{
    auto *session = m_context ? m_context->session() : nullptr;
    if (!session)
        return;

    if (m_streaming)
    {
        if (m_context && m_context->acquisition())
            m_context->acquisition()->release(AcquisitionOwner::Monitoring);
    }
    else
    {
        const QVector<quint8> chans = selectedChannels();
        if (chans.isEmpty())
        {
            m_statusLabel->setText(tr("Select at least one channel"));
            return;
        }
        // Sample rate is applied by Apply settings.  Do not send another
        // command immediately before StartStream: the MCU accepts one
        // outstanding command at a time and has no command queue.
        if (m_context && m_context->acquisition() &&
            !m_context->acquisition()->request({AcquisitionOwner::Monitoring, chans})) {
            m_statusLabel->setText(tr("Unable to start stream"));
        }
    }
}

void DevicePage::onCommandAck(int cmdId, bool ok, int error)
{
    if (m_applyInProgress && isSettingsCommand(cmdId) &&
        cmdId == m_applyExpectedCommand)
    {
        if (!ok && !m_applyFailed)
        {
            m_applyFailed = true;
            m_applyError = tr("command 0x%1 failed (error %2)")
                               .arg(cmdId, 2, 16, QChar('0'))
                               .arg(error);
            m_applyInProgress = false;
            m_applyPendingAcks = 0;
            ++m_applyGeneration;
            setControlsEnabled(m_connected);
            m_statusLabel->setText(tr("Settings failed: %1").arg(m_applyError));
            return;
        }

        if (m_applyPendingAcks > 0)
            --m_applyPendingAcks;
        if (m_applyPendingAcks == 0)
        {
            m_applyInProgress = false;
            setControlsEnabled(m_connected);
            if (m_applyFailed)
                m_statusLabel->setText(tr("Settings failed: %1").arg(m_applyError));
            else
                m_statusLabel->setText(tr("Settings applied successfully"));
        }
        else if (!m_applyFailed)
        {
            // Wait for this ACK before putting the next command on UART5.
            sendNextApplyCommand();
        }
        return;
    }

    if (!ok)
    {
        m_statusLabel->setText(tr("Command 0x%1 failed (err %2)")
                                   .arg(cmdId, 2, 16, QChar('0'))
                                   .arg(error));
        return;
    }

    if (cmdId == 0x01) // StartStream
    {
        m_streaming = true;
        m_streamButton->setText(tr("Stop streaming"));
        m_statusLabel->setText(tr("Streaming"));
    }
    else if (cmdId == 0x02) // StopStream
    {
        m_streaming = false;
        m_streamButton->setText(tr("Start streaming"));
        m_statusLabel->setText(tr("Idle"));
    }
    setControlsEnabled(m_connected);
}
