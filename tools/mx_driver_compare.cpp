// mx_driver_compare: runs the original MX music driver of an Amiga Gold Box
// game in the explorer's 68000 interpreter and prints, after every vertical
// blank, the period, volume and part (1 one-shot, 0 loop, -1 none) of the
// four Paula voices: the format of "gbexplorer --mx-trace", for a diff.
//
//   mx_driver_compare EXECUTABLE FILE.MX SONG FRAMES [d|s|p]
//     SONG from 0; d = Dark Queen of Krynn (default), s = Secret of the
//     Silver Blades, p = Pools of Darkness (the addresses of each build).
//   LOGIO=1 also logs the audio.device requests, LOGPOS=1 the position.
//
// Build (not part of the CMake project; needs the game's executable):
//   g++ -O1 -std=c++17 -fPIC -Isrc -o mx_driver_compare
//       tools/mx_driver_compare.cpp src/core/m68k.cpp $(pkg-config --cflags --libs Qt6Core)
//
// exec (AllocMem, lists, message ports, Cause), dos.library (Open/Read/Seek
// on the MX file) and audio.device (OpenDevice, CMD_WRITE queues with
// cycles and replies, ADCMD_PERVOL, ADIOF_PERVOL, flush, finish, abort) are
// emulated as far as the driver uses them; the vertical-blank server and the
// soft interrupt it causes run once per frame.
#include "core/m68k.h"
#include <QFile>
#include <cstdio>
#include <map>
#include <vector>

static M68k cpu(0x800000);
static uint32_t code, data;
static const uint32_t EXEC = 0x700000, AUDIO = 0x701000, GFX = 0x702000, DOS = 0x703000;
static QByteArray mxFile; static int mxPos;
static uint32_t heap = 0x400000;
static std::vector<uint32_t> servers;
static int frame;
static uint32_t rd(uint32_t a, int s) { return cpu.read(a, s); }
static void wr(uint32_t a, int s, uint32_t v) { cpu.write(a, s, v); }
static uint32_t D(uint32_t off) { return data + off; }   // data hunk (offset from $100000 in the dumps)

static void setupLibs()
{
    for (uint32_t base : {EXEC, AUDIO, GFX, DOS})
        for (uint32_t lvo = 6; lvo <= 0x300; lvo += 6) { wr(base - lvo, 2, 0xA000 | lvo); wr(base - lvo + 2, 4, 0x4E714E71); }
    wr(EXEC + 20, 2, 34);          // lib_Version (Kickstart 1.3)
    wr(EXEC + 530, 1, 50);         // VBlankFrequency
    wr(GFX + 206, 2, 4);           // DisplayFlags: PAL
    { auto &m = cpu.memory(); m[4] = EXEC >> 24; m[5] = (EXEC >> 16) & 0xFF; m[6] = (EXEC >> 8) & 0xFF; m[7] = EXEC & 0xFF; }
}

// ---- exec lists and ports ----
static void addTail(uint32_t list, uint32_t node)
{
    const uint32_t pred = rd(list + 8, 4);
    wr(node, 4, list + 4); wr(node + 4, 4, pred); wr(pred, 4, node); wr(list + 8, 4, node);
}
static uint32_t remHead(uint32_t list)
{
    const uint32_t node = rd(list, 4);
    const uint32_t succ = rd(node, 4);
    if (!succ) return 0;
    wr(list, 4, succ); wr(succ + 4, 4, list);
    return node;
}
static std::vector<uint32_t> causes;
static void putMsg(uint32_t port, uint32_t msg)
{
    wr(msg + 8, 1, 5);                                   // NT_MESSAGE
    addTail(port + 20, msg);
    const int flags = rd(port + 14, 1) & 3;
    if (flags == 1) causes.push_back(rd(port + 16, 4));  // PA_SOFTINT
}
static void replyMsg(uint32_t msg)
{
    const uint32_t port = rd(msg + 14, 4);
    if (port) putMsg(port, msg); else wr(msg + 8, 1, 7);
    if (port) wr(msg + 8, 1, 7);                         // NT_REPLYMSG
}

