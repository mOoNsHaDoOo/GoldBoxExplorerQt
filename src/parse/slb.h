#pragma once

#include "parse/audio.h"

#include <QByteArray>
#include <QString>
#include <QVector>

// SECRET.SLB / CURSE.SLB are small Amiga song banks ("SLBR").  Each song is a
// stream of two-byte events: command bytes >= 0x80 carry setup/rest data, note
// bytes < 0x80 carry a MIDI-style note and a duration in the low six bits of
// the second byte.  0xff 0xff terminates a song.
struct SlbEvent
{
    int offset = 0;
    quint8 command = 0;
    quint8 data = 0;
    int duration = 0;
    bool note = false;
    bool rest = false;
    bool end = false;
};

struct SlbSong
{
    int index = 0;
    int start = 0;
    int end = 0;
    int loopPoint = 0;
    QVector<SlbEvent> events;
    int totalUnits = 0;
    int noteCount = 0;
    int commandCount = 0;
};

class SlbFile
{
public:
    SlbFile() = default;
    explicit SlbFile(const QString &path) { load(path); }

    bool load(const QString &path);
    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }
    const QVector<SlbSong> &songs() const { return m_songs; }
    int fileSize() const { return m_size; }

    static bool looksLikeSlb(const QByteArray &data);

    // Builds a simple square-wave preview of a song.  The game's Music-X
    // driver uses sample instruments; this preview keeps the written melody
    // and rhythm audible without emulating the tracker.
    static AudioSample synthesize(const SlbSong &song, int outputRate = 44100);

private:
    QString m_error;
    bool m_valid = false;
    int m_size = 0;
    QVector<SlbSong> m_songs;
};
