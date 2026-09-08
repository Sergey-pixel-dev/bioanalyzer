#ifndef AIMODEL_H
#define AIMODEL_H
#include "core/dataset.h"
#include <string>

struct ModelBundleInfo
{
    int schemaVersion = 2;
    std::string taskId;
    std::string taskType;
    TargetType targetType = TargetType::Unknown;
    std::string modelFamily;
    std::string datasetPath;
    std::string weightsFile = "weights.pt";
    std::string outputKind = "top_k";
    nlohmann::json output = nlohmann::json::object();
    nlohmann::json config = nlohmann::json::object();
    std::string units = "uV";
    double scale = 1e-6;
};

std::string nextModelBundlePath(const std::string &folder, const std::string &prefix = "model");
bool writeModelManifest(const std::string &bundlePath, const DatasetSpec &spec,
                        const std::string &datasetPath, const std::string &weightsFile,
                        std::string *error = nullptr);
bool loadModelManifest(const std::string &bundlePath, ModelBundleInfo &info,
                       DatasetSpec &spec, std::string *error = nullptr);
bool modelCompatible(const DatasetSpec &dataset, const DatasetSpec &model,
                     std::string *reason = nullptr);
#endif
