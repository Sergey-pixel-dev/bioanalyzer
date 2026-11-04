#ifndef DATASET_H
#define DATASET_H
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>

// Kinds of task this version implements: classification of gestures with a
// label target. A description of another kind is read (its type becomes
// Unknown) and refused by isSupportedTask(). Adding a kind of task means a
// value here and its name in toString()/...FromString(), and the places that
// depend on it, all listed at isSupportedTask().
enum class TaskType { Classification, Unknown };
enum class TargetType { Label, Unknown };

struct TargetValue
{
    TargetType type = TargetType::Unknown;
    std::string label;
};

struct EventSpec
{
    std::string id;
    std::string cueText;
    TargetValue target;
    // In a dataset, the repetitions of the value planned in its last capture
    // session (0 --- the value was skipped in it); a continuation offers them
    // again.
    int repetitions = 0;
};

// The only kind of capture: trials by a cue schedule ("kind": "cue_schedule"
// in the description; another kind is refused).
struct ParadigmSpec
{
    int prepSeconds = 5;
    int recordSeconds = 7;
    bool randomize = true;
};

// Requirements of the task to the stream; in a dataset's copy of the task,
// what the stream actually was.
struct CaptureSpec
{
    std::vector<uint8_t> channels;
    uint32_t sampleRate = 250;
    std::string units = "uV";
};

// Versions of the formats this program reads: of task descriptions and of
// datasets. A file of another version is not opened.
constexpr int kTaskSchemaVersion = 4;
constexpr int kDatasetSchemaVersion = 5;

struct TaskDescriptor
{
    int schemaVersion = kTaskSchemaVersion;
    std::string taskId;
    std::string displayName;
    TaskType taskType = TaskType::Classification;
    TargetType targetType = TargetType::Label;
    ParadigmSpec paradigm;
    std::vector<EventSpec> events;
    CaptureSpec capture;
    // The "quality" section: checks of the trials (qualityregistry.h).
    nlohmann::json quality = nlohmann::json::object();
};

const char *toString(TaskType type);
const char *toString(TargetType type);
TaskType taskTypeFromString(const std::string &value);
TargetType targetTypeFromString(const std::string &value);
bool loadTaskDescriptor(const std::string &path, TaskDescriptor &out,
                        std::string *error = nullptr);
nlohmann::json targetToJson(const TargetValue &target);
TargetValue targetFromJson(const nlohmann::json &value);

// True if this version implements the task: classification with a label
// target. Everything that depends on the kind of task, and would change for a
// new kind, is: the task list of the collection page and the capture itself
// (labels by a cue schedule); targetToJson()/targetFromJson(); the target's
// text and editor on the Datasets page; quality::groupKey(); the model's
// output kind and its view on the recognition page. A model is matched to a
// dataset by what the dataset holds --- the target_type of the model's meta()
// against the kind of the dataset's targets, and the signal --- not by the
// task the dataset was collected for, and with no code per kind.
bool isSupportedTask(const TaskDescriptor &task);

// Factual capture metadata is kept alongside the requested task descriptor.
// The same structure describes a model: its window, the stride of
// recognition, the model family and the kind of its output.
struct DatasetSpec
{
    TaskDescriptor task;
    std::string modelFamily;
    std::string outputKind; // models only: "top_k"
    uint32_t sampleRate = 250;
    size_t windowSamples = 0;
    size_t strideSamples = 0; // models only; a dataset has one window per trial
    std::vector<uint8_t> channels;
    std::string units = "uV";
    // Amplifier gain per channel: reference information. The samples are
    // microvolts at any gain; only the saturation check takes the ADC range
    // from it (fullScaleMicrovolts()).
    std::vector<int> gain;
};

// Full scale of the ADC per channel, microvolts: the device's fixed 2.4 V
// reference over the gain. Empty if any gain is unknown.
std::vector<double> fullScaleMicrovolts(const std::vector<int> &gain);

// Returns the next non-existing bundle directory below a selected parent.
std::string nextDatasetBundlePath(const std::string &parent,
                                  const std::string &prefix = "dataset");

struct ExampleRecord
{
    uint64_t id = 0;
    // The value the cue presented; the target starts equal to it and may be
    // corrected later on the Datasets page.
    TargetValue cue;
    TargetValue target;
    std::string recordedAt; // local time, ISO 8601
    std::vector<std::vector<int32_t>> samples;
};

