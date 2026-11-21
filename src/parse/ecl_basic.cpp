#include "parse/ecl_basic.h"

#include "parse/ecl_basic_symbols.h"

#include <QFileInfo>
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

constexpr int kBase = 0x3500;
constexpr int kVarOffset = 0x4B00;

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

const char *opcodeName(int op)
{
    static const char *const names[] = {
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
    if (op < 0 || op >= static_cast<int>(sizeof(names) / sizeof(names[0])))
        return nullptr;
    return names[op];
}

// read_operands count per opcode (NOPS); -1 = variable length, -2 = unknown.
int operandCount(int op)
{
    static const int nops[] = {
        0, 1, 1, 2, 3, 3, 3, 3, 2, 2, 1, 3, 3, 0, 1, 2,
        2, 1, 1, 0, 4, -1, 0, 0, 0, 0, 0, 0, 0, 1, 6, -2,
        1, 3, 2, 4, 0, -1, -1, 8, 3, 0x0E, 3, -1, 6, 1, 5, 3,
        3, 0, 1, 0, 2, 3, 2, 3, 1, 1, 0, 3, 0, 0, 0, 1,
        1, 2,
    };
    if (op < 0 || op >= static_cast<int>(sizeof(nops) / sizeof(nops[0])))
        return -2;
    return nops[op];
}

// VARIABLE: operands of the first batch of a variable-length command.
int variableFirstBatch(int op)
{
    switch (op) {
    case 0x15: return 3;
    case 0x25: return 2;
    case 0x26: return 2;
    case 0x2B: return 2;
    default: return 0;
    }
}

// ecl_skip_command (h061.c): operands skipped after a false IF.
const int kSkipNops[] = {
    0, 1, 1, 2, 3, 3, 3, 3, 2, 2, 1, 3, 3, 0, 1, 2, 2, 1, 1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 1, 6, 2,
    1, 3, 2, 4, 0, 0, 0, 8, 3, 0x0E, 3, 0, 6, 1, 5, 3, 3, 0, 1, 0, 1, 3, 1, 3, 1, 1, 0, 3, 1, 0, 0, 1,
    1, 2,
};

bool isEnd(int op) { return op == 0x00 || op == 0x01 || op == 0x13 || op == 0x20; }
bool isIf(int op) { return op >= 0x16 && op <= 0x1B; }

// COUNTED: opcode -> index of the implicit count operand (-1: none).
int countedIndex(int op)
{
    switch (op) {
    case 0x15: return 2;
    case 0x2B: return 1;
    case 0x25: return 1;
    case 0x26: return 1;
    default: return -1;
    }
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

    int word() const { return (hi << 8) | lo; }
    int address() const { return (word() - kVarOffset) & 0xFFFF; }
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
    explicit Program(const QByteArray &data) : m_data(data) {}

    int b(int addr) const
    {
        const int i = addr - kBase;
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
        const int first = variableFirstBatch(op);
        if (first > 0) {
            int next = 0;
            std::vector<Operand> ops = operands(at, first, &next);
            const Operand cnt = ops.back();
            if (cnt.desc != 0 && cnt.desc != 2)
                fail(format("$%04X: count of %s from a variable", at, opcodeName(op)));
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
            const int n = operandCount(op);
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
        if (op >= 1 && op <= 0x41 && kSkipNops[op]) {
            int next = 0;
            operands(at, kSkipNops[op], &next);
            return next;
        }
        return at + 1;
    }

    void decode(std::vector<Command> &header, std::map<int, Command> &code) const
    {
        int pc = kBase;
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
        while (!todo.empty()) {
            int a = todo.back();
            todo.pop_back();
            while (code.find(a) == code.end()) {
                Command c = command(a);
                todo.insert(todo.end(), c.targets.begin(), c.targets.end());
                if (isIf(c.op))
                    todo.push_back(skipTarget(c.next));
                const int op = c.op;
                const int next = c.next;
                code.emplace(a, std::move(c));
                if (isEnd(op))
                    break;
                a = next;
            }
        }
    }

private:
    QByteArray m_data;
};

char regionOf(int addr)
{
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

std::string disassemble(const QByteArray &image, const std::string &fileTag, int blockId)
{
    const Program p(image);
    std::vector<Command> header;
    std::map<int, Command> code;
    p.decode(header, code);

    std::vector<int> order;
    for (const auto &entry : code)
        order.push_back(entry.first);
    const int end = kBase + image.size();

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
            if ((o.desc == 1 || o.desc == 3) && regionOf(o.address()) == 'P'
                && !isJump(cmd.op, static_cast<int>(k))) {
                used.insert(gapOf(o.address()));
            } else if (o.desc == 0x81 && regionOf(o.address()) == 'P') {
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
    auto var = [&symbols](const Operand &o, const char *prefix) {
        const char reg = regionOf(o.address());
        const std::string key = format("%c:%04X", reg, o.address());
        const auto it = symbols.constFind(QString::fromLatin1(key.c_str()));
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
            const char *name = specialName(o.word());
            if (!name)
                fail(format("$%04X: unknown SPECIAL $%04X", cmd.at, o.word()));
            return name;
        }
        if (o.desc == 0)
            return format("%d", o.lo);
        if (o.desc == 2)
            return format("#%d", o.word());
        if (o.desc == 1 || o.desc == 3) {
            if (regionOf(o.address()) == 'P') {
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
        const QByteArray raw = image.mid(g.first - kBase, g.second - g.first);
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

    out.push_back(format("; %s block %d - ECL Basic (Champions of Krynn campaign control)",
                         fileTag.c_str(), blockId));
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
            if (isUsed && prevOp >= 0 && isEnd(prevOp)) {
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
        const int counted = countedIndex(cmd.op);
        if (counted >= 0 && counted < static_cast<int>(args.size()))
            args.erase(args.begin() + counted);
        std::string joined;
        for (size_t k = 0; k < args.size(); ++k) {
            if (k)
                joined += ", ";
            joined += args[k];
        }
        std::string line = format("    %-14s %s", opcodeName(cmd.op), joined.c_str());
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
                                   const QString &fileTag, int blockId)
{
    EclBasicResult result;
    try {
        const std::string text = disassemble(programImage, fileTag.toStdString(), blockId);
        result.text = QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
        result.ok = true;
    } catch (const EclError &error) {
        result.error = QString::fromStdString(error.message);
    } catch (const std::exception &error) {
        result.error = QString::fromLocal8Bit(error.what());
    }
    return result;
}

QByteArray eclBasicProgramOfBlock(const QByteArray &block)
{
    return block.mid(2);
}

EclBasicResult eclBasicDisassembleBlock(const QByteArray &block,
                                        const QString &fileTag, int blockId)
{
    if (block.size() < 2) {
        EclBasicResult result;
        result.error = QStringLiteral("block of %1 bytes is too short").arg(block.size());
        return result;
    }
    return eclBasicDisassemble(eclBasicProgramOfBlock(block), fileTag, blockId);
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
