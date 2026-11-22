#pragma once

#include "parse/audio.h"

#include <QByteArray>
#include <QString>
#include <QVector>

// .SLB song banks of the Amiga Gold Box games (CURSE.SLB, SECRET.SLB,
// MUSIC.SLB). Curse of the Azure Bonds plays its music from them; Dark Queen
// of Krynn loads its bank when the sampled .MX music is not loaded (Secret of
// the Silver Blades and Pools of Darkness link the same code). Reconstructed from the Curse executable
// (loader $3aa54, song start $3aaee, sequencer $3ac12, Paula voice $425f8);
// the four games link the same synth and sequencer.
//
// File: "SLBR", long file size, byte song count, byte 0, words song offsets
// (count + 1, relative to the end of the table).
// Song: word tempo, byte volume, byte voices (up to 5, 4 sound), 5 words
// track offsets (relative to the end of this 14-byte header).
// Track: 2-byte events [byte][data]
//   < $80  note (MIDI number, 60 = C4), data = length; bit 6: tied to the next
//   $80    rest, data = length
//   $81    voice off, data = instrument (stored, not used by the synth)
//   $84    volume = data
//   $FF    end of the track (the song starts again when every track ended)
//   other commands ($82, $83, $87, $88) are skipped, and so are notes and
//   rests with bit 7 of data set (Dark Queen of Krynn's sequencer; only its
//   MUSIC.SLB has them)
// Length: whole note $6900 >> (data & 7), bit 3 dotted, bits 4-5 (n = 2, 4, 6)
// n / (n + 1) of it (triplets), in time units of 5 ms divided by the tempo
// factor 105 * tempo / 24000. The sequencer runs every 10 ms (timer.device).
// Sound: a sawtooth of 18, 54 or 108 samples (chosen by the period), period
// from a 12-note table for octave 5 shifted by at most two octaves, Paula
// volume = volume / 8 + 3.
struct SlbEvent
{
    int offset = 0;     // in the file
    quint8 code = 0;
    quint8 data = 0;
    bool isNote() const { return code < 0x80 && !(data & 0x80); }
    bool isRest() const { return code == 0x80 && !(data & 0x80); }
    bool isEnd() const { return code == 0xFF; }
};

struct SlbTrack
{
    int offset = 0;
    QVector<SlbEvent> events;
    int notes = 0;
};

struct SlbSong
{
    int index = 0;
    int offset = 0;
    int tempo = 0;
    int volume = 0;
    QVector<SlbTrack> tracks;
    int tempoFactor() const { return 105 * tempo / 24000; }
    int noteCount() const;
};

class SlbFile
{
public:
    SlbFile() = default;
    explicit SlbFile(const QString &path) { load(path); }

    bool load(const QString &path);
    bool parse(const QByteArray &data);
    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }
    const QVector<SlbSong> &songs() const { return m_songs; }
    int fileSize() const { return m_size; }

    static bool looksLikeSlb(const QByteArray &data);

    // Length of an event in 5 ms units (before the division by the tempo
    // factor).
    static int lengthOf(quint8 data);

    struct RenderOptions
    {
        int sampleRate = 44100;
        int maxSeconds = 600;
        int fadeSeconds = 4;     // played after the loop point, fading out
    };
    // Plays a song with the rules of the game's sequencer and synth into
    // stereo 16-bit PCM: one pass, then the start again, fading out.
    static AudioSample render(const SlbSong &song, const RenderOptions &options);
    static AudioSample render(const SlbSong &song) { return render(song, RenderOptions()); }

private:
    QString m_error;
    bool m_valid = false;
    int m_size = 0;
    QVector<SlbSong> m_songs;
};