// Lock of a dataset directory against a second writer, in this process and
// in another copy of the program: the capture holds it while it writes the
// dataset, the Datasets page while it rewrites the list of trials. It is an
// advisory flock() on <dataset>/.lock (LockFileEx() on Windows): every
// acquire() opens the file anew, so two holders in one process exclude each
// other as well, and the system drops the lock when its process ends, so a
// crash leaves no stale lock.
class DatasetLock
{
public:
    enum class Result
    {
        Acquired,
        Busy,  // held by a capture or an edit elsewhere
        Failed // the lock file cannot be created (e.g. a read-only folder)
    };
    DatasetLock() = default;
    ~DatasetLock() { release(); }
    DatasetLock(const DatasetLock &) = delete;
    DatasetLock &operator=(const DatasetLock &) = delete;
    Result acquire(const std::string &directory);
    void release();
#ifdef _WIN32
    bool held() const { return m_handle != nullptr; }
#else
    bool held() const { return m_fd >= 0; }
#endif

private:
#ifdef _WIN32
    void *m_handle = nullptr; // HANDLE of the lock file
#else
    int m_fd = -1;
#endif
};

// Unified .bset reader/writer. A dataset created with create() is writable;
// a dataset returned by open() is read-only and exposes its manifest;
// openForAppend() reopens a compatible dataset to add trials to it.
class Dataset
{
public:
    static std::unique_ptr<Dataset> create(const std::string &directory,
                                           const DatasetSpec &spec,
                                           std::string *error = nullptr);
    static std::unique_ptr<Dataset> open(const std::string &directory,
                                         std::string *error = nullptr);
    // Reopens an existing dataset to continue capturing into it. A
    // continuation changes no recording parameter: the task, the preparation
    // and recording time (the window), the sample rate, the channels, the
    // gain and the units must be the dataset's, and every target value must
    // already be in its vocabulary; only the number of trials of each value
    // is free, and its plan replaces the dataset's one. Example numbers
    // continue after the last one, and the trials go to a new session file.
    static std::unique_ptr<Dataset> openForAppend(const std::string &directory,
                                                  const DatasetSpec &capture,
                                                  std::string *error = nullptr);

    ~Dataset();

    // Changes to written trials that wait for the end of the capture: the list
    // of trials is rewritten once, by its writer, when the dataset is closed.
    struct PendingUpdates
    {
        std::set<uint64_t> repeated; // trials recorded again: excluded
    };

    bool append(const ExampleRecord &record);
    void finish(const std::string &status = "complete", const PendingUpdates &updates = {});
    bool isOpen() const { return m_open; }
    bool writable() const { return m_writable; }
    const DatasetSpec &spec() const { return m_spec; }
    size_t exampleCount() const { return m_id; }
    const std::string &status() const { return m_status; }
    // Number of the capture session written now: 0 for a new dataset, the
    // last session plus one when capture continues an existing dataset.
    uint32_t session() const { return m_session; }

private:
    Dataset() = default;
    bool createFiles(const std::string &directory, const DatasetSpec &spec,
                     std::string *error);
    bool readManifest(const std::string &directory, std::string *error);
    bool reopenFiles(const DatasetSpec &capture, std::string *error);
    // Taken before the first write and held until finish(): rewriting the
    // list meanwhile would lose trials, since the writer keeps appending to
    // the replaced file, and a second writer would reuse example numbers.
    bool lockForWriting(std::string *error);

    std::string m_dir;
    std::ofstream m_index;
    DatasetSpec m_spec;
    uint64_t m_id = 0;
    uint32_t m_session = 0;
    bool m_open = false;
    bool m_writable = false;
    // Signal of the session written now: one file, the trials one after
    // another; opened at the first trial.
    std::ofstream m_data;
    std::string m_dataFile;    // relative to the dataset, e.g. data/session-000.bin
    uint64_t m_dataOffset = 0; // samples per channel already in the file
    DatasetLock m_lock;
    std::string m_status;
};
// Rewrites the list of trials (examples.jsonl) through a temporary file,
// passing every record to `update`; a damaged line leaves the file as it was.
bool rewriteExamples(const std::string &directory,
                     const std::function<void(nlohmann::json &item)> &update,
                     std::string *error = nullptr);
#endif
