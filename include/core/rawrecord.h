#ifndef RAWRECORD_H
#define RAWRECORD_H

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

struct RawChannel
{
    uint8_t physical = 0;
    std::string label;
};

struct RawSessionHeader
{
    uint16_t version = 2;
    uint16_t deviceId = 0;
    uint8_t fwMajor = 0;
    uint8_t fwMinor = 0;
    uint32_t sampleRate = 1000;
    uint16_t pushLength = 24;
    uint16_t vrefMv = 2400;
    std::string description;
    std::vector<RawChannel> channels;
    uint64_t sampleCount = 0;
};

class RawSessionWriter
{
public:
    bool open(const std::string &path, const RawSessionHeader &header);
    void writeBlock(const std::vector<std::vector<int32_t>> &samples);
    void close();
    bool isOpen() const { return m_file.is_open(); }
    uint64_t sampleCount() const { return m_count; }
private:
    std::ofstream m_file;
    std::streampos m_countPos{};
    size_t m_channels = 0;
    uint64_t m_count = 0;
};

class RawSessionReader
{
public:
    bool open(const std::string &path);
    bool isOpen() const { return m_file.is_open(); }
    const RawSessionHeader &header() const { return m_header; }
    bool readSampleSet(uint64_t index, std::vector<int32_t> &out);
private:
    std::ifstream m_file;
    RawSessionHeader m_header;
    std::streampos m_dataStart{};
};

#endif
