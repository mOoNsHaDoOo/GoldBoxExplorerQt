#include "parse/ecl_basic.h"

#include "parse/ecl_basic_symbols.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QHash>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Port of ecl_codec.py (Program.decode) and ecl_basic.py (disassemble).
// The structure follows the Python closely so that the two can be compared
// line by line; any change of output there must be mirrored here.

namespace {


struct EclError
{
    std::string message;
};

std::string format(const char *fmt, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (n < 0)
        return std::string();
    if (n < static_cast<int>(sizeof(buffer)))
        return std::string(buffer, static_cast<size_t>(n));
    std::string big(static_cast<size_t>(n) + 1, '\0');
    va_start(args, fmt);
    std::vsnprintf(&big[0], big.size(), fmt, args);
    va_end(args);
    big.resize(static_cast<size_t>(n));
    return big;
}

[[noreturn]] void fail(const std::string &message)
{
    throw EclError{message};
}

// ---------------------------------------------------------------------------
// Dialects: the command set of each game's ECL interpreter.

const char *const kCokNames[] = {
    "EXIT", "GOTO", "GOSUB", "COMPARE", "ADD", "SUB", "DIV", "MUL",
    "RANDOM", "SAVE", "LOAD_CHARACTER", "LOAD_MONSTER", "APPROACH", "APPROACH_STEP", "PICTURE", "INPUT_NUMBER",
    "INPUT_STRING", "PRINT", "PRINTCLEAR", "RETURN", "COMPARE_AND", "VERTICAL_MENU", "IF_EQ", "IF_NE",
    "IF_LT", "IF_GT", "IF_LE", "IF_GE", "CLEAR_MONSTERS", "PARTY_STRENGTH", "PARTY_SURVEY", nullptr,
    "NEWECL", "LOAD_FILES", "PARTY_HAS_CLASS", "SURPRISE", "COMBAT", "ON_GOTO", "ON_GOSUB", "TREASURE",
    "ROB", "ENCOUNTER_MENU", "GET_TABLE", "HORIZONTAL_MENU", "PARLAY", "SPECIAL", "DAMAGE", "AND",
    "OR", "CLEAR_PICTURE", "FIND_ITEM", "NEW_LINE", "ADVANCE_TIME", "SET_TABLE", "ADD_NPC", "LOAD_FILES2",
    "PARTY_MENU", "WHO", "DELAY", "FIND_SPELL", "NOP_3C", "REDRAW_SCREEN", "REMOVE_CHARACTER", "HAS_EFFECT",
    "REMOVE_ITEMS", "EXPERIENCE",
};

// read_operands count per opcode (NOPS); -1 = variable length, -2 = unknown.
const int kCokNops[] = {
    0, 1, 1, 2, 3, 3, 3, 3, 2, 2, 1, 3, 3, 0, 1, 2,
    2, 1, 1, 0, 4, -1, 0, 0, 0, 0, 0, 0, 0, 1, 6, -2,
    1, 3, 2, 4, 0, -1, -1, 8, 3, 0x0E, 3, -1, 6, 1, 5, 3,
    3, 0, 1, 0, 2, 3, 2, 3, 1, 1, 0, 3, 0, 0, 0, 1,
    1, 2,
};

// ecl_skip_command (h061.c): operands skipped after a false IF.
const int kCokSkip[] = {
    0, 1, 1, 2, 3, 3, 3, 3, 2, 2, 1, 3, 3, 0, 1, 2, 2, 1, 1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 1, 6, 2,
    1, 3, 2, 4, 0, 0, 0, 8, 3, 0x0E, 3, 0, 6, 1, 5, 3, 3, 0, 1, 0, 1, 3, 1, 3, 1, 1, 0, 3, 1, 0, 0, 1,
    1, 2,
};

// Death Knights of Krynn (Amiga): dispatcher of h015_ecl_loop.c, operand
// reads of the handlers, ecl_skip_command of h064_ecl_vm.c.
const char *const kDkkNames[] = {
    "EXIT", "GOTO", "GOSUB", "COMPARE", "ADD", "SUB", "DIV", "MUL",
    "RANDOM", "SAVE", "LOAD_CHARACTER", "LOAD_MONSTER", "SETUP_MONSTER", "APPROACH", "PICTURE", "INPUT_NUMBER",
    "INPUT_STRING", "PRINT", "PRINTCLEAR", "RETURN", "COMPARE_AND", "VERTICAL_MENU", "IF_EQ", "IF_NE",
    "IF_LT", "IF_GT", "IF_LE", "IF_GE", "CLEAR_MONSTERS", "CONTINUE", "CHECK_PARTY", "JOURNAL",
    "NEWECL", "LOAD_FILES", "GET_YES_NO", "ENCOUNTER_EXIT", "COMBAT", "ON_GOTO", "ON_GOSUB", "TREASURE",
    "ROB", "SAVE_CHARACTER", "GET_TABLE", "HORIZONTAL_MENU", "SOUND", "CALL", "DAMAGE", "AND",
    "OR", "HMENU", "FIND_ITEM", "PRINT_RETURN", "CLOCK", "SET_TABLE", "ADD_NPC", "LOAD_PIECES",
    "PROGRAM", "WHO", "DELAY", "SPELL", "STORE", "CLEAR_BOX", "DUMP", "FIND_SPECIAL",
    "DESTROY_ITEMS", "ADD_EXPERIENCE",
};

const int kDkkNops[] = {
    0, 1, 1, 2, 3, 3, 3, 3, 2, 2, 1, 3, 4, 0, 1, 2,
    2, 1, 1, 0, 4, -1, 0, 0, 0, 0, 0, 0, 0, 0, 6, 1,
    1, 3, 0, 0, 0, -1, -1, -1, 3, 0, 3, -1, 1, 1, 5, 3,
    3, -1, 1, 0, 2, 3, 2, 3, 1, 1, 0, 3, 1, 0, 0, 1,
    1, 2,
};

const int kDkkSkip[] = {
    0, 1, 1, 2, 3, 3, 3, 3, 2, 2, 1, 3, 3, 0, 1, 2, 2, 1, 1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 1,
    1, 3, 0, 0, 0, 0, 0, 0, 3, 0, 3, 0, 1, 1, 5, 3, 3, 0, 1, 0, 1, 3, 1, 3, 1, 1, 0, 3, 1, 0, 0, 1,
    1, 2,
};

// Pool of Radiance (Amiga): ecl_dispatch_opcode, the handlers and
// ecl_skip_command of the port (mod16.c, mod04.c).  $1F and $3E.. are not
// commands (the interpreter would loop on them).
const char *const kPorNames[] = {
    "EXIT", "GOTO", "GOSUB", "COMPARE", "ADD", "SUB", "DIV", "MUL",
    "RANDOM", "SAVE", "LOAD_CHARACTER", "LOAD_MONSTER", "SETUP_MONSTER", "APPROACH", "PICTURE", "INPUT_NUMBER",
    "INPUT_STRING", "PRINT", "PRINTCLEAR", "RETURN", "COMPARE_AND", "VERTICAL_MENU", "IF_EQ", "IF_NE",
    "IF_LT", "IF_GT", "IF_LE", "IF_GE", "CLEAR_MONSTERS", "PARTY_STRENGTH", "CHECK_PARTY", nullptr,
    "NEWECL", "LOAD_FILES", "PARTY_SURPRISE", "SURPRISE", "COMBAT", "ON_GOTO", "ON_GOSUB", "TREASURE",
    "ROB", "ENCOUNTER_MENU", "GET_TABLE", "HORIZONTAL_MENU", "PARLAY", "CALL", "DAMAGE", "AND",
    "OR", "SPRITE_OFF", "FIND_ITEM", "PRINT_RETURN", "CLOCK", "SET_TABLE", "ADD_NPC", "LOAD_PIECES",
    "PROGRAM", "WHO", "DELAY", "SPELL", "PRINT_MEMSTRING", "CLEAR_BOX",
};

const int kPorNops[] = {
    0, 1, 1, 2, 3, 3, 3, 3, 2, 2, 1, 3, 3, 0, 1, 2,
    2, 1, 1, 0, 4, -1, 0, 0, 0, 0, 0, 0, 0, 1, 6, -2,
    1, 3, 2, 4, 0, -1, -1, 8, 3, 0x0E, 3, -1, 6, 1, 5, 3,
    3, 0, 1, 0, 1, 3, 2, 3, 1, 1, 0, 3, 1, 0,
};

const int kPorSkip[] = {
    0, 1, 1, 2, 3, 3, 3, 3, 2, 2, 1, 3, 2, 0, 1, 2, 2, 1, 1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 1, 6, 2,
    1, 3, 2, 4, 0, 0, 0, 8, 3, 0x0E, 3, 0, 6, 1, 5, 3, 3, 0, 1, 0, 1, 3, 1, 3, 1, 1, 0, 3, 1, 0,
};

struct Dialect
{
    EclBasicDialect id;
    const char *title;          // header comment
    int base;                   // logical address of the first program byte
    int varOffset;              // subtracted from address operands
    int programEnd;             // last address of the program area
    const char *const *names;
    const int *nops;
    const int *skip;
    int opcodes;                // entries of names / nops / skip
    const int *ends;            // commands after which execution does not fall through
    int endCount;
    bool cokSymbols;            // variable names of ecl_basic_symbols.cpp
    bool strictSpecial;         // unknown CALL words are an error (CoK)
};

const int kCokEnds[] = {0x00, 0x01, 0x13, 0x20};
const int kDkkEnds[] = {0x00, 0x01, 0x13, 0x20, 0x23};

const Dialect kDialects[] = {
    {EclBasicDialect::ChampionsOfKrynn, "Champions of Krynn campaign control", 0x3500, 0x4B00, 0x52FF,
     kCokNames, kCokNops, kCokSkip, int(sizeof(kCokNops) / sizeof(int)), kCokEnds, 4, true, true},
    {EclBasicDialect::DeathKnightsOfKrynn, "Death Knights of Krynn", 0x3500, 0x4B00, 0x52FF,
     kDkkNames, kDkkNops, kDkkSkip, int(sizeof(kDkkNops) / sizeof(int)), kDkkEnds, 5, false, false},
    {EclBasicDialect::PoolOfRadiance, "Pool of Radiance", 0x9900, 0, 0xB6FF,
     kPorNames, kPorNops, kPorSkip, int(sizeof(kPorNops) / sizeof(int)), kCokEnds, 4, false, false},
};

const Dialect &dialectOf(EclBasicDialect id)
{
    for (const Dialect &d : kDialects)
        if (d.id == id)
            return d;
    return kDialects[0];
}

const char *opcodeName(const Dialect &d, int op)
{
    if (op < 0 || op >= d.opcodes)
        return nullptr;
    return d.names[op];
}

int operandCount(const Dialect &d, int op)
{
    if (op < 0 || op >= d.opcodes)
        return -2;
    return d.nops[op];
}

// VARIABLE: operands of the first batch of a variable-length command.
int variableFirstBatch(const Dialect &d, int op)
{
    switch (op) {
    case 0x15: return 3;
    case 0x25: return 2;
    case 0x26: return 2;
    case 0x2B: return 2;
    default: break;
    }
    if (d.id == EclBasicDialect::DeathKnightsOfKrynn) {
        if (op == 0x27)
            return 4;        // TREASURE: the item ids follow
        if (op == 0x31)
            return 2;        // HMENU
    }
    return 0;
}

bool isEnd(const Dialect &d, int op)
{
    for (int i = 0; i < d.endCount; ++i)
        if (d.ends[i] == op)
            return true;
    return false;
}
bool isIf(int op) { return op >= 0x16 && op <= 0x1B; }

// COUNTED: opcode -> index of the implicit count operand (-1: none).
int countedIndex(const Dialect &d, int op)
{
    const int first = variableFirstBatch(d, op);
    return first > 0 ? first - 1 : -1;
}

const char *specialName(int word)
{
    switch (word) {
    case 0x2E10: return "REDRAW_VIEW_IF_CHANGED";
    case 0x6803: return "ANIMATE_SEQUENCE";
    case 0xB203: return "SOUND";
    case 0xC018: return "CHECK_WALL_AHEAD";
    case 0xC01E: return "STEP_FORWARD";
    default: return nullptr;
    }
}

const char *const kRoles[5] = {"step", "after_step", "camp", "after_camp", "first"};

// read_string_operand: 4 six-bit characters every 3 bytes; 0 skipped;
// codes < $20 get $40 added.
std::string unpack6(const std::vector<int> &raw)
{
    std::string s;
    size_t count = 0;
    int phase = 1;
    int cur = 0;
    int prev = 0;
    size_t i = 0;
    auto put = [&s](int ch) {
        if (ch)
            s.push_back(static_cast<char>(ch > 0x1F ? ch : ch + 0x40));
    };
    while (count < raw.size()) {
        if (phase < 4) {
            prev = cur;
            cur = raw[i];
            ++i;
            ++count;
        }
        int ch;
        if (phase == 1)
            ch = cur >> 2;
        else if (phase == 2)
            ch = ((((prev << 6) & 0xFF) + (cur >> 2)) & 0xFF) >> 2;
        else if (phase == 3)
            ch = ((((prev << 4) & 0xFF) + (cur >> 4)) & 0xFF) >> 2;
        else
            ch = cur & 0x3F;
        phase = phase < 4 ? phase + 1 : 1;
        put(ch);
    }
    if (phase == 4)
        put(cur & 0x3F);
    return s;
}

struct Operand
{
    int desc = 0;
    int lo = 0;
    int hi = 0;
    std::string text;

