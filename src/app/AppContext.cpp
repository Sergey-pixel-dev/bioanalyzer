#include "app/AppContext.h"

#include "adapters/qtdevicediscoveryadapter.h"
#include "adapters/qtdevicesessionadapter.h"
#include "adapters/qtaiworkeradapter.h"
#include "core/datahub.h"
#include <QSettings>

AppContext::AppContext(QObject *parent)
    : QObject(parent),
      m_discovery(new QtDeviceDiscoveryAdapter(this))
{
    m_dataHub = new DataHub();
    m_aiWorker = new QtAiWorkerAdapter(this);
    m_recentDatasets = QSettings().value("ai/recentDatasets").toStringList();
    m_recentModels = QSettings().value("ai/recentModels").toStringList();
}

AppContext::~AppContext() { delete m_dataHub; }

void AppContext::rememberDataset(const QString &path)
{
    if (path.isEmpty()) return;
    m_recentDatasets.removeAll(path); m_recentDatasets.prepend(path);
    while (m_recentDatasets.size() > 10) m_recentDatasets.removeLast();
    QSettings().setValue("ai/recentDatasets", m_recentDatasets);
}

void AppContext::rememberModel(const QString &path)
{
    if (path.isEmpty()) return;
    m_recentModels.removeAll(path); m_recentModels.prepend(path);
    while (m_recentModels.size() > 10) m_recentModels.removeLast();
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
        m_session->setDataHub(m_dataHub);
    }

    emit sessionChanged(m_session);

    if (old)
        old->deleteLater();
}

bool AppContext::acquireMode(AcquisitionMode mode)
{
    if (m_mode != AcquisitionMode::Idle && m_mode != mode)
        return false;
    if (m_mode == mode)
        return true;
    m_mode = mode;
    emit acquisitionModeChanged(m_mode);
    return true;
}

void AppContext::releaseMode(AcquisitionMode mode)
{
    if (m_mode != mode)
        return;
    m_mode = AcquisitionMode::Idle;
    emit acquisitionModeChanged(m_mode);
}
