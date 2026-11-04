#include "ai/dataset.h"
#include "common/translatable.h"
#include "exchange/applicationprotocol.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
    std::string readText(const fs::path &path)
    {
        std::ifstream file(path);
        return file ? std::string((std::istreambuf_iterator<char>(file)), {}) : std::string();
    }
}

json targetToJson(const TargetValue &target)
{
    // Only a label target is implemented (isSupportedTask()).
    return target.type == TargetType::Label ? json{{"label", target.label}} : json::object();
}

TargetValue targetFromJson(const json &value)
{
    TargetValue target;
    if (value.is_object() && value.contains("label"))
    {
        target.type = TargetType::Label;
        target.label = value.value("label", "");
    }
    return target;
}

std::vector<double> fullScaleMicrovolts(const std::vector<int> &gain)
{
    std::vector<double> out;
    for (int value : gain)
    {
        if (value <= 0)
            return {};
        out.push_back(ApplicationProtocol::kFixedReferenceVoltageMv * 1000.0 / value);
    }
    return out;
}

bool isSupportedTask(const TaskDescriptor &task)
{
    return task.taskType == TaskType::Classification && task.targetType == TargetType::Label;
}

namespace
{

    void parseTask(const json &value, TaskDescriptor &task)
    {
        task.schemaVersion = value.value("schema_version", 0);
        task.taskId = value.value("task_id", "");
        task.displayName = value.value("display_name", task.taskId);
        task.taskType = taskTypeFromString(value.value("task_type", "classification"));
        task.targetType = targetTypeFromString(value.value("target_type", "label"));
        const auto paradigm = value.value("paradigm", json::object());
        // A capture by a cue schedule is the only kind; another kind is an
        // error, not a silent cue schedule.
        if (paradigm.value("kind", "cue_schedule") != "cue_schedule")
            throw std::runtime_error(QT_TRANSLATE_NOOP("Errors", "unsupported paradigm kind"));
        task.paradigm.prepSeconds = paradigm.value("prep_s", 5);
        task.paradigm.recordSeconds = paradigm.value("record_s", 7);
        task.paradigm.randomize = paradigm.value("randomize", true);
        task.events.clear();
        for (const auto &eventValue : value.value("events", json::array()))
        {
            EventSpec event;
            event.id = eventValue.value("id", "");
            event.cueText = eventValue.value("cue_text", "");
            event.target = targetFromJson(eventValue.value("target", json::object()));
            event.repetitions = eventValue.value("repetitions", 0);
            task.events.push_back(std::move(event));
        }
        const auto capture = value.value("capture", json::object());
        task.capture.channels.clear();
        for (const auto &channel : capture.value("channels", json::array()))
            task.capture.channels.push_back(channel.get<uint8_t>());
        task.capture.sampleRate = capture.contains("sample_rate")
                                      ? capture.at("sample_rate").get<uint32_t>()
                                      : 0u;
        task.capture.units = capture.value("units", "uV");
        task.quality = value.value("quality", json::object());
    }

    json taskToJson(const DatasetSpec &spec)
    {
        json task;
        task["schema_version"] = kTaskSchemaVersion;
        task["task_id"] = spec.task.taskId;
        task["display_name"] = spec.task.displayName;
        task["task_type"] = toString(spec.task.taskType);
        task["target_type"] = toString(spec.task.targetType);
        task["paradigm"] = {{"kind", "cue_schedule"}, {"prep_s", spec.task.paradigm.prepSeconds}, {"record_s", spec.task.paradigm.recordSeconds}, {"randomize", spec.task.paradigm.randomize}};
        task["events"] = json::array();
        for (const auto &event : spec.task.events)
        {
            // The plan of the last session, 0 for a value skipped in it.
            task["events"].push_back({{"id", event.id},
                                      {"cue_text", event.cueText},
                                      {"target", targetToJson(event.target)},
                                      {"repetitions", event.repetitions}});
        }
        task["capture"] = {{"channels", spec.task.capture.channels},
                           {"sample_rate", spec.task.capture.sampleRate},
                           {"units", spec.task.capture.units}};
        if (spec.task.quality.is_object() && !spec.task.quality.empty())
            task["quality"] = spec.task.quality;
        return task;
    }

