#include "ai/aimodel.h"
#include "common/translatable.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;
namespace
{
    std::string read(const fs::path &p)
    {
        std::ifstream f(p);
        return f ? std::string((std::istreambuf_iterator<char>(f)), {}) : std::string();
    }
}

std::string nextModelBundlePath(const std::string &folder, const std::string &prefix)
{
    fs::create_directories(folder);
    for (int i = 1;; ++i)
    {
        std::ostringstream n;
        n << prefix << "-v" << std::setw(3) << std::setfill('0') << i << ".aimodel";
        auto p = fs::path(folder) / n.str();
        if (!fs::exists(p))
            return p.string();
    }
}

bool writeModelManifest(const std::string &bundle, const DatasetSpec &s, const std::string &dataset, const std::string &weights, std::string *error)
{
    try
    {
        fs::create_directories(bundle);
        // The kind of the model's answer comes from its meta(); top_k (the
        // most probable labels) is the only kind the recognition page shows.
        const std::string outputKind = s.outputKind.empty() ? "top_k" : s.outputKind;
        json j = {{"schema_version", kModelSchemaVersion}, {"task_id", s.task.taskId}, {"task_type", toString(s.task.taskType)}, {"target_type", toString(s.task.targetType)}, {"model_family", s.modelFamily}, {"dataset_path", dataset}, {"weights", weights}, {"sample_rate", s.sampleRate}, {"window_samples", s.windowSamples}, {"stride_samples", s.strideSamples}, {"channels", s.channels}, {"units", s.units}, {"output", {{"kind", outputKind}}}};
        std::ofstream f(fs::path(bundle) / "manifest.json");
        if (!f)
        {
            if (error)
                *error = QT_TRANSLATE_NOOP("Errors", "cannot write model manifest");
            return false;
        }
        f << j.dump(2) << '\n';
        return bool(f);
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = e.what();
        return false;
    }
}

bool loadModelManifest(const std::string &bundle, ModelBundleInfo &i, DatasetSpec &s, std::string *error)
{
    try
    {
        auto j = json::parse(read(fs::path(bundle) / "manifest.json"));
        if (j.value("schema_version", 0) != kModelSchemaVersion)
        {
            if (error)
                *error = QT_TRANSLATE_NOOP("Errors", "the model format version differs from the one this program reads");
            return false;
        }
        i.schemaVersion = kModelSchemaVersion;
        i.taskId = j.at("task_id").get<std::string>();
        i.taskType = j.at("task_type").get<std::string>();
        i.targetType = targetTypeFromString(j.at("target_type").get<std::string>());
        i.modelFamily = j.at("model_family").get<std::string>();
        i.datasetPath = j.value("dataset_path", "");
        i.weightsFile = j.at("weights").get<std::string>();
        i.output = j.value("output", json::object());
        i.outputKind = i.output.value("kind", "top_k");
        i.units = j.value("units", "uV");
        s.task.taskId = i.taskId;
        s.task.displayName = i.taskId;
        s.task.taskType = taskTypeFromString(i.taskType);
        s.task.targetType = i.targetType;
        s.modelFamily = i.modelFamily;
        s.sampleRate = j.at("sample_rate").get<uint32_t>();
        s.windowSamples = j.at("window_samples").get<size_t>();
        s.strideSamples = j.at("stride_samples").get<size_t>();
        s.channels.clear();
        for (auto &c : j.at("channels"))
            s.channels.push_back(c.get<uint8_t>());
        s.units = i.units;
        if (!fs::exists(fs::path(bundle) / i.weightsFile))
        {
            if (error)
                *error = QT_TRANSLATE_NOOP("Errors", "weights file not found");
            return false;
        }
        return true;
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = e.what();
        return false;
    }
}

