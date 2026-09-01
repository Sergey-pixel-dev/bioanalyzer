#include "adapters/qtdevicediscoveryadapter.h"

#include <QtConcurrent>
#include <QFutureWatcher>
#include <QDebug>
#include <utility>

namespace
{
QString probeStatusText(DeviceSession::ProbeStatus status)
{
    switch (status)
    {
    case DeviceSession::ProbeStatus::OpenFailed: return QStringLiteral("open failed");
    case DeviceSession::ProbeStatus::Timeout: return QStringLiteral("timeout");
    case DeviceSession::ProbeStatus::InvalidResponse: return QStringLiteral("invalid device info");
    case DeviceSession::ProbeStatus::ProtocolError: return QStringLiteral("device error");
    case DeviceSession::ProbeStatus::TransportError: return QStringLiteral("transport/CRC error");
    case DeviceSession::ProbeStatus::Success: return QStringLiteral("success");
    }
    return QStringLiteral("unknown");
}
} // namespace

QtDeviceDiscoveryAdapter::QtDeviceDiscoveryAdapter(QObject *parent)
    : QObject(parent)
{
}

QtDeviceDiscoveryAdapter::~QtDeviceDiscoveryAdapter() = default;

void QtDeviceDiscoveryAdapter::scan(int baud, int timeoutMs, int maxAttempts)
{
    if (m_scanning)
        return;

    m_scanning = true;
    emit scanStarted();

    struct ScanResult
    {
        std::vector<DeviceSession::DiscoveredDevice> devices;
        std::vector<std::pair<std::string, DeviceSession::ProbeResult>> diagnostics;
    };

    auto *watcher = new QFutureWatcher<ScanResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher]()
            {
                const auto scanResult = watcher->result();
                const auto &found = scanResult.devices;
                m_devices.clear();
                for (const auto &device : found)
                {
                    DeviceEntry entry;
                    entry.port = QString::fromStdString(device.port);
                    entry.baud = device.baud;
                    entry.deviceId = device.info.deviceId;
                    entry.channelCount = device.info.channelCount;
                    entry.firmware = QStringLiteral("%1.%2")
                                         .arg(device.info.fwMajor)
                                         .arg(device.info.fwMinor);
                    entry.description = QString::fromStdString(device.info.description);
                    m_devices.push_back(std::move(entry));
                }
                for (const auto &diagnostic : scanResult.diagnostics)
                {
                    QString message = QStringLiteral("%1: %2")
                                          .arg(QString::fromStdString(diagnostic.first))
                                          .arg(probeStatusText(diagnostic.second.status));
                    if (diagnostic.second.attempts > 0)
                        message += QStringLiteral(" after %1 attempt(s)")
                                       .arg(diagnostic.second.attempts);
                    if (diagnostic.second.status == DeviceSession::ProbeStatus::ProtocolError)
                        message += QStringLiteral(" (code 0x%1)")
                                       .arg(diagnostic.second.errorCode, 2, 16, QChar('0'));
                    qInfo().noquote() << "Discovery" << message;
                    emit probeDiagnostic(message);
                }
                m_scanning = false;
                emit devicesChanged();
                emit scanFinished();
                watcher->deleteLater();
            });

    auto future = QtConcurrent::run([baud, timeoutMs, maxAttempts]()
                                    {
                                        ScanResult result;
                                        for (const auto &port : DeviceSession::discoverPorts())
                                        {
                                            const auto probe = DeviceSession::probePortDetailed(
                                                port.path, baud, timeoutMs, maxAttempts);
                                            result.diagnostics.emplace_back(port.path, probe);
                                            if (probe.ok())
                                            {
                                                DeviceSession::DiscoveredDevice device;
                                                device.port = port.path;
                                                device.baud = baud;
                                                device.info = probe.info;
                                                result.devices.push_back(std::move(device));
                                            }
                                        }
                                        return result;
                                    });
    watcher->setFuture(future);
}
