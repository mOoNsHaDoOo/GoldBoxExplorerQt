#include "parse/mxmusic.h"

#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

class Reader
{
public:
    explicit Reader(const QByteArray &d) : m_d(d) {}
    bool ok() const { return m_ok; }
    int pos() const { return m_pos; }
    quint16 w()
    {
        if (m_pos + 2 > m_d.size()) { m_ok = false; return 0; }
        const quint16 v = qFromBigEndian<quint16>(m_d.constData() + m_pos);
        m_pos += 2;
        return v;
    }
    quint32 l()
    {
        if (m_pos + 4 > m_d.size()) { m_ok = false; return 0; }
        const quint32 v = qFromBigEndian<quint32>(m_d.constData() + m_pos);
        m_pos += 4;
        return v;
    }
    QByteArray bytes(qint64 n)
    {
        if (n < 0 || m_pos + n > m_d.size()) { m_ok = false; return {}; }
        const QByteArray b = m_d.mid(m_pos, static_cast<int>(n));
        m_pos += static_cast<int>(n);
        return b;
    }
    void skip(int n) { bytes(n); }

private:
    const QByteArray &m_d;
    int m_pos = 0;
    bool m_ok = true;
};

// ------------------------------------------------------------ the driver

constexpr int kVoices = 4;
constexpr int kChannels = 16;
constexpr double kPalClock = 3546895.0;
constexpr double kNtscClock = 3579545.0;

struct Channel
{
    const MxInstrument *instrument = nullptr;
    int volume = 0x80;      // +13, controller 7 (value + 1)
    int bend = 0;           // +10, 1/256 semitone
    int bendRange = 0x18;   // +12
    bool rightPair = false; // +16 bit 0, controller 10
    bool sustainPedal = false;
    bool pendingPitch = false; // +16 bit 7
};

struct Voice
{
    int channel = -1;       // 0 = free
    int note = 0;
    int velocity = 0x80;    // +38
    int state = 0;          // +49: 6/5 attack, 4 sustain, 3/2 release, 1 stop
    bool held = false;      // sustain pedal held the note off
    int level = 0;          // +40, 0..$8000
    qint64 segmentLeft = 0; // +20, ms << 8
    qint64 rate = 0;        // +28, level per frame
    int segmentsLeft = 0;   // +36
    int segment = 0;
    int period = 0;         // +42
    int sampleIndex = -1;
    int octaveShift = 0;    // +32 / $10000
    const MxSample *sample = nullptr;
    // Paula
    double position = 0.0;
    bool inLoop = false;
    bool playing = false;
    int paulaVolume = 0;    // 0..64
    int noteOffAt = -1;     // scheduled release (fixed point ticks << 8)
    qint64 noteOffLeft = 0;
};

// $524a8: period = 2^(value / $10000), as the driver computes it: a table
// of 2^(i/256) for the high byte of the fraction (257 words,
// round(65536 * (2^(i/256) - 1))), linear interpolation on the low byte,
// the implicit leading 1, a right shift by 31 - exponent and rounding with
// the last bit shifted out.
int periodFor(qint64 logValue)
{
    if (logValue < 0x6f73d)
        return 0; // too high for Paula: the driver leaves the voice silent
    static const auto table = [] {
        std::array<quint16, 257> t{};
        for (int i = 0; i < 257; ++i)
            t[i] = static_cast<quint16>(std::lround(65536.0 * (std::pow(2.0, i / 256.0) - 1.0)) & 0xFFFF);
        return t;
    }();
    const quint32 x = static_cast<quint32>(logValue);
    const quint32 exponent = x >> 16;
    if (exponent >= 32)
        return 0;
    const quint32 fraction = x & 0xFFFF;
    const quint32 index = fraction >> 8;
    const quint16 t0 = table[index];
    const quint32 step = static_cast<quint16>(table[index + 1] - t0);
    const quint32 weight = static_cast<quint16>((fraction << 8) + 0x80);
    const quint32 mantissa = static_cast<quint16>(t0 + ((step * weight) >> 16));
    const quint32 value = 0x80000000u | (mantissa << 15);
    const quint32 shift = 31 - exponent;
    if (shift == 0)
        return static_cast<int>(value);
    const quint32 result = (value >> shift) + ((value >> (shift - 1)) & 1);
    return static_cast<int>(result);
}

