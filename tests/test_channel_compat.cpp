// .bsig cross-channel-count compatibility.
//
// A recording captured with one channel count (e.g. 12) must remain usable by
// a session that streams a different set (e.g. 8). The phantom maps each
// requested physical channel to its column in the recording and emits zeros
// for channels the recording does not contain. This test verifies both
// directions: streaming a subset of a wider recording, and requesting channels
// beyond a narrower recording.
//
// Build (from project root):
//   g++ -std=c++17 -Iinclude tests/test_channel_compat.cpp \
//       src/core/transportprotocol.cpp src/core/applicationprotocol.cpp \
//       src/core/ecgadcprotocol.cpp src/core/samplebuffer.cpp \
//       src/core/sessionrecord.cpp src/core/phantomdevice.cpp \
//       src/core/devicesession.cpp \
//       -o /tmp/test_channel_compat && /tmp/test_channel_compat

#include "core/sessionrecord.h"
#include "core/phantomdevice.h"
#include "core/devicesession.h"
#include "core/ecgadcprotocol.h"

#include <cstdio>
#include <string>
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

    // Write a recording with `channels` physical channels (indices 0..N-1),
    // where channel c holds the constant value 1000 + c so we can identify it.
    void writeRecording(const std::string &path, int channels, int samples)
    {
        SessionHeader h;
        h.version = 1;
        h.sampleRate = 250;
        h.vrefMv = 3300;
        h.description = std::to_string(channels) + "ch";
        for (int c = 0; c < channels; ++c)
            h.channels.push_back(ChannelInfo{static_cast<uint8_t>(c), true,
                                             "ch" + std::to_string(c)});

        SessionWriter w;
        check(w.open(path, h), "writer opens");
        for (int i = 0; i < samples; ++i)
        {
            std::vector<int32_t> set;
            set.reserve(channels);
            for (int c = 0; c < channels; ++c)
                set.push_back(1000 + c);
            w.writeSampleSet(set);
        }
        w.close();
    }

    // Play `requestChannels` from a recording, returning the first sample set
    // seen per requested channel.
    std::vector<int32_t> playFirstSet(const std::string &path,
                                      const std::vector<uint8_t> &requestChannels)
    {
        auto *phantom = new PhantomDevice();
        phantom->loadRecording(path);
        DeviceSession session(phantom, /*ownTransport=*/true);
        session.open();

        std::vector<int32_t> first(requestChannels.size(), 0);
        bool captured = false;
        session.setSampleCallback([&](const EcgAdcProtocol::SampleFrame &f)
                                  {
            if (captured) return;
            for (size_t c = 0; c < f.samples.size() && c < first.size(); ++c)
                if (!f.samples[c].empty())
                    first[c] = f.samples[c].front();
            captured = true; });

        session.startStream(requestChannels);
        session.poll();
        for (int step = 0; step < 4 && !captured; ++step)
        {
            phantom->pump(100.0);
            session.poll();
        }
        return first;
    }

    void testSubsetOfWiderRecording()
    {
        std::printf("testSubsetOfWiderRecording (stream 8 of 12)\n");
        const std::string path = "/tmp/test_12ch.bsig";
        writeRecording(path, 12, 300);

        std::vector<uint8_t> req = {0, 1, 2, 3, 4, 5, 6, 7};
        auto first = playFirstSet(path, req);
        bool ok = first.size() == 8;
        for (int c = 0; c < 8 && ok; ++c)
            if (first[c] != 1000 + c)
                ok = false;
        check(ok, "8-channel subset reads the correct columns of a 12-ch file");
    }

    void testRequestBeyondNarrowRecording()
    {
        std::printf("testRequestBeyondNarrowRecording (stream 8, file has 8)\n");
        // Ask for the full 8 from an 8-channel file.
        const std::string path = "/tmp/test_8ch.bsig";
        writeRecording(path, 8, 300);

        std::vector<uint8_t> req = {0, 1, 2, 3, 4, 5, 6, 7};
        auto first = playFirstSet(path, req);
        bool ok = first.size() == 8;
        for (int c = 0; c < 8 && ok; ++c)
            if (first[c] != 1000 + c)
                ok = false;
        check(ok, "8-channel file streams all 8 channels intact");
    }
} // namespace

int main()
{
    testSubsetOfWiderRecording();
    testRequestBeyondNarrowRecording();

    if (g_failures == 0)
        std::printf("\nALL TESTS PASSED\n");
    else
        std::printf("\n%d CHECK(S) FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
