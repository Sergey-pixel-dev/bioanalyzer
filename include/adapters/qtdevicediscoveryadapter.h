#ifndef QTDEVICEDISCOVERYADAPTER_H
#define QTDEVICEDISCOVERYADAPTER_H

#include <QObject>
#include <QString>
#include <QVector>

#include "core/devicesession.h"

// Qt bridge for the stateless DeviceSession discovery helpers. It only scans
// and probes ports; ownership of the single active session remains in
// AppContext.
class QtDeviceDiscoveryAdapter : public QObject
{
    Q_OBJECT
public:
    struct DeviceEntry
    {
        QString port;
        int baud = 921600;
        quint16 deviceId = 0;
        int channelCount = 0;
        QString firmware;
        QString description;
    };

    explicit QtDeviceDiscoveryAdapter(QObject *parent = nullptr);
    ~QtDeviceDiscoveryAdapter() override;

    const QVector<DeviceEntry> &devices() const { return m_devices; }
    bool isScanning() const { return m_scanning; }

public slots:
    void scan(int baud = 921600, int timeoutMs = 650, int maxAttempts = 3);

signals:
    void scanStarted();
    void scanFinished();
    void devicesChanged();
    void probeDiagnostic(const QString &message);

private:
    QVector<DeviceEntry> m_devices;
    bool m_scanning = false;
};

#endif // QTDEVICEDISCOVERYADAPTER_H