    void writeManifest(const std::string &directory, const DatasetSpec &spec, const std::string &status)
    {
        const json manifest = {{"schema_version", kDatasetSchemaVersion},
                               {"task", taskToJson(spec)},
                               {"capture", {{"channels", spec.channels}, {"sample_rate", spec.sampleRate}, {"window_samples", spec.windowSamples}, {"units", spec.units}, {"gain", spec.gain}}},
                               {"status", status}};
        std::ofstream file(fs::path(directory) / "manifest.json", std::ios::trunc);
        file << manifest.dump(2) << '\n';
    }
} // namespace

std::string nextDatasetBundlePath(const std::string &parent, const std::string &prefix)
{
    fs::create_directories(parent);
    for (int i = 1;; ++i)
    {
        std::ostringstream name;
        name << prefix << "-v" << std::setw(3) << std::setfill('0') << i << ".bset";
        const fs::path candidate = fs::path(parent) / name.str();
        if (!fs::exists(candidate))
            return candidate.string();
    }
}

const char *toString(TaskType type)
{
    switch (type)
    {
    case TaskType::Classification:
        return "classification";
    default:
        return "unknown";
    }
}

const char *toString(TargetType type)
{
    switch (type)
    {
    case TargetType::Label:
        return "label";
    default:
        return "unknown";
    }
}

TaskType taskTypeFromString(const std::string &value)
{
    if (value == "classification")
        return TaskType::Classification;
    return TaskType::Unknown;
}

TargetType targetTypeFromString(const std::string &value)
{
    if (value == "label")
        return TargetType::Label;
    return TargetType::Unknown;
}

bool loadTaskDescriptor(const std::string &path, TaskDescriptor &out, std::string *error)
{
    try
    {
        parseTask(json::parse(readText(path)), out);
        if (out.schemaVersion != kTaskSchemaVersion)
        {
            if (error)
                *error = QT_TRANSLATE_NOOP("Errors", "the task description format version differs from the one this program reads");
            return false;
        }
        if (out.taskId.empty())
        {
            if (error)
                *error = QT_TRANSLATE_NOOP("Errors", "the task descriptor has no task_id");
            return false;
        }
        if (!isSupportedTask(out))
        {
            if (error)
                *error = QT_TRANSLATE_NOOP("Errors", "this version supports only classification tasks with label targets");
            return false;
        }
        return true;
    }
    catch (const std::exception &exception)
    {
        if (error)
            *error = exception.what();
        return false;
    }
}

std::unique_ptr<Dataset> Dataset::create(const std::string &directory, const DatasetSpec &spec,
                                         std::string *error)
{
    auto dataset = std::unique_ptr<Dataset>(new Dataset);
    return dataset->createFiles(directory, spec, error) ? std::move(dataset) : nullptr;
}

std::unique_ptr<Dataset> Dataset::open(const std::string &directory, std::string *error)
{
    auto dataset = std::unique_ptr<Dataset>(new Dataset);
    return dataset->readManifest(directory, error) ? std::move(dataset) : nullptr;
}

Dataset::~Dataset() = default;

DatasetLock::Result DatasetLock::acquire(const std::string &directory)
{
    release();
#ifndef _WIN32
    const int fd = ::open((fs::path(directory) / ".lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0)
        return Result::Failed;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        const bool busy = errno == EWOULDBLOCK;
        ::close(fd);
        return busy ? Result::Busy : Result::Failed;
    }
    m_fd = fd;
#else
    // The path arrives in UTF-8; the wide form keeps Cyrillic folder names.
    const std::wstring path = (fs::u8path(directory) / ".lock").wstring();
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return Result::Failed;
    OVERLAPPED region;
    std::memset(&region, 0, sizeof(region));
    if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &region))
    {
        const bool busy = GetLastError() == ERROR_LOCK_VIOLATION;
        CloseHandle(handle);
        return busy ? Result::Busy : Result::Failed;
    }
    m_handle = handle;
