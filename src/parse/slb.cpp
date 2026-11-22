#include "parse/slb.h"

#include "core/paula.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace {

int be16(const QByteArray &d, int at)
{
    if (at < 0 || at + 2 > d.size())
        return -1;
    return (static_cast<quint8>(d.at(at)) << 8) | static_cast<quint8>(d.at(at + 1));
}

// The synth tables of the executables (data hunk of Curse, a4-20730 to a4-20524):
// periods of octave 5 and three sawtooth cycles.
const quint16 kPeriods[12] = {0x2F8, 0x2CD, 0x2A5, 0x27F, 0x25B, 0x239, 0x219, 0x1FB, 0x1DF, 0x1C4, 0x1AB, 0x193};
const quint8 kWave18[18] = {0x07, 0x16, 0x25, 0x34, 0x43, 0x52, 0x61, 0x70, 0x7F,
                            0x81, 0x90, 0x9F, 0xAE, 0xBD, 0xCC, 0xDB, 0xEA, 0xF9};

// the 54 and 108 byte cycles: a rising ramp from -127 to 127
std::vector<uint8_t> chipMemory()
{
    std::vector<uint8_t> mem(0x400, 0);
    std::copy(std::begin(kWave18), std::end(kWave18), mem.begin() + 0x100);
    static const quint8 wave54[54] = {
        0x81, 0x86, 0x8B, 0x8F, 0x94, 0x99, 0x9E, 0xA3, 0xA7, 0xAC, 0xB1, 0xB6, 0xBB, 0xBF, 0xC4, 0xC9, 0xCE, 0xD2,
        0xD7, 0xDC, 0xE1, 0xE6, 0xEA, 0xEF, 0xF4, 0xF9, 0xFE, 0x02, 0x07, 0x0C, 0x11, 0x16, 0x1A, 0x1F, 0x24, 0x29,
        0x2E, 0x32, 0x37, 0x3C, 0x41, 0x45, 0x4A, 0x4F, 0x54, 0x59, 0x5D, 0x62, 0x67, 0x6C, 0x71, 0x75, 0x7A, 0x7F};
    static const quint8 wave108[108] = {
        0x81, 0x83, 0x86, 0x88, 0x8A, 0x8D, 0x8F, 0x92, 0x94, 0x96, 0x99, 0x9B, 0x9D, 0xA0, 0xA2, 0xA5, 0xA7, 0xA9,
        0xAC, 0xAE, 0xB0, 0xB3, 0xB5, 0xB8, 0xBA, 0xBC, 0xBF, 0xC1, 0xC3, 0xC6, 0xC8, 0xCB, 0xCD, 0xCF, 0xD2, 0xD4,
        0xD6, 0xD9, 0xDB, 0xDE, 0xE0, 0xE2, 0xE5, 0xE7, 0xE9, 0xEC, 0xEE, 0xF1, 0xF3, 0xF5, 0xF8, 0xFA, 0xFC, 0xFF,
        0x01, 0x04, 0x06, 0x08, 0x0B, 0x0D, 0x0F, 0x12, 0x14, 0x17, 0x19, 0x1B, 0x1E, 0x20, 0x22, 0x25, 0x27, 0x2A,
        0x2C, 0x2E, 0x31, 0x33, 0x35, 0x38, 0x3A, 0x3D, 0x3F, 0x41, 0x44, 0x46, 0x48, 0x4B, 0x4D, 0x50, 0x52, 0x54,
        0x57, 0x59, 0x5B, 0x5E, 0x60, 0x63, 0x65, 0x67, 0x6A, 0x6C, 0x6E, 0x71, 0x73, 0x76, 0x78, 0x7A, 0x7D, 0x7F};
    std::copy(std::begin(wave54), std::end(wave54), mem.begin() + 0x200);
    std::copy(std::begin(wave108), std::end(wave108), mem.begin() + 0x300);
    return mem;
}

// Paula voice routines ($424b8, $425f8)
void voiceOff(PaulaMixer &paula, int voice)
{
    if (voice > 3)
        return;
    paula.write(0xA8 + voice * 16, 0);
    paula.write(0x96, 1u << voice);
}