    int addr = 0;   // word minus the variable offset of the dialect

    int word() const { return (hi << 8) | lo; }
    int address() const { return addr; }
};

struct Command
{
    int at = 0;
    int op = 0;
    std::vector<Operand> ops;
    int next = 0;
    std::vector<int> targets;
};

class Program
{
public:
    Program(const Dialect &d, const QByteArray &data) : m_d(d), m_data(data) {}

    int b(int addr) const
    {
        const int i = addr - m_d.base;
        if (i < 0 || i >= m_data.size())
            fail(format("address $%04X outside the program", addr));
        return static_cast<quint8>(m_data.at(i));
    }

    // read_operands(n) with pc on the opcode: operands, pc of the next opcode.
    std::vector<Operand> operands(int pc, int n, int *next) const
    {
        std::vector<Operand> ops;
        for (int k = 0; k < n; ++k) {
            Operand o;
            o.desc = b(pc + 1);
            o.lo = b(pc + 2);
            pc += 2;
            const int kind = o.desc > 0x7F ? o.desc - 0x7C : o.desc;
            if (kind == 1 || kind == 2 || kind == 3 || kind == 5) {
                pc += 1;
                o.hi = b(pc);
            } else if (kind == 4) {
                std::vector<int> raw;
                raw.reserve(static_cast<size_t>(o.lo));
                for (int j = 0; j < o.lo; ++j)
                    raw.push_back(b(pc + 1 + j));
                o.text = unpack6(raw);
                pc += o.lo;
            }
            o.addr = (o.word() - m_d.varOffset) & 0xFFFF;
            ops.push_back(std::move(o));
        }
        *next = pc + 1;
        return ops;
    }