class Player
{
public:
    Player(const MxMusicFile &file, const MxSong &song, const MxMusicFile::RenderOptions &o)
        : m_file(file), m_song(song), m_options(o)
    {
        m_vblank = o.pal ? 50 : 60;
        m_frameMs8 = 0x3e800 / m_vblank;            // 16(a4): ms << 8 per frame
        m_clock = o.pal ? kPalClock : kNtscClock;
        for (int c = 0; c < kChannels; ++c) {
            m_channels[c].instrument = file.instrument(c);
            m_channels[c].rightPair = c & 1;   // $521e4: odd channels on the right pair
        }
        // $532c6: play starts at the file's tempo (the word after "MXTX");
        // the first frame advances with it, before the song's own tempo event
        setTempo((file.flags() > 0 ? file.flags() : song.initialTempo) << 4);
    }

    AudioSample run()
    {
        AudioSample out;
        out.sampleRate = m_options.sampleRate;
        out.channels = 2;
        out.bits = 16;
        const double samplesPerFrame = static_cast<double>(m_options.sampleRate) / m_vblank;
        double carry = 0.0;
        const int maxFrames = m_options.maxSeconds * m_vblank;
        int tailFrames = -1;
        QByteArray pcm;
        for (int frame = 0; frame < maxFrames; ++frame) {
            // after the end of the song ($FF) the driver reads no more events
            // but the pending note offs and the envelopes go on
            interrupt();
            if (m_ended) {
                if (tailFrames < 0)
                    tailFrames = m_options.releaseSeconds * m_vblank;
                const bool silent = std::none_of(std::begin(m_voices), std::end(m_voices),
                                                 [](const Voice &v) { return v.playing && v.paulaVolume > 0; });
                if (silent || --tailFrames <= 0)
                    break;
            }
            carry += samplesPerFrame;
            const int n = static_cast<int>(carry);
            carry -= n;
            mix(pcm, n);
        }
        out.pcm = pcm;
        out.frameCount = pcm.size() / 4;
        out.valid = out.frameCount > 0;
        return out;
    }

    QVector<QVector<MxMusicFile::VoiceFrame>> trace(int frames)
    {
        QVector<QVector<MxMusicFile::VoiceFrame>> result;
        const double samplesPerFrame = static_cast<double>(m_options.sampleRate) / m_vblank;
        double carry = 0.0;
        QByteArray pcm;
        for (int frame = 0; frame < frames; ++frame) {
            interrupt();
            carry += samplesPerFrame;
            const int n = static_cast<int>(carry);
            carry -= n;
            pcm.clear();
            mix(pcm, n);
            QVector<MxMusicFile::VoiceFrame> row(kVoices);
            for (int v = 0; v < kVoices; ++v) {
                const Voice &voice = m_voices[v];
                if (voice.playing && voice.sample) {
                    row[v].period = voice.period;
                    row[v].volume = voice.paulaVolume;
                    row[v].cycles = voice.inLoop ? 0 : 1;
                }
            }
            result.append(row);
        }
        return result;
    }

private:
    // $52e04: tempo in BPM * 16.
    void setTempo(int tempo16)
    {
        m_tempo16 = tempo16;
        m_increment = (static_cast<qint64>(tempo16 >> 4) * 0xC000) / (m_vblank * 60);
    }