void voiceOn(PaulaMixer &paula, int voice, int note, int volume)
{
    if (voice > 3)
        return;
    int period = kPeriods[note % 12];
    const int octave = note / 12 - 5;
    if (octave < 0)
        period <<= std::min(2, -octave);
    else if (octave > 0)
        period >>= std::min(2, octave);
    period &= 0xFFFF;
    if (period == 0 || volume <= 0) {
        voiceOff(paula, voice);
        return;
    }
    const uint32_t reg = 0xA0 + voice * 16;
    uint32_t address, words;
    if (period < 0x177) {
        address = 0x100;
        words = 9;
    } else if (period < 0x2EE) {
        address = 0x200;
        words = 0x1B;
        period = (period + 1) / 3;
    } else {
        address = 0x300;
        words = 0x36;
        period = (period + 3) / 6;
    }
    paula.write(reg + 0, address >> 16);
    paula.write(reg + 2, address & 0xFFFF);
    paula.write(reg + 4, words);
    paula.write(reg + 6, static_cast<uint32_t>(period));
    paula.write(reg + 8, static_cast<uint32_t>((volume >> 3) + 3));
    paula.write(0x96, 0x8000u | (1u << voice));
}

// One voice of the sequencer (14-byte records at a4-6768).
struct Voice
{
    qint64 time = 0;        // next event, 0 = the track ended
    int pos = 0;            // event index
    int tempo = 1;
    int fraction = 0;
    int volume = 0;
    int state = 3;          // 0 repeat gap, 1 sounding, 2 tied, 3 silent
};

} // namespace

int SlbSong::noteCount() const
{
    int n = 0;
    for (const SlbTrack &t : tracks)
        n += t.notes;
    return n;
}

bool SlbFile::looksLikeSlb(const QByteArray &data)
{
    return data.size() >= 14 && data.startsWith("SLBR");
}

int SlbFile::lengthOf(quint8 data)
{
    quint32 d = 0x6900u >> (data & 7);
    if (data & 8)
        d = ((d >> 1) * 3) & 0xFFFF;
    if (data & 0x30) {
        const quint32 n = (data & 0x30) >> 3;
        d = (d / (n + 1)) * n;
    }
    return static_cast<int>(d & 0xFFFF);
}

bool SlbFile::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        m_valid = false;
        return false;
    }
    return parse(file.readAll());
}

bool SlbFile::parse(const QByteArray &d)
{
    m_valid = false;
    m_songs.clear();
    m_size = static_cast<int>(d.size());
    if (!looksLikeSlb(d)) {
        m_error = QStringLiteral("missing SLBR signature");
        return false;
    }
    const int count = static_cast<quint8>(d.at(8));
    const int base = 10 + 2 * (count + 1);
    if (count == 0 || base > d.size()) {
        m_error = QStringLiteral("invalid song table");
        return false;
    }
    for (int s = 0; s < count; ++s) {
        SlbSong song;
        song.index = s;
        song.offset = base + be16(d, 10 + 2 * s);
        const int end = base + be16(d, 12 + 2 * s);
        if (song.offset + 14 > d.size() || end > d.size() || end < song.offset + 14) {
            m_error = QStringLiteral("song %1 is outside the file").arg(s + 1);
            return false;
        }
        song.tempo = be16(d, song.offset);
        song.volume = static_cast<quint8>(d.at(song.offset + 2));
        const int voices = std::min(5, static_cast<int>(static_cast<quint8>(d.at(song.offset + 3))));
        for (int v = 0; v < voices; ++v) {
            SlbTrack track;
            track.offset = song.offset + 14 + be16(d, song.offset + 4 + 2 * v);
            for (int at = track.offset; at + 2 <= end; at += 2) {
                SlbEvent e;
                e.offset = at;
                e.code = static_cast<quint8>(d.at(at));
                e.data = static_cast<quint8>(d.at(at + 1));
                track.events.append(e);
                if (e.isNote())
                    ++track.notes;
                if (e.isEnd())
                    break;
            }
            if (track.events.isEmpty() || !track.events.last().isEnd()) {
                SlbEvent e;
                e.offset = end;
                e.code = 0xFF;
                track.events.append(e);
            }
            song.tracks.append(track);
        }
        m_songs.append(song);
    }
    m_valid = true;
    return true;
}

