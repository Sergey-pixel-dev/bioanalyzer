#ifndef SESSIONRECORD_H
#define SESSIONRECORD_H

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>

// .bsig — Biosignal session recording format (v1).
//
// A self-describing binary container for a monitoring session. It always
// stores the selected physical channels; the per-channel `enabled` flag keeps
// display/recording selection metadata alongside the samples.
//
// Layout (little-endian):
//
//   Header:
//     magic       4 B   "BSIG"
//     version     u16   = 1
//     channels    u16   number of channels stored
//     sampleRate  u32   Hz
//     vrefMv      u16   reference voltage in mV
//     reserved    u16
//     descLen     u32   length of UTF-8 description
//     description descLen bytes
//     Per channel (channels times):
//       physIndex u8    physical ADC channel index
//       enabled   u8    1 = real data, 0 = synthetic sine placeholder
//       labelLen  u16
//       label     labelLen bytes  (UTF-8)
//     sampleCount u64   number of sample sets (per channel)
//
//   Data:
//     sampleCount records, each = channels * int32 (µV), interleaved.
//
// Pure C++/POSIX: no Qt.

struct ChannelInfo
{
    uint8_t physIndex = 0;
    bool enabled = true;
    std::string label;
};

struct SessionHeader
{
    uint16_t version = 1;
    uint32_t sampleRate = 1000;
    // ADS1298 controller uses its internal, fixed 2.4 V reference.
    uint16_t vrefMv = 2400;
    std::string description;
    std::vector<ChannelInfo> channels;
    uint64_t sampleCount = 0; // filled by reader; recorder tracks internally
};

// Streaming writer: open, write header, append sample sets, finalize.
class SessionWriter
{
public:
    SessionWriter() = default;
    ~SessionWriter();

    bool open(const std::string &path, const SessionHeader &header);
    bool isOpen() const { return m_file.is_open(); }

    // Append one sample per channel (µV). size must equal channel count.
    void writeSampleSet(const std::vector<int32_t> &values);

    // Append a block: samples[c] holds N samples for channel c.
    void writeBlock(const std::vector<std::vector<int32_t>> &samples);

    // Patch the sample count into the header and close.
    void close();

    uint64_t sampleCount() const { return m_sampleCount; }

private:
    std::ofstream m_file;
    int m_channels = 0;
    uint64_t m_sampleCount = 0;
    std::streampos m_countPos = 0; // where sampleCount is stored, for patching
};

// Reader: loads header, then random-accesses sample sets.
class SessionReader
{
public:
    bool open(const std::string &path);
    bool isOpen() const { return m_file.is_open(); }

    const SessionHeader &header() const { return m_header; }
    uint64_t sampleCount() const { return m_header.sampleCount; }

    // Read sample set at index into `out` (channels values). Returns false if
    // index is out of range.
    bool readSampleSet(uint64_t index, std::vector<int32_t> &out);

    // Sequential read from current position; returns number of sample sets
    // read into `out` (out is resized to count*channels, interleaved).
    size_t readSampleSets(uint64_t startIndex, size_t count, std::vector<int32_t> &out);

private:
    std::ifstream m_file;
    SessionHeader m_header;
    std::streampos m_dataStart = 0;
};

#endif // SESSIONRECORD_H