bool modelCompatible(const DatasetSpec &a, const DatasetSpec &b, std::string *r)
{
    auto fail = [&](const char *m)
    {if(r)*r=m;return false; };
    // A model and data are matched by what the data hold, not by the task
    // they were collected for: data of another task description with the
    // same kind of targets and the same signal suit the model. Which labels
    // a continued model knows, its module checks against its checkpoint.
    if (a.task.targetType != b.task.targetType)
        return fail(QT_TRANSLATE_NOOP("Errors", "target type differs"));
    // The window is what the model consumes; the stride belongs to how the
    // dataset was cut and to how often inference runs, not to the model.
    if (a.sampleRate != b.sampleRate || a.windowSamples != b.windowSamples)
        return fail(QT_TRANSLATE_NOOP("Errors", "capture geometry differs"));
    // Channels are compared by count: their numbers depend on the device (the
    // virtual device numbers them by position in the recording), and which
    // electrode goes to which input is the operator's montage.
    if (a.channels.size() != b.channels.size())
        return fail(QT_TRANSLATE_NOOP("Errors", "channel count differs"));
    if (a.units != b.units)
        return fail(QT_TRANSLATE_NOOP("Errors", "units differ"));
    return true;
}

bool modelSpecFor(const DatasetSpec &dataset, int windowMs, int skipMs, DatasetSpec &model, std::string *reason)
{
    model = dataset;
    if (windowMs <= 0)
        return true;
    // The same integer arithmetic as the model family uses for its segments.
    const size_t window = static_cast<size_t>(windowMs) * dataset.sampleRate / 1000;
    const size_t skip = static_cast<size_t>(std::max(0, skipMs)) * dataset.sampleRate / 1000;
    if (window == 0 || window + skip > dataset.windowSamples)
    {
        if (reason)
            *reason = QT_TRANSLATE_NOOP("Errors", "the model window is longer than a trial of the dataset");
        return false;
    }
    model.windowSamples = window;
    model.strideSamples = std::max<size_t>(1, window / 4);
    return true;
}

std::vector<TrainingOutput> trainingOutputsFrom(const json &outputs)
{
    std::vector<TrainingOutput> result;
    if (!outputs.is_array())
        return result;
    for (const auto &item : outputs)
    {
        if (!item.is_object() || !item.contains("name") || !item.at("name").is_string())
            continue;
        TrainingOutput output;
        output.name = item.at("name").get<std::string>();
        output.axis = item.contains("axis") && item.at("axis").is_string() ? item.at("axis").get<std::string>() : "";
        result.push_back(std::move(output));
    }
    return result;
}

bool loadTrainingHistory(const std::string &bundle, TrainingHistory &history, std::string *error)
{
    const fs::path path = fs::path(bundle) / "history.json";
    if (!fs::exists(path))
    {
        if (error)
            *error = QT_TRANSLATE_NOOP("Errors", "the model has no training history");
        return false;
    }
    try
    {
        const json j = json::parse(read(path));
        if (j.value("schema_version", 0) != kTrainingHistorySchemaVersion)
        {
            if (error)
                *error = QT_TRANSLATE_NOOP("Errors",
                                           "the training history format version differs from the one this program reads");
            return false;
        }
        TrainingHistory h;
        h.family = j.value("family", "");
        h.datasetPath = j.value("dataset", "");
        h.hyperparameters = j.value("hyperparameters", json::object());
        h.outputs = trainingOutputsFrom(j.value("outputs", json::array()));
        h.plannedEpochs = j.value("planned_epochs", 0);
        const json epochs = j.value("epochs", json::array());
        for (const auto &item : epochs)
        {
            TrainingEpoch epoch;
            epoch.epoch = item.value("epoch", 0);
            epoch.best = item.value("best", false);
            // items() refers to the object it is called on: the values are
            // kept in a variable, not in a temporary that dies before the loop.
            const json values = item.value("values", json::object());
            for (const auto &[name, value] : values.items())
                if (value.is_number())
                    epoch.values[name] = value.get<double>();
            h.epochs.push_back(std::move(epoch));
        }
        history = std::move(h);
        return true;
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = e.what();
        return false;
    }
}

bool Model::open(const std::string &folder, std::string *error)
{
    ModelBundleInfo info;
    DatasetSpec spec;
    TrainingHistory history;
    if (!loadModelManifest(folder, info, spec, error) || !loadTrainingHistory(folder, history, error))
        return false;
    m_folder = folder;
    m_info = std::move(info);
    m_spec = std::move(spec);
    m_history = std::move(history);
    return true;
}

void Model::reloadHistory()
{
    TrainingHistory history;
    if (isOpen() && loadTrainingHistory(m_folder, history))
        m_history = std::move(history);
}

bool Model::suits(const DatasetSpec &data, std::string *reason) const
{
    return modelCompatible(m_spec, data, reason);
}
