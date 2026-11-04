#ifndef QUALITYREGISTRY_H
#define QUALITYREGISTRY_H
#include "ai/dataset.h"
#include "monitoring/qualitycheck.h"

#include <memory>
#include <string>
#include <vector>

// The checks a task asks for and how they are applied to a dataset or to one
// new trial during capture. Built from the "quality" section of the task
// description:
//
//   "quality": {"modality": "emg", "skip_ms": 500, "edge_ms": 50, "group_by": {"field": "label"},
//               "checks": [{"id": "deviation", "params": {"min_group": 15}}, ...]}
//
// Nothing of the checks is stored in the dataset: the trials are measured
// anew whenever the dataset is opened.
namespace quality
{
    struct ConfiguredCheck
    {
        std::shared_ptr<const Check> check;
        nlohmann::json params; // the check's defaults with the task's values on top
    };

    struct Config
    {
        std::string modality = "emg";
        double skipSeconds = 0.5;
        double edgeSeconds = 0.05;
        nlohmann::json groupBy = {{"field", "label"}};
        std::vector<ConfiguredCheck> checks;
        // Checks the description names but this version does not know, or that
        // are not meant for its modality; they are skipped.
        std::vector<std::string> skipped;
    };

    Config configFromTask(const nlohmann::json &quality);

    // Group of a trial for conditional checks: its label ({"field": "label"});
    // one group for any other field.
    std::string groupKey(const TargetValue &target, const nlohmann::json &groupBy);

    // One trial as the checks of the config see it: the steady part as the
    // config sets it.
    TrialData trialData(const Config &config, const std::vector<std::vector<double>> &raw, double sampleRate,
                        const std::vector<double> &fullScale);

    // Measurements of one trial by every check of the config, in its order.
    std::vector<Measurement> measureAll(const Config &config, const TrialData &trial);
    // Judgements of every trial by every check: measurements[trial][check].
    std::vector<std::vector<Judgement>> judgeAll(const Config &config, const std::vector<std::string> &groups,
                                                 const std::vector<std::vector<Measurement>> &measurements);
    // Judgements of the last trial against all the trials before it and itself.
    std::vector<Judgement> judgeLast(const Config &config, const std::vector<std::string> &groups,
                                     const std::vector<std::vector<Measurement>> &measurements);

}

#endif
