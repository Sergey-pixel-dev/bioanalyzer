#include "ai/datasetindex.h"
#include "common/translatable.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;
using nlohmann::json;

bool TrialInfo::relabeled() const
{
    return cue.type != target.type || cue.label != target.label;
}

bool TrialInfo::flagged() const
{
    return std::any_of(judgements.begin(), judgements.end(),
                       [](const quality::Judgement &j) { return j.verdict == quality::Verdict::Flagged; });
}

bool DatasetIndex::open(const std::string &directory, std::string *error)
{
    auto dataset = Dataset::open(directory, error);
    if (!dataset)
        return false;
    std::vector<TrialInfo> trials;
    try
    {
        std::ifstream index(fs::path(directory) / "examples.jsonl");
        if (!index)
        {
            if (error)
                *error = QT_TRANSLATE_NOOP("Errors", "the list of trials (examples.jsonl) cannot be read");
            return false;
        }
        std::string line;
        while (std::getline(index, line))
        {
            if (line.find_first_not_of(" \t\r") == std::string::npos)
                continue;
            const auto item = json::parse(line);
            TrialInfo trial;
            trial.id = item.at("id").get<uint64_t>();
            trial.session = item.value("session", uint32_t(0));
            trial.file = item.at("file").get<std::string>();
            trial.offset = item.at("offset").get<uint64_t>();
            trial.recordedAt = item.value("recorded_at", std::string());
            trial.target = targetFromJson(item.at("target"));
            trial.cue = item.contains("cue") ? targetFromJson(item.at("cue")) : trial.target;
            trial.excluded = item.value("excluded", false);
            trial.note = item.value("note", std::string());
            trials.push_back(std::move(trial));
        }
    }
    catch (const std::exception &exception)
    {
        if (error)
            *error = exception.what();
        return false;
    }
    m_dir = directory;
    m_spec = dataset->spec();
    m_config = quality::configFromTask(m_spec.task.quality);
    m_trials = std::move(trials);
    return true;
}

bool DatasetIndex::readSamples(const TrialInfo &trial, std::vector<std::vector<double>> &out,
                               std::string *error) const
{
    const size_t channels = m_spec.channels.size();
    std::ifstream file(fs::path(m_dir) / trial.file, std::ios::binary);
    if (channels == 0 || trial.file.empty() || !file)
    {
        if (error)
            *error = QT_TRANSLATE_NOOP("Errors", "the signal file of a trial cannot be read");
        return false;
    }
    // The trial is one window from its offset in the session's file; samples
    // are little-endian int32, interleaved by channel.
    const size_t samples = m_spec.windowSamples;
    std::vector<int32_t> raw(samples * channels);
    file.seekg(static_cast<std::streamoff>(trial.offset * channels * sizeof(int32_t)));
    file.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size() * sizeof(int32_t)));
    if (!file || static_cast<size_t>(file.gcount()) != raw.size() * sizeof(int32_t))
    {
        if (error)
            *error = QT_TRANSLATE_NOOP("Errors", "the signal file of a trial is shorter than the manifest says");
        return false;
    }
    out.assign(channels, std::vector<double>(samples));
    for (size_t s = 0; s < samples; ++s)
        for (size_t c = 0; c < channels; ++c)
            out[c][s] = static_cast<double>(raw[s * channels + c]);
    return true;
}

bool DatasetIndex::evaluate(std::string *error)
{
    std::vector<std::vector<double>> signal;
    for (auto &trial : m_trials)
    {
        trial.measurements.assign(m_config.checks.size(), quality::Measurement{});
        bool signalRead = false;
        for (size_t c = 0; c < m_config.checks.size(); ++c)
        {
            const auto &configured = m_config.checks[c];
            if (!signalRead)
            {
                if (!readSamples(trial, signal, error))
                    return false;
                signalRead = true;
            }
            const quality::TrialData data =
                quality::trialData(m_config, signal, static_cast<double>(m_spec.sampleRate), fullScaleMicrovolts(m_spec.gain));
            trial.measurements[c] = configured.check->measure(data, configured.params);
        }
    }
    judge();
    return true;
}

