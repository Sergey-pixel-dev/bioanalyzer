#include "core/sessionrecord.h"

#include <cstring>
#include <algorithm>

namespace
{
    template <typename T>
    void writeLE(std::ostream &os, T value)
    {
        uint8_t buf[sizeof(T)];
        for (size_t i = 0; i < sizeof(T); ++i)
            buf[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
        os.write(reinterpret_cast<const char *>(buf), sizeof(T));
    }

    template <typename T>
    bool readLE(std::istream &is, T &value)
    {
        uint8_t buf[sizeof(T)];
        if (!is.read(reinterpret_cast<char *>(buf), sizeof(T)))
            return false;
        value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            value |= static_cast<T>(buf[i]) << (8 * i);
        return true;
    }

    void writeI32(std::ostream &os, int32_t v)
    {
        writeLE<uint32_t>(os, static_cast<uint32_t>(v));
    }

    bool readI32(std::istream &is, int32_t &v)
    {
        uint32_t u = 0;
        if (!readLE<uint32_t>(is, u))
            return false;
        v = static_cast<int32_t>(u);
        return true;
    }
} // namespace

// ---------------- SessionWriter ----------------

SessionWriter::~SessionWriter()
{
    close();
}

bool SessionWriter::open(const std::string &path, const SessionHeader &header)
{
    if (header.version != 2 || header.channels.empty() || header.channels.size() > 8)
        return false;
    m_file.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!m_file.is_open())
        return false;

    m_channels = static_cast<int>(header.channels.size());
    m_physicalChannels.clear();
    m_physicalChannels.reserve(header.channels.size());
    for (const ChannelInfo &channel : header.channels)
        m_physicalChannels.push_back(channel.physIndex);
    m_sampleCount = 0;

    m_file.write("BSIG", 4);
    writeLE<uint16_t>(m_file, header.version);
    writeLE<uint16_t>(m_file, static_cast<uint16_t>(m_channels));
    writeLE<uint32_t>(m_file, header.sampleRate);
    writeLE<uint16_t>(m_file, header.vrefMv);
    writeLE<uint16_t>(m_file, 0); // reserved

    writeLE<uint32_t>(m_file, static_cast<uint32_t>(header.description.size()));
    m_file.write(header.description.data(), static_cast<std::streamsize>(header.description.size()));

    for (const ChannelInfo &ch : header.channels)
    {
        writeLE<uint8_t>(m_file, ch.physIndex);
        writeLE<uint8_t>(m_file, ch.enabled ? 1 : 0);
        writeLE<uint16_t>(m_file, static_cast<uint16_t>(ch.label.size()));
        m_file.write(ch.label.data(), static_cast<std::streamsize>(ch.label.size()));
    }

    // Remember where sampleCount lives so we can patch it on close().
    m_countPos = m_file.tellp();
    writeLE<uint64_t>(m_file, 0); // placeholder
    return true;
}

void SessionWriter::writeSampleSet(const std::vector<int32_t> &values)
{
    if (!m_file.is_open() || static_cast<int>(values.size()) != m_channels)
        return;
    for (int c = 0; c < m_channels; ++c)
        writeI32(m_file, values[c]);
    ++m_sampleCount;
}

void SessionWriter::writeBlock(const std::vector<std::vector<int32_t>> &samples)
{
    writeBlock(m_physicalChannels, samples);
}

void SessionWriter::writeBlock(const std::vector<uint8_t> &channels,
                               const std::vector<std::vector<int32_t>> &samples)
{
    if (!m_file.is_open() || channels != m_physicalChannels ||
        static_cast<int>(samples.size()) != m_channels || m_channels == 0)
        return;
    const size_t n = samples[0].size();
    for (int c = 1; c < m_channels; ++c)
        if (samples[c].size() != n)
            return;

    for (size_t s = 0; s < n; ++s)
    {
        for (int c = 0; c < m_channels; ++c)
            writeI32(m_file, samples[c][s]);
        ++m_sampleCount;
    }
}

void SessionWriter::close()
{
    if (!m_file.is_open())
        return;
    // Patch sampleCount, then close.
    m_file.seekp(m_countPos);
    writeLE<uint64_t>(m_file, m_sampleCount);
    m_file.close();
}

// ---------------- SessionReader ----------------

bool SessionReader::open(const std::string &path)
{
    m_file.open(path, std::ios::binary | std::ios::in);
    if (!m_file.is_open())
        return false;

    char magic[4];
    if (!m_file.read(magic, 4) || std::memcmp(magic, "BSIG", 4) != 0)
    {
        m_file.close();
        return false;
    }

    uint16_t channels = 0;
    if (!readLE<uint16_t>(m_file, m_header.version) ||
        !readLE<uint16_t>(m_file, channels) ||
        !readLE<uint32_t>(m_file, m_header.sampleRate) ||
        !readLE<uint16_t>(m_file, m_header.vrefMv))
    {
        m_file.close();
        return false;
    }
    if (m_header.version != 2 || channels == 0 || channels > 8)
    {
        m_file.close();
        return false;
    }
    uint16_t reserved = 0;
    readLE<uint16_t>(m_file, reserved);

    uint32_t descLen = 0;
    if (!readLE<uint32_t>(m_file, descLen))
    {
        m_file.close();
        return false;
    }
    m_header.description.resize(descLen);
    if (descLen > 0 && !m_file.read(&m_header.description[0], descLen))
    {
        m_file.close();
        return false;
    }

    m_header.channels.clear();
    for (uint16_t i = 0; i < channels; ++i)
    {
        ChannelInfo ch;
        uint8_t phys = 0, en = 0;
        uint16_t labelLen = 0;
        if (!readLE<uint8_t>(m_file, phys) ||
            !readLE<uint8_t>(m_file, en) ||
            !readLE<uint16_t>(m_file, labelLen))
        {
            m_file.close();
            return false;
        }
        ch.physIndex = phys;
        ch.enabled = (en != 0);
        ch.label.resize(labelLen);
        if (labelLen > 0 && !m_file.read(&ch.label[0], labelLen))
        {
            m_file.close();
            return false;
        }
        m_header.channels.push_back(ch);
    }

    if (!readLE<uint64_t>(m_file, m_header.sampleCount))
    {
        m_file.close();
        return false;
    }

    m_dataStart = m_file.tellg();
    return true;
}

bool SessionReader::readSampleSet(uint64_t index, std::vector<int32_t> &out)
{
    const int channels = static_cast<int>(m_header.channels.size());
    if (!m_file.is_open() || index >= m_header.sampleCount || channels == 0)
        return false;

    const std::streamoff offset =
        static_cast<std::streamoff>(index) * channels * 4;
    m_file.seekg(m_dataStart + offset);

    out.resize(channels);
    for (int c = 0; c < channels; ++c)
        if (!readI32(m_file, out[c]))
            return false;
    return true;
}

size_t SessionReader::readSampleSets(uint64_t startIndex, size_t count, std::vector<int32_t> &out)
{
    const int channels = static_cast<int>(m_header.channels.size());
    out.clear();
    if (!m_file.is_open() || channels == 0 || startIndex >= m_header.sampleCount)
        return 0;

    const uint64_t available = m_header.sampleCount - startIndex;
    const size_t n = static_cast<size_t>(std::min<uint64_t>(count, available));

    const std::streamoff offset =
        static_cast<std::streamoff>(startIndex) * channels * 4;
    m_file.seekg(m_dataStart + offset);

    out.resize(n * channels);
    for (size_t i = 0; i < n * channels; ++i)
        if (!readI32(m_file, out[i]))
            return i / channels;
    return n;
}