    // $52e84: one vertical blank.
    void interrupt()
    {
        m_position += m_increment;
        const qint64 now = m_position >> 8;

        // scheduled note offs, one per voice
        for (int v = 0; v < kVoices; ++v) {
            Voice &voice = m_voices[v];
            if (voice.noteOffAt < 0)
                continue;
            voice.noteOffLeft -= m_increment;
            if (voice.noteOffLeft > 0)
                continue;
            noteOff(voice.noteOffAt >> 8 & 0xFF, voice.noteOffAt & 0xFF, v);
            voice.noteOffAt = -1;
        }

        // events due now (none after the end, $53072 clears "playing")
        while (!m_ended && m_next < m_song.events.size()) {
            const MxEvent &e = m_song.events.at(m_next);
            const qint64 at = m_eventTime + e.delta;
            if (at > now)
                break;
            m_eventTime = at;
            ++m_next;
            if (e.status < 0x80) {
                const int velocity = (e.data >> 1) & 0x78;
                const int channel = e.data & 0x0F;
                if (velocity == 0)
                    continue;
                const int v = noteOn(channel, e.status, velocity);
                if (v >= 0) {
                    m_voices[v].noteOffAt = (e.status << 8) | channel;
                    // Dark Queen ($52fee) subtracts the lateness, Secret and Pools do not
                    const qint64 late = m_file.driver() == MxMusicFile::DarkQueen ? at - now : 0;
                    m_voices[v].noteOffLeft = (static_cast<qint64>(e.word) + late) << 8;
                }
            } else if (e.status == 0x80) {
                // $53008: cmp.w + bmi, a signed word comparison: ramps of
                // $8000 ticks or more start at once (pod.mx has 64000)
                const quint16 diff = static_cast<quint16>((m_increment >> 8) - e.word);
                if (!(diff & 0x8000)) {
                    setTempo(e.data << 4);
                    m_rampLength = 0;
                } else {
                    m_rampFrom = m_tempo16;
                    m_rampDelta = (e.data << 4) - m_tempo16;
                    m_rampLength = static_cast<qint64>(e.word) << 8;
                    m_rampDone = 0;
                }
            } else if (e.status == 0xFF) {
                m_ended = true;
                break;
            } else if (e.status == 0xB0) {
                controller(e.data & 0x0F, e.b4, e.b5);
            } else if (e.status == 0xC0) {
                m_channels[e.data & 0x0F].instrument = m_file.instrument(e.b5 & 0x3F);
            } else if (e.status == 0xE0) {
                Channel &c = m_channels[e.data & 0x0F];
                const int bend = ((e.b4 & 0x7F) << 7 | (e.b5 & 0x7F)) - 0x2000;
                c.bend = bend * (c.bendRange << 8) / 0x2000;
                c.pendingPitch = true;
            } else if (e.status == 0xA0) {
                loopCommand(e);
            }
        }

        // tempo ramp
        // $531b4: tempo ramp, with the driver's arithmetic: the delta is
        // read back as an unsigned word, multiplied by the elapsed ticks in 32
        // bits ($52484, overflowing on long ramps), divided with sign by the
        // ramp length ($52408) and added to the start tempo as a word.
        if (m_rampLength > 0) {
            m_rampDone = static_cast<quint32>(m_rampDone + m_increment);
            if (m_rampDone >= m_rampLength) {
                setTempo(static_cast<quint16>(m_rampFrom + m_rampDelta));
                m_rampLength = 0;
            } else {
                const quint32 product = static_cast<quint32>(static_cast<quint16>(m_rampDelta))
                                        * static_cast<quint32>(m_rampDone);
                const qint32 numerator = static_cast<qint32>(product);
                const qint32 step = numerator / static_cast<qint32>(m_rampLength);
                setTempo(static_cast<quint16>(step + m_rampFrom));
            }
        }
        updateVoices();
    }

    void loopCommand(const MxEvent &e)
    {
        if (e.b4 == 2 && m_loopDepth < 4) {
            m_loopCount[m_loopDepth] = e.b5;
            m_loopStart[m_loopDepth] = m_next;
            ++m_loopDepth;
        } else if (e.b4 == 3 && m_loopDepth > 0) {
            const int d = m_loopDepth - 1;
            if (m_loopCount[d] > 0) {
                --m_loopCount[d];
                m_next = m_loopStart[d];
                m_eventTime = m_position >> 8;
            } else {
                m_loopDepth = d;
            }
        }
    }

