#ifndef DATASETCAPTURE_H
#define DATASETCAPTURE_H

#include "datahub/datahub.h"
#include "datahub/commandqueue.h"
#include "ai/dataset.h"
#include "ai/slidingwindowaccumulator.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <deque>
#include <string>
#include <vector>
#include <utility>

// Qt-free engine for cue-driven dataset capture — the control object of the
// "collect a labelled dataset" use case.
//
// Threads. onBlock() runs in the packet-receiving thread. The cue timer lives
// in the GUI thread and changes the phase through setPhase(), which is queued
// and applied at the start of the next block, so a block is never labelled
// half with the old target and half with the new one. start() prepares all
// state before subscribing; finish() unsubscribes first (which waits for a
// block in progress) and only then closes the dataset, so closing never
// meets a window being written.
//
// One trial gives one example: the window spans the whole recording phase
// (window = sample rate x recording time) and never covers two targets. It is
// cut by the SlidingWindowAccumulator shared with inference; the preparation
// phase is not stored. The cue timer runs on the wall clock and the stream on
// the device clock, so a recording phase may end a block or so before the
// window is full; the next phase then waits until the window is complete.
class DatasetCaptureEngine
{
public:
    // Called from the packet-receiving thread after each written window.
    using ProgressCallback = std::function<void(int cycle, uint64_t examples)>;
    // Called from the packet-receiving thread with each written example and
    // its number in the dataset; the trial check reads its samples.
    using ExampleCallback = std::function<void(int cycle, uint64_t id, const ExampleRecord &record)>;

    explicit DatasetCaptureEngine(DataHub *hub = nullptr);
    ~DatasetCaptureEngine();

    void setProgressCallback(ProgressCallback callback) { m_progress = std::move(callback); }
    void setExampleCallback(ExampleCallback callback) { m_example = std::move(callback); }

    // GUI thread. `labels` is the whole schedule: one target per trial.
    // With `append` the trials are added to the existing dataset in
    // `directory` (see Dataset::openForAppend). On failure *error says why.
    bool start(const std::string &directory, const DatasetSpec &spec,
               const std::vector<std::string> &labels, bool append = false,
               std::string *error = nullptr);
    // GUI thread, queued: trial `cycle` entered its preparation or recording.
    void setPhase(int cycle, bool recording);
    // GUI thread: stop receiving blocks and close the dataset.
    void finish(bool stopped);
    // GUI thread: example `id` will be recorded again; it is excluded with the
    // reason "repeated" when the dataset is closed.
    void markRepeated(uint64_t id);
    // GUI thread, queued: one more trial with target `label` at the end of the
    // schedule (the repeat of a trial).
    void appendLabel(const std::string &label);
    bool running() const { return m_running; }

private:
    void onBlock(const DataHub::Block &block);
    void applyPhase(int cycle, bool recording);
    void writeWindow(const SlidingWindowAccumulator::Window &window);
    void unsubscribe();

    DataHub *m_hub = nullptr;
    uint64_t m_token = 0;
    bool m_running = false;
    CommandQueue m_queue;

    // Owned by the packet-receiving thread while subscribed.
    DatasetSpec m_spec;
    std::vector<std::string> m_labels;
    std::unique_ptr<Dataset> m_dataset;
    SlidingWindowAccumulator m_accumulator;
    uint64_t m_examples = 0;
    int m_cycle = -1;
    bool m_recording = false;
    bool m_windowDone = false; // this trial's window is written
    // Phase changes that arrived before this trial's window was complete, in
    // order (with no preparation, a trial's recording follows at once).
    std::deque<std::pair<int, bool>> m_pendingPhases;
    ProgressCallback m_progress;
    ExampleCallback m_example;
    // Owned by the GUI thread: applied by the dataset's writer in finish().
    Dataset::PendingUpdates m_pending;
};

#endif