// ---- audio.device: a queue of writes per channel ----
struct Channel { std::vector<uint32_t> queue; double left = 0; };
static Channel chans[4];
static uint32_t curPeriod[4], curVolume[4];
static void startHead(int c)
{
    Channel &ch = chans[c];
    if (ch.queue.empty()) return;
    const uint32_t io = ch.queue.front();
    const uint32_t cycles = rd(io + 46, 2);
    ch.left = cycles ? double(cycles) * rd(io + 38, 4) : -1;
    if (rd(io + 30, 1) & 0x10) { curPeriod[c] = rd(io + 42, 2); curVolume[c] = rd(io + 44, 2); }   // ADIOF_PERVOL
}
static void finishIo(uint32_t io, int error)
{
    wr(io + 31, 1, error & 0xFF);
    wr(io + 30, 1, rd(io + 30, 1) & ~1);                 // not IOF_QUICK
    replyMsg(io);
}
static void abortChannel(int c, bool all)
{
    Channel &ch = chans[c];
    while (!ch.queue.empty()) {
        finishIo(ch.queue.front(), -2);
        ch.queue.erase(ch.queue.begin());
        if (!all) break;
    }
    startHead(c);
}
static void beginIO(uint32_t io)
{
    const int cmd = rd(io + 28, 2);
    const int mask = rd(io + 24, 4) & 15;
    bool quick = true;
    switch (cmd) {
    case 3:                                              // CMD_WRITE
        if (getenv("LOGIO")) fprintf(stderr, "frame %d WRITE io %X mask %X per %u vol %u cyc %u data %X len %u flags %X\n", frame, io, mask, rd(io+42,2), rd(io+44,2), rd(io+46,2), rd(io+34,4), rd(io+38,4), rd(io+30,1));
        for (int c = 0; c < 4; ++c) if (mask & (1 << c)) {
            chans[c].queue.push_back(io);
            if (chans[c].queue.size() == 1) startHead(c);
        }
        quick = false;
        break;
    case 12:                                             // ADCMD_PERVOL
        if (getenv("LOGIO")) fprintf(stderr, "frame %d PERVOL io %X mask %X per %u vol %u\n", frame, io, mask, rd(io+42,2), rd(io+44,2));
        for (int c = 0; c < 4; ++c) if (mask & (1 << c)) { curPeriod[c] = rd(io + 42, 2); curVolume[c] = rd(io + 44, 2); }
        break;
    case 8: case 1:                                      // CMD_FLUSH, CMD_RESET
        for (int c = 0; c < 4; ++c) if (mask & (1 << c)) abortChannel(c, true);
        break;
    case 11:                                             // ADCMD_FINISH
        for (int c = 0; c < 4; ++c) if (mask & (1 << c)) abortChannel(c, false);
        break;
    case 6: case 7: case 9: case 10: case 13: case 32:
        break;
    default:
        fprintf(stderr, "frame %d: audio command %d\n", frame, cmd);
    }
    wr(io + 31, 1, 0);
    if (quick) {
        if (!(rd(io + 30, 1) & 1)) replyMsg(io);
    } else {
        wr(io + 30, 1, rd(io + 30, 1) & ~1);
    }
}
static void audioFrame()
{
    for (int c = 0; c < 4; ++c) {
        Channel &ch = chans[c];
        if (ch.queue.empty() || ch.left < 0 || curPeriod[c] < 64) continue;
        ch.left -= 3546895.0 / curPeriod[c] / 50.0;
        if (ch.left <= 0) {
            finishIo(ch.queue.front(), 0);
            ch.queue.erase(ch.queue.begin());
            startHead(c);
        }
    }
}

static uint32_t alloc(uint32_t size)
{
    uint32_t a = heap; heap = (heap + size + 15) & ~7u;
    for (uint32_t i = 0; i < size; ++i) wr(a + i, 1, 0);
    return a;
}

