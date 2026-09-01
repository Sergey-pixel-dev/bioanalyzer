#ifndef DATASET_H
#define DATASET_H
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

struct LabelSpec
{
    std::string id;
    std::string displayName;
};

struct TrainingConfig
{
    int epochs = 20;
    int batchSize = 32;
    double learningRate = 1.0e-3;
    double validationSplit = 0.2;
    uint64_t seed = 1;
    std::string device = "cuda";
};

// Self-contained task/model descriptor persisted with every dataset.
struct DatasetSpec
{
    std::string taskId = "gesture-classification";
    std::string displayName = "Gesture classification";
    std::string taskType = "classification";
    std::string modelFamily = "custom";
    uint32_t sampleRate = 250;
    size_t windowSamples = 0, strideSamples = 0;
    std::vector<uint8_t> channels;
    std::vector<std::string> labels; // compatibility shorthand; ids/names mirror labels
    std::vector<LabelSpec> labelSpecs;
    std::string preprocessing = "none";
    std::string normalization = "none";
    TrainingConfig training;
};

// Reads the descriptor and basic counters from a .bset manifest.  The parser
// intentionally accepts both schema v1 and v2 manifests so existing datasets
// can still be selected by the UI.
bool loadDatasetManifest(const std::string &directory, DatasetSpec &spec,
                         size_t *examples = nullptr, std::string *status = nullptr,
                         std::string *error = nullptr);
class DatasetWriter
{
public:
    bool open(const std::string &directory,const DatasetSpec &spec);
    bool append(const std::string &label,const std::vector<std::vector<int32_t>> &window,uint64_t sourceSample);
    void finish(const std::string &status="complete");
    bool isOpen() const{return m_open;}
private:
    std::string m_dir; std::ofstream m_index; DatasetSpec m_spec; uint64_t m_id=0; bool m_open=false;
};
#endif
