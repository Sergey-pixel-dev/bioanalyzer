#ifndef AIMODEL_H
#define AIMODEL_H
#include "ai/dataset.h"
#include <map>
#include <string>
#include <vector>

// Version of the model manifest this program reads.
constexpr int kModelSchemaVersion = 2;

struct ModelBundleInfo
{
    int schemaVersion = kModelSchemaVersion;
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
};

std::string nextModelBundlePath(const std::string &folder, const std::string &prefix = "model");
bool writeModelManifest(const std::string &bundlePath, const DatasetSpec &spec,
                        const std::string &datasetPath, const std::string &weightsFile,
                        std::string *error = nullptr);
bool loadModelManifest(const std::string &bundlePath, ModelBundleInfo &info,
                       DatasetSpec &spec, std::string *error = nullptr);
bool modelCompatible(const DatasetSpec &dataset, const DatasetSpec &model,
                     std::string *reason = nullptr);
// Spec of a model trained on a dataset. A model that takes segments shorter
// than a trial (windowMs > 0, the window_ms hyperparameter) has a window of
// windowMs * sampleRate / 1000 samples and a quarter of it as the default
// inference stride; segments start skipMs (skip_ms) after the cue, so the
// window together with the skip must fit into a trial. Without windowMs the
// model takes whole examples, as the dataset stores them.
bool modelSpecFor(const DatasetSpec &dataset, int windowMs, int skipMs, DatasetSpec &model,
                  std::string *reason = nullptr);

// Version of the training history (history.json in the model folder) this
// program reads.
constexpr int kTrainingHistorySchemaVersion = 1;

// A value the model reports after every training epoch, as its description
// declares it in outputs.training.
struct TrainingOutput
{
    std::string name;
    // Axis of the chart the value is drawn on by default: "left", "right";
    // empty --- not drawn.
    std::string axis;
};

struct TrainingEpoch
{
    int epoch = 0;
    std::map<std::string, double> values;
    // The weights of this epoch were kept for recognition.
    bool best = false;
};

// The training history of a model. The worker writes it after every epoch;
// it gives the hyperparameters for continuing the training and the values
// for the table and the chart.
struct TrainingHistory
{
    std::string family;
    std::string datasetPath;
    nlohmann::json hyperparameters = nlohmann::json::object();
    std::vector<TrainingOutput> outputs;
    int plannedEpochs = 0;
    std::vector<TrainingEpoch> epochs;
};

std::vector<TrainingOutput> trainingOutputsFrom(const nlohmann::json &outputs);
bool loadTrainingHistory(const std::string &bundlePath, TrainingHistory &history,
                         std::string *error = nullptr);

// A model in its folder, as the application knows it: which module trained
// it, what it takes and what it returns (the manifest), and how it was
// trained (the history). An entity: it starts no process --- the training
// and the recognition sessions run it, each through its own worker.
class Model
{
public:
    // Reads the manifest and the training history of the model in `folder`.
    bool open(const std::string &folder, std::string *error = nullptr);
    // Reads the history again: the worker rewrites it after every epoch.
    void reloadHistory();
    // The model takes data described by `data` (modelCompatible()).
    bool suits(const DatasetSpec &data, std::string *reason = nullptr) const;

    bool isOpen() const { return !m_folder.empty(); }
    const std::string &folder() const { return m_folder; }
    const ModelBundleInfo &info() const { return m_info; }
    // What the model takes: sample rate, window, stride, channels and units,
    // and the kind of targets it was trained on.
    const DatasetSpec &spec() const { return m_spec; }
    const TrainingHistory &history() const { return m_history; }

private:
    std::string m_folder;
    ModelBundleInfo m_info;
    DatasetSpec m_spec;
    TrainingHistory m_history;
};
#endif
