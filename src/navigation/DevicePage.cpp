#include "navigation/DevicePage.h"

#include "navigation/AppContext.h"
#include "notifications/Notifications.h"
#include "device/qtdevicediscoveryadapter.h"
#include "device/qtdevicesessionadapter.h"
#include "exchange/applicationprotocol.h"
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
#include <QFileDialog>
#include <QInputDialog>
#include <QTimer>

namespace
{
    // Описание записи или устройства вводится свободно: в нём бывают
    // переводы строк и сотни символов. В строке списка оно сводится к одной
    // строке ограниченной длины, полный текст — во всплывающей подсказке.
    constexpr int kDescriptionChars = 60;

    QString shortDescription(const QString &description)
    {
        const QString line = description.simplified();
        return line.size() > kDescriptionChars ? line.left(kDescriptionChars - 1) + QChar(0x2026) : line;
    }
}


namespace
{
    // The list entry of the virtual device carries this flag.
    constexpr int kVirtualRole = Qt::UserRole + 2;

    bool isSettingsCommand(int cmdId)
    {
        switch (cmdId)
        {
        case 0x11: // SetSamplerate
        case 0x30: // SetGain
        case 0x31: // SetShortInput
        case 0x32: // SetTestSignal
        case 0x33: // NormalInput
        case 0x34: // SetChannels
            return true;
        default:
            return false;
        }
    }
}

DevicePage::DevicePage(AppContext *context, QWidget *parent)
    : QWidget(parent), m_context(context)
{
    setObjectName(QStringLiteral("device"));
    buildUi();
    if (m_context && m_context->notifications())
        m_context->notifications()->attachStatusLabel(objectName(), m_statusLabel);

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

    connect(m_deviceList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *) { updateConnectButton(); });
    populateDeviceList();
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
    m_recordButton = new QPushButton(tr("Record"), m_settingsBox);
    m_recordButton->setCheckable(true);
    btnRow->addWidget(m_applyButton);
    btnRow->addWidget(m_streamButton);
    btnRow->addSpacing(24);
    btnRow->addWidget(m_recordButton);
    btnRow->addStretch();
    form->addRow(btnRow);

    root->addWidget(m_settingsBox);

    connect(m_scanButton, &QPushButton::clicked, this, &DevicePage::onScanClicked);
    connect(m_connectButton, &QPushButton::clicked, this, &DevicePage::onConnectSelected);
    connect(m_deviceList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { onConnectSelected(); });
    connect(m_applyButton, &QPushButton::clicked, this, &DevicePage::applySettings);
    connect(m_streamButton, &QPushButton::clicked, this, &DevicePage::toggleStreaming);
    connect(m_recordButton, &QPushButton::clicked, this, &DevicePage::toggleRecording);
    connect(m_muxModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                const bool test = index == 2 && !m_streaming && !m_applyInProgress && !m_virtual;
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
        fail(tr("Disconnect the device before scanning"));
        return;
    }
    if (m_context && m_context->discovery())
        m_context->discovery()->scan();
}

void DevicePage::onScanStarted()
{
    m_scanButton->setEnabled(false);
    m_scanButton->setText(tr("Scanning..."));
    m_context->notifications()->status(QStringLiteral("device"), tr("Scanning for devices..."));
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

    populateDeviceList();
    const auto &devices = m_context->discovery()->devices();
    if (devices.isEmpty())
        m_context->notifications()->status(QStringLiteral("device"), tr("No devices found"));
    else
        m_context->notifications()->status(QStringLiteral("device"), tr("Found %n device(s)", nullptr, int(devices.size())));
}

void DevicePage::populateDeviceList()
{
    m_deviceList->clear();
    // Виртуальное устройство в списке есть всегда: оно воспроизводит запись
    // сеанса и позволяет работать без аппаратуры.
    auto *virt = new QListWidgetItem(virtualItemText(), m_deviceList);
    virt->setData(kVirtualRole, true);

    if (m_context && m_context->discovery())
    {
        for (const auto &d : m_context->discovery()->devices())
        {
            const QString title = d.description.isEmpty()
                                      ? tr("Device 0x%1").arg(d.deviceId, 4, 16, QChar('0'))
                                      : shortDescription(d.description);
            const QString detail = tr("%1  •  fw %2  •  %3 ch  •  %4")
                                       .arg(title)
                                       .arg(d.firmware)
                                       .arg(d.channelCount)
                                       .arg(d.port);
            auto *item = new QListWidgetItem(detail, m_deviceList);
            item->setData(Qt::UserRole, d.port);
            item->setData(Qt::UserRole + 1, d.baud);
            item->setToolTip(d.description);
        }
    }
    updateConnectButton();
}