    Command command(int at) const
    {
        Command cmd;
        cmd.at = at;
        cmd.op = b(at);
        const int op = cmd.op;
        const int first = variableFirstBatch(m_d, op);
        if (first > 0) {
            int next = 0;
            std::vector<Operand> ops = operands(at, first, &next);
            const Operand cnt = ops.back();
            if (cnt.desc != 0 && cnt.desc != 2)
                fail(format("$%04X: count of %s from a variable", at, opcodeName(m_d, op)));
            const int n = cnt.desc == 0 ? cnt.lo : cnt.word();
            std::vector<Operand> rest = operands(next - 1, n, &next);
            if (op == 0x25 || op == 0x26) {
                for (const Operand &o : rest)
                    cmd.targets.push_back(o.address());
            }
            ops.insert(ops.end(), rest.begin(), rest.end());
            cmd.ops = std::move(ops);
            cmd.next = next;
        } else {
            const int n = operandCount(m_d, op);
            if (n < 0)
                fail(format("$%04X: unknown opcode $%02X", at, op));
            if (n > 0) {
                cmd.ops = operands(at, n, &cmd.next);
            } else {
                cmd.next = at + 1;
            }
            if (op == 0x01 || op == 0x02)
                cmd.targets.push_back(cmd.ops[0].address());
        }
        return cmd;
    }

