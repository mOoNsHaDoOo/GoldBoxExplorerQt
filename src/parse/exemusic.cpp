#include "parse/exemusic.h"

#include "core/m68k.h"

#include <QtEndian>

#include <cmath>
#include <cstring>

namespace {

constexpr uint32_t kLoadBase = 0x10000;

// ---- Pool of Radiance: Wally Beben's player ----
constexpr uint32_t kBebenCodeSize = 33568;
constexpr uint32_t kBebenCopyright = 0x7C92;
constexpr uint32_t kBebenSetSong = 0x78A0;     // d0 = song (0 = stop)
constexpr uint32_t kBebenTick = 0x78C2;        // once per vertical blank
constexpr uint32_t kBebenPlaying = 0x17D5;      // data hunk: playing flag, cleared by the end of a song
constexpr uint32_t kBebenTrackRestart = 0x7976; // clr.b $C(a0): $FF in a track, d1 = voice
constexpr uint32_t kBebenSongTable = 0x197C;   // data hunk: 4 track pointers per song
constexpr uint32_t kBebenPatterns = 0x19DC;    // data hunk: pattern table (ends the song table)

int findBebenHunk(const QVector<AmigaHunk> &hunks, const M68k &cpu)
{
    static const char sig[] = "(c)Wally Beben";
    for (int i = 0; i + 1 < hunks.size(); ++i) {
        const AmigaHunk &h = hunks.at(i);
        if (h.type != AmigaHunk::Code || h.size != kBebenCodeSize)
            continue;
        const uint8_t *p = const_cast<M68k &>(cpu).memory().data() + h.address;
        if (std::memcmp(p + kBebenCopyright, sig, sizeof sig - 1) == 0
            && p[kBebenSetSong] == 0x13 && p[kBebenSetSong + 1] == 0xC0      // move.b d0,abs.l
            && p[kBebenTick] == 0x4C && p[kBebenTick + 1] == 0xF9)            // movem.l abs.l,...
            return i;
    }
    return -1;
}

} // namespace

// ---- players ----

ExeMusic::Player ExeMusic::detect(const QByteArray &exe)
{
    if (!AmigaHunkLoader::isHunkExecutable(exe))
        return None;
    if (exe.contains("(c)Wally Beben"))
        return WallyBeben;
    return None;
}

QString ExeMusic::playerName() const
{
    switch (m_player) {
    case WallyBeben: return QStringLiteral("Wally Beben's music player (Pool of Radiance)");
    default: return QString();
    }
}

bool ExeMusic::load(const QByteArray &exe)
{
    m_player = None;
    m_songs.clear();
    m_exe = exe;
    const Player kind = detect(exe);
    if (kind == None) {
        m_error = QStringLiteral("no known music player in this executable");
        return false;
    }
    M68k cpu;
    QVector<AmigaHunk> hunks;
    if (!AmigaHunkLoader::load(exe, cpu, kLoadBase, hunks, &m_error))
        return false;
    if (kind == WallyBeben) {
        const int code = findBebenHunk(hunks, cpu);
        if (code < 0) {
            m_error = QStringLiteral("unknown version of the Wally Beben player");
            return false;
        }
        const int count = (kBebenPatterns - kBebenSongTable) / 16;
        static const char *const names[] = {"Title and camp", "Combat", "Character creation",
                                            "Song 4 (not used by the game)", "Game over",
                                            "Song 6 (not used by the game)"};
        for (int i = 0; i < count; ++i)
            m_songs.append(QString::fromLatin1(names[i]));
    }
    m_player = kind;
    return true;
}

AudioSample ExeMusic::render(int song, const RenderOptions &options) const
{
    AudioSample out;
    out.sampleRate = options.sampleRate;
    out.channels = 2;
    out.bits = 16;
    if (!isValid() || song < 0 || song >= m_songs.size()) {
        out.error = QStringLiteral("no such song");
        return out;
    }
    out.name = m_songs.at(song);

    M68k cpu;
    QVector<AmigaHunk> hunks;
    QString error;
    if (!AmigaHunkLoader::load(m_exe, cpu, kLoadBase, hunks, &error)) {
        out.error = error;
        return out;
    }
    PaulaMixer paula(options.sampleRate);
    cpu.chipWrite = [&paula](uint32_t reg, uint32_t v) { paula.write(reg, v); };
    cpu.chipRead = [](uint32_t) { return 0u; };

    const int code = findBebenHunk(hunks, cpu);
    if (code < 0) {
        out.error = QStringLiteral("player not found");
        return out;
    }
    const uint32_t codeBase = hunks.at(code).address;
    const uint32_t dataBase = hunks.at(code + 1).address;

    uint32_t dregs[8] = {static_cast<uint32_t>(song + 1)};
    if (!cpu.call(codeBase + kBebenSetSong, dregs)) {
        out.error = cpu.error;
        return out;
    }

    const int framesPerTick = options.sampleRate / 50;
    const int maxTicks = options.maxSeconds * 50;
    const int fadeTicks = options.fadeSeconds * 50;
    bool looped[4] = {false, false, false, false};
    cpu.watchAddress = codeBase + kBebenTrackRestart;
    cpu.onWatch = [&cpu, &looped] { looped[cpu.d[1] & 3] = true; };
    int loopTick = -1;
    int silentTicks = 0;
    qsizetype audibleEnd = 0;
    QByteArray pcm;
    for (int tick = 0; tick < maxTicks; ++tick) {
        if (!cpu.call(codeBase + kBebenTick)) {
            out.error = cpu.error;
            return out;
        }
        // every voice has reached the $FF of its track
        const bool all = looped[0] && looped[1] && looped[2] && looped[3];
        if (all && loopTick < 0)
            loopTick = tick;
        const qsizetype before = pcm.size();
        paula.mix(cpu.memory(), framesPerTick, pcm);
        // a song that ends (not every song loops): the player clears its
        // playing flag, or the voices stay silent
        if (paula.silent()) {
            ++silentTicks;
        } else {
            silentTicks = 0;
            audibleEnd = pcm.size();
        }
        if (cpu.read(dataBase + kBebenPlaying, 1) == 0 || silentTicks >= 5 * 50) {
            pcm.truncate(std::min(pcm.size(), audibleEnd + static_cast<qsizetype>(framesPerTick) * 4 * 10));
            break;
        }
        if (loopTick >= 0) {
            // fade out after the loop point
            const int elapsed = tick - loopTick;
            if (elapsed >= fadeTicks)
                break;
            auto *p = reinterpret_cast<uchar *>(pcm.data() + before);
            const int n = static_cast<int>((pcm.size() - before) / 2);
            for (int i = 0; i < n; ++i) {
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