bool DevicePage::virtualSelected() const
{
    const QListWidgetItem *item = m_deviceList->currentItem();
    return item && item->data(kVirtualRole).toBool();
}

QString DevicePage::virtualItemText() const
{
    QString text = tr("Virtual device (recording playback)");
    if (m_virtual && m_connected)
        text += tr("  •  %1  •  %2 ch")
                    .arg(m_description.isEmpty() ? tr("no description") : shortDescription(m_description))
                    .arg(m_channelCount);
    return text;
}

void DevicePage::refreshVirtualItem()
{
    for (int i = 0; i < m_deviceList->count(); ++i)
    {
        QListWidgetItem *item = m_deviceList->item(i);
        if (item->data(kVirtualRole).toBool())
        {
            item->setText(virtualItemText());
            item->setToolTip(m_virtual && m_connected ? m_description : QString());
        }
    }
}

void DevicePage::updateConnectButton()
{
    if (m_connected)
        m_connectButton->setText(tr("Disconnect"));
    else if (virtualSelected())
        m_connectButton->setText(tr("Choose recording…"));
    else
        m_connectButton->setText(tr("Connect"));
}

void DevicePage::chooseRecording()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose recording"), QString(), tr("Biosignal recordings (*.bsig)"));
    if (path.isEmpty())
        return;

    auto *session = new QtDeviceSessionAdapter(m_context ? m_context->dataHub() : nullptr);
    if (!session->setupPlayback(path))
    {
        delete session;
        fail(tr("Cannot open recording: %1").arg(path));
        return;
    }
    // Выбор записи — это подключение виртуального устройства; её параметры
    // применяются автоматически, как только устройство сообщит сведения о себе.
    m_virtual = true;
    m_autoApplyPending = true;
    m_description.clear();
    m_context->setSession(session);
    wireSession(session);
    session->connectDevice();
}

void DevicePage::applyRecordingSettings()
{
    auto *session = m_context ? m_context->session() : nullptr;
    if (!session)
        return;
    // Частота — записанная; другой виртуальное устройство не примет.
    const int rate = session->playbackSampleRate();
    int rateIndex = -1;
    for (int i = 0; i < m_sampleRateCombo->count(); ++i)
        if (ApplicationProtocol::kSampleRateHz[m_sampleRateCombo->itemData(i).toInt()] == rate)
            rateIndex = i;
    if (rateIndex < 0)
    {
        fail(tr("The recording's sample rate %1 Hz is not supported").arg(rate));
        return;
    }
    m_sampleRateCombo->setCurrentIndex(rateIndex);
    m_gainCombo->setCurrentIndex(0);
    m_muxModeCombo->setCurrentIndex(0); // normal input: a recording has no other
    for (int i = 0; i < m_channelChecks.size(); ++i)
        m_channelChecks[i]->setChecked(i < m_channelCount);
    applySettings();
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
    connect(session, &QtDeviceSessionAdapter::connectionLost, this, [this](const QString &reason)
            {
                // Причину выводит окно главного окна; строка показывает состояние.
                Q_UNUSED(reason);
                m_context->notifications()->status(QStringLiteral("device"), tr("Connection lost")); });
    connect(session, &QtDeviceSessionAdapter::commandAck,
            this, &DevicePage::onCommandAck);
    connect(session, &QtDeviceSessionAdapter::deviceInfoChanged,
            this, &DevicePage::onDeviceInfoChanged);
    connect(session, &QtDeviceSessionAdapter::deviceDescriptionChanged,
            this, &DevicePage::onDeviceDescriptionChanged);
    connect(session, &QtDeviceSessionAdapter::streamingChanged,
            this, &DevicePage::onStreamingChanged);
    connect(session, &QtDeviceSessionAdapter::recordingChanged, this, [this](bool recording)
            {
                // The file is open only once the session confirms it.
                m_recording = recording;
                m_recordButton->setChecked(recording);
                m_recordButton->setText(recording ? tr("Stop recording") : tr("Record"));
                setControlsEnabled(m_connected); });
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
                    m_context->notifications()->status(QStringLiteral("device"), tr("Settings not applied"));
                }
                // Само сообщение об ошибке выводит окном главное окно,
                // подписанное на сеанс: второе окно здесь не нужно.
                Q_UNUSED(msg);
            });
}

void DevicePage::fail(const QString &text)
{
    if (m_context && m_context->notifications())
        m_context->notifications()->warning(this, tr("Device"), text);
}