#endif
    return Result::Acquired;
}

void DatasetLock::release()
{
#ifdef _WIN32
    if (m_handle != nullptr)
        CloseHandle(static_cast<HANDLE>(m_handle)); // closing the handle releases the lock
    m_handle = nullptr;
#else
    if (m_fd >= 0)
        ::close(m_fd); // closing the last descriptor releases the flock
    m_fd = -1;
#endif
}

bool Dataset::lockForWriting(std::string *error)
{
    switch (m_lock.acquire(m_dir))
    {
    case DatasetLock::Result::Acquired:
        return true;
    case DatasetLock::Result::Busy:
        if (error)
            *error = QT_TRANSLATE_NOOP("Errors", "the dataset is being written elsewhere: it is captured or edited, possibly in another copy of the program");
        return false;
    case DatasetLock::Result::Failed:
        break;
    }
    if (error)
        *error = QT_TRANSLATE_NOOP("Errors", "the dataset cannot be locked for writing");
    return false;
}

std::unique_ptr<Dataset> Dataset::openForAppend(const std::string &directory, const DatasetSpec &capture,
                                                std::string *error)
{
    auto dataset = std::unique_ptr<Dataset>(new Dataset);
    if (!dataset->readManifest(directory, error) || !dataset->reopenFiles(capture, error))
        return nullptr;
    return dataset;
}

bool Dataset::reopenFiles(const DatasetSpec &capture, std::string *error)
{
    auto fail = [&](const char *reason)
    {
        if (error)
            *error = reason;
        return false;
    };
    // The same comparison as when a model is checked against a stream.
    if (m_spec.task.taskId != capture.task.taskId)
        return fail(QT_TRANSLATE_NOOP("Errors", "the dataset was collected for another task"));
    if (m_spec.task.taskType != capture.task.taskType || m_spec.task.targetType != capture.task.targetType)
        return fail(QT_TRANSLATE_NOOP("Errors", "the dataset has another task type"));
    if (m_spec.task.paradigm.prepSeconds != capture.task.paradigm.prepSeconds)
        return fail(QT_TRANSLATE_NOOP("Errors", "the dataset has another preparation time"));
    if (m_spec.sampleRate != capture.sampleRate)
        return fail(QT_TRANSLATE_NOOP("Errors", "the dataset has another sample rate"));
    if (m_spec.windowSamples != capture.windowSamples)
        return fail(QT_TRANSLATE_NOOP("Errors", "the dataset has another window size (recording time)"));
    if (m_spec.channels.size() != capture.channels.size())
        return fail(QT_TRANSLATE_NOOP("Errors", "the dataset has another channel count"));
    // Other channels mean other electrode sites in the same columns.
    if (m_spec.channels != capture.channels)
        return fail(QT_TRANSLATE_NOOP("Errors", "the dataset was recorded on other channels"));
    // The saturation check of every trial takes the ADC range from the
    // dataset's gain: a trial at another gain would be judged by a wrong range.
    if (m_spec.gain != capture.gain)
        return fail(QT_TRANSLATE_NOOP("Errors", "the dataset was recorded with another gain"));
    if (m_spec.units != capture.units)
        return fail(QT_TRANSLATE_NOOP("Errors", "the dataset has other units"));
    for (const auto &event : capture.task.events)
        if (std::none_of(m_spec.task.events.begin(), m_spec.task.events.end(), [&](const EventSpec &existing)
                         { return existing.target.type == event.target.type && existing.target.label == event.target.label; }))
            return fail(QT_TRANSLATE_NOOP("Errors", "a continued dataset gets no new target values"));
    // The plan of this session replaces the dataset's one: the next
    // continuation offers the repetitions the operator set last.
    for (auto &existing : m_spec.task.events)
    {
        const auto planned = std::find_if(capture.task.events.begin(), capture.task.events.end(),
                                          [&](const EventSpec &event)
                                          { return event.target.type == existing.target.type &&
                                                   event.target.label == existing.target.label; });
        existing.repetitions = planned != capture.task.events.end() ? planned->repetitions : 0;
    }
    try
    {
        // Which example number and session are next: from the list itself.
        if (!lockForWriting(error))
            return false;
        uint64_t nextId = 0;
        uint32_t lastSession = 0;
        bool hasExamples = false;
        {
            std::ifstream index(fs::path(m_dir) / "examples.jsonl");
            std::string line;
            while (std::getline(index, line))
            {
                if (line.find_first_not_of(" \t\r") == std::string::npos)
                    continue;
                const auto item = json::parse(line);
                nextId = std::max(nextId, item.value("id", uint64_t(0)) + 1);
                // Datasets collected before sessions were numbered are session 0.
                lastSession = std::max(lastSession, item.value("session", uint32_t(0)));
                hasExamples = true;
            }
        }
        m_id = nextId;
        m_session = hasExamples ? lastSession + 1 : 0;
        m_index.open(fs::path(m_dir) / "examples.jsonl", std::ios::app);
        m_writable = bool(m_index);
        m_open = m_writable;
        if (!m_open)
            return fail(QT_TRANSLATE_NOOP("Errors", "cannot open dataset files"));
        m_status = "in_progress";
        writeManifest(m_dir, m_spec, m_status);
        return true;
    }
    catch (const std::exception &exception)
    {
        if (error)
            *error = exception.what();
        return false;
    }
}