    // PC after ecl_skip_command with PC on the opcode at 'at'.
    int skipTarget(int at) const
    {
        const int op = b(at);
        if (op >= 1 && op < m_d.opcodes && m_d.skip[op]) {
            int next = 0;
            operands(at, m_d.skip[op], &next);
            return next;
        }
        return at + 1;
    }

    void decode(std::vector<Command> &header, std::map<int, Command> &code) const
    {
        int pc = m_d.base;
        for (int i = 0; i < 5; ++i) {
            if (b(pc) != 0x01)
                fail(format("header: $%04X is not a GOTO", pc));
            Command cmd = command(pc);
            pc = cmd.next;
            header.push_back(std::move(cmd));
        }
        std::vector<int> todo;
        for (const Command &c : header)
            todo.push_back(c.targets[0]);
        std::vector<int> deferred;
        walk(todo, code, deferred);
        // Speculative paths (after an ON_GOTO, see walk): kept only when
        // everything they reach decodes.
        while (!deferred.empty()) {
            const int start = deferred.back();
            deferred.pop_back();
            if (code.find(start) != code.end())
                continue;
            std::map<int, Command> trial = code;
            std::vector<int> more;
            try {
                walk({start}, trial, more);
            } catch (const EclError &) {
                continue;
            }
            // a path over bytes that a command reads as a table is data
            const std::set<int> refs = dataReferences(trial);
            bool overlaps = false;
            for (const auto &entry : trial) {
                if (code.find(entry.first) != code.end())
                    continue;
                const auto it = refs.lower_bound(entry.first);
                if (it != refs.end() && *it < entry.second.next) {
                    overlaps = true;
                    break;
                }
            }
            if (overlaps)
                continue;
            code = std::move(trial);
            deferred.insert(deferred.end(), more.begin(), more.end());
        }
    }

