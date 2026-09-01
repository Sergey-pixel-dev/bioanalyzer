// Generate sample .bsig recordings for exercising the playback path.
//
// Build & run (from project root):
//   g++ -std=c++17 -Iinclude tools/make_sample_bsig.cpp \
//       src/core/sessionrecord.cpp -o /tmp/make_bsig && \
//   /tmp/make_bsig sample.bsig           # legacy 4-channel demo
//   /tmp/make_bsig sample8.bsig 8        # 8-channel demo
//
// With one argument it writes a 10 s, 4-channel, 250 Hz recording:
//   CH0: 1 Hz ECG-like (sum of harmonics)   CH1: 10 Hz sine
//   CH2: 50 Hz mains + noise                CH3: 0.5 Hz slow drift
//
// With a channel-count argument (e.g. 8) it writes that many channels, each
// carrying a distinct standard sine so every trace is visually identifiable:
//   CHn: (n+1) Hz sine at (400 + 100n) µV amplitude.
// Values are in microvolts.

#include "core/sessionrecord.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    // Legacy 4-channel content (kept for the default sample.bsig).
    void writeLegacy(const char *path, int fs, int n)
    {
        SessionHeader h;
        h.version = 1;
        h.sampleRate = fs;
        h.vrefMv = 3300;
        h.description = "Synthetic sample: ECG-like, sine, mains, drift";
        h.channels = {
            ChannelInfo{0, true, "ECG"},
            ChannelInfo{1, true, "10Hz"},
            ChannelInfo{2, true, "Mains"},
            ChannelInfo{3, true, "Drift"},
        };

        SessionWriter w;
        if (!w.open(path, h))
        {
            std::fprintf(stderr, "Failed to open %s for writing\n", path);
            std::exit(1);
        }

        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(i) / fs;
            double ecg = 800.0 * std::sin(2 * M_PI * 1.0 * t) +
                         300.0 * std::sin(2 * M_PI * 2.0 * t) +
                         150.0 * std::sin(2 * M_PI * 5.0 * t);
            double s10 = 500.0 * std::sin(2 * M_PI * 10.0 * t);
            double mains = 400.0 * std::sin(2 * M_PI * 50.0 * t) +
                           50.0 * std::sin(2 * M_PI * 123.0 * t);
            double drift = 1000.0 * std::sin(2 * M_PI * 0.5 * t);

            std::vector<int32_t> set = {
                static_cast<int32_t>(ecg),
                static_cast<int32_t>(s10),
                static_cast<int32_t>(mains),
                static_cast<int32_t>(drift),
            };
            w.writeSampleSet(set);
        }
        w.close();
        std::printf("Wrote %s: %llu sample sets, %d channels @ %d Hz\n",
                    path, static_cast<unsigned long long>(w.sampleCount()),
                    static_cast<int>(h.channels.size()), fs);
    }

    // N-channel demo: each channel a distinct standard sine.
    void writeSines(const char *path, int channels, int fs, int n)
    {
        SessionHeader h;
        h.version = 1;
        h.sampleRate = fs;
        h.vrefMv = 3300;
        h.description = "Synthetic " + std::to_string(channels) +
                        "-channel sines for visual testing";
        for (int c = 0; c < channels; ++c)
        {
            const double freq = c + 1;
            h.channels.push_back(
                ChannelInfo{static_cast<uint8_t>(c), true,
                            std::to_string(static_cast<int>(freq)) + "Hz"});
        }

        SessionWriter w;
        if (!w.open(path, h))
        {
            std::fprintf(stderr, "Failed to open %s for writing\n", path);
            std::exit(1);
        }

        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(i) / fs;
            std::vector<int32_t> set;
            set.reserve(channels);
            for (int c = 0; c < channels; ++c)
            {
                const double freq = c + 1;         // 1,2,3,... Hz
                const double amp = 400.0 + 100.0 * c; // distinct amplitudes
                set.push_back(static_cast<int32_t>(amp * std::sin(2 * M_PI * freq * t)));
            }
            w.writeSampleSet(set);
        }
        w.close();
        std::printf("Wrote %s: %llu sample sets, %d channels @ %d Hz\n",
                    path, static_cast<unsigned long long>(w.sampleCount()),
                    channels, fs);
    }
} // namespace

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "sample.bsig";
    const int fs = 250;
    const int seconds = 10;
    const int n = fs * seconds;

    if (argc > 2)
    {
        int channels = std::atoi(argv[2]);
        if (channels < 1)
            channels = 1;
        if (channels > 32)
            channels = 32;
        writeSines(path, channels, fs, n);
    }
    else
    {
        writeLegacy(path, fs, n);
    }
    return 0;
}
