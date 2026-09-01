// Standalone unit tests for the SerProt transport + ECG/ADC application layer.
//
// Build (from project root):
//   g++ -std=c++17 -Iinclude tests/test_transport.cpp \
//       src/core/transportprotocol.cpp src/core/applicationprotocol.cpp \
//       src/core/ecgadcprotocol.cpp -o /tmp/test_transport && /tmp/test_transport
//
// No Qt, no hardware. A LoopbackTransport captures written bytes so we can
// feed them back into the parser and assert round-trip behaviour.

#include "core/itransport.h"
#include "core/transportprotocol.h"
#include "core/ecgadcprotocol.h"

#include <cassert>
#include <cstdio>
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

    // Transport that records bytes written and lets tests inject bytes to read.
    class LoopbackTransport : public ITransport
    {
    public:
        std::vector<uint8_t> written;
        std::vector<uint8_t> toRead;
        size_t readPos = 0;

        int open() override { return 0; }
        int close() override { return 0; }
        bool isOpen() const override { return true; }

        int write(const uint8_t *buf, int length) override
        {
            for (int i = 0; i < length; ++i)
                written.push_back(buf[i]);
            return length;
        }

        int read(uint8_t *buf, int length) override
        {
            int n = 0;
            while (readPos < toRead.size() && n < length)
                buf[n++] = toRead[readPos++];
            return n;
        }
    };

    class ShortWriteTransport : public LoopbackTransport
    {
    public:
        int write(const uint8_t *buf, int length) override
        {
            const int partial = length > 0 ? length - 1 : 0;
            for (int i = 0; i < partial; ++i)
                written.push_back(buf[i]);
            return partial;
        }
    };

    // CRC-16/Modbus reference (same as production).
    uint16_t crc16(const std::vector<uint8_t> &d)
    {
        uint16_t crc = 0xFFFF;
        for (uint8_t b : d)
        {
            crc ^= b;
            for (int j = 0; j < 8; ++j)
                crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
        return crc;
    }

    // Encode a full SerProt frame (with escaping) for a given type/seq/payload.
    std::vector<uint8_t> encodeFrame(uint8_t type, uint8_t seq,
                                     const std::vector<uint8_t> &payload)
    {
        std::vector<uint8_t> crcRegion;
        crcRegion.push_back(type);
        crcRegion.push_back(seq);
        crcRegion.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
        crcRegion.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
        for (uint8_t b : payload)
            crcRegion.push_back(b);
        uint16_t crc = crc16(crcRegion);

        std::vector<uint8_t> frame;
        frame.push_back(0xAA);
        frame.push_back(type);
        frame.push_back(seq);
        frame.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
        frame.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
        auto esc = [&](uint8_t b)
        {
            if (b == 0xAA || b == 0xBB)
                frame.push_back(0xBB);
            frame.push_back(b);
        };
        for (uint8_t b : payload)
            esc(b);
        esc(static_cast<uint8_t>(crc & 0xFF));
        esc(static_cast<uint8_t>((crc >> 8) & 0xFF));
        return frame;
    }

    void testCommandEncoding()
    {
        std::printf("testCommandEncoding\n");
        LoopbackTransport t;
        TransportProtocol proto(&t);

        // SetVref = 1000 mV -> payload 0x10 0xE8 0x03
        uint8_t payload[3] = {0x10, 0xE8, 0x03};
        int seq = proto.sendCommand(payload, 3);
        check(seq == 0, "first seq is 0");

        // Expected: no escaping needed for these bytes.
        std::vector<uint8_t> expected = encodeFrame(0xCC, 0, {0x10, 0xE8, 0x03});
        check(t.written == expected, "SetVref frame matches reference encoding");
    }

    void testEscaping()
    {
        std::printf("testEscaping\n");
        LoopbackTransport t;
        TransportProtocol proto(&t);

        // Payload containing bytes that must be escaped.
        uint8_t payload[3] = {0xAA, 0xBB, 0x01};
        proto.sendCommand(payload, 3);
        std::vector<uint8_t> expected = encodeFrame(0xCC, 0, {0xAA, 0xBB, 0x01});
        check(t.written == expected, "payload with 0xAA/0xBB is escaped correctly");
    }

    void testResponseDispatch()
    {
        std::printf("testResponseDispatch\n");
        LoopbackTransport t;
        TransportProtocol proto(&t);

        bool called = false;
        bool gotOk = false;
        uint8_t p = 0x02; // StopStream
        int seq = proto.sendCommand(&p, 1, [&](bool ok, const uint8_t *, int)
                                    {
            called = true;
            gotOk = ok; });

        // Device replies with empty Response for that seq.
        std::vector<uint8_t> resp = encodeFrame(0xDD, static_cast<uint8_t>(seq), {});
        proto.feed(resp.data(), static_cast<int>(resp.size()));

        check(called, "response handler invoked");
        check(gotOk, "response reported ok");
    }

    void testErrorDispatch()
    {
        std::printf("testErrorDispatch\n");
        LoopbackTransport t;
        TransportProtocol proto(&t);

        bool gotOk = true;
        int err = -1;
        uint8_t p = 0x11;
        int seq = proto.sendCommand(&p, 1, [&](bool ok, const uint8_t *pl, int len)
                                    {
            gotOk = ok;
            if (!ok && len >= 1) err = pl[0]; });

        std::vector<uint8_t> resp = encodeFrame(0xEE, static_cast<uint8_t>(seq), {0x02});
        proto.feed(resp.data(), static_cast<int>(resp.size()));

        check(!gotOk, "error reported not ok");
        check(err == 0x02, "error code 0x02 decoded");
    }

    void testCrcRejection()
    {
        std::printf("testCrcRejection\n");
        LoopbackTransport t;
        TransportProtocol proto(&t);

        bool called = false;
        uint8_t p = 0x02;
        int seq = proto.sendCommand(&p, 1, [&](bool, const uint8_t *, int)
                                    { called = true; });

        std::vector<uint8_t> resp = encodeFrame(0xDD, static_cast<uint8_t>(seq), {});
        resp[resp.size() - 1] ^= 0xFF; // corrupt CRC high byte
        proto.feed(resp.data(), static_cast<int>(resp.size()));
        check(!called, "corrupted CRC frame is dropped");
    }

    void testCrcDiagnostic()
    {
        std::printf("testCrcDiagnostic\n");
        LoopbackTransport t;
        TransportProtocol proto(&t);
        bool crcReported = false;
        proto.setParseErrorHandler([&](TransportProtocol::ParseError error)
                                    { crcReported = error == TransportProtocol::ParseError::CrcMismatch; });

        uint8_t p = 0x02;
        const int seq = proto.sendCommand(&p, 1);
        auto resp = encodeFrame(0xDD, static_cast<uint8_t>(seq), {});
        resp.back() ^= 0xFF;
        proto.feed(resp.data(), static_cast<int>(resp.size()));
        check(crcReported, "CRC mismatch is reported to diagnostics callback");
    }

    void testShortWriteRejected()
    {
        std::printf("testShortWriteRejected\n");
        ShortWriteTransport t;
        TransportProtocol proto(&t);
        uint8_t p = 0x20;
        check(proto.sendCommand(&p, 1) < 0, "partial command write is rejected");
    }

    void testResync()
    {
        std::printf("testResync\n");
        LoopbackTransport t;
        TransportProtocol proto(&t);

        bool called = false;
        uint8_t p = 0x02;
        int seq = proto.sendCommand(&p, 1, [&](bool ok, const uint8_t *, int)
                                    { called = ok; });

        // Junk bytes then a valid frame.
        std::vector<uint8_t> stream = {0x12, 0x34, 0x56};
        std::vector<uint8_t> good = encodeFrame(0xDD, static_cast<uint8_t>(seq), {});
        stream.insert(stream.end(), good.begin(), good.end());
        proto.feed(stream.data(), static_cast<int>(stream.size()));
        check(called, "parser resyncs after junk and dispatches valid frame");
    }

    void testPushDecode()
    {
        std::printf("testPushDecode\n");
        LoopbackTransport t;
        TransportProtocol proto(&t);
        EcgAdcProtocol app(&proto);

        // Simulate a successful StartStream for channels {0, 2}.
        std::vector<uint8_t> chans = {0, 2};
        int seq = app.startStream(chans);
        std::vector<uint8_t> ack = encodeFrame(0xDD, static_cast<uint8_t>(seq), {});
        proto.feed(ack.data(), static_cast<int>(ack.size()));
        check(app.activeChannels() == chans, "active channels set after StartStream ack");

        // Push: 1 sample of 2 channels. ch0 = 10000 µV, ch2 = 20000 µV.
        // 10000 = 0x2710 -> 0x10 0x27 0x00 ; 20000 = 0x4E20 -> 0x20 0x4E 0x00
        EcgAdcProtocol::SampleFrame got;
        bool gotFrame = false;
        app.setSampleHandler([&](const EcgAdcProtocol::SampleFrame &f)
                             {
            got = f;
            gotFrame = true; });

        std::vector<uint8_t> pushPayload = {0x10, 0x27, 0x00, 0x20, 0x4E, 0x00};
        std::vector<uint8_t> push = encodeFrame(0xFF, 7, pushPayload);
        proto.feed(push.data(), static_cast<int>(push.size()));

        check(gotFrame, "push handler invoked");
        check(got.samples.size() == 2, "two channels decoded");
        check(got.samples[0].size() == 1 && got.samples[0][0] == 10000, "ch0 = 10000 µV");
        check(got.samples[1].size() == 1 && got.samples[1][0] == 20000, "ch2 = 20000 µV");
    }

    void testNegativeSampleDecode()
    {
        std::printf("testNegativeSampleDecode\n");
        // -1 in 24-bit two's complement = 0xFFFFFF -> bytes 0xFF 0xFF 0xFF
        uint8_t neg[3] = {0xFF, 0xFF, 0xFF};
        check(EcgAdcProtocol::decodeSample(neg) == -1, "0xFFFFFF decodes to -1");
        uint8_t minv[3] = {0x00, 0x00, 0x80}; // 0x800000 = -8388608
        check(EcgAdcProtocol::decodeSample(minv) == -8388608, "0x800000 decodes to min");
    }

    void testControllerCapabilities()
    {
        std::printf("testControllerCapabilities\n");
        check(EcgAdcProtocol::kFixedReferenceVoltageMv == 2400,
              "ADS1298 reference is fixed at 2.4 V");
        check(EcgAdcProtocol::kSampleRateHz[EcgAdcProtocol::kDefaultSampleRateIndex] == 1000,
              "controller default sample rate is 1000 Hz");
        check(EcgAdcProtocol::kGainValues[0] == 6 && EcgAdcProtocol::kGainValues[6] == 12,
              "gain code mapping matches ADS1298");

        LoopbackTransport t;
        TransportProtocol proto(&t);
        EcgAdcProtocol app(&proto);
        check(app.setTestSignal(1, 2) < 0, "test frequency code 2 is rejected");
        check(app.setTestSignal(1, 3) < 0, "constant test signal frequency code 3 is rejected");
        check(app.setTestSignal(2, 0) < 0, "test amplitude above 1 is rejected");
        check(app.setGain(8, 0) < 0, "gain channel 8 is rejected");
        check(app.setGain(0, 7) < 0, "gain code 7 is rejected");
        check(app.setSamplerate(4) < 0, "sample-rate index 4 is rejected");

        // The three global input modes have distinct, parameter-free/typed
        // payloads in the updated application protocol.
        LoopbackTransport modeTransport;
        TransportProtocol modeProto(&modeTransport);
        EcgAdcProtocol modeApp(&modeProto);
        check(modeApp.setShortInput(true) == 0, "InputShort command is accepted");
        check(modeTransport.written == encodeFrame(0xCC, 0, {0x31}),
              "InputShort payload has no enable parameter");
        modeTransport.written.clear();
        check(modeApp.setShortInput(false) == 1, "setShortInput(false) selects NormalInput");
        check(modeTransport.written == encodeFrame(0xCC, 1, {0x33}),
              "NormalInput uses command 0x33");
        modeTransport.written.clear();
        check(modeApp.setTestSignal(0, 1) == 2, "TestSignal amplitude 0 is accepted");
        check(modeTransport.written == encodeFrame(0xCC, 2, {0x32, 0x00, 0x01}),
              "TestSignal payload encodes amplitude and frequency");
    }
} // namespace

int main()
{
    testCommandEncoding();
    testEscaping();
    testResponseDispatch();
    testErrorDispatch();
    testCrcRejection();
    testCrcDiagnostic();
    testShortWriteRejected();
    testResync();
    testPushDecode();
    testNegativeSampleDecode();
    testControllerCapabilities();

    if (g_failures == 0)
        std::printf("\nALL TESTS PASSED\n");
    else
        std::printf("\n%d CHECK(S) FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
