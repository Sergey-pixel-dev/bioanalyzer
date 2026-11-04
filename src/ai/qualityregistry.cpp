#include "ai/qualityregistry.h"

#include <algorithm>
#include <map>

namespace quality
{
    namespace
    {
        std::vector<const Measurement *> reference(const std::vector<std::vector<Measurement>> &measurements,
                                                   size_t check, const std::vector<size_t> &trials)
        {
            std::vector<const Measurement *> out;
            out.reserve(trials.size());
            for (size_t trial : trials)
                if (check < measurements[trial].size() && measurements[trial][check].applicable)
                    out.push_back(&measurements[trial][check]);
            return out;
        }
    }

    Config configFromTask(const nlohmann::json &quality)
    {
        Config config;
        if (!quality.is_object())
            return config;
        config.modality = quality.value("modality", std::string("emg"));
        config.skipSeconds = std::max(0.0, quality.value("skip_ms", 500.0) / 1000.0);
        config.edgeSeconds = std::max(0.0, quality.value("edge_ms", 50.0) / 1000.0);
        if (quality.contains("group_by"))
            config.groupBy = quality.at("group_by");
        const nlohmann::json checks = quality.value("checks", nlohmann::json::array());
        for (const auto &entry : checks)
        {
            const std::string id = entry.value("id", std::string());
            std::shared_ptr<const Check> check;
            for (const auto &builtin : builtinChecks())
                if (builtin->id() == id)
                    check = builtin;
            const auto modalities = check ? check->modalities() : std::vector<std::string>{};
            if (!check || std::find(modalities.begin(), modalities.end(), config.modality) == modalities.end())
            {
                config.skipped.push_back(id);
                continue;
            }
            ConfiguredCheck configured;
            configured.check = check;
            configured.params = check->defaults();
            const nlohmann::json params = entry.value("params", nlohmann::json::object());
            for (const auto &item : params.items())
                configured.params[item.key()] = item.value();
            config.checks.push_back(std::move(configured));
        }
        return config;
    }

    std::string groupKey(const TargetValue &target, const nlohmann::json &groupBy)
    {
        // Grouping by the kind of target (isSupportedTask()): only the label
        // so far; any other field puts all trials in one group.
        const std::string field = groupBy.is_object() ? groupBy.value("field", std::string("label")) : "label";
        return field == "label" ? target.label : "*";
    }

    TrialData trialData(const Config &config, const std::vector<std::vector<double>> &raw, double sampleRate,
                        const std::vector<double> &fullScale)
    {
        TrialData data;
        data.raw = &raw;
        data.sampleRate = sampleRate;
        data.fullScale = fullScale;
        data.skipSeconds = config.skipSeconds;
        data.edgeSeconds = config.edgeSeconds;
        return data;
    }

    std::vector<Measurement> measureAll(const Config &config, const TrialData &trial)
    {
        std::vector<Measurement> out;
        out.reserve(config.checks.size());
        for (const auto &configured : config.checks)
            out.push_back(configured.check->measure(trial, configured.params));
        return out;
    }

    std::vector<std::vector<Judgement>> judgeAll(const Config &config, const std::vector<std::string> &groups,
                                                 const std::vector<std::vector<Measurement>> &measurements)
    {
        std::vector<size_t> everyone(measurements.size());
        for (size_t i = 0; i < everyone.size(); ++i)
            everyone[i] = i;
        std::map<std::string, std::vector<size_t>> byGroup;
        for (size_t i = 0; i < groups.size(); ++i)
            byGroup[groups[i]].push_back(i);
        std::vector<std::vector<Judgement>> out(measurements.size(), std::vector<Judgement>(config.checks.size()));
        for (size_t c = 0; c < config.checks.size(); ++c)
        {
            const auto &configured = config.checks[c];
            const bool conditional = configured.check->scope() == Scope::Conditional;
            const auto all = conditional ? std::vector<const Measurement *>{} : reference(measurements, c, everyone);
            std::map<std::string, std::vector<const Measurement *>> groupReference;
            if (conditional)
                for (const auto &group : byGroup)
                    groupReference[group.first] = reference(measurements, c, group.second);
            for (size_t t = 0; t < measurements.size(); ++t)
                out[t][c] = configured.check->judge(measurements[t][c], conditional ? groupReference[groups[t]] : all,
                                                    configured.params);
        }
        return out;
    }

    std::vector<Judgement> judgeLast(const Config &config, const std::vector<std::string> &groups,
                                     const std::vector<std::vector<Measurement>> &measurements)
    {
        std::vector<Judgement> out(config.checks.size());
        if (measurements.empty())
            return out;
        const size_t last = measurements.size() - 1;
        std::vector<size_t> everyone(measurements.size());
        std::vector<size_t> sameGroup;
        for (size_t i = 0; i < measurements.size(); ++i)
        {
            everyone[i] = i;
            if (groups[i] == groups[last])
                sameGroup.push_back(i);
        }
        for (size_t c = 0; c < config.checks.size(); ++c)
        {
            const auto &configured = config.checks[c];
            const bool conditional = configured.check->scope() == Scope::Conditional;
            out[c] = configured.check->judge(measurements[last][c],
                                             reference(measurements, c, conditional ? sameGroup : everyone),
                                             configured.params);
        }
        return out;
    }
}