AudioSample SlbFile::render(const SlbSong &song, const RenderOptions &options)
{
    AudioSample out;
    out.name = QStringLiteral("Song %1").arg(song.index + 1);
    out.sampleRate = options.sampleRate;
    out.channels = 2;
    out.bits = 16;
    const int factor = song.tempoFactor();
    if (song.tracks.isEmpty() || factor <= 0) {
        out.error = QStringLiteral("the song has no tracks or no tempo");
        return out;
    }

    const std::vector<uint8_t> mem = chipMemory();
    PaulaMixer paula(options.sampleRate);
    QVector<Voice> voices(song.tracks.size());

    // song start ($3aaee)
    const auto start = [&](qint64 now) {
        for (Voice &v : voices) {
            v = Voice();
            v.time = now;
            v.tempo = factor;
            v.volume = song.volume;
        }
    };
    qint64 now = 2;            // time in 5 ms units, the task wakes up every 10 ms
    start(now);

    const int framesPerTick = options.sampleRate / 100;
    const qint64 maxTicks = static_cast<qint64>(options.maxSeconds) * 100;
    const int fadeTicks = options.fadeSeconds * 100;
    qint64 loopTick = -1;
    QByteArray pcm;
    for (qint64 tick = 0; tick < maxTicks; ++tick, now += 2) {
        // sequencer ($3ac12)
        bool ended = false;
        for (int n = 0; n < voices.size(); ++n) {
            Voice &v = voices[n];
            const QVector<SlbEvent> &events = song.tracks.at(n).events;
            if (v.time == 0 || v.time > now)
                continue;
            if (v.state == 0) {                     // gap before a repeated note
                voiceOff(paula, n);
                v.state = 3;
                v.time += 1;
                continue;
            }
            // commands; Dark Queen of Krynn also skips notes and rests with
            // bit 7 of the length set (none in the other banks)
            while ((events.at(v.pos).code > 0x80 || (events.at(v.pos).data & 0x80)) && !events.at(v.pos).isEnd()) {
                const SlbEvent &c = events.at(v.pos);
                if (c.code == 0x81) {
                    if (v.state < 3)
                        voiceOff(paula, n);
                    v.state = 3;
                } else if (c.code == 0x84) {
                    v.volume = c.data;
                }
                ++v.pos;
            }
            const SlbEvent &e = events.at(v.pos);
            if (e.isEnd()) {
                v.time = 0;
                if (v.state < 3)
                    voiceOff(paula, n);
                v.state = 3;
                ended = true;
                continue;
            }
            const int length = lengthOf(e.data);
            v.time += length / v.tempo;
            v.fraction += length % v.tempo;
            if (v.fraction >= v.tempo) {
                v.time += 1;
                v.fraction -= v.tempo;
            }
            if (e.isRest()) {
                if (v.state < 3)
                    voiceOff(paula, n);
                v.state = 3;
            } else {
                if (v.state != 2) {
                    if (v.state < 3)
                        voiceOff(paula, n);
                    voiceOn(paula, n, e.code, v.volume);
                }
                if (e.data & 0x40) {
                    v.state = 2;
                } else if (v.pos + 1 < events.size() && events.at(v.pos + 1).code == e.code) {
                    v.time -= 1;
                    v.state = 0;
                } else {
                    v.state = 1;
                }
            }
            ++v.pos;
        }
        if (ended && std::all_of(voices.cbegin(), voices.cend(), [](const Voice &v) { return v.time == 0; })) {
            if (loopTick < 0)
                loopTick = tick;
            start(now);                             // the song starts again
        }

        const qsizetype before = pcm.size();
        paula.mix(mem, framesPerTick, pcm);
        if (loopTick >= 0) {
            const qint64 elapsed = tick - loopTick;
            if (elapsed >= fadeTicks)
                break;
            auto *p = reinterpret_cast<uchar *>(pcm.data() + before);
            const int count = static_cast<int>((pcm.size() - before) / 2);
            for (int i = 0; i < count; ++i) {
                const double g = 1.0 - (elapsed + static_cast<double>(i / 2) / framesPerTick) / fadeTicks;
                const qint16 s = qFromLittleEndian<qint16>(p + i * 2);
                qToLittleEndian<qint16>(static_cast<qint16>(std::lround(s * std::max(0.0, g))), p + i * 2);
            }
        }
    }
    out.pcm = pcm;
    out.frameCount = static_cast<int>(pcm.size() / 4);
    out.valid = out.frameCount > 0;
    return out;
}
