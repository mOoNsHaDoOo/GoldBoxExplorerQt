#include "core/paula.h"

#include <QtEndian>

#include <cmath>

namespace {

constexpr double kPaulaClock = 3546895.0;   // PAL

} // namespace


PaulaMixer::PaulaMixer(int sampleRate)
    : m_rate(sampleRate)
{
}

void PaulaMixer::start(Channel &c)
{
    c.ptr = c.lc;
    c.left = static_cast<uint32_t>(c.len ? c.len : 0x10000) * 2;
    c.phase = 0;
}

void PaulaMixer::setDmacon(uint16_t v)
{
    const uint16_t old = m_dmacon;
    if (v & 0x8000)
        m_dmacon |= v & 0x7FFF;
    else
        m_dmacon &= ~v;
    for (int i = 0; i < 4; ++i) {
        const bool was = (old & 0x200) && (old & (1 << i));
        const bool now = (m_dmacon & 0x200) && (m_dmacon & (1 << i));
        if (now && !was) {
            m_ch[i].on = true;
            start(m_ch[i]);
        }
        if (!now)
            m_ch[i].on = false;
    }
}

void PaulaMixer::write(uint32_t reg, uint32_t v)
{
    if (reg == 0x96) {
        setDmacon(static_cast<uint16_t>(v));
        return;
    }
    if (reg < 0xA0 || reg >= 0xE0)
        return;
    Channel &c = m_ch[(reg - 0xA0) >> 4];
    switch (reg & 0xF) {
    case 0x0: c.lc = (c.lc & 0xFFFF) | (v << 16); break;
    case 0x2: c.lc = (c.lc & 0xFFFF0000u) | (v & 0xFFFE); break;
    case 0x4: c.len = static_cast<uint16_t>(v); break;
    case 0x6: c.per = static_cast<uint16_t>(v); break;
    case 0x8: c.vol = static_cast<uint16_t>(v & 0x7F); break;
    default: break;
    }
}

bool PaulaMixer::silent() const
{
    for (const Channel &c : m_ch)
        if (c.on && c.vol && c.per >= 64)
            return false;
    return true;
}

void PaulaMixer::mix(const std::vector<uint8_t> &mem, int frames, QByteArray &pcm, double gain)
{
    std::vector<int32_t> buf(static_cast<size_t>(frames) * 2, 0);
    const uint32_t memSize = static_cast<uint32_t>(mem.size());
    for (int i = 0; i < 4; ++i) {
        Channel &c = m_ch[i];
        if (!c.on || c.per < 64)
            continue;
        const double step = kPaulaClock / c.per / m_rate;
        const int vol = c.vol > 64 ? 64 : c.vol;
        int32_t *out = buf.data() + ((i == 0 || i == 3) ? 0 : 1);
        for (int s = 0; s < frames; ++s) {
            out[2 * s] += c.cur * vol;
            c.phase += step;
            while (c.phase >= 1.0) {
                c.phase -= 1.0;
                c.cur = c.ptr < memSize ? static_cast<int8_t>(mem[c.ptr]) : 0;
                ++c.ptr;
                if (--c.left == 0)
                    start(c);
            }
        }
    }
    const qsizetype at = pcm.size();
    pcm.resize(at + static_cast<qsizetype>(buf.size()) * 2);
    char *dst = pcm.data() + at;
    for (size_t i = 0; i < buf.size(); ++i) {
        // two channels per side, each at most 128 * 64
        const int v = qBound(-32768, static_cast<int>(std::lround(buf[i] * 2 * gain)), 32767);
        qToLittleEndian<qint16>(static_cast<qint16>(v), dst + i * 2);
    }
}
