#ifndef APPCONTEXT_H
#define APPCONTEXT_H

#include <QObject>
#include <memory>

class QtDeviceDiscoveryAdapter;
class QtDeviceSessionAdapter;
class QtAiWorkerAdapter;
class DataHub;
class AcquisitionService;
class ThemeManager;
class Notifications;
class SettingsService;

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
    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    QtDeviceDiscoveryAdapter *discovery() const { return m_discovery; }
    QtDeviceSessionAdapter *session() const { return m_session; }
    DataHub *dataHub() const { return m_dataHub; }
    QtAiWorkerAdapter *aiWorker() const { return m_aiWorker; }
    AcquisitionService *acquisition() const { return m_acquisition; }
    ThemeManager *themeManager() const { return m_themeManager; }
    Notifications *notifications() const { return m_notifications; }
    SettingsService *settings() const { return m_settings; }

    // Replace the current session adapter. Takes ownership. The previous
    // session is deleted after emitting sessionChanged().
    void setSession(QtDeviceSessionAdapter *session);

signals:
    void sessionChanged(QtDeviceSessionAdapter *session);

private:
    QtDeviceDiscoveryAdapter *m_discovery = nullptr;
    QtDeviceSessionAdapter *m_session = nullptr;
    DataHub *m_dataHub = nullptr;
    QtAiWorkerAdapter *m_aiWorker = nullptr;
    AcquisitionService *m_acquisition = nullptr;
    ThemeManager *m_themeManager = nullptr;
    Notifications *m_notifications = nullptr;
    SettingsService *m_settings = nullptr;
};

#endif // APPCONTEXT_H