    // $5222a (the controllers the files use, plus the common ones).
    void controller(int channel, int number, int value)
    {
        Channel &c = m_channels[channel];
        switch (number) {
        case 6:
            c.bendRange = qMin(value, 0x18);
            break;
        case 7:
            c.volume = value ? value + 1 : 0;
            break;
        case 10:
            // $522c8: centre goes back to the channel's own side
            if (value < 0x40) c.rightPair = true;
            else if (value > 0x40) c.rightPair = false;
            else c.rightPair = channel & 1;
            break;
        case 0x40:
            c.sustainPedal = value & 0x40;
            if (!c.sustainPedal)
                for (Voice &v : m_voices)
                    if (v.channel == channel && v.held) { v.held = false; v.state = 3; }
            break;
        case 0x79:
            c = Channel{c.instrument};
            break;
        case 0x7B:
            for (Voice &v : m_voices)
                if (v.channel == channel && v.state > 3) v.state = 3;
            break;
        case 0x78:
            for (Voice &v : m_voices)
                if (v.channel == channel) stopVoice(v);
            break;
        default:
            break;
        }
    }

    // $51d1e (Dark Queen), $4b16e (Pools): the voice for a new note.
    // Start with the channel's side pair (0/3 left, 1/2 right), or the other
    // pair when no voice of this one is free or releasing but one of the
    // other is. In a pair the voice with the lower state is taken; on equal
    // states the two voices alternate (two bytes of the driver remember the
    // last choice per pair). The driver also has priorities for notes played
    // through its other entry ($53638): the sequencer plays every note with
    // priority 0, so a music note always gets a voice.
    int allocate(int channel)
    {
        int d0 = m_channels[channel].rightPair ? 1 : 0;
        auto pairMin = [this](int first) {
            return qMin(m_voices[first].state, m_voices[first ^ 3].state);
        };
        if (pairMin(d0) > 3 && pairMin(d0 ^ 1) <= 3)
            d0 ^= 1;
        // $51e0a
        const int tieIndex = ((d0 - 1) & 2) ? 1 : 0;
        const int diff = m_voices[d0].state - m_voices[d0 ^ 3].state;
        int v = d0;
        if (diff > 0 || (diff == 0 && (d0 & 2) == m_pairChoice[tieIndex]))
            v = d0 ^ 3;
        m_pairChoice[tieIndex] = v & 2;
        return v;
    }

    // $52a52
    int noteOn(int channel, int note, int velocity)
    {
        Channel &c = m_channels[channel];
        if (!c.instrument || c.instrument->samples.isEmpty())
            return -1;
        const int v = allocate(channel);
        if (v < 0)
            return -1;
        Voice &voice = m_voices[v];
        stopVoice(voice);
        voice.channel = channel;
        voice.note = note;
        voice.velocity = m_file.velocitySensitive() ? velocity + 1 : 0x80;
        voice.level = 0;
        voice.held = false;
        voice.sampleIndex = -1;
        voice.state = 6;
        choosePitch(voice, false);
        if (voice.sample) {
            voice.position = 0.0;
            voice.inLoop = voice.sample->oneShot == 0;
            voice.playing = voice.period > 0
                            && (voice.sample->oneShot > 0 || voice.sample->loop > 0);
        }
        voice.paulaVolume = 0; // the first write goes out at volume 0
        return v;
    }

    // $52cea
    void noteOff(int note, int channel, int v)
    {
        Voice &voice = m_voices[v];
        if (voice.channel != channel || voice.note != note || voice.state <= 3)
            return;
        if (m_channels[channel].sustainPedal)
            voice.held = true;
        else
            voice.state = 3;
    }

    void stopVoice(Voice &voice)
    {
        voice.channel = -1;
        voice.state = 0;
        voice.playing = false;
        voice.noteOffAt = -1;
        voice.paulaVolume = 0;
    }

