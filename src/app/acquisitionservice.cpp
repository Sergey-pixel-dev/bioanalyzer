#include "app/acquisitionservice.h"
#include "app/AppContext.h"
#include "adapters/qtdevicesessionadapter.h"
#include "core/datahub.h"

AcquisitionService::AcquisitionService(AppContext *context, QObject *parent)
    : QObject(parent), m_context(context)
{
    if (m_context)
    {
        connect(m_context, &AppContext::sessionChanged, this, &AcquisitionService::onSessionChanged);
        bindSession(m_context->session());
    }
}

AcquisitionService::~AcquisitionService()
{
    if (m_context && m_token)
        m_context->dataHub()->unsubscribe(m_token);
}

void AcquisitionService::bindSession(QtDeviceSessionAdapter *session)
{
    if (m_context && m_token)
        m_context->dataHub()->unsubscribe(m_token);
    m_token = 0;
    m_session = session;
    if (m_context)
    {
        /* TODO: вот здесь по сути мы из Block структуры преварщаем в структуру SampleBlock, которая содержит меньше инфы о поток. Может этго не делать и передавать чисто Block?  */
        m_token = m_context->dataHub()->subscribe([this](const DataHub::Block &block)
                                                  {
            QtDeviceSessionAdapter::SampleBlock out;
            out.channels.reserve(static_cast<int>(block.channels.size()));
            for (auto channel : block.channels)
                out.channels.push_back(static_cast<quint8>(channel));
            out.samples.reserve(static_cast<int>(block.samples.size()));
            for (const auto &channelSamples : block.samples) {
                QVector<qint32> values;
                values.reserve(static_cast<int>(channelSamples.size()));
                for (auto value : channelSamples)
                    values.push_back(static_cast<qint32>(value));
                out.samples.push_back(std::move(values));
            }
            emit samplesReady(out); });
    }
}

void AcquisitionService::onSessionChanged(QtDeviceSessionAdapter *session)
{
    bindSession(session);
    m_channels.clear();
    m_configuredChannels.clear();
    m_hasOwner = false;
    emit ownerChanged(AcquisitionOwner::Monitoring);
}

bool AcquisitionService::request(const StreamRequest &request)
{
    if (!m_session || !m_session->isConnected() || request.channels.isEmpty())
    {
        emit errorOccurred(QStringLiteral("A connected device and at least one channel are required"));
        return false;
    }
    if (m_hasOwner && m_owner != request.owner)
    {
        emit errorOccurred(QStringLiteral("Device stream is busy"));
        return false;
    }
    // A stream has one immutable channel geometry. Visibility choices on the
    // Monitoring page must never trigger a different MCU channel selection.
    if (m_hasOwner && request.channels != m_channels)
    {
        emit errorOccurred(QStringLiteral("Channel selection cannot change while streaming"));
        return false;
    }
    if (m_hasOwner)
        return true;
    m_owner = request.owner;
    m_channels = request.channels;
    m_configuredChannels = request.channels;
    m_hasOwner = true;
    m_session->startStream(m_channels);
    emit ownerChanged(m_owner);
    return true;
}

void AcquisitionService::release(AcquisitionOwner owner)
{
    if (!m_hasOwner || m_owner != owner)
        return;
    if (m_session)
        m_session->stopStream();
    m_hasOwner = false;
    m_channels.clear();
    emit ownerChanged(AcquisitionOwner::Monitoring);
}
