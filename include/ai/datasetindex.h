#ifndef DATASETINDEX_H
#define DATASETINDEX_H
#include "ai/dataset.h"
#include "ai/qualityregistry.h"
#include <cstdint>
#include <string>
#include <vector>

// One trial of a dataset with its metadata. The signal file never changes;
// everything the operator decides is kept in the list of trials.
struct TrialInfo
{
    uint64_t id = 0;
    uint32_t session = 0;
    std::string file;    // signal of the trial's session
    uint64_t offset = 0; // where the trial starts in it, samples per channel
    std::string recordedAt;
    TargetValue cue;    // presented by the cue; never changes
    TargetValue target; // used for training; may be corrected
    bool excluded = false;
    std::string note;
    // Per configured check, in the order of DatasetIndex::config().checks.
    std::vector<quality::Measurement> measurements;
    std::vector<quality::Judgement> judgements;

    bool relabeled() const;
    bool flagged() const;
};

// Index of a dataset for browsing and editing its trials. The quality checks
// measure every trial anew on each opening: measurements are derived data and
// are not stored, so opening never writes the dataset. Changes are
// written through a temporary file under the dataset's lock (DatasetLock) and
// refused while the dataset is being captured, here or in another copy of the
// program: the writer would keep appending to the replaced file.
class DatasetIndex
{
public:
    // Reads the manifest and the list of trials, without the signals.
    bool open(const std::string &directory, std::string *error = nullptr);
    // Measures every trial by the task's checks and judges it; reads the
    // signals, writes nothing.
    bool evaluate(std::string *error = nullptr);
    // Signal of one trial, [channel][sample], as the model's preprocessing
    // sees it.
    bool signal(size_t trial, std::vector<std::vector<double>> &out,
                std::string *error = nullptr) const;

    bool setExcluded(const std::vector<size_t> &trials, bool excluded, std::string *error = nullptr);
    bool setTarget(size_t trial, const TargetValue &target, std::string *error = nullptr);
    bool setNote(size_t trial, const std::string &note, std::string *error = nullptr);

    const std::string &directory() const { return m_dir; }
    const DatasetSpec &spec() const { return m_spec; }
    const quality::Config &config() const { return m_config; }
    const std::vector<TrialInfo> &trials() const { return m_trials; }
    // Target values that still have trials in the selection, in order of
    // first appearance.
    std::vector<std::string> includedLabels() const;
    size_t excludedCount() const;

private:
    bool readSamples(const TrialInfo &trial, std::vector<std::vector<double>> &out,
                     std::string *error) const;
    void judge();
    // Writes the metadata of every trial back to the list.
    bool save(std::string *error);

    std::string m_dir;
    DatasetSpec m_spec;
    quality::Config m_config;
    std::vector<TrialInfo> m_trials;
};
#endif
