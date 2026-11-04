#include "monitoring/qualitycheck.h"
#include "monitoring/biquad.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace quality
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        // Second-order sections of an even-order Butterworth filter: section k
        // has Q = 1 / (2 cos((2k - 1) pi / 2n)), 0.541 and 1.307 for n = 4.
        void addButterworth(dsp::FilterChain &chain, dsp::Biquad::Type type, double fs, double hz, int order)
        {
            for (int k = 1; k <= order / 2; ++k)
            {
                dsp::Biquad section;
                section.configure(type, fs, hz, 1.0 / (2.0 * std::cos((2.0 * k - 1.0) * kPi / (2.0 * order))));
                chain.add(section);
            }
        }

        std::vector<double> withoutMean(const std::vector<double> &x)
        {
            double mean = 0.0;
            for (double value : x)
                mean += value;
            mean /= static_cast<double>(std::max<size_t>(1, x.size()));
            std::vector<double> out(x.size());
            for (size_t i = 0; i < x.size(); ++i)
                out[i] = x[i] - mean;
            return out;
        }

        // Forward and backward through the chain: zero phase. The odd
        // extension at both ends damps the edge transients, but the last tens
        // of milliseconds still depend on how the end is extended (scipy pads
        // differently), so the measures leave them out: TrialData::edgeSeconds.
        std::vector<double> zeroPhase(const std::vector<double> &x, double fs,
                                      const std::function<void(dsp::FilterChain &)> &configure)
        {
            if (x.size() < 2 || fs <= 0.0)
                return x;
            const size_t pad = std::min(x.size() - 1, static_cast<size_t>(fs * 0.15));
            std::vector<double> data;
            data.reserve(x.size() + 2 * pad);
            for (size_t i = pad; i >= 1; --i)
                data.push_back(2.0 * x.front() - x[i]);
            data.insert(data.end(), x.begin(), x.end());
            for (size_t i = 1; i <= pad; ++i)
                data.push_back(2.0 * x.back() - x[x.size() - 1 - i]);
            dsp::FilterChain chain;
            configure(chain);
            chain.process(data);
            std::reverse(data.begin(), data.end());
            chain.reset();
            chain.process(data);
            std::reverse(data.begin(), data.end());
            return std::vector<double>(data.begin() + static_cast<std::ptrdiff_t>(pad),
                                       data.end() - static_cast<std::ptrdiff_t>(pad));
        }

        double rms(const std::vector<double> &x, Range range)
        {
            if (range.from >= range.to || range.to > x.size())
                return 0.0;
            double sum = 0.0;
            for (size_t i = range.from; i < range.to; ++i)
                sum += x[i] * x[i];
            return std::sqrt(sum / static_cast<double>(range.to - range.from));
        }

        double median(std::vector<double> values)
        {
            const size_t middle = values.size() / 2;
            std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
            const double upper = values[middle];
            if (values.size() % 2)
                return upper;
            return (*std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle)) + upper) / 2.0;
        }

        // Modified z-score (Iglewicz and Hoaglin) of log(value) among the
        // logarithms of the reference values. The spread is kept above
        // `minSpread`: trials of one group differ by tens of percent, and with
        // a spread near zero any trifle would score high.
        double logZ(double value, const std::vector<double> &reference, double minSpread)
        {
            constexpr double kFloor = 1e-6;
            std::vector<double> logs;
            logs.reserve(reference.size());
            for (double v : reference)
                logs.push_back(std::log(std::max(v, kFloor)));
            const double center = median(logs);
            std::vector<double> spread;
            spread.reserve(logs.size());
            for (double v : logs)
                spread.push_back(std::abs(v - center));
            const double mad = std::max(median(spread), minSpread);
            return 0.6745 * (std::log(std::max(value, kFloor)) - center) / mad;
        }

        double param(const nlohmann::json &params, const char *name, double fallback)
        {
            const auto it = params.find(name);
            return it != params.end() && it->is_number() ? it->get<double>() : fallback;
        }

        // Relative judgement: the trial against the reference, channel by
        // channel, by the modified z-score of the logarithm; the largest score
        // over the channels decides.
        Judgement relative(const Measurement &trial, const std::vector<const Measurement *> &reference,
                           const nlohmann::json &params, const char *minimumName, bool twoSided)
        {
            Judgement result;
            const auto minimum = static_cast<size_t>(param(params, minimumName, 15));
            if (reference.size() < minimum)
            {
                result.verdict = Verdict::InsufficientData;
                return result;
            }
            const double minSpread = param(params, "min_spread", 0.05);
            double best = 0.0;
            for (size_t c = 0; c < trial.channels.size(); ++c)
            {
                std::vector<double> column;
                column.reserve(reference.size());
                for (const Measurement *other : reference)
                    if (c < other->channels.size())
                        column.push_back(other->channels[c]);
                if (column.size() < minimum)
                    continue;
                const double z = logZ(trial.channels[c], column, minSpread);
                best = std::max(best, twoSided ? std::abs(z) : z);
            }
            result.score = best;
            result.verdict = best > param(params, "threshold", 3.5) ? Verdict::Flagged : Verdict::Ok;
            return result;
        }

        double largest(const std::vector<double> &values)
        {
            return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
        }

        // Activity of the steady part: deviation from the trials of the same group.
        class DeviationCheck : public Check
        {
        public:
            std::string id() const override { return "deviation"; }
            Scope scope() const override { return Scope::Conditional; }
            std::vector<std::string> modalities() const override { return {"emg"}; }
            nlohmann::json defaults() const override
            {
                return {{"threshold", 3.5}, {"min_group", 15}, {"min_spread", 0.05},
                        {"low_hz", 20.0}, {"high_hz", 450.0}, {"notch_hz", 50.0}};
            }
            Measurement measure(const TrialData &trial, const nlohmann::json &params) const override
            {
                Measurement m;
                for (const auto &channel : *trial.raw)
                {
                    const auto band = bandLimited(channel, trial.sampleRate, param(params, "low_hz", 20.0),
                                                  param(params, "high_hz", 450.0), param(params, "notch_hz", 50.0));
                    m.channels.push_back(rms(band, steadyPart(band.size(), trial)));
                }
                m.value = largest(m.channels);
                return m;
            }
            Judgement judge(const Measurement &trial, const std::vector<const Measurement *> &reference,
                            const nlohmann::json &params) const override
            {
                return relative(trial, reference, params, "min_group", true);
            }
        };

        // Crest factor of the steady part: spikes among all trials.
        class CrestCheck : public Check
        {
        public:
            std::string id() const override { return "crest"; }
            Scope scope() const override { return Scope::Global; }
            std::vector<std::string> modalities() const override { return {"emg"}; }
            nlohmann::json defaults() const override
            {
                return {{"threshold", 3.5}, {"min_reference", 15}, {"min_spread", 0.05},
                        {"low_hz", 20.0}, {"high_hz", 450.0}, {"notch_hz", 50.0}};
            }
            Measurement measure(const TrialData &trial, const nlohmann::json &params) const override
            {
                Measurement m;
                for (const auto &channel : *trial.raw)
                {
                    const auto band = bandLimited(channel, trial.sampleRate, param(params, "low_hz", 20.0),
                                                  param(params, "high_hz", 450.0), param(params, "notch_hz", 50.0));
                    const Range range = steadyPart(band.size(), trial);
                    double peak = 0.0;
                    for (size_t i = range.from; i < range.to; ++i)
                        peak = std::max(peak, std::abs(band[i]));
                    const double level = rms(band, range);
                    m.channels.push_back(level > 0.0 ? peak / level : 0.0);
                }
                m.value = largest(m.channels);
                return m;
            }
            Judgement judge(const Measurement &trial, const std::vector<const Measurement *> &reference,
                            const nlohmann::json &params) const override
            {
                return relative(trial, reference, params, "min_reference", false);
            }
        };

        // RMS below 20 Hz in the steady part: electrode and cable motion,
        // baseline jumps of a poor contact.
        class LowFrequencyCheck : public Check
        {
        public:
            std::string id() const override { return "low_frequency"; }
            Scope scope() const override { return Scope::Global; }
            std::vector<std::string> modalities() const override { return {"emg"}; }
            nlohmann::json defaults() const override
            {
                return {{"threshold", 3.5}, {"min_reference", 15}, {"min_spread", 0.05}, {"cutoff_hz", 20.0}};
            }
            Measurement measure(const TrialData &trial, const nlohmann::json &params) const override
            {
                Measurement m;
                const double fs = trial.sampleRate;
                const double cutoff = param(params, "cutoff_hz", 20.0);
                for (const auto &channel : *trial.raw)
                {
                    const auto low = zeroPhase(withoutMean(channel), fs, [&](dsp::FilterChain &chain)
                                               { addButterworth(chain, dsp::Biquad::Type::LowPass, fs, cutoff, 4); });
                    m.channels.push_back(rms(low, steadyPart(low.size(), trial)));
                }
                m.value = largest(m.channels);
                return m;
            }
            Judgement judge(const Measurement &trial, const std::vector<const Measurement *> &reference,
                            const nlohmann::json &params) const override
            {
                return relative(trial, reference, params, "min_reference", false);
            }
        };

        // Share of raw samples at the ends of the ADC range, over the whole trial:
        // the gain is too high or an electrode came off.
        class SaturationCheck : public Check
        {
        public:
            std::string id() const override { return "saturation"; }
            Scope scope() const override { return Scope::Global; }
            std::vector<std::string> modalities() const override { return {"emg"}; }
            nlohmann::json defaults() const override { return {{"margin", 0.01}, {"max_share", 0.001}}; }
            Measurement measure(const TrialData &trial, const nlohmann::json &params) const override
            {
                Measurement m;
                const double margin = param(params, "margin", 0.01);
                for (size_t c = 0; c < trial.raw->size(); ++c)
                {
                    const double fullScale = c < trial.fullScale.size() ? trial.fullScale[c] : 0.0;
                    if (fullScale <= 0.0)
                    {
                        m.applicable = false;
                        m.channels.push_back(0.0);
                        continue;
                    }
                    const auto &x = (*trial.raw)[c];
                    const double limit = (1.0 - margin) * fullScale;
                    const auto near = std::count_if(x.begin(), x.end(), [&](double v) { return std::abs(v) >= limit; });
                    m.channels.push_back(x.empty() ? 0.0 : static_cast<double>(near) / static_cast<double>(x.size()));
                }
                m.value = largest(m.channels);
                return m;
            }
            Judgement judge(const Measurement &trial, const std::vector<const Measurement *> &,
                            const nlohmann::json &params) const override
            {
                Judgement result;
                result.score = trial.value;
                if (!trial.applicable)
                    result.verdict = Verdict::NotApplicable;
                else if (trial.value > param(params, "max_share", 0.001))
                    result.verdict = Verdict::Flagged;
                return result;
            }
        };

        // RMS of a channel near zero: the channel is dead or not connected.
        class FlatCheck : public Check
        {
        public:
            std::string id() const override { return "flat"; }
            Scope scope() const override { return Scope::Global; }
            std::vector<std::string> modalities() const override { return {"emg"}; }
            nlohmann::json defaults() const override { return {{"min_rms", 0.5}}; }
            Measurement measure(const TrialData &trial, const nlohmann::json &) const override
            {
                Measurement m;
                for (const auto &channel : *trial.raw)
                    m.channels.push_back(rms(withoutMean(channel), Range{0, channel.size()}));
                m.value = m.channels.empty() ? 0.0 : *std::min_element(m.channels.begin(), m.channels.end());
                return m;
            }
            Judgement judge(const Measurement &trial, const std::vector<const Measurement *> &,
                            const nlohmann::json &params) const override
            {
                Judgement result;
                result.score = trial.value;
                if (trial.value < param(params, "min_rms", 0.5))
                    result.verdict = Verdict::Flagged;
                return result;
            }
        };
    }

    Range steadyPart(size_t samples, const TrialData &trial)
    {
        const size_t skip = static_cast<size_t>(std::max(0.0, trial.skipSeconds) * trial.sampleRate);
        const size_t edge = static_cast<size_t>(std::max(0.0, trial.edgeSeconds) * trial.sampleRate);
        if (samples > 2 * (skip + edge))
            return {skip, samples - edge};
        return {0, samples};
    }

    std::vector<double> bandLimited(const std::vector<double> &raw, double fs, double lowHz, double highHz,
                                    double notchHz)
    {
        return zeroPhase(withoutMean(raw), fs, [&](dsp::FilterChain &chain)
                         {
                             if (lowHz > 0.0)
                                 addButterworth(chain, dsp::Biquad::Type::HighPass, fs, lowHz, 4);
                             if (highHz > 0.0 && highHz < fs / 2.0)
                                 addButterworth(chain, dsp::Biquad::Type::LowPass, fs, highHz, 4);
                             if (notchHz > 0.0 && notchHz < fs / 2.0)
                             {
                                 dsp::Biquad notch;
                                 notch.configure(dsp::Biquad::Type::Notch, fs, notchHz, 30.0);
                                 chain.add(notch);
                             } });
    }

    const std::vector<std::shared_ptr<const Check>> &builtinChecks()
    {
        static const std::vector<std::shared_ptr<const Check>> checks{
            std::make_shared<DeviationCheck>(), std::make_shared<CrestCheck>(),
            std::make_shared<LowFrequencyCheck>(), std::make_shared<SaturationCheck>(),
            std::make_shared<FlatCheck>()};
        return checks;
    }
}