static bool libcall(uint16_t op)
{
    const int lvo = op & 0xFFF;
    const uint32_t base = cpu.pc - 2 + lvo;
    auto ret = [](uint32_t d0) { cpu.d[0] = d0; cpu.pc = cpu.read(cpu.a[7], 4); cpu.a[7] += 4; return true; };
    if (base == EXEC) {
        switch (lvo) {
        case 198: return ret(alloc(cpu.d[0]));                       // AllocMem
        case 210: return ret(0);                                       // FreeMem
        case 168: servers.push_back(cpu.a[1]); return ret(0);          // AddIntServer
        case 174: return ret(0);                                       // RemIntServer
        case 624: for (uint32_t i = 0; i < cpu.d[0]; ++i) wr(cpu.a[1] + i, 1, rd(cpu.a[0] + i, 1)); return ret(0);   // CopyMem
        case 372: return ret(remHead(cpu.a[0] + 20));                  // GetMsg
        case 366: putMsg(cpu.a[0], cpu.a[1]); return ret(0);           // PutMsg
        case 378: replyMsg(cpu.a[1]); return ret(0);                   // ReplyMsg
        case 180: causes.push_back(cpu.a[1]); return ret(0);           // Cause
        case 324: return ret(0);                                       // Signal
        case 330: return ret(16);                                      // AllocSignal
        case 336: return ret(0);                                       // FreeSignal
        case 294: return ret(0x705000);                                // FindTask
        case 456: beginIO(cpu.a[1]); return ret(rd(cpu.a[1] + 31, 1)); // DoIO
        case 462: beginIO(cpu.a[1]); return ret(0);                    // SendIO
        case 474: return ret(rd(cpu.a[1] + 31, 1));                    // WaitIO
        case 480: for (int c = 0; c < 4; ++c) { auto &q = chans[c].queue; for (size_t i = 0; i < q.size(); ++i) if (q[i] == cpu.a[1]) { q.erase(q.begin() + i); finishIo(cpu.a[1], -2); if (i == 0) startHead(c); break; } } return ret(0);  // AbortIO
        case 444: {                                                    // OpenDevice(a0 name, d0 unit, a1 io, d1 flags)
            const uint32_t io = cpu.a[1];
            wr(io + 20, 4, AUDIO);
            const uint32_t len = rd(io + 38, 4), dataPtr = rd(io + 34, 4);
            wr(io + 24, 4, len ? rd(dataPtr, 1) : 0);                 // io_Unit: the first channel combination
            wr(io + 32, 2, 1);                                         // ioa_AllocKey
            wr(io + 31, 1, 0);
            fprintf(stderr, "OpenDevice io %X unit %X len %u\n", io, rd(io + 24, 4), len);
            return ret(0);
        }
        case 120: case 126: case 132: case 138: return ret(0);         // Disable/Enable/Forbid/Permit
        }
    }
    if (base == AUDIO) {
        switch (lvo) {
        case 30: beginIO(cpu.a[1]); return ret(0);                     // BeginIO
        case 36: for (int c = 0; c < 4; ++c) { auto &q = chans[c].queue; for (size_t i = 0; i < q.size(); ++i) if (q[i] == cpu.a[1]) { q.erase(q.begin() + i); finishIo(cpu.a[1], -2); if (i == 0) startHead(c); break; } } return ret(0);  // AbortIO
        }
    }
    if (base == DOS) {
        switch (lvo) {
        case 30: mxPos = 0; return ret(0x1234);                              // Open
        case 36: return ret(-1);                                              // Close
        case 42: {                                                            // Read(d1 fh, d2 buf, d3 len)
            int n = 0;
            for (; n < int(cpu.d[3]) && mxPos < mxFile.size(); ++n) wr(cpu.d[2] + n, 1, uint8_t(mxFile[mxPos++]));
            return ret(n);
        }
        case 66: {                                                            // Seek(d1, d2 pos, d3 mode)
            const int old = mxPos;
            const int mode = int32_t(cpu.d[3]);
            mxPos = (mode == -1 ? 0 : mode == 0 ? mxPos : mxFile.size()) + int32_t(cpu.d[2]);
            return ret(old);
        }
        }
    }
    fprintf(stderr, "frame %d: library call base %06X -%d (pc %06X) d0=%X d1=%X a0=%X a1=%X\n",
            frame, base, lvo, cpu.pc - 2, cpu.d[0], cpu.d[1], cpu.a[0], cpu.a[1]);
    return false;
}

