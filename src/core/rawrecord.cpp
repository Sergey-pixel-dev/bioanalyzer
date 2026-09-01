#include "core/rawrecord.h"
#include <cstring>

namespace
{
    template <class T>
    void w(std::ostream &s, T v)
    {
        uint8_t b[sizeof(T)];
        for (size_t i = 0; i < sizeof(T); ++i)
            b[i] = uint8_t((uint64_t(v) >> (8 * i)) & 255);
        s.write((char *)b, sizeof b);
    }
    template <class T>
    bool r(std::istream &s, T &v)
    {
        uint8_t b[sizeof(T)];
        if (!s.read((char *)b, sizeof b))
            return false;
        v = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            v |= T(uint64_t(b[i]) << (8 * i));
        return true;
    }
}

bool RawSessionWriter::open(const std::string &path, const RawSessionHeader &h)
{
    m_file.open(path, std::ios::binary | std::ios::trunc);
    if (!m_file)
        return false;
    m_channels = h.channels.size();
    m_count = 0;
    m_file.write("BSG2", 4);
    w<uint16_t>(m_file, h.version);
    w<uint16_t>(m_file, h.deviceId);
    w<uint8_t>(m_file, h.fwMajor);
    w<uint8_t>(m_file, h.fwMinor);
    w<uint32_t>(m_file, h.sampleRate);
    w<uint16_t>(m_file, h.pushLength);
    w<uint16_t>(m_file, h.vrefMv);
    w<uint32_t>(m_file, uint32_t(h.description.size()));
    m_file.write(h.description.data(), h.description.size());
    w<uint16_t>(m_file, uint16_t(m_channels));
    for (const auto &c : h.channels)
    {
        w<uint8_t>(m_file, c.physical);
        w<uint16_t>(m_file, uint16_t(c.label.size()));
        m_file.write(c.label.data(), c.label.size());
    }
    m_countPos = m_file.tellp();
    w<uint64_t>(m_file, 0);
    return true;
}
void RawSessionWriter::writeBlock(const std::vector<std::vector<int32_t>> &s)
{
    if (!m_file || s.size() != m_channels || !m_channels)
        return;
    size_t n = s[0].size();
    for (size_t c = 1; c < m_channels; ++c)
        if (s[c].size() != n)
            return;
    for (size_t i = 0; i < n; ++i)
        for (size_t c = 0; c < m_channels; ++c)
            w<uint32_t>(m_file, uint32_t(s[c][i]));
    m_count += n;
}
void RawSessionWriter::close()
{
    if (!m_file)
        return;
    m_file.seekp(m_countPos);
    w<uint64_t>(m_file, m_count);
    m_file.close();
}

bool RawSessionReader::open(const std::string &path)
{
    m_file.open(path, std::ios::binary);
    if (!m_file)
        return false;
    char magic[4];
    if (!m_file.read(magic, 4) || std::memcmp(magic, "BSG2", 4))
    {
        m_file.close();
        return false;
    }
    uint16_t n = 0;
    if (!r(m_file, m_header.version) || !r(m_file, m_header.deviceId) || !r(m_file, m_header.fwMajor) || !r(m_file, m_header.fwMinor) || !r(m_file, m_header.sampleRate) || !r(m_file, m_header.pushLength) || !r(m_file, m_header.vrefMv))
    {
        m_file.close();
        return false;
    }
    uint32_t dl = 0;
    if (!r(m_file, dl))
    {
        m_file.close();
        return false;
    }
    m_header.description.resize(dl);
    if (dl && !m_file.read(m_header.description.data(), dl))
    {
        m_file.close();
        return false;
    }
    if (!r(m_file, n))
    {
        m_file.close();
        return false;
    }
    m_header.channels.clear();
    for (uint16_t i = 0; i < n; ++i)
    {
        RawChannel c;
        uint16_t ll = 0;
        if (!r(m_file, c.physical) || !r(m_file, ll))
        {
            m_file.close();
            return false;
        }
        c.label.resize(ll);
        if (ll && !m_file.read(c.label.data(), ll))
        {
            m_file.close();
            return false;
        }
        m_header.channels.push_back(std::move(c));
    }
    if (!r(m_file, m_header.sampleCount))
    {
        m_file.close();
        return false;
    }
    m_dataStart = m_file.tellg();
    return true;
}
bool RawSessionReader::readSampleSet(uint64_t i, std::vector<int32_t> &out)
{
    if (!m_file || i >= m_header.sampleCount || m_header.channels.empty())
        return false;
    m_file.seekg(m_dataStart + std::streamoff(i * m_header.channels.size() * 4));
    out.resize(m_header.channels.size());
    for (auto &v : out)
    {
        uint32_t u;
        if (!r(m_file, u))
            return false;
        v = int32_t(u);
    }
    return true;
}
