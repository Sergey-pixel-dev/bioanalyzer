#ifndef AI_INFERENCEPAGE_H
#define AI_INFERENCEPAGE_H

#include "ai/AiPageBase.h"
#include "ai/dataset.h"
#include "ai/slidingwindowaccumulator.h"
#include "device/qtdevicesessionadapter.h"

#include <cstdint>

class QLineEdit;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

class InferencePage : public AiPageBase
{
    Q_OBJECT
public:
    explicit InferencePage(AppContext *context, QWidget *parent = nullptr);

private slots:
    void chooseModel();
    void start();
    void stop();
    void onBlock(const QtDeviceSessionAdapter::SampleBlock &);
    void onStrideChanged(int samples);

private:
    QLineEdit *m_model;
    QLabel *m_descriptor;
    QTableWidget *m_top5;
    QSpinBox *m_inferenceStride = nullptr;
    QPushButton *m_modelBrowse = nullptr;
    QPushButton *m_start;
    QPushButton *m_stop;
    DatasetSpec m_spec;
    bool m_modelLoaded = false;
    SlidingWindowAccumulator m_windowAccumulator;
    uint64_t m_seen = 0;
    uint64_t m_windowsSent = 0;
    uint64_t m_resultsReceived = 0;
    bool m_running = false;
    bool m_acquisitionRequested = false;
};

#endif // AI_INFERENCEPAGE_H
