#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <cstdint>
#include <functional>
#include <vector>

// A small 68000 interpreter (user-mode integer instructions) used to run the
// original music players of the Amiga executables. Memory is flat, 24-bit;
// the custom chip registers ($DFFxxx) go to the callbacks. Ported from the
// interpreter of the Pool of Radiance C port (src/host/m68k.c).
class M68k
{
public:
    explicit M68k(uint32_t memorySize = 0x200000);

    std::vector<uint8_t> &memory() { return m_mem; }
    uint32_t memorySize() const { return static_cast<uint32_t>(m_mem.size()); }

    uint32_t read(uint32_t address, int size);
    void write(uint32_t address, int size, uint32_t value);

    // custom chip register $DFF000 + reg (word aligned)
    std::function<void(uint32_t reg, uint32_t value)> chipWrite;
    std::function<uint32_t(uint32_t reg)> chipRead;

    // Runs the routine at address until it returns, with d and a registers
    // (a7 is set to the stack top). Returns false when it halted or ran more
    // than maxInstructions instructions.
    bool call(uint32_t address, const uint32_t *d = nullptr, const uint32_t *a = nullptr,
              uint64_t maxInstructions = 5000000);
    void setStackTop(uint32_t top) { m_stackTop = top; }

    // line-A opcodes ($Axxx, used for fake library jump tables): return true
    // when handled (the handler sets the registers and pc), else the CPU halts
    std::function<bool(uint16_t opcode)> onLineA;

    // called before the instruction at watchAddress runs
    uint32_t watchAddress = 0xFFFFFFFFu;
    std::function<void()> onWatch;

    uint32_t d[8] = {};
    uint32_t a[8] = {};
    uint32_t pc = 0;
    uint16_t ccr = 0;
    bool halted = false;
    QString error;

private:
    struct EA { int kind; uint32_t addr; int reg; };   // kind 0 = Dn, 1 = An, 2 = memory, 3 = immediate

    uint32_t readRaw(uint32_t address, int size);
    uint16_t fetch16();
    uint32_t fetch32();
    void setNZ(uint32_t r, int sz);
    uint32_t doAdd(uint32_t s, uint32_t dv, int sz, int x);
    uint32_t doSub(uint32_t s, uint32_t dv, int sz, int x, bool setX);
    bool cond(int cc) const;
    EA decode(int mode, int reg, int sz);
    uint32_t eaRead(const EA &e, int sz);
    void eaWrite(const EA &e, int sz, uint32_t v);
    void push32(uint32_t v);
    uint32_t pop32();
    void illegal(uint16_t op);
    uint32_t shift(int type, bool left, uint32_t v, int cnt, int sz);
    void step();

    std::vector<uint8_t> m_mem;
    uint32_t m_imm = 0;
    uint32_t m_stackTop = 0;
};

// An AmigaOS hunk executable loaded into the memory of an M68k with its
// relocations applied (HUNK_CODE, DATA, BSS, RELOC32, SYMBOL, DEBUG, END).
struct AmigaHunk
{
    enum Type { Code, Data, Bss };
    Type type = Code;
    uint32_t address = 0;
    uint32_t size = 0;
};

class AmigaHunkLoader
{
public:
    static bool isHunkExecutable(const QByteArray &data);
    // Loads every hunk from address base (each aligned to 8 bytes). Returns
    // false with an error when the file is not a supported executable or does
    // not fit.
    static bool load(const QByteArray &data, M68k &cpu, uint32_t base, QVector<AmigaHunk> &hunks, QString *error);
};
