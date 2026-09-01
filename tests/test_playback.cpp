// End-to-end test: record a .bsig session, then play it back through a
// PhantomDevice driving the real DeviceSession stack, and verify samples
// round-trip intact.
//
// Build (from project root):
//   g++ -std=c++17 -Iinclude tests/test_playback.cpp \
//       src/core/transportprotocol.cpp src/core/applicationprotocol.cpp \
//       src/core/ecgadcprotocol.cpp src/core/samplebuffer.cpp \
//       src/core/sessionrecord.cpp src/core/phantomdevice.cpp \
//       src/core/devicesession.cpp \
//       -o /tmp/test_playback && /tmp/test_playback

#include "core/sessionrecord.h"
#include "core/phantomdevice.h"
#include "core/devicesession.h"
#include "core/ecgadcprotocol.h"

#include <cassert>
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

    const char *kPath = "/tmp/test_session.bsig";

    // Write a recording with 2 channels, 500 samples each, known waveforms.
    void writeRecording()
    {
        SessionHeader h;
        h.version = 1;
        h.sampleRate = 250;
        h.vrefMv = 3300;
        h.description = "test";
        h.channels = {
            ChannelInfo{0, true, "ch0"},
            ChannelInfo{2, true, "ch2"},
        };

        SessionWriter w;
        bool ok = w.open(kPath, h);
        check(ok, "writer opens");

        for (int i = 0; i < 500; ++i)
        {
            std::vector<int32_t> set = {i, 1000 + i * 2};
            w.writeSampleSet(set);
        }
        w.close();
        check(w.sampleCount() == 500, "500 sample sets written");
    }

    void testHeaderRoundTrip()
    {
        std::printf("testHeaderRoundTrip\n");
        SessionReader r;
        check(r.open(kPath), "reader opens");
        check(r.header().sampleRate == 250, "sample rate preserved");
        check(r.header().vrefMv == 3300, "vref preserved");
        check(r.header().channels.size() == 2, "2 channels");
        check(r.header().channels[1].physIndex == 2, "ch1 physIndex == 2");
        check(r.sampleCount() == 500, "500 samples in header");

        std::vector<int32_t> set;
        check(r.readSampleSet(10, set), "read sample 10");
        check(set.size() == 2 && set[0] == 10 && set[1] == 1020, "sample 10 values");
    }

    void testPlaybackThroughPhantom()
    {
        std::printf("testPlaybackThroughPhantom\n");

        auto phantom = new PhantomDevice();
        check(phantom->loadRecording(kPath), "phantom loads recording");

        DeviceSession session(phantom, /*ownTransport=*/true);
        session.setBufferCapacity(2000);
        check(session.open(), "session opens");

        // Collect decoded frames.
        std::vector<int32_t> ch0, ch2;
        session.setSampleCallback([&](const EcgAdcProtocol::SampleFrame &f)
                                  {
            for (int32_t v : f.samples[0]) ch0.push_back(v);
            for (int32_t v : f.samples[1]) ch2.push_back(v); });

        // Master starts streaming channels {0,2}.
        bool started = false;
        session.startStream({0, 2}, [&](bool ok, ApplicationProtocol::Error)
                            { started = ok; });
        // Phantom processes the command synchronously on write(); pull the ack.
        session.poll();
        check(started, "StartStream acked by phantom");

        // Drive playback: 500 samples @ 250 Hz = 2000 ms. Pump in 100 ms steps.
        for (int step = 0; step < 25; ++step)
        {
            phantom->pump(100.0);
            session.poll();
        }

        check(ch0.size() == 500, "all 500 ch0 samples played back");
        check(ch2.size() == 500, "all 500 ch2 samples played back");

        bool valuesOk = ch0.size() == 500 && ch2.size() == 500;
        if (valuesOk)
        {
            for (int i = 0; i < 500 && valuesOk; ++i)
            {
                if (ch0[i] != i || ch2[i] != 1000 + i * 2)
                    valuesOk = false;
            }
        }
        check(valuesOk, "played-back sample values match recording");

        // Buffer should hold the latest samples too.
        std::vector<int32_t> latest;
        size_t n = session.buffer().readLatest(0, 10, latest);
        check(n == 10 && latest.back() == 499, "buffer latest sample is 499");
    }

    void testSeek()
    {
        std::printf("testSeek\n");
        auto phantom = new PhantomDevice();
        phantom->loadRecording(kPath);
        DeviceSession session(phantom, true);
        session.open();

        std::vector<int32_t> ch0;
        session.setSampleCallback([&](const EcgAdcProtocol::SampleFrame &f)
                                  { for (int32_t v : f.samples[0]) ch0.push_back(v); });

        session.startStream({0, 2});
        session.poll();

        phantom->seekToSample(250);
        // Play remaining 250 samples = 1000 ms.
        for (int step = 0; step < 12; ++step)
        {
            phantom->pump(100.0);
            session.poll();
        }
        check(!ch0.empty() && ch0.front() == 250, "seek to 250 starts at sample 250");
    }
} // namespace

int main()
{
    writeRecording();
    testHeaderRoundTrip();
    testPlaybackThroughPhantom();
    testSeek();

    if (g_failures == 0)
        std::printf("\nALL TESTS PASSED\n");
    else
        std::printf("\n%d CHECK(S) FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
