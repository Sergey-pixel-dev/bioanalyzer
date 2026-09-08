#include "core/dataset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
    std::string readText(const fs::path &path)
    {
        std::ifstream file(path);
        return file ? std::string((std::istreambuf_iterator<char>(file)), {}) : std::string();
    }

    json targetToJson(const TargetValue &target)
    {
        json value = target.extra.is_object() ? target.extra : json::object();
        switch (target.type)
        {
        case TargetType::Label:
            value["label"] = target.label;
            break;
        case TargetType::Value:
            value["value"] = target.value;
            break;
        case TargetType::Spans:
            value["spans"] = json::array();
            for (const auto &span : target.spans)
                value["spans"].push_back({span.first, span.second});
            break;
        default:
            break;
        }
        return value;
    }

    TargetValue targetFromJson(const json &value)
    {
        TargetValue target;
        if (!value.is_object())
            return target;
        target.extra = value;
        target.extra.erase("label");
        target.extra.erase("value");
        target.extra.erase("spans");
        if (value.contains("label"))
        {
            target.type = TargetType::Label;
            target.label = value.value("label", "");
        }
        else if (value.contains("value"))
        {
            target.type = TargetType::Value;
            target.value = value.value("value", 0.0);
        }
        else if (value.contains("spans"))
        {
            target.type = TargetType::Spans;
            for (const auto &span : value["spans"])
                if (span.is_array() && span.size() >= 2)
                    target.spans.emplace_back(span[0].get<uint64_t>(), span[1].get<uint64_t>());
        }
        return target;
    }

    void parseTask(const json &value, TaskDescriptor &task)
    {
        task.schemaVersion = value.value("schema_version", 3);
        task.taskId = value.value("task_id", "");
        task.displayName = value.value("display_name", task.taskId);
        task.taskType = taskTypeFromString(value.value("task_type", "classification"));
        task.targetType = targetTypeFromString(value.value("target_type", "label"));
        const auto paradigm = value.value("paradigm", json::object());
        const auto kind = paradigm.value("kind", "cue_schedule");
        task.paradigm.kind = kind == "free_recording" ? ParadigmSpec::Kind::FreeRecording
                             : kind == "unlabeled"    ? ParadigmSpec::Kind::Unlabeled
                                                      : ParadigmSpec::Kind::CueSchedule;
        task.paradigm.prepSeconds = paradigm.value("prep_s", 5);
        task.paradigm.recordSeconds = paradigm.value("record_s", 7);
        task.paradigm.randomize = paradigm.value("randomize", true);
        task.paradigm.hotkeys = paradigm.value("hotkeys", "");
        task.paradigm.durationSeconds = paradigm.value("duration_s", 0);
        task.events.clear();
        for (const auto &eventValue : value.value("events", json::array()))
        {
            EventSpec event;
            event.id = eventValue.value("id", "");
            event.cueText = eventValue.value("cue_text", "");
            event.target = targetFromJson(eventValue.value("target", json::object()));
            task.events.push_back(std::move(event));
        }
        const auto capture = value.value("capture", json::object());
        task.capture.channels.clear();
        for (const auto &channel : capture.value("channels", json::array()))
            task.capture.channels.push_back(channel.get<uint8_t>());
        task.capture.sampleRate = capture.contains("sample_rate")
                                      ? capture.at("sample_rate").get<uint32_t>()
                                      : 0u;
        task.capture.windowSamples = capture.value("window_samples", size_t(0));
        task.capture.strideSamples = capture.value("stride_samples", size_t(0));
        task.capture.units = capture.value("units", "uV");
        task.capture.scale = capture.value("scale", 1e-6);
        task.extra = value.value("extra", json::object());
    }

    json taskToJson(const DatasetSpec &spec)
    {
        json task = spec.task.extra.is_object() ? spec.task.extra : json::object();
        task["schema_version"] = 3;
        task["task_id"] = spec.task.taskId;
        task["display_name"] = spec.task.displayName;
        task["task_type"] = toString(spec.task.taskType);
        task["target_type"] = toString(spec.task.targetType);
        const char *kind = spec.task.paradigm.kind == ParadigmSpec::Kind::FreeRecording ? "free_recording"
                           : spec.task.paradigm.kind == ParadigmSpec::Kind::Unlabeled   ? "unlabeled"
                                                                                        : "cue_schedule";
        task["paradigm"] = {{"kind", kind}, {"prep_s", spec.task.paradigm.prepSeconds}, {"record_s", spec.task.paradigm.recordSeconds}, {"randomize", spec.task.paradigm.randomize}};
        if (!spec.task.paradigm.hotkeys.empty())
            task["paradigm"]["hotkeys"] = spec.task.paradigm.hotkeys;
        if (spec.task.paradigm.durationSeconds > 0)
            task["paradigm"]["duration_s"] = spec.task.paradigm.durationSeconds;
        task["events"] = json::array();
        for (const auto &event : spec.task.events)
            task["events"].push_back({{"id", event.id}, {"cue_text", event.cueText}, {"target", targetToJson(event.target)}});
        task["capture"] = {{"channels", spec.task.capture.channels},
                           {"sample_rate", spec.task.capture.sampleRate},
                           {"window_samples", spec.task.capture.windowSamples},
                           {"stride_samples", spec.task.capture.strideSamples},
                           {"units", spec.task.capture.units},
                           {"scale", spec.task.capture.scale}};
        return task;
    }

    void writeManifest(const std::string &directory, const DatasetSpec &spec,
                       const std::string &status, uint64_t count)
    {
        json manifest = {{"schema_version", 3}, {"task", taskToJson(spec)}, {"capture", {{"channels", spec.channels}, {"sample_rate", spec.sampleRate}, {"window_samples", spec.windowSamples}, {"stride_samples", spec.strideSamples}, {"units", spec.units}, {"scale", spec.scale}}}, {"examples", count}, {"status", status}, {"preprocessing", spec.preprocessing}, {"normalization", spec.normalization}};
        if (spec.extra.is_object() && !spec.extra.empty())
            manifest["extra"] = spec.extra;
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
    case TaskType::Regression:
        return "regression";
    case TaskType::Detection:
        return "detection";
    case TaskType::Anomaly:
        return "anomaly";
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
    case TargetType::Value:
        return "value";
    case TargetType::Spans:
        return "spans";
    case TargetType::None:
        return "none";
    default:
        return "unknown";
    }
}

