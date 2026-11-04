#ifndef QUALITYCHECK_H
#define QUALITYCHECK_H
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>

// Quality checks of trials, by analogy with the model interface. They belong
// to the analysis package: like the display filters, they are signal
// processing (RMS, crest factor, the part below 20 Hz, saturation, a flat
// channel, the robust z-score) and know nothing of datasets or tasks; the
// registry of the AI package (ai/qualityregistry.h) builds them from a task
// description and groups the trials by their target. A check
// measures one trial — the measurements are not stored, they are taken anew
// whenever a dataset is opened — and judges it against a reference: the trials of its group for conditional checks
// (for classification, the same target value), all trials or an absolute
// limit for global ones. Which checks run, with which parameters and how the
// trials are grouped is set by the task description; no check knows the type
// of the target.
namespace quality
{
    enum class Scope
    {
        Global,     // does not depend on the target: compared with all trials or a limit
        Conditional // "unlike its group": compared within the group only
    };

    enum class Verdict
    {
        Ok,
        Flagged,
        InsufficientData, // the reference is too small to judge
        NotApplicable     // the trial lacks what the check needs (e.g. the ADC range)
    };

    // One trial as a check sees it.
    struct TrialData
    {
        const std::vector<std::vector<double>> *raw = nullptr; // [channel][sample], units of the dataset
        double sampleRate = 0.0;
        // Full scale of the ADC per channel in the units of the dataset; empty
        // or zero when unknown (a recording played back).
        std::vector<double> fullScale;
        // The steady part of the trial starts here: before it the subject
        // reacts to the cue and builds up the force.
        double skipSeconds = 0.5;
        // The end of the trial left out: a filter run forward and backward
        // distorts the last tens of milliseconds.
        double edgeSeconds = 0.05;
    };

    // Value of a check on one trial: per channel and over the channels.
    struct Measurement
    {
        std::vector<double> channels;
        double value = 0.0;
        bool applicable = true;
    };

    struct Judgement
    {
        Verdict verdict = Verdict::Ok;
        double score = 0.0; // a z-score, or the measured value for absolute checks
    };

    class Check
    {
    public:
        virtual ~Check() = default;
        virtual std::string id() const = 0;
        virtual Scope scope() const = 0;
        // Kinds of signal the check is meant for ("emg").
        virtual std::vector<std::string> modalities() const = 0;
        virtual nlohmann::json defaults() const = 0;
        virtual Measurement measure(const TrialData &trial, const nlohmann::json &params) const = 0;
        // `reference` is the group (conditional) or all trials (global) and may
        // include the trial itself.
        virtual Judgement judge(const Measurement &trial, const std::vector<const Measurement *> &reference,
                                const nlohmann::json &params) const = 0;
    };

    // Samples [from, to) of a trial.
    struct Range
    {
        size_t from = 0;
        size_t to = 0;
    };

    // The steady part: after skipSeconds and before the last edgeSeconds; a
    // trial too short for that is taken whole.
    Range steadyPart(size_t samples, const TrialData &trial);

    // The trial as the model's preprocessing sees it: without the mean, 20-450 Hz
    // (the upper edge when the rate allows it) and the mains notch, fourth-order
    // Butterworth sections run forward and backward like scipy's sosfiltfilt.
    std::vector<double> bandLimited(const std::vector<double> &raw, double fs, double lowHz = 20.0,
                                    double highHz = 450.0, double notchHz = 50.0);

    // The checks this version runs, by id.
    const std::vector<std::shared_ptr<const Check>> &builtinChecks();
}

#endif
