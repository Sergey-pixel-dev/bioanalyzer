#ifndef DSP_SPECTRUM_H
#define DSP_SPECTRUM_H

#include <cstddef>
#include <vector>

namespace dsp
{

    // Window functions applied before the FFT to reduce spectral leakage.
    enum class Window
    {
        Rectangular,
        Hann,
        Hamming,
        Blackman,
    };

    // One spectral peak (harmonic) found in a magnitude spectrum.
    struct Harmonic
    {
        double frequencyHz = 0;
        double magnitude = 0; // linear magnitude (same units as input amplitude)
    };

    // Result of analysing one channel window.
    struct SpectrumResult
    {
        std::vector<double> frequencies; // Hz, length = bins
        std::vector<double> magnitude;   // linear magnitude per bin
        std::vector<Harmonic> peaks;     // strongest harmonics, sorted desc
        double binHz = 0;                // frequency resolution
    };

    // FFT-based spectrum analyser built on Eigen's unsupported FFT module.
    //
    // Analyses a real-valued signal window into a single-sided magnitude
    // spectrum and extracts the strongest harmonics. Used by the FFT tab.
    //
    // Uses Eigen internally; no Qt.
    class SpectrumAnalyzer
    {
    public:
        // Compute the single-sided magnitude spectrum of `signal` sampled at
        // `sampleRateHz`. The signal is windowed, zero-mean-removed (DC), and
        // (optionally) zero-padded to the next power of two for speed.
        static SpectrumResult analyze(const std::vector<double> &signal,
                                      double sampleRateHz,
                                      Window window = Window::Hann,
                                      int maxPeaks = 5);

        // Generate window coefficients of length n.
        static std::vector<double> makeWindow(Window type, size_t n);
    };

} // namespace dsp

#endif // DSP_SPECTRUM_H
