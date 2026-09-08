#ifndef QTDATASETCAPTUREADAPTER_H
#define QTDATASETCAPTUREADAPTER_H

#include <QObject>
#include <QString>
#include "core/datasetcapture.h"

class QtDatasetCaptureAdapter : public QObject
{
    Q_OBJECT
public:
    explicit QtDatasetCaptureAdapter(DataHub *hub, QObject *parent = nullptr);
    ~QtDatasetCaptureAdapter() override;

    bool start(const std::string &directory, const DatasetSpec &spec,
               const std::vector<std::string> &labels, int repetitions = 1);
    void stop();
    bool running() const { return m_engine.running(); }

signals:
    void progress(int labelIndex, int repetition, double seconds, const QString &label);
    void completed(bool stopped);

private:
    DatasetCaptureEngine m_engine;
};

#endif
