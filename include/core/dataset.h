#ifndef DATASET_H
#define DATASET_H
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>

enum class TaskType { Classification, Regression, Detection, Anomaly, Unknown };
enum class TargetType { Label, Value, Spans, None, Unknown };

struct TargetValue
{
    TargetType type = TargetType::None;
    std::string label;
    double value = 0.0;
    std::vector<std::pair<uint64_t, uint64_t>> spans;
    nlohmann::json extra = nlohmann::json::object();
};

struct EventSpec
{
    std::string id;
    std::string cueText;
    TargetValue target;
};

struct ParadigmSpec
{
    enum class Kind { CueSchedule, FreeRecording, Unlabeled };
    Kind kind = Kind::CueSchedule;
    int prepSeconds = 5;
    int recordSeconds = 7;
    bool randomize = true;
    std::string hotkeys;
    int durationSeconds = 0;
};

struct CaptureSpec
{
    std::vector<uint8_t> channels;
    uint32_t sampleRate = 250;
    size_t windowSamples = 0;
    size_t strideSamples = 0;
    std::string units = "uV";
    double scale = 1e-6;
};

struct TaskDescriptor
{
    int schemaVersion = 3;
    std::string taskId;
    std::string displayName;
    TaskType taskType = TaskType::Classification;
    TargetType targetType = TargetType::Label;
    ParadigmSpec paradigm;
    std::vector<EventSpec> events;
    CaptureSpec capture;
    nlohmann::json extra = nlohmann::json::object();
};

const char *toString(TaskType type);
const char *toString(TargetType type);
TaskType taskTypeFromString(const std::string &value);
TargetType targetTypeFromString(const std::string &value);
bool loadTaskDescriptor(const std::string &path, TaskDescriptor &out,
                        std::string *error = nullptr);

struct LabelSpec
{
    std::string id;
    std::string displayName;
};

// Factual capture metadata is kept alongside the requested task descriptor.
struct DatasetSpec
{
    TaskDescriptor task;
    std::string modelFamily;
    uint32_t sampleRate = 250;
    size_t windowSamples = 0, strideSamples = 0;
    std::vector<uint8_t> channels;
    std::string preprocessing = "none";
    std::string normalization = "none";
    std::string units = "uV";
    double scale = 1e-6;
    nlohmann::json extra = nlohmann::json::object();
};

// Returns the label target vocabulary declared by the task events.
std::vector<LabelSpec> labelVocabulary(const DatasetSpec &spec);

// Returns the next non-existing bundle directory below a selected parent.
std::string nextDatasetBundlePath(const std::string &parent,
                                  const std::string &prefix = "dataset");

struct ExampleRecord
{
    uint64_t id = 0;
    TargetValue target;
    uint64_t sourceSample = 0;
    std::vector<std::vector<int32_t>> samples;
    nlohmann::json extra = nlohmann::json::object();
};

// Unified .bset reader/writer. A dataset created with create() is writable;
// a dataset returned by open() is read-only and exposes its manifest.
class Dataset
{
public:
    static std::unique_ptr<Dataset> create(const std::string &directory,
                                           const DatasetSpec &spec,
                                           std::string *error = nullptr);
    static std::unique_ptr<Dataset> open(const std::string &directory,
                                         std::string *error = nullptr);

    bool append(const ExampleRecord &record);
    bool appendEvent(uint64_t tSample, const std::string &eventId,
                     const TargetValue &target);
    void finish(const std::string &status="complete");
    bool isOpen() const { return m_open; }
    bool writable() const { return m_writable; }
    const DatasetSpec &spec() const { return m_spec; }
    size_t exampleCount() const { return m_id; }
    const std::string &status() const { return m_status; }

private:
    Dataset() = default;
    bool createFiles(const std::string &directory, const DatasetSpec &spec,
                     std::string *error);
    bool readManifest(const std::string &directory, std::string *error);

    std::string m_dir;
    std::ofstream m_index;
    std::ofstream m_events;
    DatasetSpec m_spec;
    uint64_t m_id = 0;
    bool m_open = false;
    bool m_writable = false;
    std::string m_status;
};
#endif
