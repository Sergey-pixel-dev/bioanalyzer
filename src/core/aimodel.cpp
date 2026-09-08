#include "core/aimodel.h"
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
        std::string outputKind = s.extra.value("output_kind", std::string());
        if (outputKind.empty())
            outputKind = s.task.targetType == TargetType::Label ? "top_k" : s.task.targetType == TargetType::Value ? "value"
                                                                                                                   : "spans";
        json j = {{"schema_version", 2}, {"task_id", s.task.taskId}, {"task_type", toString(s.task.taskType)}, {"target_type", toString(s.task.targetType)}, {"model_family", s.modelFamily}, {"dataset_path", dataset}, {"weights", weights}, {"sample_rate", s.sampleRate}, {"window_samples", s.windowSamples}, {"stride_samples", s.strideSamples}, {"channels", s.channels}, {"units", s.units}, {"scale", s.scale}, {"output", {{"kind", outputKind}}}};
        std::ofstream f(fs::path(bundle) / "manifest.json");
        if (!f)
        {
            if (error)
                *error = "cannot write model manifest";
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
        if (j.value("schema_version", 0) != 2)
        {
            if (error)
                *error = "model schema_version 2 required";
            return false;
        }
        i.schemaVersion = 2;
        i.taskId = j.at("task_id").get<std::string>();
        i.taskType = j.at("task_type").get<std::string>();
        i.targetType = targetTypeFromString(j.at("target_type").get<std::string>());
        i.modelFamily = j.at("model_family").get<std::string>();
        i.datasetPath = j.value("dataset_path", "");
        i.weightsFile = j.at("weights").get<std::string>();
        i.output = j.value("output", json::object());
        i.outputKind = i.output.value("kind", "top_k");
        i.units = j.value("units", "uV");
        i.scale = j.value("scale", 1e-6);
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
        s.scale = i.scale;
        if (!fs::exists(fs::path(bundle) / i.weightsFile))
        {
            if (error)
                *error = "weights file not found";
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
    if (a.task.taskId != b.task.taskId)
        return fail("task id differs");
    if (a.task.taskType != b.task.taskType)
        return fail("task type differs");
    if (a.task.targetType != b.task.targetType)
        return fail("target type differs");
    if (a.sampleRate != b.sampleRate || a.windowSamples != b.windowSamples || a.strideSamples != b.strideSamples)
        return fail("capture geometry differs");
    if (a.channels != b.channels)
        return fail("channel selection differs");
    if (a.units != b.units || a.scale != b.scale)
        return fail("units or scale differs");
    return true;
}