int main(int argc, char **argv)
{
    QFile f(argv[1]); f.open(QIODevice::ReadOnly);
    QVector<AmigaHunk> h; QString e;
    if (!AmigaHunkLoader::load(f.readAll(), cpu, 0x10000, h, &e)) { puts(qPrintable(e)); return 1; }
    code = h[0].address; data = h[1].address;
    setupLibs();
    const bool pod = argc > 5 && argv[5][0] == 'p';
    const bool ssb = argc > 5 && argv[5][0] == 's';
    // driver entries and library base variables (data hunk offsets)
    const uint32_t fInit = ssb ? 0x4ac0a : pod ? 0x4bb32 : 0x526f0, fLoad = ssb ? 0x4c094 : pod ? 0x4cfd8 : 0x53ae0;
    const uint32_t fSong = ssb ? 0x4b950 : pod ? 0x4c894 : 0x533c4, fPlay = ssb ? 0x4b852 : pod ? 0x4c796 : 0x532c6;
    wr(D(ssb ? 0x8f2c : pod ? 0x7f94 : 0x8d72), 4, EXEC);        // SysBase copy of the driver
    wr(D(ssb ? 0x3470 : pod ? 0x38da : 0x4b3e), 4, GFX);         // GfxBase
    wr(D(ssb ? 0x8f1c : pod ? 0x7f82 : 0x8d60), 4, DOS);         // DOSBase
    cpu.onLineA = libcall;
    static uint32_t beam = 0;
    cpu.chipRead = [](uint32_t reg) -> uint32_t { if (reg == 6) { beam += 0x40; return beam & 0x7FFF; } return 0; };   // VHPOSR moves on
    if (getenv("LOGOFF")) { cpu.watchAddress = code + 0x52ffa; cpu.onWatch = [] { fprintf(stderr, "ON frame %d note %d left %d pos %d d4 %d\n", frame, int(rd(cpu.a[0], 1)), int(cpu.d[0]), int(rd(data + 0x5a22 + 8, 4) >> 8), int(cpu.d[4])); }; }
    cpu.setStackTop(0x7F0000);
    uint32_t regs[8] = {}; regs[4] = data + 0x7FFE;
    if (!cpu.call(code + fInit, nullptr, regs)) { printf("init: %s\n", qPrintable(cpu.error)); return 1; }
    fprintf(stderr, "init ok d0=%d, %zu servers\n", cpu.d[0], servers.size());
    QFile mx(argv[2]); mx.open(QIODevice::ReadOnly); mxFile = mx.readAll();
    const uint32_t buf = alloc(16);
    for (int i = 0; i < 7; ++i) wr(buf + i, 1, uint8_t("dqk.mx"[i]));
    const uint32_t top = 0x7F0000;
    cpu.setStackTop(top);
    auto runCauses = [&]() {
        for (int guard = 0; !causes.empty() && guard < 64; ++guard) {
            const uint32_t is = causes.front(); causes.erase(causes.begin());
            uint32_t r[8] = {}; r[1] = rd(is + 14, 4); r[5] = rd(is + 18, 4); r[6] = EXEC; r[4] = regs[4];
            if (!cpu.call(rd(is + 18, 4), nullptr, r)) { printf("softint: %s\n", qPrintable(cpu.error)); exit(1); }
        }
    };
    auto callArgs = [&](uint32_t fn, std::vector<uint32_t> args) {
        for (size_t i = 0; i < args.size(); ++i) wr(top + 4 * i, 4, args[i]);
        if (!cpu.call(code + fn, nullptr, regs)) { printf("call %X: %s\n", fn, qPrintable(cpu.error)); exit(1); }
        const uint32_t r = cpu.d[0];
        runCauses();
        return r;
    };
    fprintf(stderr, "load -> %d\n", callArgs(fLoad, {buf, 0, 0}));
    fprintf(stderr, "song -> %d\n", callArgs(fSong, {uint32_t(atoi(argv[3]))}));
    fprintf(stderr, "play -> %d\n", callArgs(fPlay, {0}));
    auto st = [&](const char *w) { if (getenv("LOGPOS")) fprintf(stderr, "%s frame %d pos %u inc %u flags %X\n", w, frame, rd(data + 0x5a22 + 8, 4), rd(data + 0x5a22 + 12, 4), rd(data + 0x5a22 + 111, 1)); };
    st("afterplay");
    for (frame = 0; frame < atoi(argv[4]); ++frame) {
        if (frame < 3) st("before");
        for (uint32_t is : servers) {
            uint32_t r[8] = {}; r[1] = rd(is + 14, 4); r[5] = rd(is + 18, 4); r[6] = EXEC; r[4] = regs[4];
            if (!cpu.call(rd(is + 18, 4), nullptr, r)) { printf("frame %d: %s pc %X\n", frame, qPrintable(cpu.error), cpu.pc); return 1; }
        }
        { static const uint32_t sb = data + (argc > 5 && argv[5][0] == 'p' ? 0x47a2 : 0x5a22);
          if (getenv("LOGPOS")) fprintf(stderr, "P %d %u %u\n", frame, rd(sb + 8, 4) >> 8, rd(sb + 12, 4)); }
        runCauses();
        audioFrame();
        runCauses();
        printf("%d", frame);
        for (int c = 0; c < 4; ++c) {
            const bool on = !chans[c].queue.empty();
            printf(" | %d %d %d", on ? curPeriod[c] : 0, on ? curVolume[c] : 0, on ? int(rd(chans[c].queue.front() + 46, 2)) : -1);
        }
        printf("\n");
    }
    return 0;
}
