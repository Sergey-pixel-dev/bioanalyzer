#ifndef DATASETCAPTURE_H
#define DATASETCAPTURE_H
#include "core/datahub.h"
#include <QObject>
#include <QString>
#include <deque>
#include "core/dataset.h"
class DatasetCaptureController : public QObject
{
    Q_OBJECT
public:
    explicit DatasetCaptureController(DataHub *hub=nullptr, QObject *parent=nullptr); ~DatasetCaptureController() override;
    bool start(const std::string &dir,const DatasetSpec &spec,const std::vector<std::string>&labels,int repetitions=1,int prepSeconds=5,int recordSeconds=7);
    void stop(); bool running() const{return m_running;}
signals:
    void progress(int labelIndex, int repetition, double seconds, const QString &label);
    void completed(bool stopped);
private:
    void onBlock(const DataHub::Block&);
    DataHub *m_hub;
    uint64_t m_token = 0, m_seen = 0;
    bool m_running = false;
    int m_prep = 5, m_duration = 7, m_repetition = 0, m_repetitions = 1;
    std::vector<std::string> m_labels;
    DatasetWriter m_writer;
    DatasetSpec m_spec;
    // Push blocks are not guaranteed to contain a complete training window.
    // Retain the recent samples so examples can span block boundaries.
    std::vector<std::deque<int32_t>> m_history;
};
#endif
