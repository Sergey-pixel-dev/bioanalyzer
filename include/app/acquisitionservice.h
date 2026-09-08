#ifndef ACQUISITIONSERVICE_H
#define ACQUISITIONSERVICE_H

#include <QObject>
#include <QVector>
#include "adapters/qtdevicesessionadapter.h"

class AppContext;
enum class AcquisitionOwner
{
    Monitoring,
    DatasetCapture,
    Inference,
    Playback
};
struct StreamRequest
{
    AcquisitionOwner owner = AcquisitionOwner::Monitoring;
    QVector<quint8> channels;
};

class AcquisitionService : public QObject
{
    Q_OBJECT
public:
    explicit AcquisitionService(AppContext *context, QObject *parent = nullptr);
    ~AcquisitionService() override;
    bool request(const StreamRequest &request);
    void release(AcquisitionOwner owner);
    bool isOwner(AcquisitionOwner owner) const { return m_hasOwner && m_owner == owner; }
    QVector<quint8> activeChannels() const { return m_channels; }
    QVector<quint8> configuredChannels() const { return m_configuredChannels; }
    bool hasOwner() const { return m_hasOwner; }
signals:
    void samplesReady(const QtDeviceSessionAdapter::SampleBlock &block);
    void ownerChanged(AcquisitionOwner owner);
    void errorOccurred(const QString &message);
private slots:
    void onSessionChanged(class QtDeviceSessionAdapter *session);

private:
    void bindSession(class QtDeviceSessionAdapter *session);
    AppContext *m_context = nullptr;
    class QtDeviceSessionAdapter *m_session = nullptr;
    uint64_t m_token = 0;
    AcquisitionOwner m_owner = AcquisitionOwner::Monitoring;
    bool m_hasOwner = false;
    QVector<quint8> m_channels;
    QVector<quint8> m_configuredChannels;
};

#endif