TaskType taskTypeFromString(const std::string &value)
{
    if (value == "classification")
        return TaskType::Classification;
    if (value == "regression")
        return TaskType::Regression;
    if (value == "detection")
        return TaskType::Detection;
    if (value == "anomaly")
        return TaskType::Anomaly;
    return TaskType::Unknown;
}

TargetType targetTypeFromString(const std::string &value)
{
    if (value == "label")
        return TargetType::Label;
    if (value == "value")
        return TargetType::Value;
    if (value == "spans")
        return TargetType::Spans;
    if (value == "none")
        return TargetType::None;
    return TargetType::Unknown;
}

bool loadTaskDescriptor(const std::string &path, TaskDescriptor &out, std::string *error)
{
    try
    {
        parseTask(json::parse(readText(path)), out);
        if (out.schemaVersion != 3)
        {
            if (error)
                *error = "task schema_version 3 required";
            return false;
        }
        if (out.taskId.empty() || out.taskType == TaskType::Unknown || out.targetType == TargetType::Unknown)
        {
            if (error)
                *error = "unsupported or incomplete task descriptor";
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

std::vector<LabelSpec> labelVocabulary(const DatasetSpec &spec)
{
    std::vector<LabelSpec> result;
    for (const auto &event : spec.task.events)
    {
        if (event.target.type != TargetType::Label || event.target.label.empty())
            continue;
        const auto found = std::find_if(result.begin(), result.end(), [&](const LabelSpec &entry)
                                        { return entry.id == event.target.label; });
        if (found == result.end())
            result.push_back({event.target.label, event.cueText.empty() ? event.target.label : event.cueText});
    }
    return result;
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

bool Dataset::createFiles(const std::string &directory, const DatasetSpec &spec, std::string *error)
{
    try
    {
        fs::create_directories(fs::path(directory) / "data");
        m_dir = directory;
        m_spec = spec;
        m_id = 0;
        m_status = "in_progress";
        writeManifest(m_dir, m_spec, m_status, m_id);
        m_index.open(fs::path(m_dir) / "examples.jsonl", std::ios::trunc);
        m_events.open(fs::path(m_dir) / "events.jsonl", std::ios::trunc);
        m_writable = bool(m_index) && bool(m_events);
        m_open = m_writable;
        if (!m_open && error)
            *error = "cannot open dataset files";
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
        if (manifest.value("schema_version", 0) != 3)
        {
            if (error)
                *error = "schema_version 3 required";
            return false;
        }
        parseTask(manifest.at("task"), m_spec.task);
        if (m_spec.task.taskId.empty() || m_spec.task.taskType == TaskType::Unknown ||
            m_spec.task.targetType == TargetType::Unknown)
        {
            if (error)
                *error = "unsupported or incomplete task descriptor";
            return false;
        }
        const auto capture = manifest.at("capture");
        m_spec.channels.clear();
        for (const auto &channel : capture.at("channels"))
            m_spec.channels.push_back(channel.get<uint8_t>());
        m_spec.sampleRate = capture.at("sample_rate").get<uint32_t>();
        m_spec.windowSamples = capture.at("window_samples").get<size_t>();
        m_spec.strideSamples = capture.at("stride_samples").get<size_t>();
        m_spec.units = capture.value("units", "uV");
        m_spec.scale = capture.value("scale", 1e-6);
        m_spec.preprocessing = manifest.value("preprocessing", "none");
        m_spec.normalization = manifest.value("normalization", "none");
        m_spec.extra = manifest.value("extra", json::object());
        m_id = manifest.value("examples", uint64_t(0));
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
    if (!m_open || !m_writable || record.samples.size() != m_spec.channels.size())
        return false;
    const size_t sampleCount = record.samples.empty() ? 0 : record.samples.front().size();
    for (const auto &channel : record.samples)
        if (channel.size() != sampleCount)
            return false;
    std::ostringstream filename;
    filename << "data/chunk-" << std::setw(8) << std::setfill('0') << m_id << ".bin";
    std::ofstream file(fs::path(m_dir) / filename.str(), std::ios::binary);
    if (!file)
        return false;
    for (size_t sample = 0; sample < sampleCount; ++sample)
        for (size_t channel = 0; channel < record.samples.size(); ++channel)
            file.write(reinterpret_cast<const char *>(&record.samples[channel][sample]), 4);
    json line = {{"id", m_id}, {"target", targetToJson(record.target)}, {"source_sample", record.sourceSample}, {"samples", sampleCount}, {"file", filename.str()}};
    if (record.extra.is_object())
        for (auto it = record.extra.begin(); it != record.extra.end(); ++it)
            line[it.key()] = it.value();
    m_index << line.dump() << '\n';
    ++m_id;
    return bool(m_index);
}

bool Dataset::appendEvent(uint64_t tSample, const std::string &eventId, const TargetValue &target)
{
    if (!m_open || !m_writable || !m_events)
        return false;
    m_events << json({{"t_sample", tSample}, {"event_id", eventId}, {"target", targetToJson(target)}}).dump() << '\n';
    return bool(m_events);
}

void Dataset::finish(const std::string &status)
{
    if (!m_open || !m_writable)
        return;
    m_index.close();
    m_events.close();
    m_status = status;
    writeManifest(m_dir, m_spec, m_status, m_id);
    m_open = false;
    m_writable = false;
}
