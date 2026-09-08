#ifndef DATASETCAPTURE_H
#define DATASETCAPTURE_H

#include "core/datahub.h"
#include "core/dataset.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include <utility>

// Qt-free engine for cue-driven dataset capture. The engine subscribes to the
// shared DataHub and reports state through standard-library callbacks.
class DatasetCaptureEngine
{
public:
    using ProgressCallback = std::function<void(int labelIndex, int repetition,
                                                  double seconds,
                                                  const std::string &label)>;
    using CompletedCallback = std::function<void(bool stopped)>;

    explicit DatasetCaptureEngine(DataHub *hub = nullptr);
    ~DatasetCaptureEngine();

    void setProgressCallback(ProgressCallback callback) { m_progress = std::move(callback); }
    void setCompletedCallback(CompletedCallback callback) { m_completed = std::move(callback); }

    bool start(const std::string &directory, const DatasetSpec &spec,
               const std::vector<std::string> &labels, int repetitions = 1);
    void stop();
    bool running() const { return m_running; }

private:
    void onBlock(const DataHub::Block &block);

    DataHub *m_hub = nullptr;
    uint64_t m_token = 0;
    uint64_t m_seen = 0;
    bool m_running = false;
    int m_prep = 5;
    int m_duration = 7;
    int m_repetitions = 1;
    std::vector<std::string> m_labels;
    std::unique_ptr<Dataset> m_dataset;
    DatasetSpec m_spec;
    std::vector<std::deque<int32_t>> m_history;
    ProgressCallback m_progress;
    CompletedCallback m_completed;
};

#endif