    // $51e46: period, and the sample of the right octave.
    void choosePitch(Voice &voice, bool keepSample)
    {
        const Channel &c = m_channels[voice.channel];
        const MxInstrument *ins = c.instrument;
        qint64 value = c.bend + (static_cast<qint64>(voice.note) << 8);
        value += (static_cast<qint64>(ins->tune) << 8) / 24;
        value -= 0x2d00;
        value = value * 4 / 3;
        value <<= 4;
        value = 0x9fd77 - value;
        if (!keepSample) {
            int index = 0;
            voice.octaveShift = 0;
            while (value > 0x8fd77 && index + 1 < ins->samples.size()) {
                ++index;
                ++voice.octaveShift;
                value -= 0x10000;
            }
            voice.sampleIndex = index;
            voice.sample = &ins->samples.at(index);
        } else {
            value -= static_cast<qint64>(voice.octaveShift) * 0x10000;
        }
        voice.period = periodFor(value);
    }

    // $51f06: level change per frame to reach a target in timeMs.
    qint64 rateFor(int delta, int timeMs) const
    {
        if (timeMs == 0)
            return delta;
        const qint64 r = static_cast<qint64>(std::abs(delta)) * 1000 / (static_cast<qint64>(timeMs) * m_vblank);
        if (r >= std::abs(delta))
            return delta;
        return delta < 0 ? -r : r;
    }

    void startSegment(Voice &voice, const QVector<MxEnvelopePoint> &table, int index)
    {
        voice.segment = index;
        voice.segmentLeft = static_cast<qint64>(table.at(index).timeMs) << 8;
        voice.rate = rateFor(table.at(index).level - voice.level, table.at(index).timeMs);
    }

    // $51f38: envelope state machine and Paula period/volume (once a frame).
    void updateVoices()
    {
        for (Voice &voice : m_voices) {
            if (voice.channel < 0 || voice.state == 0)
                continue;
            const MxInstrument *ins = m_channels[voice.channel].instrument;
            if (voice.state == 1) {
                stopVoice(voice);
                continue;
            }
            if (voice.state == 6) {
                if (ins->attack.isEmpty()) {
                    voice.state = 4;
                    voice.level = ins->sustain;
                } else {
                    voice.segmentsLeft = ins->attack.size();
                    startSegment(voice, ins->attack, 0);
                    voice.state = 5;
                }
            } else if (voice.state == 3) {
                if (ins->release.isEmpty()) {
                    voice.state = 1;
                    voice.paulaVolume = 0;
                    continue;
                }
                voice.segmentsLeft = ins->release.size();
                startSegment(voice, ins->release, 0);
                voice.state = 2;
            }
            if (voice.state == 5 || voice.state == 2) {
                const QVector<MxEnvelopePoint> &table = voice.state == 5 ? ins->attack : ins->release;
                if (voice.segmentLeft <= m_frameMs8) {
                    voice.level = table.at(voice.segment).level;
                    if (--voice.segmentsLeft == 0) {
                        if (voice.state == 2) {
                            voice.state = 1;
                            voice.paulaVolume = 0;
                            continue;
                        }
                        voice.state = 4;
                    } else {
                        startSegment(voice, table, voice.segment + 1);
                    }
                } else {
                    voice.level = static_cast<int>(qBound<qint64>(qint64(0), voice.level + voice.rate, qint64(0x8000)));
                    voice.segmentLeft -= m_frameMs8;
                }
            }
            if (m_channels[voice.channel].pendingPitch)
                choosePitch(voice, true);
            voice.paulaVolume = volumeOf(voice);
            if (voice.period == 0)
                voice.paulaVolume = 0;
        }
        for (Channel &c : m_channels)
            c.pendingPitch = false;
    }