    // Program addresses that commands read or write as data (tables).
    std::set<int> dataReferences(const std::map<int, Command> &code) const
    {
        std::set<int> refs;
        const int end = m_d.base + m_data.size();
        for (const auto &entry : code) {
            const Command &cmd = entry.second;
            if (cmd.op == 0x2D)
                continue;
            for (size_t k = 0; k < cmd.ops.size(); ++k) {
                const Operand &o = cmd.ops[k];
                const bool jump = ((cmd.op == 0x01 || cmd.op == 0x02) && k == 0)
                                  || ((cmd.op == 0x25 || cmd.op == 0x26) && k >= 2);
                if ((o.desc == 1 || o.desc == 3) && !jump && o.address() >= m_d.base
                    && o.address() < end)
                    refs.insert(o.address());
            }
        }
        return refs;
    }

    // Decodes every command reachable from todo into code.  Outside Champions
    // of Krynn the command after an ON_GOTO is only reached when the index is
    // out of range, and scripts often put data there: that address goes to
    // deferred instead.
    void walk(std::vector<int> todo, std::map<int, Command> &code, std::vector<int> &deferred) const
    {
        while (!todo.empty()) {
            int a = todo.back();
            todo.pop_back();
            int prev = -1;
            while (code.find(a) == code.end()) {
                Command c;
                try {
                    c = command(a);
                } catch (const EclError &error) {
                    if (prev < 0)
                        throw;
                    fail(error.message + format(" (after the command at $%04X)", prev));
                }
                prev = a;
                todo.insert(todo.end(), c.targets.begin(), c.targets.end());
                if (isIf(c.op))
                    todo.push_back(skipTarget(c.next));
                const int op = c.op;
                const int next = c.next;
                code.emplace(a, std::move(c));
                if (isEnd(m_d, op))
                    break;
                if (op == 0x25 && m_d.id != EclBasicDialect::ChampionsOfKrynn) {
                    deferred.push_back(next);
                    break;
                }
                a = next;
            }
        }
    }

private:
    const Dialect &m_d;
    QByteArray m_data;
};

char regionOf(const Dialect &d, int addr)
{
    if (d.id == EclBasicDialect::PoolOfRadiance) {
        // ecl_classify_var_address: game, area, temporaries, program
        if (addr >= 0x4900 && addr <= 0x4CFF)
            return 'G';
        if (addr >= 0x6B00 && addr <= 0x6EFF)
            return 'A';
        if (addr >= 0x9700 && addr <= 0x98FF)
            return 'T';
        if (addr >= 0x9900 && addr <= 0xB6FF)
            return 'P';
        return 'R';
    }
    if (addr <= 0x3FF)
        return 'A';
    if (addr >= 0x3100 && addr <= 0x34FF)
        return 'B';
    if (addr >= 0x2F00 && addr <= 0x30FF)
        return 'C';
    if (addr >= 0x3500 && addr <= 0x52FF)
        return 'P';
    return 'R';
}

bool isJump(int op, int k)
{
    return ((op == 0x01 || op == 0x02) && k == 0) || ((op == 0x25 || op == 0x26) && k >= 2);
}

const QHash<QString, const EclBasicSymbol *> &symbolsByKey()
{
    static const QHash<QString, const EclBasicSymbol *> table = [] {
        QHash<QString, const EclBasicSymbol *> result;
        int count = 0;
        const EclBasicSymbol *symbols = eclBasicSymbols(&count);
        for (int i = 0; i < count; ++i)
            result.insert(QString::fromLatin1(symbols[i].key), &symbols[i]);
        return result;
    }();
    return table;
}

using Gap = std::pair<int, int>;

std::string disassemble(const Dialect &d, const QByteArray &image, const std::string &fileTag,
                        int blockId)
{
    const Program p(d, image);
    std::vector<Command> header;
    std::map<int, Command> code;
    p.decode(header, code);

    std::vector<int> order;
    for (const auto &entry : code)
        order.push_back(entry.first);
    const int end = d.base + image.size();

    std::vector<Gap> gaps;
    int pos = header.back().next;
    {
        std::vector<int> walk = order;
        walk.push_back(end);
        for (int a : walk) {
            if (a > pos)
                gaps.emplace_back(pos, a);
            const auto it = code.find(a);
            if (it != code.end())
                pos = std::max(pos, it->second.next);
        }
    }

    auto gapOf = [&gaps](int x) -> Gap {
        for (const Gap &g : gaps) {
            if (g.first <= x && x < g.second)
                return g;
        }
        fail(format("P:$%04X is not inside the data of the program", x));
    };

    std::set<Gap> used;
    for (int a : order) {
        const Command &cmd = code.at(a);
        if (cmd.op == 0x2D)
            continue;
        for (size_t k = 0; k < cmd.ops.size(); ++k) {
            const Operand &o = cmd.ops[k];
            if ((o.desc == 1 || o.desc == 3) && regionOf(d, o.address()) == 'P'
                && !isJump(cmd.op, static_cast<int>(k))) {
                used.insert(gapOf(o.address()));
            } else if (o.desc == 0x81 && regionOf(d, o.address()) == 'P') {
                fail(format("$%04X: text variable in the program area", a));
            }
        }
    }
    std::map<Gap, std::string> tname;
    {
        int i = 0;
        for (const Gap &g : used)
            tname[g] = format("table_%d", ++i);
    }

    std::vector<int> entryAt;
    for (const Command &h : header)
        entryAt.push_back(h.targets[0]);

    std::map<int, std::string> kind;
    for (int a : order) {
        const Command &cmd = code.at(a);
        for (int t : cmd.targets) {
            const char *k = (cmd.op == 0x02 || cmd.op == 0x26) ? "sub" : "loc";
            const auto it = kind.find(t);
            if (it == kind.end() || it->second != "sub")
                kind[t] = k;
        }
    }
    std::map<int, std::string> lname;
    for (int i = 0; i < 5; ++i)
        lname.emplace(entryAt[static_cast<size_t>(i)], kRoles[i]);
    int countSub = 0;
    int countLoc = 0;
    for (int a : order) {
        const auto it = kind.find(a);
        if (it != kind.end() && lname.find(a) == lname.end()) {
            const int n = it->second == "sub" ? ++countSub : ++countLoc;
            lname[a] = format("%s_%d", it->second.c_str(), n);
        }
    }

    const auto &symbols = symbolsByKey();
    auto var = [&symbols, &d](const Operand &o, const char *prefix) {
        const char reg = regionOf(d, o.address());
        const std::string key = format("%c:%04X", reg, o.address());
        const auto it = d.cokSymbols ? symbols.constFind(QString::fromLatin1(key.c_str()))
                                     : symbols.constEnd();
        const std::string name = it != symbols.constEnd()
                                     ? std::string(it.value()->name)
                                     : format("%c:$%04X", reg, o.address());
        return std::string(prefix) + "[" + name + "]";
    };

    auto label = [&lname](int address) -> std::string {
        const auto it = lname.find(address);
        if (it == lname.end())
            fail(format("no label for $%04X", address));
        return it->second;
    };

    auto arg = [&](const Command &cmd, int k, const Operand &o) -> std::string {
        if (isJump(cmd.op, k))
            return label(o.address());
        if (cmd.op == 0x2D && k == 0 && o.desc == 1) {
            const char *name = d.strictSpecial ? specialName(o.word()) : nullptr;
            if (!name && !d.strictSpecial)
                return format("$%04X", o.word());
            if (!name)
                fail(format("$%04X: unknown SPECIAL $%04X", cmd.at, o.word()));
            return name;
        }
        if (o.desc == 0)
            return format("%d", o.lo);
        if (o.desc == 2)
            return format("#%d", o.word());
        if (o.desc == 1 || o.desc == 3) {
            if (regionOf(d, o.address()) == 'P') {
                if (o.desc != 1)
                    fail(format("$%04X: descriptor 3 operand in the program area", cmd.at));
                const Gap g = gapOf(o.address());
                const auto it = tname.find(g);
                if (it == tname.end())
                    fail(format("$%04X: program address $%04X without a table", cmd.at, o.address()));
                const int off = o.address() - g.first;
                return it->second + (off ? format("+%d", off) : std::string());
            }
            return var(o, o.desc == 3 ? "v3" : "");
        }
        if (o.desc == 0x80)
            return "\"" + o.text + "\"";
        if (o.desc == 0x81)
            return var(o, "str");
        fail(format("$%04X: operand with descriptor $%02X", cmd.at, o.desc));
    };

    std::vector<std::string> out;
    auto tableLines = [&](const Gap &g) {
        const QByteArray raw = image.mid(g.first - d.base, g.second - g.first);
        out.emplace_back();
        out.push_back(format("table %-20s ; %d bytes", tname.at(g).c_str(), int(raw.size())));
        for (int i = 0; i < raw.size(); i += 16) {
            std::string line = "    db ";
            for (int j = i; j < raw.size() && j < i + 16; ++j) {
                if (j > i)
                    line += ", ";
                line += format("$%02X", static_cast<quint8>(raw.at(j)));
            }
            out.push_back(line);
        }
    };

    out.push_back(format("; %s block %d - ECL Basic (%s)", fileTag.c_str(), blockId, d.title));
    out.push_back(format("program %s %d", fileTag.c_str(), blockId));
    out.emplace_back();
    for (int i = 0; i < 5; ++i)
        out.push_back(format("entry %-11s %s", kRoles[i],
                             lname.at(entryAt[static_cast<size_t>(i)]).c_str()));

    int dropped = 0;
    std::set<Gap> placed;
    int prevOp = -1;
    // (address, 0 = command / 1 = gap): commands sort before gaps on a tie,
    // as ('cmd' < 'gap') does in the Python.
    std::vector<std::pair<int, int>> items;
    for (int a : order)
        items.emplace_back(a, 0);
    for (const Gap &g : gaps)
        items.emplace_back(g.first, 1);
    std::sort(items.begin(), items.end());

    for (const auto &item : items) {
        const int a = item.first;
        if (item.second == 1) {
            Gap g;
            for (const Gap &x : gaps) {
                if (x.first == a) {
                    g = x;
                    break;
                }
            }
            const bool isUsed = used.count(g) != 0;
            if (isUsed && prevOp >= 0 && isEnd(d, prevOp)) {
                tableLines(g);
                placed.insert(g);
            } else if (!isUsed) {
                dropped += g.second - g.first;
            }
            continue;
        }
        const Command &cmd = code.at(a);
        const auto labelIt = lname.find(a);
        if (labelIt != lname.end()) {
            out.emplace_back();
            out.push_back(labelIt->second + ":");
        }
        std::vector<std::string> args;
        for (size_t k = 0; k < cmd.ops.size(); ++k)
            args.push_back(arg(cmd, static_cast<int>(k), cmd.ops[k]));
        const int counted = countedIndex(d, cmd.op);
        if (counted >= 0 && counted < static_cast<int>(args.size()))
            args.erase(args.begin() + counted);
        std::string joined;
        for (size_t k = 0; k < args.size(); ++k) {
            if (k)
                joined += ", ";
            joined += args[k];
        }
        std::string line = format("    %-14s %s", opcodeName(d, cmd.op), joined.c_str());
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        out.push_back(line);
        prevOp = cmd.op;
    }
    for (const Gap &g : used) {
        if (!placed.count(g))
            tableLines(g);
    }
    if (dropped) {
        out.emplace_back();
        out.push_back(format("; %d bytes of the original block that nothing reads (padding) were left out",
                             dropped));
    }

    std::string text;
    for (size_t i = 0; i < out.size(); ++i) {
        if (i)
            text += '\n';
        text += out[i];
    }
    text += '\n';
    return text;
}

} // namespace

