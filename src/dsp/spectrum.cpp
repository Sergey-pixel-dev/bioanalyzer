#include "dsp/spectrum.h"

#include <cmath>
#include <complex>
#include <algorithm>

#include <Eigen/Core>
#include <unsupported/Eigen/FFT>

namespace dsp
{

    std::vector<double> SpectrumAnalyzer::makeWindow(Window type, size_t n)
    {
        std::vector<double> w(n, 1.0);
        if (n <= 1)
            return w;
        const double N = static_cast<double>(n - 1);
        for (size_t i = 0; i < n; ++i)
        {
            const double x = static_cast<double>(i) / N;
            switch (type)
            {
            case Window::Rectangular:
                w[i] = 1.0;
                break;
            case Window::Hann:
                w[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * x);
                break;
            case Window::Hamming:
                w[i] = 0.54 - 0.46 * std::cos(2.0 * M_PI * x);
                break;
            case Window::Blackman:
                w[i] = 0.42 - 0.5 * std::cos(2.0 * M_PI * x) +
                       0.08 * std::cos(4.0 * M_PI * x);
                break;
            }
        }
        return w;
    }

    namespace
    {
        size_t nextPow2(size_t n)
        {
            size_t p = 1;
            while (p < n)
                p <<= 1;
            return p;
        }
    } // namespace

    SpectrumResult SpectrumAnalyzer::analyze(const std::vector<double> &signal,
                                             double sampleRateHz,
                                             Window window,
                                             int maxPeaks)
    {
        SpectrumResult result;
        const size_t n = signal.size();
        if (n < 2 || sampleRateHz <= 0)
            return result;

        // Remove DC (mean) so the 0 Hz bin does not swamp the spectrum.
        double mean = 0;
        for (double v : signal)
            mean += v;
        mean /= static_cast<double>(n);

        // Apply window and compute the coherent gain for amplitude correction.
        std::vector<double> win = makeWindow(window, n);
        double winSum = 0;
        for (double w : win)
            winSum += w;
        if (winSum == 0)
            winSum = 1;

        // Zero-pad to next power of two.
        const size_t nfft = nextPow2(n);
        std::vector<double> buf(nfft, 0.0);
        for (size_t i = 0; i < n; ++i)
            buf[i] = (signal[i] - mean) * win[i];

        Eigen::FFT<double> fft;
        std::vector<std::complex<double>> spec;
        fft.fwd(spec, buf);

        // Single-sided spectrum: bins 0..nfft/2.
        const size_t bins = nfft / 2 + 1;
        result.binHz = sampleRateHz / static_cast<double>(nfft);
        result.frequencies.resize(bins);
        result.magnitude.resize(bins);

        // Amplitude scaling: 2/coherentGain for a single-sided spectrum
        // (coherent gain = winSum). DC and Nyquist are not doubled.
        const double scale = 2.0 / winSum;
        for (size_t k = 0; k < bins; ++k)
        {
            result.frequencies[k] = k * result.binHz;
            double mag = std::abs(spec[k]) * scale;
            if (k == 0 || k == bins - 1)
                mag *= 0.5;
            result.magnitude[k] = mag;
        }

        // Peak picking: local maxima, skip DC bin, sort by magnitude desc.
        std::vector<Harmonic> peaks;
        for (size_t k = 1; k + 1 < bins; ++k)
        {
            if (result.magnitude[k] > result.magnitude[k - 1] &&
                result.magnitude[k] >= result.magnitude[k + 1])
            {
                peaks.push_back({result.frequencies[k], result.magnitude[k]});
            }
        }
        std::sort(peaks.begin(), peaks.end(),
                  [](const Harmonic &a, const Harmonic &b)
                  { return a.magnitude > b.magnitude; });
        if (maxPeaks > 0 && static_cast<int>(peaks.size()) > maxPeaks)
            peaks.resize(maxPeaks);
        result.peaks = std::move(peaks);

        return result;
    }

} // namespace dsp
