#include "navigation/AppContext.h"

#include "device/qtdevicediscoveryadapter.h"
#include "device/qtdevicesessionadapter.h"
#include "ai/qtaiworkeradapter.h"
#include "datahub/datahub.h"
#include "device/acquisitionservice.h"
#include "notifications/Notifications.h"
#include "settings/SettingsService.h"
#include "settings/thememanager.h"
#include <QApplication>
#include <QCoreApplication>
#include <QSettings>

AppContext::AppContext(QObject *parent)
    : QObject(parent),
      m_discovery(new QtDeviceDiscoveryAdapter(this))
{
    m_dataHub = new DataHub();
    m_themeManager = new ThemeManager(qobject_cast<QApplication *>(QCoreApplication::instance()), this);
    QSettings settings;
    m_themeManager->restore(settings);
    m_aiWorker = new QtAiWorkerAdapter(this);
    m_acquisition = new AcquisitionService(this, this);
    m_notifications = new Notifications(this);
    m_settings = new SettingsService(this);
}

AppContext::~AppContext()
{
    // Session and acquisition service borrow DataHub. Destroy all QObject
    // consumers first so their final flush/unsubscribe operations still see
    // a live bus; QObject removes explicitly deleted children from its list.
    delete m_session;
    m_session = nullptr;
    delete m_acquisition;
    m_acquisition = nullptr;
    m_themeManager = nullptr;
    delete m_aiWorker;
    m_aiWorker = nullptr;
    delete m_discovery;
    m_discovery = nullptr;
    delete m_dataHub;
    m_dataHub = nullptr;
}

void AppContext::setSession(QtDeviceSessionAdapter *session)
{
    if (m_session == session)
        return;

    QtDeviceSessionAdapter *old = m_session;
    m_session = session;
    if (m_session)
    {
        m_session->setParent(this);
    }

    emit sessionChanged(m_session);

    if (old)
        old->deleteLater();
}