void DevicePage::settingsFailed()
{
    m_context->notifications()->status(QStringLiteral("device"), tr("Settings not applied"));
    fail(tr("Settings failed: %1").arg(m_applyError));
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
        fail(tr("Select a device first"));
        return;
    }
    if (item->data(kVirtualRole).toBool())
    {
        chooseRecording();
        return;
    }

    const QString port = item->data(Qt::UserRole).toString();
    const int baud = item->data(Qt::UserRole + 1).toInt();
    if (port.isEmpty())
        return;

    auto *session = new QtDeviceSessionAdapter(m_context ? m_context->dataHub() : nullptr);
    Q_UNUSED(baud);
    session->setupSerial(port, 921600);
    m_virtual = false;
    m_autoApplyPending = false;
    m_context->setSession(session);
    wireSession(session);
    session->connectDevice();
}

void DevicePage::onConnectionChanged(bool connected)
{
    m_connected = connected;
    if (!connected)
    {
        m_virtual = false;
        m_autoApplyPending = false;
        m_description.clear();
        // The session closed its file together with the connection.
        m_recording = false;
        m_recordButton->setChecked(false);
        m_recordButton->setText(tr("Record"));
        // A disconnect invalidates any outstanding settings transaction; its
        // ACKs can no longer be used to determine the result.
        m_applyInProgress = false;
        m_applyPendingAcks = 0;
        ++m_applyGeneration;
        m_streaming = false;
        m_streamButton->setText(tr("Start streaming"));
        m_context->notifications()->status(QStringLiteral("device"), tr("Disconnected"));
    }
    else
    {
        m_context->notifications()->status(QStringLiteral("device"), tr("Connected"));
    }
    updateConnectButton();
    refreshVirtualItem();
    setControlsEnabled(connected);
}

void DevicePage::onDeviceInfoChanged(int channelCount)
{
    m_channelCount = qBound(1, channelCount, ApplicationProtocol::kMaxChannels);
    updateChannelChecks();
    if (m_virtual)
    {
        refreshVirtualItem();
        if (m_autoApplyPending)
        {
            m_autoApplyPending = false;
            applyRecordingSettings();
        }
    }
}

void DevicePage::onDeviceDescriptionChanged(const QString &description)
{
    m_description = description;
    if (m_virtual)
        refreshVirtualItem();
}

void DevicePage::onStreamingChanged(bool streaming)
{
    // Поток запускают и страница устройства, и панель воспроизведения;
    // кнопка показывает состояние сеанса, а не то, кто его изменил.
    m_streaming = streaming;
    m_streamButton->setText(streaming ? tr("Stop streaming") : tr("Start streaming"));
    m_context->notifications()->status(QStringLiteral("device"),
                                       streaming ? tr("Streaming") : tr("Idle"));
    setControlsEnabled(m_connected);
}

void DevicePage::updateChannelChecks()
{
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
    // Пока устройство не подтвердило запуск потока, настройки не меняются:
    // оно, возможно, уже передаёт данные.
    const auto *session = m_context ? m_context->session() : nullptr;
    const bool starting = session && session->isStartPending();
    const bool editable = connected && !m_streaming && !m_applyInProgress && !starting;
    // У виртуального устройства параметры — записанные: меняется только
    // выбор среди записанных каналов.
    const bool deviceSettings = editable && !m_virtual;
    m_gainCombo->setEnabled(deviceSettings);
    m_muxModeCombo->setEnabled(deviceSettings);
    m_testAmplitudeCombo->setEnabled(deviceSettings && m_muxModeCombo->currentData().toInt() == 2);
    m_sampleRateCombo->setEnabled(deviceSettings);
    m_applyButton->setEnabled(editable);
    // Do not allow a stream transition while a settings sequence is awaiting
    // acknowledgements; the stream stop/start control remains available in
    // the normal connected state.
    m_streamButton->setEnabled(connected && !m_applyInProgress && !starting);
    // A recording starts only on a running stream of a real device; a running
    // recording can always be stopped.
    m_recordButton->setEnabled(m_recording || (connected && m_streaming && !m_virtual));
    m_recordButton->setToolTip(m_virtual ? tr("The virtual device already plays a recording")
                                         : tr("Record the stream to a .bsig file"));
    updateChannelChecks();
    m_testFrequencyCombo->setEnabled(deviceSettings && m_muxModeCombo->currentData().toInt() == 2);
}

