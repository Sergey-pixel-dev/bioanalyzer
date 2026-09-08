#include "adapters/qtdatasetcaptureadapter.h"

QtDatasetCaptureAdapter::QtDatasetCaptureAdapter(DataHub *hub, QObject *parent)
    : QObject(parent), m_engine(hub)
{
    m_engine.setProgressCallback([this](int labelIndex, int repetition, double seconds,
                                        const std::string &label)
                                 { emit progress(labelIndex, repetition, seconds, QString::fromStdString(label)); });
    m_engine.setCompletedCallback([this](bool stopped)
                                  { emit completed(stopped); });
}

QtDatasetCaptureAdapter::~QtDatasetCaptureAdapter() = default;

bool QtDatasetCaptureAdapter::start(const std::string &directory, const DatasetSpec &spec,
                                    const std::vector<std::string> &labels, int repetitions)
{
    return m_engine.start(directory, spec, labels, repetitions);
}

void QtDatasetCaptureAdapter::stop()
{
    m_engine.stop();
}