void DatasetIndex::judge()
{
    std::vector<std::string> groups;
    std::vector<std::vector<quality::Measurement>> measurements;
    groups.reserve(m_trials.size());
    measurements.reserve(m_trials.size());
    // Excluded trials stay in the reference, so that the flags do not change
    // with the selection.
    for (const auto &trial : m_trials)
    {
        groups.push_back(quality::groupKey(trial.target, m_config.groupBy));
        measurements.push_back(trial.measurements);
    }
    const auto judgements = quality::judgeAll(m_config, groups, measurements);
    for (size_t t = 0; t < m_trials.size(); ++t)
        m_trials[t].judgements = judgements[t];
}

bool DatasetIndex::signal(size_t trial, std::vector<std::vector<double>> &out, std::string *error) const
{
    if (trial >= m_trials.size() || !readSamples(m_trials[trial], out, error))
        return false;
    for (auto &channel : out)
        channel = quality::bandLimited(channel, static_cast<double>(m_spec.sampleRate));
    return true;
}

bool DatasetIndex::save(std::string *error)
{
    // The lock is held for the rewrite: a capture, here or in another copy of
    // the program, keeps appending to the file being replaced and would lose
    // its trials.
    DatasetLock lock;
    const auto locked = lock.acquire(m_dir);
    if (locked != DatasetLock::Result::Acquired)
    {
        if (error)
            *error = locked == DatasetLock::Result::Busy
                         ? QT_TRANSLATE_NOOP("Errors", "the dataset is being captured now, possibly in another copy of the program; change it after the capture ends")
                         : QT_TRANSLATE_NOOP("Errors", "the dataset cannot be locked for writing");
        return false;
    }
    std::map<uint64_t, const TrialInfo *> byId;
    for (const auto &trial : m_trials)
        byId[trial.id] = &trial;
    return rewriteExamples(m_dir, [&](json &item)
                           {
                               const auto found = byId.find(item.value("id", uint64_t(0)));
                               if (found == byId.end())
                                   return;
                               const TrialInfo &trial = *found->second;
                               item["target"] = targetToJson(trial.target);
                               item["cue"] = targetToJson(trial.cue);
                               item["excluded"] = trial.excluded;
                               item["note"] = trial.note; },
                           error);
}

bool DatasetIndex::setExcluded(const std::vector<size_t> &trials, bool excluded, std::string *error)
{
    const auto before = m_trials;
    for (size_t trial : trials)
        if (trial < m_trials.size())
            m_trials[trial].excluded = excluded;
    if (!save(error))
    {
        m_trials = before;
        return false;
    }
    return true;
}

bool DatasetIndex::setTarget(size_t trial, const TargetValue &target, std::string *error)
{
    if (trial >= m_trials.size())
        return false;
    const TargetValue before = m_trials[trial].target;
    m_trials[trial].target = target;
    if (!save(error))
    {
        m_trials[trial].target = before;
        return false;
    }
    // The trial moved to another group: the conditional checks change.
    judge();
    return true;
}

bool DatasetIndex::setNote(size_t trial, const std::string &note, std::string *error)
{
    if (trial >= m_trials.size())
        return false;
    const std::string before = m_trials[trial].note;
    m_trials[trial].note = note;
    if (!save(error))
    {
        m_trials[trial].note = before;
        return false;
    }
    return true;
}

std::vector<std::string> DatasetIndex::includedLabels() const
{
    std::vector<std::string> labels;
    for (const auto &trial : m_trials)
        if (!trial.excluded && std::find(labels.begin(), labels.end(), trial.target.label) == labels.end())
            labels.push_back(trial.target.label);
    return labels;
}

size_t DatasetIndex::excludedCount() const
{
    return static_cast<size_t>(std::count_if(m_trials.begin(), m_trials.end(),
                                             [](const TrialInfo &trial) { return trial.excluded; }));
}
