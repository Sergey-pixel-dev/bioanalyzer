// DSP tests: biquad filtering and FFT spectral analysis.
//
// Build (from project root):
//   g++ -std=c++17 -Iinclude -I/usr/include/eigen3 tests/test_dsp.cpp \
//       src/dsp/biquad.cpp src/dsp/spectrum.cpp -o /tmp/test_dsp && /tmp/test_dsp

#include "dsp/biquad.h"
#include "dsp/spectrum.h"

#include <cstdio>
#include <cmath>
#include <vector>

namespace
{
    int g_failures = 0;
    void check(bool cond, const char *what)
    {
        if (!cond)
        {
            std::printf("  FAIL: %s\n", what);
            ++g_failures;
        }
    }

    // Generate a sine wave.
    std::vector<double> sine(double freq, double fs, int n, double amp = 1.0)
    {
        std::vector<double> s(n);
        for (int i = 0; i < n; ++i)
            s[i] = amp * std::sin(2.0 * M_PI * freq * i / fs);
        return s;
    }

    double rms(const std::vector<double> &v)
    {
        double sum = 0;
        for (double x : v)
            sum += x * x;
        return std::sqrt(sum / v.size());
    }

    void testLowPassAttenuatesHigh()
    {
        std::printf("testLowPassAttenuatesHigh\n");
        const double fs = 1000;
        // 200 Hz sine through a 50 Hz low-pass should be strongly attenuated.
        auto high = sine(200, fs, 4000);
        dsp::Biquad lp;
        lp.configure(dsp::Biquad::Type::LowPass, fs, 50, 0.707);
        double before = rms(high);
        lp.process(high);
        double after = rms(high);
        check(after < before * 0.2, "200 Hz attenuated by 50 Hz LP");
    }

    void testLowPassPassesLow()
    {
        std::printf("testLowPassPassesLow\n");
        const double fs = 1000;
        auto low = sine(5, fs, 4000);
        dsp::Biquad lp;
        lp.configure(dsp::Biquad::Type::LowPass, fs, 50, 0.707);
        double before = rms(low);
        // Skip transient (first 200 samples).
        lp.process(low);
        std::vector<double> tail(low.begin() + 200, low.end());
        double after = rms(tail);
        check(after > before * 0.8, "5 Hz passes through 50 Hz LP");
    }

    void testNotchRejectsMains()
    {
        std::printf("testNotchRejectsMains\n");
        const double fs = 1000;
        auto mains = sine(50, fs, 4000);
        dsp::Biquad notch;
        notch.configure(dsp::Biquad::Type::Notch, fs, 50, 30);
        notch.process(mains);
        std::vector<double> tail(mains.begin() + 400, mains.end());
        check(rms(tail) < 0.1, "50 Hz notch rejects mains");
    }

    void testFftFindsPeak()
    {
        std::printf("testFftFindsPeak\n");
        const double fs = 1000;
        // 50 Hz sine, amplitude 2.0.
        auto s = sine(50, fs, 1024, 2.0);
        auto res = dsp::SpectrumAnalyzer::analyze(s, fs, dsp::Window::Hann, 5);
        check(!res.peaks.empty(), "at least one peak found");
        if (!res.peaks.empty())
        {
            double f = res.peaks[0].frequencyHz;
            check(std::abs(f - 50.0) < 2.0, "dominant peak near 50 Hz");
            // Amplitude estimate should be close to 2.0 (within 10%).
            double a = res.peaks[0].magnitude;
            check(std::abs(a - 2.0) < 0.2, "peak amplitude near 2.0");
        }
    }

    void testFftTwoTones()
    {
        std::printf("testFftTwoTones\n");
        const double fs = 2000;
        int n = 2048;
        auto s1 = sine(60, fs, n, 1.0);
        auto s2 = sine(180, fs, n, 0.5);
        std::vector<double> mix(n);
        for (int i = 0; i < n; ++i)
            mix[i] = s1[i] + s2[i];
        auto res = dsp::SpectrumAnalyzer::analyze(mix, fs, dsp::Window::Hann, 5);
        check(res.peaks.size() >= 2, "two peaks found");
        if (res.peaks.size() >= 2)
        {
            // Strongest should be 60 Hz, second 180 Hz.
            check(std::abs(res.peaks[0].frequencyHz - 60.0) < 3.0, "peak0 ~ 60 Hz");
            bool has180 = false;
            for (auto &p : res.peaks)
                if (std::abs(p.frequencyHz - 180.0) < 3.0)
                    has180 = true;
            check(has180, "180 Hz peak present");
        }
    }
} // namespace

int main()
{
    testLowPassAttenuatesHigh();
    testLowPassPassesLow();
    testNotchRejectsMains();
    testFftFindsPeak();
    testFftTwoTones();

    if (g_failures == 0)
        std::printf("\nALL TESTS PASSED\n");
    else
        std::printf("\n%d CHECK(S) FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