bool Dataset::createFiles(const std::string &directory, const DatasetSpec &spec, std::string *error)
{
    try
    {
        fs::create_directories(fs::path(directory) / "data");
        m_dir = directory;
        if (!lockForWriting(error))
            return false;
        m_spec = spec;
        m_id = 0;
        m_session = 0;
        m_status = "in_progress";
        writeManifest(m_dir, m_spec, m_status);
        m_index.open(fs::path(m_dir) / "examples.jsonl", std::ios::trunc);
        m_writable = bool(m_index);
        m_open = m_writable;
        if (!m_open && error)
            *error = QT_TRANSLATE_NOOP("Errors", "cannot open dataset files");
        return m_open;
    }
    catch (const std::exception &exception)
    {
        if (error)
            *error = exception.what();
        return false;
    }
}

bool Dataset::readManifest(const std::string &directory, std::string *error)
{
    try
    {
        const auto manifest = json::parse(readText(fs::path(directory) / "manifest.json"));
        if (manifest.value("schema_version", 0) != kDatasetSchemaVersion)
        {
            if (error)
                *error = QT_TRANSLATE_NOOP("Errors", "the dataset format version differs from the one this program reads");
            return false;
        }
        parseTask(manifest.at("task"), m_spec.task);
        if (m_spec.task.taskId.empty() || !isSupportedTask(m_spec.task))
        {
            if (error)
                *error = QT_TRANSLATE_NOOP("Errors", "this version supports only classification tasks with label targets");
            return false;
        }
        const auto capture = manifest.at("capture");
        m_spec.channels.clear();
        for (const auto &channel : capture.at("channels"))
            m_spec.channels.push_back(channel.get<uint8_t>());
        m_spec.sampleRate = capture.at("sample_rate").get<uint32_t>();
        m_spec.windowSamples = capture.at("window_samples").get<size_t>();
        m_spec.strideSamples = m_spec.windowSamples; // one window per trial
        m_spec.units = capture.value("units", "uV");
        m_spec.gain = capture.value("gain", std::vector<int>{});
        m_status = manifest.value("status", "unknown");
        m_dir = directory;
        m_open = true;
        m_writable = false;
        return true;
    }
    catch (const std::exception &exception)
    {
        if (error)
            *error = exception.what();
        return false;
    }
}