EclBasicResult eclBasicDisassemble(const QByteArray &programImage,
                                   const QString &fileTag, int blockId,
                                   EclBasicDialect dialect)
{
    EclBasicResult result;
    result.dialect = dialect;
    try {
        const std::string text = disassemble(dialectOf(dialect), programImage,
                                             fileTag.toStdString(), blockId);
        result.text = QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
        result.ok = true;
    } catch (const EclError &error) {
        result.error = QString::fromStdString(error.message);
    } catch (const std::exception &error) {
        result.error = QString::fromLocal8Bit(error.what());
    }
    return result;
}

QByteArray eclBasicProgramOfBlock(const QByteArray &block, EclBasicDialect dialect)
{
    switch (dialect) {
    case EclBasicDialect::DeathKnightsOfKrynn:
        // load_ecl_program copies size - $102 bytes after the first 2 into a
        // $1DFF-byte buffer, but dax_load_block reports the size plus $100
        return block.mid(2, 0x1DFF);
    case EclBasicDialect::PoolOfRadiance:
        // ecl_load_script_block: size - 2 bytes into a $1E00-byte buffer
        return block.mid(2, 0x1E00);
    case EclBasicDialect::ChampionsOfKrynn:
        break;
    }
    return block.mid(2);
}

EclBasicResult eclBasicDisassembleBlock(const QByteArray &block,
                                        const QString &fileTag, int blockId,
                                        EclBasicDialect dialect)
{
    if (block.size() < 2) {
        EclBasicResult result;
        result.dialect = dialect;
        result.error = QStringLiteral("block of %1 bytes is too short").arg(block.size());
        return result;
    }
    return eclBasicDisassemble(eclBasicProgramOfBlock(block, dialect), fileTag, blockId, dialect);
}

