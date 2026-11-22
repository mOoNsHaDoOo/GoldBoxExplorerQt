#pragma once

#include "core/paula.h"
#include "parse/audio.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

// Music players linked into the Amiga Gold Box executables, run from their
// original 68000 code:
//  * Pool of Radiance, `program`: Wally Beben's player (code hunk with
//    "(c)Wally Beben" at $7C92; $78A0 posts a song number, $78C2 is the
//    vertical-blank tick), 6 songs.
class ExeMusic
{
public:
    enum Player { None, WallyBeben };

    // Cheap check on the file contents.
    static Player detect(const QByteArray &exe);

    bool load(const QByteArray &exe);
    bool isValid() const { return m_player != None; }
    Player player() const { return m_player; }
    QString playerName() const;
    QString errorString() const { return m_error; }
    QStringList songNames() const { return m_songs; }

    struct RenderOptions
    {
        int sampleRate = 44100;
        int maxSeconds = 600;     // a song that does not loop
        int fadeSeconds = 4;      // played after the loop point, fading out
    };
    // Plays one song (0-based) until every voice has looped once.
    AudioSample render(int song, const RenderOptions &options) const;
    AudioSample render(int song) const { return render(song, RenderOptions()); }

private:
    QByteArray m_exe;
    Player m_player = None;
    QString m_error;
    QStringList m_songs;
};