bool Dataset::append(const ExampleRecord &record)
{
    const size_t channels = m_spec.channels.size();
    // Every trial is one window long: the list of trials keeps no length.
    if (!m_open || !m_writable || record.samples.size() != channels)
        return false;
    for (const auto &channel : record.samples)
        if (channel.size() != m_spec.windowSamples)
            return false;
    // The session's signal is one file, appended trial after trial; a file
    // left by an interrupted attempt is continued, the offsets stay right.
    if (!m_data.is_open())
    {
        std::ostringstream name;
        name << "data/session-" << std::setw(3) << std::setfill('0') << m_session << ".bin";
        m_dataFile = name.str();
        const fs::path path = fs::path(m_dir) / m_dataFile;
        std::error_code ec;
        const auto bytes = fs::exists(path, ec) ? fs::file_size(path, ec) : 0;
        m_dataOffset = ec ? 0 : bytes / (channels * sizeof(int32_t));
        m_data.open(path, std::ios::binary | std::ios::app);
        if (!m_data)
            return false;
    }
    // Little-endian int32, interleaved by channel.
    std::vector<int32_t> interleaved(m_spec.windowSamples * channels);
    for (size_t sample = 0; sample < m_spec.windowSamples; ++sample)
        for (size_t channel = 0; channel < channels; ++channel)
            interleaved[sample * channels + channel] = record.samples[channel][sample];
    m_data.write(reinterpret_cast<const char *>(interleaved.data()),
                 static_cast<std::streamsize>(interleaved.size() * sizeof(int32_t)));
    // The signal reaches the file before its line in the list.
    m_data.flush();
    if (!m_data)
        return false;
    json line = {{"id", m_id},
                 {"file", m_dataFile},
                 {"offset", m_dataOffset},
                 {"session", m_session},
                 {"recorded_at", record.recordedAt},
                 {"cue", targetToJson(record.cue)},
                 {"target", targetToJson(record.target)},
                 {"excluded", false},
                 {"note", ""}};
    m_index << line.dump() << '\n';
    ++m_id;
    m_dataOffset += m_spec.windowSamples;
    return bool(m_index);
}

void Dataset::finish(const std::string &status, const PendingUpdates &updates)
{
    if (!m_open || !m_writable)
        return;
    m_index.close();
    m_data.close();
    // The writer applies what waited for the end of the capture: the trials
    // recorded again.
    if (!updates.repeated.empty())
        rewriteExamples(m_dir, [&](json &item)
                        {
                            const uint64_t id = item.value("id", uint64_t(0));
                            if (updates.repeated.count(id))
                                item["excluded"] = true; });
    m_status = status;
    writeManifest(m_dir, m_spec, m_status);
    m_open = false;
    m_writable = false;
    m_lock.release();
}

bool rewriteExamples(const std::string &directory, const std::function<void(json &item)> &update,
                     std::string *error)
{
    const fs::path path = fs::path(directory) / "examples.jsonl";
    const fs::path temporary = fs::path(directory) / "examples.jsonl.tmp";
    try
    {
        // The whole list is parsed first: a damaged line leaves the file as it was.
        std::vector<json> items;
        {
            std::ifstream input(path);
            if (!input)
            {
                if (error)
                    *error = QT_TRANSLATE_NOOP("Errors", "the list of trials (examples.jsonl) cannot be read");
                return false;
            }
            std::string line;
            while (std::getline(input, line))
                if (line.find_first_not_of(" \t\r") != std::string::npos)
                    items.push_back(json::parse(line));
        }
        for (auto &item : items)
            update(item);
        {
            std::ofstream output(temporary, std::ios::trunc);
            for (const auto &item : items)
                output << item.dump() << '\n';
            output.flush();
            if (!output)
            {
                if (error)
                    *error = QT_TRANSLATE_NOOP("Errors", "the list of trials cannot be written");
                return false;
            }
        }
        fs::rename(temporary, path);
        return true;
    }
    catch (const std::exception &exception)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        if (error)
            *error = exception.what();
        return false;
    }
}
