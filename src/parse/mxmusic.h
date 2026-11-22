#pragma once

#include "parse/audio.h"

#include <QByteArray>
#include <QString>
#include <QVector>

// "MXTX" music files of the Amiga Gold Box games (secret.mx, pod.mx, dqk.mx),
// with the playback rules of their music driver, reconstructed from the Dark
// Queen of Krynn executable ($51c80-$53e00, the same driver is linked into
// Secret of the Silver Blades and Pools of Darkness).
//
// File (loader $53a5e, big-endian):
//   "MXTX", word flags (bit 1 of the second word: velocity sensitive)
//   word song count, then per song: long event count, events of 6 bytes
//   word instrument count, then per instrument a 20-byte header
//     id, tune (1/24 semitone), sustain level, sample count, one-shot
//     length, loop length, attack/decay entries, release entries
//   followed by the two envelope tables (time ms, level 0..$8000 per entry)
//   and the samples: one-shot part then loop part, each further sample twice
//   as long (one octave lower).
// Events (sequencer $52e84): [status][data][delta ticks][word]
//   status < $80  note: data = velocity (high nibble) | channel, word = length
//   $80 tempo (BPM in data, ramp over the word ticks), $B0 controller,
//   $C0 program change, $E0 pitch bend, $A0 loop/signal commands, $FF end.
//   192 ticks per quarter note, the sequencer runs at the vertical blank.
struct MxEnvelopePoint
{
    int timeMs = 0;
    int level = 0;      // 0..$8000
};

struct MxSample
{
    QByteArray data;    // signed 8-bit
    int oneShot = 0;    // bytes played once
    int loop = 0;       // bytes repeated after the one-shot part
};

struct MxInstrument
{
    int id = 0;
    int tune = 0;       // signed, 1/24 semitone
    int sustain = 0;    // level used when there is no attack table
    QVector<MxEnvelopePoint> attack;
    QVector<MxEnvelopePoint> release;
    QVector<MxSample> samples; // shortest (highest octave) first
};

struct MxEvent
{
    quint8 status = 0;
    quint8 data = 0;
    int delta = 0;
    int word = 0;
    quint8 b4 = 0;
    quint8 b5 = 0;
};

struct MxSong
{
    int index = 0;
    QVector<MxEvent> events;
    int notes = 0;
    QVector<int> channels;  // channels used by notes
    int initialTempo = 120;
};

class MxMusicFile
{
public:
    static bool looksLikeMx(const QByteArray &data);

    bool parse(const QByteArray &data);
    bool load(const QString &path);

    // The driver differs a little between the games: Dark Queen of Krynn's
    // sequencer shortens a note by the time its event was served late, so
    // that the note off falls on time; the drivers of Secret of the Silver
    // Blades and Pools of Darkness count the length from the frame that
    // starts the note. load() picks the driver by the file name
    // (secret.mx, pod.mx); other names use Dark Queen's.
    enum Driver { DarkQueen, SilverBlades };
    static Driver driverForFileName(const QString &fileName);
    void setDriver(Driver driver) { m_driver = driver; }
    Driver driver() const { return m_driver; }

    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }
    int flags() const { return m_flags; }
    bool velocitySensitive() const { return m_flags2 & 2; }
    const QVector<MxSong> &songs() const { return m_songs; }
    const QVector<MxInstrument> &instruments() const { return m_instruments; }
    const MxInstrument *instrument(int id) const;

    struct RenderOptions
    {
        int sampleRate = 44100;
        bool pal = true;            // 50 Hz sequencer, PAL Paula clock
        int maxSeconds = 600;       // stop a song that never ends
        int releaseSeconds = 3;     // time left for the last notes to fade
    };
    // Plays one song with the driver rules into stereo 16-bit PCM (Paula
    // channels 0/3 left, 1/2 right).
    AudioSample render(int song, const RenderOptions &options) const;
    AudioSample render(int song) const { return render(song, RenderOptions()); }

private:
    QVector<MxSong> m_songs;
    QVector<MxInstrument> m_instruments;
    QString m_error;
    int m_flags = 0;
    int m_flags2 = 0;
    Driver m_driver = DarkQueen;
    bool m_valid = false;
};