EclBasicDialect eclBasicDialectForFile(const QString &path)
{
    const QFileInfo info(path);
    const QString name = info.fileName().toUpper();
    if (name == QStringLiteral("ECL.DAX"))
        return EclBasicDialect::PoolOfRadiance;
    if (name == QStringLiteral("ECL3.DAX"))
        return EclBasicDialect::DeathKnightsOfKrynn;
    const QDir dir = info.absoluteDir();
    const QStringList entries = dir.entryList(QDir::Files);
    for (const QString &entry : entries) {
        const QString upper = entry.toUpper();
        if (upper == QStringLiteral("DKK.CFG") || upper == QStringLiteral("GLOBALS.DAA")
            || upper == QStringLiteral("MON1WIZ.DAX"))
            return EclBasicDialect::DeathKnightsOfKrynn;
        if (upper == QStringLiteral("POOL.CFG"))
            return EclBasicDialect::PoolOfRadiance;
    }
    return EclBasicDialect::ChampionsOfKrynn;
}

EclBasicResult eclBasicDisassembleBlockAuto(const QByteArray &block,
                                            const QString &fileTag, int blockId,
                                            EclBasicDialect preferred)
{
    EclBasicResult first = eclBasicDisassembleBlock(block, fileTag, blockId, preferred);
    if (first.ok)
        return first;
    for (const Dialect &d : kDialects) {
        if (d.id == preferred)
            continue;
        EclBasicResult other = eclBasicDisassembleBlock(block, fileTag, blockId, d.id);
        if (other.ok)
            return other;
    }
    return first;
}

QString eclBasicDialectName(EclBasicDialect dialect)
{
    switch (dialect) {
    case EclBasicDialect::ChampionsOfKrynn: return QStringLiteral("Champions of Krynn");
    case EclBasicDialect::DeathKnightsOfKrynn: return QStringLiteral("Death Knights of Krynn");
    case EclBasicDialect::PoolOfRadiance: return QStringLiteral("Pool of Radiance");
    }
    return QString();
}

QString eclBasicFileTag(const QString &path)
{
    return QFileInfo(path).completeBaseName().toUpper();
}

QString eclBasicSymbolDescription(const QString &name)
{
    static const QHash<QString, QString> byName = [] {
        QHash<QString, QString> result;
        int count = 0;
        const EclBasicSymbol *symbols = eclBasicSymbols(&count);
        for (int i = 0; i < count; ++i)
            result.insert(QString::fromLatin1(symbols[i].name),
                          QStringLiteral("%1 (%2)")
                              .arg(QString::fromLatin1(symbols[i].description),
                                   QString::fromLatin1(symbols[i].key)));
        return result;
    }();
    return byName.value(name);
}