    // $51cd8
    int volumeOf(const Voice &voice) const
    {
        int v = 0x40; // 92(a4), the master volume
        if (voice.velocity < 0x80)
            v = v * voice.velocity >> 7;
        const int env = voice.level >> 8;
        if (env < 0x80)
            v = v * env >> 7;
        const int chan = m_channels[voice.channel].volume;
        if (chan < 0x80)
            v = v * chan >> 7;
        return qMin(v, 0x40);
    }

    // Paula: 8-bit samples, no interpolation, 0..64 volume; voices 0/3 left.
    void mix(QByteArray &pcm, int frames)
    {
        const int base = pcm.size();
        pcm.resize(base + frames * 4);
        qint16 *out = reinterpret_cast<qint16 *>(pcm.data() + base);
        for (int i = 0; i < frames; ++i) {
            int left = 0;
            int right = 0;
            for (int v = 0; v < kVoices; ++v) {
                Voice &voice = m_voices[v];
                if (!voice.playing || !voice.sample || voice.period <= 0)
                    continue;
                const MxSample &s = *voice.sample;
                // one-shot part once, then the loop part for ever (audio.device
                // requests with 1 and 0 cycles); no loop: the write is
                // returned and the driver frees the voice ($521b2)
                if (!voice.inLoop && voice.position >= s.oneShot) {
                    if (s.loop <= 0) {
                        stopVoice(voice);
                        continue;
                    }
                    voice.position -= s.oneShot;
                    voice.inLoop = true;
                }
                if (voice.inLoop && voice.position >= s.loop)
                    voice.position = std::fmod(voice.position, static_cast<double>(s.loop));
                const int offset = (voice.inLoop ? s.oneShot : 0) + static_cast<int>(voice.position);
                const int value = offset < s.data.size() ? static_cast<qint8>(s.data.at(offset)) : 0;
                const int scaled = value * voice.paulaVolume;
                if (v == 0 || v == 3)
                    left += scaled;
                else
                    right += scaled;
                voice.position += m_clock / voice.period / m_options.sampleRate;
            }
            // two voices per side: 2 * 127 * 64 fits in 16 bits after << 1
            out[i * 2] = qToLittleEndian<qint16>(static_cast<qint16>(qBound(-32768, left * 2, 32767)));
            out[i * 2 + 1] = qToLittleEndian<qint16>(static_cast<qint16>(qBound(-32768, right * 2, 32767)));
        }
    }

    const MxMusicFile &m_file;
    const MxSong &m_song;
    MxMusicFile::RenderOptions m_options;
    Channel m_channels[kChannels];
    Voice m_voices[kVoices];
    int m_vblank = 50;
    qint64 m_frameMs8 = 0;
    double m_clock = kPalClock;
    int m_tempo16 = 0;
    qint64 m_increment = 0;
    qint64 m_position = 0;
    qint64 m_eventTime = 0;
    int m_pairChoice[2] = {0, 0};
    int m_next = 0;
    bool m_ended = false;
    int m_rampFrom = 0;
    int m_rampDelta = 0;
    qint64 m_rampLength = 0;
    qint64 m_rampDone = 0;
    int m_loopDepth = 0;
    int m_loopCount[4] = {};
    int m_loopStart[4] = {};
};

} // namespace

bool MxMusicFile::looksLikeMx(const QByteArray &data)
{
    return data.size() >= 12 && data.left(4) == QByteArrayLiteral("MXTX");
}

bool MxMusicFile::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }
    m_driver = driverForFileName(QFileInfo(path).fileName());
    return parse(file.readAll());
}

MxMusicFile::Driver MxMusicFile::driverForFileName(const QString &fileName)
{
    const QString name = fileName.toLower();
    return name == QStringLiteral("secret.mx") || name == QStringLiteral("pod.mx") ? SilverBlades : DarkQueen;
}

