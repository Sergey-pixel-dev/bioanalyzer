#ifndef APPCONTEXT_H
#define APPCONTEXT_H

// sessionChanged() carries the session adapter, and moc needs its complete
// definition: otherwise the meta-type is incomplete in one translation unit
// and complete in another.
#include "device/qtdevicesessionadapter.h"

#include <QObject>
#include <memory>

class QtDeviceDiscoveryAdapter;
class DataHub;
class ThemeManager;
class Notifications;

// Shared application state passed to the pages.
//
// Owns the discovery adapter and the single "current" device session adapter.
// The device page creates/replaces the current session. Only device controls
// start or stop its stream — the device page and, for the virtual device, the
// playback bar; the other pages consume the samples it emits.
// sessionChanged() lets pages re-wire when the active session is swapped
// (e.g. switching from a live device to the virtual one).
class AppContext : public QObject
{
    Q_OBJECT
public:
    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    QtDeviceDiscoveryAdapter *discovery() const { return m_discovery; }
    QtDeviceSessionAdapter *session() const { return m_session; }
    DataHub *dataHub() const { return m_dataHub; }
    ThemeManager *themeManager() const { return m_themeManager; }
    Notifications *notifications() const { return m_notifications; }

    // Replace the current session adapter. Takes ownership. The previous
    // session is deleted after emitting sessionChanged().
    void setSession(QtDeviceSessionAdapter *session);

signals:
    void sessionChanged(QtDeviceSessionAdapter *session);

private:
    QtDeviceDiscoveryAdapter *m_discovery = nullptr;
    QtDeviceSessionAdapter *m_session = nullptr;
    DataHub *m_dataHub = nullptr;
    ThemeManager *m_themeManager = nullptr;
    Notifications *m_notifications = nullptr;
};

#endif // APPCONTEXT_H
