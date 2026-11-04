#ifndef QTDATASETCAPTUREADAPTER_H
#define QTDATASETCAPTUREADAPTER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include "ai/CueTimer.h"
#include "ai/datasetcapture.h"
#include "ai/qualityregistry.h"

#include <utility>
#include <vector>

// Qt side of dataset capture: owns the Qt-free capture engine and the cue
// timer, and ties them together. The cue timer's phase changes go to the
// engine's command queue and, as signals, to the page that shows the cue.
// The end of the schedule and an operator stop both finish the engine in the
// GUI thread; after the end of the schedule it waits for the check of the
// last trial. Each trial is checked once written; a suspicious one pauses the
// cue timer, and with it the capture, until the operator resumes it.
class QtDatasetCaptureAdapter : public QObject
{
    Q_OBJECT
public:
    explicit QtDatasetCaptureAdapter(DataHub *hub, QObject *parent = nullptr);
    ~QtDatasetCaptureAdapter() override;

    // `labels` is the schedule: one target per trial.
    bool start(const std::string &directory, const DatasetSpec &spec,
               const std::vector<std::string> &labels, bool append = false,
               std::string *error = nullptr);
    // The cue timer starts separately, after the page has connected to the
    // signals: the first cue is emitted at once.
    void startCues();
    void stop();
    // Records trial `cycle` (example `id`) once more at the end of the
    // schedule; the first recording is excluded. False if the capture has
    // ended.
    bool repeatTrial(int cycle, quint64 id);
    // Continues the schedule paused after a suspicious trial.
    void resume();
    bool running() const { return m_engine.running(); }

signals:
    // Trial `cycle` with target `label` entered preparation or recording.
    void cueChanged(int cycle, const QString &label, bool recording, int seconds);
    void countdown(double remainingSeconds);
    // Windows written so far.
    void progress(int cycle, quint64 examples);
    // Trial `cycle`, written as example `id`, was compared with the trials
    // collected before it; `flagged` lists the ids of the checks it failed.
    void trialChecked(int cycle, quint64 id, const QString &label, const QStringList &flagged);
    // The schedule stopped before trial `cycle` (equal to the number of
    // trials after the last one) and waits for resume().
    void paused(int cycle);
    void completed(bool stopped);

private:
    void complete(bool stopped);
    void checkTrial(int cycle, quint64 id, const std::string &label,
                    const std::vector<std::vector<double>> &samples);

    DatasetCaptureEngine m_engine;
    CueTimer m_cueTimer;
    // The schedule has ended; the last trial's check is awaited.
    QTimer m_finishDelay;
    // The last trial checked.
    int m_checked = -1;
    std::vector<std::string> m_labels;
    ParadigmSpec m_paradigm;
    double m_sampleRate = 0.0;
    std::vector<double> m_fullScale;
    // The checks of the task and the measurements of the trials collected so
    // far, the continued dataset's included: each new trial is judged against
    // them, the conditional checks within its group.
    quality::Config m_quality;
    std::vector<std::string> m_groups;
    std::vector<std::vector<quality::Measurement>> m_measurements;
};

#endif