bool MxMusicFile::parse(const QByteArray &data)
{
    m_songs.clear();
    m_instruments.clear();
    m_valid = false;
    if (!looksLikeMx(data)) {
        m_error = QStringLiteral("Not an MXTX music file.");
        return false;
    }
    Reader r(data);
    r.skip(4);
    m_flags = r.w();
    m_flags2 = r.w();
    if (m_flags2 & 0x8000)
        r.skip(0x100);
    const int songs = r.w();
    for (int s = 0; s < songs && r.ok(); ++s) {
        MxSong song;
        song.index = s;
        const quint32 count = r.l();
        if (count > 200000) {
            m_error = QStringLiteral("Song %1 has %2 events.").arg(s).arg(count);
            return false;
        }
        const QByteArray events = r.bytes(static_cast<qint64>(count) * 6);
        bool tempoSeen = false;
        for (quint32 i = 0; i < count && r.ok(); ++i) {
            const char *e = events.constData() + i * 6;
            MxEvent ev;
            ev.status = static_cast<quint8>(e[0]);
            ev.data = static_cast<quint8>(e[1]);
            ev.delta = qFromBigEndian<quint16>(e + 2);
            ev.word = qFromBigEndian<quint16>(e + 4);
            ev.b4 = static_cast<quint8>(e[4]);
            ev.b5 = static_cast<quint8>(e[5]);
            if (ev.status < 0x80 && ((ev.data >> 1) & 0x78)) {
                ++song.notes;
                if (!song.channels.contains(ev.data & 0x0F))
                    song.channels.append(ev.data & 0x0F);
            }
            if (ev.status == 0x80 && !tempoSeen) {
                song.initialTempo = ev.data;
                tempoSeen = true;
            }
            song.events.append(ev);
        }
        std::sort(song.channels.begin(), song.channels.end());
        m_songs.append(song);
    }
    const int instruments = r.w();
    for (int i = 0; i < instruments && r.ok(); ++i) {
        MxInstrument ins;
        ins.id = r.w();
        ins.tune = static_cast<qint16>(r.w());
        ins.sustain = r.w();
        const int sampleCount = r.w();
        qint64 oneShot = r.l();
        qint64 loop = r.l();
        const int attack = r.w();
        const int release = r.w();
        for (int k = 0; k < attack; ++k) {
            MxEnvelopePoint p;
            p.timeMs = r.w();
            p.level = r.w();
            ins.attack.append(p);
        }
        for (int k = 0; k < release; ++k) {
            MxEnvelopePoint p;
            p.timeMs = r.w();
            p.level = r.w();
            ins.release.append(p);
        }
        for (int k = 0; k < sampleCount && r.ok(); ++k) {
            if (oneShot + loop > 0x1000000) {
                m_error = QStringLiteral("Instrument %1 has an implausible sample size.").arg(ins.id);
                return false;
            }
            MxSample s;
            s.oneShot = static_cast<int>(oneShot);
            s.loop = static_cast<int>(loop);
            s.data = r.bytes(oneShot + loop);
            ins.samples.append(s);
            oneShot *= 2;
            loop *= 2;
        }
        m_instruments.append(ins);
    }
    if (!r.ok()) {
        m_error = QStringLiteral("The MXTX file ends early (byte %1).").arg(r.pos());
        return false;
    }
    m_valid = true;
    return true;
}

const MxInstrument *MxMusicFile::instrument(int id) const
{
    for (const MxInstrument &ins : m_instruments) {
        if (ins.id == id)
            return &ins;
    }
    return nullptr;
}

AudioSample MxMusicFile::render(int song, const RenderOptions &options) const
{
    if (song < 0 || song >= m_songs.size()) {
        AudioSample empty;
        empty.error = QStringLiteral("No song %1.").arg(song);
        return empty;
    }
    Player player(*this, m_songs.at(song), options);
    AudioSample sample = player.run();
    sample.name = QStringLiteral("Song %1").arg(song + 1);
    return sample;
}

QVector<QVector<MxMusicFile::VoiceFrame>> MxMusicFile::trace(int song, int frames) const
{
    if (song < 0 || song >= m_songs.size())
        return {};
    Player player(*this, m_songs.at(song), RenderOptions());
    return player.trace(frames);
}