void DevicePage::applySettings()
{
    auto *session = m_context ? m_context->session() : nullptr;
    if (!session)
    {
        fail(tr("Settings failed: no device connected"));
        return;
    }
    const QVector<quint8> chans = selectedChannels();
    if (chans.isEmpty())
    {
        fail(tr("Settings failed: select at least one channel"));
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
    // Набор каналов применяется такой же командой, как усиление и частота,
    // и потому входит в ту же последовательность.
    m_applyPendingAcks = m_applyChannels.size() + 3; // каналы + усиления + режим входа + частота
    m_applyNextCommand = 0;
    m_applyExpectedCommand = -1;
    m_context->notifications()->status(QStringLiteral("device"), tr("Applying settings..."));
    setControlsEnabled(m_connected);

    sendNextApplyCommand();
}

void DevicePage::sendNextApplyCommand()
{
    if (!m_applyInProgress)
        return;
    const int channelCount = m_applyChannels.size();
    if (m_applyNextCommand >= channelCount + 3)
        return;
    auto *session = m_context ? m_context->session() : nullptr;
    if (!session)
        return;
    const quint64 generation = m_applyGeneration;
    const int commandNumber = m_applyNextCommand++;
    if (commandNumber == 0)
    {
        // Выбор каналов идёт первым: от него зависит размер кадра данных,
        // а усиление задаётся для каждого выбранного канала.
        m_applyExpectedCommand = 0x34;
        session->setActiveChannels(m_applyChannels);
    }
    else if (commandNumber <= channelCount)
    {
        m_applyExpectedCommand = 0x30;
        session->setGain(m_applyChannels[commandNumber - 1], m_applyGainCode);
    }
    else if (commandNumber == channelCount + 1)
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
        settingsFailed();
    });
}

void DevicePage::toggleStreaming()
{
    auto *session = m_context ? m_context->session() : nullptr;
    if (!session)
        return;

    if (m_streaming)
    {
        session->stopStream();
        return;
    }

    // Набор каналов, усиление и частота применяются кнопкой «Применить»
    // отдельными командами. Устройство принимает одну команду за раз и не
    // имеет очереди, поэтому перед StartStream ничего не отправляется:
    // команда лишь включает передачу данных при действующих настройках.
    const QVector<quint8> applied = session->activeChannels();
    if (applied.isEmpty())
    {
        fail(tr("Apply settings first: the channel selection is not set"));
        return;
    }
    if (applied != selectedChannels())
    {
        fail(tr("Channel selection changed: apply settings before streaming"));
        return;
    }
    session->startStream();
    setControlsEnabled(m_connected);
}

void DevicePage::toggleRecording()
{
    auto *session = m_context ? m_context->session() : nullptr;
    if (!session)
    {
        m_recordButton->setChecked(false);
        return;
    }

    if (m_recording)
    {
        // The button follows recordingChanged().
        session->stopRecording();
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save recording"), QStringLiteral("session.bsig"),
        tr("Biosignal recordings (*.bsig)"));
    if (path.isEmpty())
    {
        m_recordButton->setChecked(false);
        return;
    }

    // A free-text description is stored in the .bsig header, so the operator
    // can note what the recording is (subject, montage, notes...). The virtual
    // device later shows it as its description.
    bool ok = false;
    const QString description = QInputDialog::getMultiLineText(
        this, tr("Recording description"),
        tr("Describe this recording (optional):"),
        tr("Recording session"), &ok);
    if (!ok)
    {
        m_recordButton->setChecked(false);
        return;
    }

    // The raw stream of every acquired channel is written; display settings
    // and filters of other pages do not touch it.
    const QVector<quint8> channels = session->activeChannels();
    QVector<bool> enabled(channels.size(), true);
    QVector<QString> labels;
    labels.reserve(channels.size());
    for (quint8 channel : channels)
        labels.push_back(QStringLiteral("CH%1").arg(channel));
    // The button follows recordingChanged(), which also reports a failure to
    // open the file.
    m_recordButton->setChecked(false);
    session->startRecording(path, description, channels, enabled, labels);
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
            settingsFailed();
            return;
        }

        if (m_applyPendingAcks > 0)
            --m_applyPendingAcks;
        if (m_applyPendingAcks == 0)
        {
            m_applyInProgress = false;
            setControlsEnabled(m_connected);
            if (m_applyFailed)
                settingsFailed();
            else
                m_context->notifications()->status(QStringLiteral("device"), tr("Settings applied successfully"));
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
        setControlsEnabled(m_connected);
        fail(tr("Command 0x%1 failed (error %2)")
                 .arg(cmdId, 2, 16, QChar('0'))
                 .arg(error));
        return;
    }

    // The stream state itself arrives through streamingChanged().
    setControlsEnabled(m_connected);
}
