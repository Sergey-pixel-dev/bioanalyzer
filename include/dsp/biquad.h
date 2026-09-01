#ifndef DSP_BIQUAD_H
#define DSP_BIQUAD_H

#include <cstddef>
#include <vector>

namespace dsp
{

    // Single Direct-Form-II transposed biquad section with RBJ cookbook
    // coefficients. Filters biosignals (ECG/EEG/EMG): high-pass to remove
    // baseline drift, low-pass to limit bandwidth, notch to reject mains hum.
    //
    // Pure C++/math: no Qt, no Eigen needed here. Stateful — one instance per
    // channel per filter stage.
    class Biquad
    {
    public:
        enum class Type
        {
            LowPass,
            HighPass,
            BandPass,
            Notch,
        };

        Biquad() = default;

        // Configure coefficients. fs = sample rate (Hz), f0 = corner/center (Hz),
        // Q = quality factor. Resets internal state.
        void configure(Type type, double fs, double f0, double q);

        // Process one sample.
        double process(double x);

        // Process a block in place.
        void process(std::vector<double> &data);

        // Zero the delay line (call when signal geometry changes).
        void reset();

    private:
        double m_b0 = 1, m_b1 = 0, m_b2 = 0;
        double m_a1 = 0, m_a2 = 0;
        double m_z1 = 0, m_z2 = 0;
    };

    // A cascade of biquads applied in series (e.g. HP + LP + notch), one chain per
    // channel. Provides a convenient front for the filter tab.
    class FilterChain
    {
    public:
        void clear() { m_stages.clear(); }
        void add(const Biquad &stage) { m_stages.push_back(stage); }

        // Build a standard biosignal chain: optional high-pass, low-pass, and
        // 50/60 Hz notch. Pass <= 0 to skip a stage.
        void configureStandard(double fs, double hpHz, double lpHz, double notchHz, double notchQ);

        double process(double x);
        void process(std::vector<double> &data);
        void reset();

        size_t stageCount() const { return m_stages.size(); }

    private:
        std::vector<Biquad> m_stages;
    };

} // namespace dsp

#endif // DSP_BIQUAD_H
