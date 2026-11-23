#include "core/m68k.h"

namespace {

constexpr uint32_t kReturnMagic = 0x00FFFFF0u;   // return address that ends call()

constexpr int FC = 1;
constexpr int FV = 2;
constexpr int FZ = 4;
constexpr int FN = 8;
constexpr int FX = 16;

constexpr int kSizes[4] = {1, 2, 4, 0};

inline uint32_t msk(int sz) { return sz == 1 ? 0xFFu : sz == 2 ? 0xFFFFu : 0xFFFFFFFFu; }
inline uint32_t sbit(int sz) { return sz == 1 ? 0x80u : sz == 2 ? 0x8000u : 0x80000000u; }
inline int32_t sext(uint32_t v, int sz)
{
    return sz == 1 ? static_cast<int8_t>(v) : sz == 2 ? static_cast<int16_t>(v) : static_cast<int32_t>(v);
}
inline bool isCustom(uint32_t a) { return (a & 0x00FFF000u) == 0x00DFF000u; }

} // namespace

M68k::M68k(uint32_t memorySize)
    : m_mem(memorySize, 0)
    , m_stackTop(memorySize - 16)
{
}

uint32_t M68k::readRaw(uint32_t addr, int sz)
{
    addr &= 0x00FFFFFFu;
    if (isCustom(addr)) {
        const auto rd = [this](uint32_t reg) { return chipRead ? chipRead(reg & 0x1FE) & 0xFFFF : 0u; };
        if (sz == 1) {
            const uint32_t w = rd(addr);
            return (addr & 1) ? (w & 0xFF) : (w >> 8);
        }
        if (sz == 4)
            return rd(addr) << 16 | rd(addr + 2);
        return rd(addr);
    }
    if (addr + static_cast<uint32_t>(sz) > m_mem.size())
        return 0;
    const uint8_t *p = m_mem.data() + addr;
    if (sz == 1)
        return p[0];
    if (sz == 2)
        return static_cast<uint32_t>(p[0]) << 8 | p[1];
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

uint32_t M68k::read(uint32_t address, int size)
{
    return readRaw(address, size);
}

void M68k::write(uint32_t addr, int sz, uint32_t v)
{
    addr &= 0x00FFFFFFu;
    if (isCustom(addr)) {
        if (!chipWrite)
            return;
        if (sz == 4) {
            chipWrite(addr & 0x1FE, v >> 16);
            chipWrite((addr + 2) & 0x1FE, v & 0xFFFF);
        } else if (sz == 2) {
            chipWrite(addr & 0x1FE, v & 0xFFFF);
        } else {
            chipWrite(addr & 0x1FE, (addr & 1) ? (v & 0xFF) : (v & 0xFF) << 8);
        }
        return;
    }
    if (addr < 8 || addr + static_cast<uint32_t>(sz) > m_mem.size())
        return;
    uint8_t *p = m_mem.data() + addr;
    if (sz == 1) {
        p[0] = static_cast<uint8_t>(v);
    } else if (sz == 2) {
        p[0] = static_cast<uint8_t>(v >> 8);
        p[1] = static_cast<uint8_t>(v);
    } else {
        p[0] = static_cast<uint8_t>(v >> 24);
        p[1] = static_cast<uint8_t>(v >> 16);
        p[2] = static_cast<uint8_t>(v >> 8);
        p[3] = static_cast<uint8_t>(v);
    }
}

uint16_t M68k::fetch16()
{
    const uint16_t w = static_cast<uint16_t>(readRaw(pc, 2));
    pc += 2;
    return w;
}

uint32_t M68k::fetch32()
{
    const uint32_t l = readRaw(pc, 4);
    pc += 4;
    return l;
}

void M68k::setNZ(uint32_t r, int sz)
{
    ccr &= ~(FN | FZ | FV | FC);
    r &= msk(sz);
    if (!r)
        ccr |= FZ;
    if (r & sbit(sz))
        ccr |= FN;
}

uint32_t M68k::doAdd(uint32_t s, uint32_t dv, int sz, int x)
{
    const uint32_t m = msk(sz), sb = sbit(sz);
    const uint64_t r64 = static_cast<uint64_t>(s & m) + (dv & m) + static_cast<uint32_t>(x);
    const uint32_t r = static_cast<uint32_t>(r64) & m;
    const bool c = (r64 >> (sz * 8)) & 1;
    const bool v = ((~(s ^ dv)) & (s ^ r) & sb) != 0;
    ccr &= ~(FN | FZ | FV | FC | FX);
    if (r & sb)
        ccr |= FN;
    if (!r)
        ccr |= FZ;
    if (v)
        ccr |= FV;
    if (c)
        ccr |= FC | FX;
    return r;
}

uint32_t M68k::doSub(uint32_t s, uint32_t dv, int sz, int x, bool setX)
{
    const uint32_t m = msk(sz), sb = sbit(sz);
    const uint32_t r = (dv - s - static_cast<uint32_t>(x)) & m;
    const bool c = (static_cast<uint64_t>(s & m) + static_cast<uint32_t>(x)) > (dv & m);
    const bool v = ((s ^ dv) & (dv ^ r) & sb) != 0;
    const int keepX = ccr & FX;
    ccr &= ~(FN | FZ | FV | FC | FX);
    if (r & sb)
        ccr |= FN;
    if (!r)
        ccr |= FZ;
    if (v)
        ccr |= FV;
    if (c)
        ccr |= FC;
    if (setX) {
        if (c)
            ccr |= FX;
    } else {
        ccr |= keepX;
    }
    return r;
}

bool M68k::cond(int cc) const
{
    const bool c = ccr & FC, v = ccr & FV, z = ccr & FZ, n = ccr & FN;
    switch (cc) {
    case 0: return true;
    case 1: return false;
    case 2: return !c && !z;
    case 3: return c || z;
    case 4: return !c;
    case 5: return c;
    case 6: return !z;
    case 7: return z;
    case 8: return !v;
    case 9: return v;
    case 10: return !n;
    case 11: return n;
    case 12: return n == v;
    case 13: return n != v;
    case 14: return !z && n == v;
    default: return z || n != v;
    }
}

M68k::EA M68k::decode(int mode, int reg, int sz)
{
    EA e{2, 0, reg};
    const auto indexed = [this](uint32_t base) {
        const uint16_t ext = fetch16();
        const int xr = (ext >> 12) & 7;
        int32_t xv = (ext & 0x8000) ? static_cast<int32_t>(a[xr]) : static_cast<int32_t>(d[xr]);
        if (!(ext & 0x800))
            xv = static_cast<int16_t>(xv);
        return base + static_cast<int8_t>(ext & 0xFF) + xv;
    };
    switch (mode) {
    case 0: e.kind = 0; return e;
    case 1: e.kind = 1; return e;
    case 2: e.addr = a[reg]; return e;
    case 3:
        e.addr = a[reg];
        a[reg] += (reg == 7 && sz == 1) ? 2 : sz;
        return e;
    case 4:
        a[reg] -= (reg == 7 && sz == 1) ? 2 : sz;
        e.addr = a[reg];
        return e;
    case 5: e.addr = a[reg] + static_cast<int16_t>(fetch16()); return e;
    case 6: e.addr = indexed(a[reg]); return e;
    case 7:
        switch (reg) {
        case 0: e.addr = static_cast<uint32_t>(static_cast<int16_t>(fetch16())); return e;
        case 1: e.addr = fetch32(); return e;
        case 2: {
            const uint32_t base = pc;
            e.addr = base + static_cast<int16_t>(fetch16());
            return e;
        }
        case 3: e.addr = indexed(pc); return e;
        case 4:
            e.kind = 3;
            m_imm = sz == 4 ? fetch32() : (fetch16() & msk(sz));
            return e;
        default: break;
        }
        break;
    default: break;
    }
    error = QStringLiteral("bad addressing mode %1/%2 at $%3").arg(mode).arg(reg).arg(pc, 6, 16, QLatin1Char('0'));
    halted = true;
    return e;
}

uint32_t M68k::eaRead(const EA &e, int sz)
{
    switch (e.kind) {
    case 0: return d[e.reg] & msk(sz);
    case 1: return a[e.reg] & msk(sz);
    case 3: return m_imm & msk(sz);
    default: return readRaw(e.addr, sz);
    }
}

void M68k::eaWrite(const EA &e, int sz, uint32_t v)
{
    v &= msk(sz);
    switch (e.kind) {
    case 0: d[e.reg] = (d[e.reg] & ~msk(sz)) | v; return;
    case 1: a[e.reg] = static_cast<uint32_t>(sext(v, sz)); return;
    case 3: return;
    default: write(e.addr, sz, v);
    }
}

void M68k::push32(uint32_t v)
{
    a[7] -= 4;
    write(a[7], 4, v);
}

uint32_t M68k::pop32()
{
    const uint32_t v = readRaw(a[7], 4);
    a[7] += 4;
    return v;
}

void M68k::illegal(uint16_t op)
{
    error = QStringLiteral("unsupported instruction %1 at $%2")
                .arg(op, 4, 16, QLatin1Char('0')).arg(pc - 2, 6, 16, QLatin1Char('0'));
    halted = true;
}

uint32_t M68k::shift(int type, bool left, uint32_t v, int cnt, int sz)
{
    const uint32_t m = msk(sz), sb = sbit(sz);
    bool c = false, x = ccr & FX, vf = false;
    v &= m;
    if (cnt == 0) {
        ccr &= ~(FN | FZ | FV | FC);
        if (type == 2 && x)
            ccr |= FC;
        if (!v)
            ccr |= FZ;
        if (v & sb)
            ccr |= FN;
        return v;
    }
    for (int i = 0; i < cnt; ++i) {
        if (left) {
            c = v & sb;
            switch (type) {
            case 0: {
                const uint32_t nv = (v << 1) & m;
                if ((nv & sb) != (v & sb))
                    vf = true;
                v = nv;
                x = c;
                break;
            }
            case 1: v = (v << 1) & m; x = c; break;
            case 2: v = ((v << 1) | (x ? 1u : 0u)) & m; x = c; break;
            default: v = ((v << 1) | (c ? 1u : 0u)) & m; break;
            }
        } else {
            c = v & 1;
            switch (type) {
            case 0: v = (v >> 1) | (v & sb); x = c; break;
            case 1: v >>= 1; x = c; break;
            case 2: v = (v >> 1) | (x ? sb : 0); x = c; break;
            default: v = (v >> 1) | (c ? sb : 0); break;
            }
        }
    }
    ccr &= ~(FN | FZ | FV | FC);
    if (type != 3) {
        ccr &= ~FX;
        if (x)
            ccr |= FX;
    }
    if (c)
        ccr |= FC;
    if (vf)
        ccr |= FV;
    if (!v)
        ccr |= FZ;
    if (v & sb)
        ccr |= FN;
    return v;
}

void M68k::step()
{
    const uint16_t op = fetch16();
    const int hi = op >> 12;
    const int dreg = (op >> 9) & 7, mode = (op >> 3) & 7, reg = op & 7;

    switch (hi) {
    case 0x0: {
        if ((op & 0x100) || (op & 0x0F00) == 0x0800) {
            // btst/bchg/bclr/bset, dynamic (Dn) or static (#imm)
            int bitn = (op & 0x100) ? static_cast<int>(d[dreg]) : (fetch16() & 0xFF);
            const int kind = (op >> 6) & 3;
            if (mode == 0) {
                bitn &= 31;
                uint32_t v = d[reg];
                if (v & (1u << bitn)) ccr &= ~FZ; else ccr |= FZ;
                if (kind == 1) v ^= 1u << bitn;
                else if (kind == 2) v &= ~(1u << bitn);
                else if (kind == 3) v |= 1u << bitn;
                d[reg] = v;
            } else {
                bitn &= 7;
                const EA e = decode(mode, reg, 1);
                uint32_t v = eaRead(e, 1);
                if (v & (1u << bitn)) ccr &= ~FZ; else ccr |= FZ;
                if (kind) {
                    if (kind == 1) v ^= 1u << bitn;
                    else if (kind == 2) v &= ~(1u << bitn);
                    else v |= 1u << bitn;
                    eaWrite(e, 1, v);
                }
            }
            return;
        }
        const int sz = kSizes[(op >> 6) & 3];
        const int kind = (op >> 9) & 7;   // 0 ori 1 andi 2 subi 3 addi 5 eori 6 cmpi
        if ((op & 0x3F) == 0x3C) {        // to CCR / SR
            const uint16_t imm = fetch16();
            if (kind == 0) ccr |= imm & 0x1F;
            else if (kind == 1) ccr &= imm & 0x1F;
            else if (kind == 5) ccr ^= imm & 0x1F;
            return;
        }
        if (!sz) { illegal(op); return; }
        const uint32_t imm = sz == 4 ? fetch32() : (fetch16() & msk(sz));
        const EA e = decode(mode, reg, sz);
        const uint32_t dv = eaRead(e, sz);
        uint32_t r;
        switch (kind) {
        case 0: r = dv | imm; setNZ(r, sz); eaWrite(e, sz, r); return;
        case 1: r = dv & imm; setNZ(r, sz); eaWrite(e, sz, r); return;
        case 2: r = doSub(imm, dv, sz, 0, true); eaWrite(e, sz, r); return;
        case 3: r = doAdd(imm, dv, sz, 0); eaWrite(e, sz, r); return;
        case 5: r = dv ^ imm; setNZ(r, sz); eaWrite(e, sz, r); return;
        case 6: doSub(imm, dv, sz, 0, false); return;
        default: illegal(op); return;
        }
    }
    case 0x1: case 0x2: case 0x3: {
        const int sz = hi == 1 ? 1 : hi == 3 ? 2 : 4;
        const EA s = decode(mode, reg, sz);
        const uint32_t v = eaRead(s, sz);
        const int dmode = (op >> 6) & 7;
        if (dmode == 1) { a[dreg] = static_cast<uint32_t>(sext(v, sz)); return; }   // movea
        const EA dst = decode(dmode, dreg, sz);
        setNZ(v, sz);
        eaWrite(dst, sz, v);
        return;
    }
    case 0x4: {
        if (op == 0x4E75) { pc = pop32(); return; }                                    // rts
        if (op == 0x4E71) return;                                                      // nop
        if (op == 0x4E77) { ccr = static_cast<uint16_t>(readRaw(a[7], 2) & 0x1F); a[7] += 2; pc = pop32(); return; }  // rtr
        if ((op & 0xFFF8) == 0x4E50) {                                                 // link
            const int16_t disp = static_cast<int16_t>(fetch16());
            push32(a[reg]);
            a[reg] = a[7];
            a[7] += disp;
            return;
        }
        if ((op & 0xFFF8) == 0x4E58) { a[7] = a[reg]; a[reg] = pop32(); return; }      // unlk
        if ((op & 0xFFC0) == 0x4E80) { const EA e = decode(mode, reg, 4); push32(pc); pc = e.addr; return; }   // jsr
        if ((op & 0xFFC0) == 0x4EC0) { const EA e = decode(mode, reg, 4); pc = e.addr; return; }                // jmp
        if ((op & 0xF1C0) == 0x41C0) { const EA e = decode(mode, reg, 4); a[dreg] = e.addr; return; }          // lea
        if ((op & 0xFFC0) == 0x4840 && mode != 0) { const EA e = decode(mode, reg, 4); push32(e.addr); return; } // pea
        if ((op & 0xFFF8) == 0x4840) { uint32_t v = d[reg]; v = (v >> 16) | (v << 16); d[reg] = v; setNZ(v, 4); return; }  // swap
        if ((op & 0xFFF8) == 0x4880) {                                                 // ext.w
            const uint32_t v = static_cast<uint16_t>(static_cast<int8_t>(d[reg]));
            d[reg] = (d[reg] & 0xFFFF0000u) | v;
            setNZ(v, 2);
            return;
        }
        if ((op & 0xFFF8) == 0x48C0) { const uint32_t v = static_cast<uint32_t>(static_cast<int16_t>(d[reg])); d[reg] = v; setNZ(v, 4); return; }  // ext.l
        if ((op & 0xFB80) == 0x4880) {                                                 // movem
            const int sz = (op & 0x40) ? 4 : 2;
            const uint16_t list = fetch16();
            if (op & 0x400) {                                                          // memory -> registers
                uint32_t addr;
                if (mode == 3) addr = a[reg];
                else addr = decode(mode, reg, sz).addr;
                for (int i = 0; i < 16; ++i) {
                    if (!(list & (1 << i)))
                        continue;
                    uint32_t v = readRaw(addr, sz);
                    if (sz == 2)
                        v = static_cast<uint32_t>(static_cast<int16_t>(v));
                    if (i < 8) d[i] = v; else a[i - 8] = v;
                    addr += sz;
                }
                if (mode == 3)
                    a[reg] = addr;
            } else if (mode == 4) {                                                    // predecrement: reversed list
                uint32_t addr = a[reg];
                for (int i = 0; i < 16; ++i) {
                    if (!(list & (1 << i)))
                        continue;
                    const int r = 15 - i;
                    addr -= sz;
                    write(addr, sz, r < 8 ? d[r] : a[r - 8]);
                }
                a[reg] = addr;
            } else {
                uint32_t addr = decode(mode, reg, sz).addr;
                for (int i = 0; i < 16; ++i) {
                    if (!(list & (1 << i)))
                        continue;
                    write(addr, sz, i < 8 ? d[i] : a[i - 8]);
                    addr += sz;
                }
            }
            return;
        }
        if ((op & 0xFFC0) == 0x44C0 || (op & 0xFFC0) == 0x46C0) {                     // move to ccr / sr
            const EA e = decode(mode, reg, 2);
            ccr = static_cast<uint16_t>(eaRead(e, 2) & 0x1F);
            return;
        }
        if ((op & 0xFFC0) == 0x40C0) { const EA e = decode(mode, reg, 2); eaWrite(e, 2, ccr); return; }   // move from sr
        if ((op & 0xFF00) == 0x4200) {                                                 // clr
            const int sz = kSizes[(op >> 6) & 3];
            const EA e = decode(mode, reg, sz);
            eaWrite(e, sz, 0);
            ccr = static_cast<uint16_t>((ccr & FX) | FZ);
            return;
        }
        if ((op & 0xFF00) == 0x4400) {                                                 // neg
            const int sz = kSizes[(op >> 6) & 3];
            const EA e = decode(mode, reg, sz);
            const uint32_t v = eaRead(e, sz);
            eaWrite(e, sz, doSub(v, 0, sz, 0, true));
            return;
        }
        if ((op & 0xFF00) == 0x4000) {                                                 // negx
            const int sz = kSizes[(op >> 6) & 3];
            const EA e = decode(mode, reg, sz);
            const uint32_t v = eaRead(e, sz);
            const int z = ccr & FZ;
            const uint32_t r = doSub(v, 0, sz, (ccr & FX) ? 1 : 0, true);
            if (r == 0 && z) ccr |= FZ; else if (r == 0) ccr &= ~FZ;
            eaWrite(e, sz, r);
            return;
        }
        if ((op & 0xFF00) == 0x4600) {                                                 // not
            const int sz = kSizes[(op >> 6) & 3];
            const EA e = decode(mode, reg, sz);
            const uint32_t v = ~eaRead(e, sz);
            setNZ(v, sz);
            eaWrite(e, sz, v);
            return;
        }
        if ((op & 0xFF00) == 0x4A00 && (op & 0xC0) != 0xC0) {                         // tst
            const int sz = kSizes[(op >> 6) & 3];
            const EA e = decode(mode, reg, sz);
            setNZ(eaRead(e, sz), sz);
            return;
        }
        if (op == 0x4AFC) { illegal(op); return; }                                     // illegal
        if ((op & 0xFFC0) == 0x4AC0) {                                                 // tas
            const EA e = decode(mode, reg, 1);
            const uint32_t v = eaRead(e, 1);
            setNZ(v, 1);
            eaWrite(e, 1, v | 0x80);
            return;
        }
        illegal(op);
        return;
    }
    case 0x5: {
        if ((op & 0xC0) == 0xC0) {
            const int cc = (op >> 8) & 15;
            if (mode == 1) {                                                           // dbcc
                const int16_t disp = static_cast<int16_t>(fetch16());
                if (!cond(cc)) {
                    const uint16_t w = static_cast<uint16_t>(d[reg] - 1);
                    d[reg] = (d[reg] & 0xFFFF0000u) | w;
                    if (w != 0xFFFF)
                        pc = pc - 2 + disp;
                }
                return;
            }
            const EA e = decode(mode, reg, 1);                                         // scc
            eaWrite(e, 1, cond(cc) ? 0xFF : 0);
            return;
        }
        const int sz = kSizes[(op >> 6) & 3];
        const uint32_t q = dreg ? dreg : 8;
        if (mode == 1) {                                                               // addq/subq to An
            if (op & 0x100) a[reg] -= q; else a[reg] += q;
            return;
        }
        const EA e = decode(mode, reg, sz);
        const uint32_t dv = eaRead(e, sz);
        eaWrite(e, sz, (op & 0x100) ? doSub(q, dv, sz, 0, true) : doAdd(q, dv, sz, 0));
        return;
    }
    case 0x6: {
        const int cc = (op >> 8) & 15;
        int32_t disp = static_cast<int8_t>(op & 0xFF);
        const uint32_t base = pc;
        if (disp == 0)
            disp = static_cast<int16_t>(fetch16());
        if (cc == 1) { push32(pc); pc = base + disp; return; }                         // bsr
        if (cond(cc))
            pc = base + disp;
        return;
    }
    case 0x7: {                                                                        // moveq
        const uint32_t v = static_cast<uint32_t>(static_cast<int8_t>(op & 0xFF));
        d[dreg] = v;
        setNZ(v, 4);
        return;
    }
    case 0x8: case 0x9: case 0xB: case 0xC: case 0xD: {
        const int opm = (op >> 6) & 7;
        if (hi == 0x8 && opm == 3) {                                                   // divu
            const EA e = decode(mode, reg, 2);
            const uint32_t s = eaRead(e, 2);
            if (!s) { illegal(op); return; }
            const uint32_t q = d[dreg] / s, r = d[dreg] % s;
            ccr &= ~(FN | FZ | FV | FC);
            if (q > 0xFFFF) { ccr |= FV; return; }
            d[dreg] = (r << 16) | q;
            if (!q) ccr |= FZ;
            if (q & 0x8000) ccr |= FN;
            return;
        }
        if (hi == 0x8 && opm == 7) {                                                   // divs
            const EA e = decode(mode, reg, 2);
            const int32_t s = static_cast<int16_t>(eaRead(e, 2));
            if (!s) { illegal(op); return; }
            const int32_t q = static_cast<int32_t>(d[dreg]) / s, r = static_cast<int32_t>(d[dreg]) % s;
            ccr &= ~(FN | FZ | FV | FC);
            if (q > 32767 || q < -32768) { ccr |= FV; return; }
            d[dreg] = (static_cast<uint32_t>(r & 0xFFFF) << 16) | (q & 0xFFFF);
            if (!q) ccr |= FZ;
            if (q & 0x8000) ccr |= FN;
            return;
        }
        if (hi == 0xC && opm == 3) {                                                   // mulu
            const EA e = decode(mode, reg, 2);
            const uint32_t r = (d[dreg] & 0xFFFF) * eaRead(e, 2);
            d[dreg] = r;
            setNZ(r, 4);
            return;
        }
        if (hi == 0xC && opm == 7) {                                                   // muls
            const EA e = decode(mode, reg, 2);
            const int32_t r = static_cast<int16_t>(d[dreg]) * static_cast<int16_t>(eaRead(e, 2));
            d[dreg] = static_cast<uint32_t>(r);
            setNZ(static_cast<uint32_t>(r), 4);
            return;
        }
        if (hi == 0xC && (op & 0x1F8) == 0x140) { std::swap(d[dreg], d[reg]); return; }  // exg dd
        if (hi == 0xC && (op & 0x1F8) == 0x148) { std::swap(a[dreg], a[reg]); return; }  // exg aa
        if (hi == 0xC && (op & 0x1F8) == 0x188) { std::swap(d[dreg], a[reg]); return; }  // exg da
        if ((hi == 0x9 || hi == 0xB || hi == 0xD) && (opm == 3 || opm == 7)) {         // suba / cmpa / adda
            const int sz = opm == 3 ? 2 : 4;
            const EA e = decode(mode, reg, sz);
            const uint32_t s = static_cast<uint32_t>(sext(eaRead(e, sz), sz));
            if (hi == 0x9) a[dreg] -= s;
            else if (hi == 0xD) a[dreg] += s;
            else doSub(s, a[dreg], 4, 0, false);
            return;
        }
        const int sz = kSizes[opm & 3];
        if ((hi == 0x9 || hi == 0xD) && (opm & 4) && mode <= 1) {                      // addx / subx
            const int x = (ccr & FX) ? 1 : 0, z = ccr & FZ;
            uint32_t r;
            if (mode == 0) {
                r = hi == 0xD ? doAdd(d[reg], d[dreg], sz, x) : doSub(d[reg], d[dreg], sz, x, true);
                d[dreg] = (d[dreg] & ~msk(sz)) | r;
            } else {
                a[reg] -= sz;
                a[dreg] -= sz;
                const uint32_t s = readRaw(a[reg], sz), dv = readRaw(a[dreg], sz);
                r = hi == 0xD ? doAdd(s, dv, sz, x) : doSub(s, dv, sz, x, true);
                write(a[dreg], sz, r);
            }
            if (r == 0 && z) ccr |= FZ; else if (r == 0) ccr &= ~FZ;
            return;
        }
        if (hi == 0xB && (opm & 4) && mode == 1) {                                     // cmpm
            const uint32_t s = readRaw(a[reg], sz);
            a[reg] += sz;
            const uint32_t dv = readRaw(a[dreg], sz);
            a[dreg] += sz;
            doSub(s, dv, sz, 0, false);
            return;
        }
        const EA e = decode(mode, reg, sz);
        if (!(opm & 4)) {                                                              // <ea> op Dn -> Dn
            const uint32_t s = eaRead(e, sz), dv = d[dreg] & msk(sz);
            uint32_t r;
            switch (hi) {
            case 0x8: r = dv | s; setNZ(r, sz); break;
            case 0x9: r = doSub(s, dv, sz, 0, true); break;
            case 0xB: doSub(s, dv, sz, 0, false); return;
            case 0xC: r = dv & s; setNZ(r, sz); break;
            default: r = doAdd(s, dv, sz, 0); break;
            }
            d[dreg] = (d[dreg] & ~msk(sz)) | (r & msk(sz));
        } else {                                                                       // Dn op <ea> -> <ea>
            const uint32_t s = d[dreg] & msk(sz), dv = eaRead(e, sz);
            uint32_t r;
            switch (hi) {
            case 0x8: r = dv | s; setNZ(r, sz); break;
            case 0x9: r = doSub(s, dv, sz, 0, true); break;
            case 0xB: r = dv ^ s; setNZ(r, sz); break;                                 // eor
            case 0xC: r = dv & s; setNZ(r, sz); break;
            default: r = doAdd(s, dv, sz, 0); break;
            }
            eaWrite(e, sz, r);
        }
        return;
    }
    case 0xE: {
        if ((op & 0xC0) == 0xC0) {                                                     // memory shift, one bit, word
            const int type = (op >> 9) & 3;
            const bool left = (op >> 8) & 1;
            const EA e = decode(mode, reg, 2);
            eaWrite(e, 2, shift(type, left, eaRead(e, 2), 1, 2));
            return;
        }
        const int sz = kSizes[(op >> 6) & 3];
        const int type = (op >> 3) & 3;
        const bool left = (op >> 8) & 1;
        const int cnt = (op & 0x20) ? static_cast<int>(d[dreg] & 63) : (dreg ? dreg : 8);
        const uint32_t r = shift(type, left, d[reg], cnt, sz);
        d[reg] = (d[reg] & ~msk(sz)) | r;
        return;
    }
    case 0xA:
        if (onLineA && onLineA(op))
            return;
        illegal(op);
        return;
    default:
        illegal(op);   // line F
    }
}

bool M68k::call(uint32_t address, const uint32_t *dregs, const uint32_t *aregs, uint64_t maxInstructions)
{
    for (int i = 0; i < 8; ++i) {
        d[i] = dregs ? dregs[i] : 0;
        a[i] = aregs ? aregs[i] : 0;
    }
    a[7] = m_stackTop & ~3u;
    push32(kReturnMagic);
    pc = address;
    halted = false;
    error.clear();
    uint64_t count = 0;
    while (pc != kReturnMagic && !halted) {
        if (pc == watchAddress && onWatch)
            onWatch();
        step();
        if (++count > maxInstructions) {
            error = QStringLiteral("the routine at $%1 did not return").arg(address, 6, 16, QLatin1Char('0'));
            halted = true;
        }
    }
    return !halted;
}

// ---- hunk loader ----

namespace {

uint32_t be32(const QByteArray &b, qsizetype at)
{
    if (at < 0 || at + 4 > b.size())
        return 0;
    const auto *p = reinterpret_cast<const uint8_t *>(b.constData() + at);
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

} // namespace

bool AmigaHunkLoader::isHunkExecutable(const QByteArray &data)
{
    return data.size() >= 24 && be32(data, 0) == 0x3F3;
}

bool AmigaHunkLoader::load(const QByteArray &b, M68k &cpu, uint32_t base, QVector<AmigaHunk> &hunks, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (!isHunkExecutable(b))
        return fail(QStringLiteral("not an AmigaOS hunk executable"));
    qsizetype p = 4;
    while (be32(b, p)) {                       // resident library names (none in practice)
        p += 4 + static_cast<qsizetype>(be32(b, p)) * 4;
        if (p >= b.size())
            return fail(QStringLiteral("truncated header"));
    }
    p += 4;
    const uint32_t tableSize = be32(b, p);
    const uint32_t first = be32(b, p + 4), last = be32(b, p + 8);
    p += 12;
    if (last < first || last - first + 1 != tableSize || tableSize > 4096)
        return fail(QStringLiteral("invalid hunk table"));
    hunks.clear();
    uint32_t at = base;
    for (uint32_t i = 0; i < tableSize; ++i) {
        uint32_t size = be32(b, p);
        p += 4;
        if ((size >> 30) == 3)                 // extended memory flags
            p += 4;
        AmigaHunk h;
        h.address = at;
        h.size = (size & 0x3FFFFFFF) * 4;
        hunks.append(h);
        at = (at + h.size + 8 + 7) & ~7u;
    }
    if (at > cpu.memorySize() - 0x10000)
        return fail(QStringLiteral("the executable is too large"));

    int index = 0;
    while (p + 4 <= b.size() && index < hunks.size()) {
        const uint32_t type = be32(b, p) & 0x3FFFFFFF;
        p += 4;
        AmigaHunk &h = hunks[index];
        switch (type) {
        case 0x3E9:                            // HUNK_CODE
        case 0x3EA: {                          // HUNK_DATA
            const qsizetype n = static_cast<qsizetype>(be32(b, p)) * 4;
            p += 4;
            if (p + n > b.size())
                return fail(QStringLiteral("truncated hunk %1").arg(index));
            h.type = type == 0x3E9 ? AmigaHunk::Code : AmigaHunk::Data;
            std::copy(b.constData() + p, b.constData() + p + std::min<qsizetype>(n, h.size), cpu.memory().begin() + h.address);
            p += n;
            break;
        }
        case 0x3EB:                            // HUNK_BSS
            h.type = AmigaHunk::Bss;
            p += 4;
            break;
        case 0x3EC: {                          // HUNK_RELOC32
            for (;;) {
                const uint32_t count = be32(b, p);
                p += 4;
                if (!count)
                    break;
                const uint32_t target = be32(b, p);
                p += 4;
                if (target >= static_cast<uint32_t>(hunks.size()))
                    return fail(QStringLiteral("bad relocation in hunk %1").arg(index));
                for (uint32_t k = 0; k < count; ++k, p += 4) {
                    const uint32_t off = be32(b, p);
                    if (off + 4 > h.size)
                        return fail(QStringLiteral("bad relocation offset in hunk %1").arg(index));
                    cpu.write(h.address + off, 4, cpu.read(h.address + off, 4) + hunks.at(target).address);
                }
                if (p > b.size())
                    return fail(QStringLiteral("truncated relocations"));
            }
            break;
        }
        case 0x3F0:                            // HUNK_SYMBOL
            for (;;) {
                const uint32_t n = be32(b, p);
                p += 4;
                if (!n)
                    break;
                p += static_cast<qsizetype>(n) * 4 + 4;
                if (p > b.size())
                    return fail(QStringLiteral("truncated symbols"));
            }
            break;
        case 0x3F1:                            // HUNK_DEBUG
            p += 4 + static_cast<qsizetype>(be32(b, p)) * 4;
            break;
        case 0x3F2:                            // HUNK_END
            ++index;
            break;
        default:
            return fail(QStringLiteral("unsupported hunk type $%1").arg(type, 0, 16));
        }
    }
    if (index < hunks.size())
        return fail(QStringLiteral("truncated executable"));
    return true;
}
