#ifndef APPCONTEXT_H
#define APPCONTEXT_H

#include <QObject>
#include <memory>
#include <QStringList>

class QtDeviceDiscoveryAdapter;
class QtDeviceSessionAdapter;
class QtAiWorkerAdapter;
class DataHub;

// Shared application state passed to the pages.
//
// Owns the discovery adapter and the single "current" device session adapter.
// The device page creates/replaces the current session; the
// monitoring page consumes it. sessionChanged() lets pages re-wire when the
// active session is swapped (e.g. switching from live capture to playback).
class AppContext : public QObject
{
    Q_OBJECT
public:
    enum class AcquisitionMode { Idle, Monitoring, DatasetCapture, Inference, Playback };
    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    QtDeviceDiscoveryAdapter *discovery() const { return m_discovery; }
    QtDeviceSessionAdapter *session() const { return m_session; }
    DataHub *dataHub() const { return m_dataHub; }
    QtAiWorkerAdapter *aiWorker() const { return m_aiWorker; }
    AcquisitionMode acquisitionMode() const { return m_mode; }
    const QStringList &recentDatasets() const { return m_recentDatasets; }
    const QStringList &recentModels() const { return m_recentModels; }
    void rememberDataset(const QString &path);
    void rememberModel(const QString &path);

    // Replace the current session adapter. Takes ownership. The previous
    // session is deleted after emitting sessionChanged().
    void setSession(QtDeviceSessionAdapter *session);
    bool acquireMode(AcquisitionMode mode);
    void releaseMode(AcquisitionMode mode);

signals:
    void sessionChanged(QtDeviceSessionAdapter *session);
    void acquisitionModeChanged(AppContext::AcquisitionMode mode);

private:
    QtDeviceDiscoveryAdapter *m_discovery = nullptr;
    QtDeviceSessionAdapter *m_session = nullptr;
    DataHub *m_dataHub = nullptr;
    QtAiWorkerAdapter *m_aiWorker = nullptr;
    AcquisitionMode m_mode = AcquisitionMode::Idle;
    QStringList m_recentDatasets;
    QStringList m_recentModels;
};

#endif // APPCONTEXT_H
