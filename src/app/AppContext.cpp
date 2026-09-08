#include "app/AppContext.h"

#include "adapters/qtdevicediscoveryadapter.h"
#include "adapters/qtdevicesessionadapter.h"
#include "adapters/qtaiworkeradapter.h"
#include "core/datahub.h"
#include "app/acquisitionservice.h"
#include "app/thememanager.h"
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
    m_recentDatasets = QSettings().value("ai/recentDatasets").toStringList();
    m_recentModels = QSettings().value("ai/recentModels").toStringList();
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

void AppContext::rememberDataset(const QString &path)
{
    if (path.isEmpty())
        return;
    m_recentDatasets.removeAll(path);
    m_recentDatasets.prepend(path);
    while (m_recentDatasets.size() > 10)
        m_recentDatasets.removeLast();
    QSettings().setValue("ai/recentDatasets", m_recentDatasets);
}

void AppContext::rememberModel(const QString &path)
{
    if (path.isEmpty())
        return;
    m_recentModels.removeAll(path);
    m_recentModels.prepend(path);
    while (m_recentModels.size() > 10)
        m_recentModels.removeLast();
    QSettings().setValue("ai/recentModels", m_recentModels);
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
