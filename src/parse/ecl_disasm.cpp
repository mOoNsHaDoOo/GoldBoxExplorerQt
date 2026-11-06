#include "parse/ecl_disasm.h"

#include <QMap>
#include <QQueue>
#include <QSet>
#include <QStringList>

namespace {

struct CommandInfo
{
    const char *name;
    int size;
};

const QMap<int, CommandInfo> &commands()
{
    static const QMap<int, CommandInfo> table = {
        {0x00, {"EXIT", 0}},
        {0x01, {"GOTO", 1}},
        {0x02, {"GOSUB", 1}},
        {0x03, {"COMPARE", 2}},
        {0x04, {"ADD", 3}},
        {0x05, {"SUBTRACT", 3}},
        {0x06, {"DIVIDE", 3}},
        {0x07, {"MULTIPLY", 3}},
        {0x08, {"RANDOM", 2}},
        {0x09, {"SAVE", 2}},
        {0x0A, {"LOAD CHARACTER", 1}},
        {0x0B, {"LOAD MONSTER", 3}},
        {0x0C, {"SETUP MONSTER", 3}},
        {0x0D, {"APPROACH", 0}},
        {0x0E, {"PICTURE", 1}},
        {0x0F, {"INPUT NUMBER", 2}},
        {0x10, {"INPUT STRING", 2}},
        {0x11, {"PRINT", 1}},
        {0x12, {"PRINTCLEAR", 1}},
        {0x13, {"RETURN", 0}},
        {0x14, {"COMPARE AND", 4}},
        {0x15, {"VERTICAL MENU", 3}},
        {0x16, {"IF =", 0}},
        {0x17, {"IF <>", 0}},
        {0x18, {"IF <", 0}},
        {0x19, {"IF >", 0}},
        {0x1A, {"IF <=", 0}},
        {0x1B, {"IF >=", 0}},
        {0x1C, {"CLEARMONSTERS", 0}},
        {0x1D, {"PARTYSTRENGTH", 1}},
        {0x1E, {"CHECKPARTY", 6}},
        {0x1F, {"notsure 0x1f", 2}},
        {0x20, {"NEWECL", 1}},
        {0x21, {"LOAD FILES", 3}},
        {0x22, {"PARTY SURPRISE", 2}},
        {0x23, {"SURPRISE", 4}},
        {0x24, {"COMBAT", 0}},
        {0x25, {"ON GOTO", 0}},
        {0x26, {"ON GOSUB", 0}},
        {0x27, {"TREASURE", 8}},
        {0x28, {"ROB", 3}},
        {0x29, {"ENCOUNTER MENU", 14}},
        {0x2A, {"GETTABLE", 3}},
        {0x2B, {"HORIZONTAL MENU", 2}},
        {0x2C, {"PARLAY", 6}},
        {0x2D, {"CALL", 1}},
        {0x2E, {"DAMAGE", 5}},
        {0x2F, {"AND", 3}},
        {0x30, {"OR", 3}},
        {0x31, {"SPRITE OFF", 0}},
        {0x32, {"FIND ITEM", 1}},
        {0x33, {"PRINT RETURN", 0}},
        {0x34, {"ECL CLOCK", 2}},
        {0x35, {"SAVE TABLE", 3}},
        {0x36, {"ADD NPC", 2}},
        {0x37, {"LOAD PIECES", 3}},
        {0x38, {"PROGRAM", 1}},
        {0x39, {"WHO", 1}},
        {0x3A, {"DELAY", 0}},
        {0x3B, {"SPELL", 3}},
        {0x3C, {"PROTECTION", 1}},
        {0x3D, {"CLEAR BOX", 0}},
    };
    return table;
}

struct Operand
{
    int code = 0;
    int low = 0;
    int high = 0;
    int word = 0;
    bool wordSet = false;
    QString string;
};

class EclDisassembler
{
public:
    EclDisassembler(const QByteArray &blockData, bool pool)
        : m_data(blockData.mid(2))
        , m_memStart(pool ? 0x9900 : 0x8000)
    {
    }

    EclDisasmResult run()
    {
        EclDisasmResult result;
        if (m_data.isEmpty()) {
            result.error = QStringLiteral("Empty ECL block.");
            return result;
        }

        m_offset = m_memStart;
        int entries[5] = {0, 0, 0, 0, 0};
        for (int i = 0; i < 5; ++i) {
            const QVector<Operand> operands = loadOperands(1);
            entries[i] = operands.isEmpty() ? 0 : operands.first().word;
        }
        for (int entry : entries)
            addAddress(entry, QStringLiteral("StartUp"));

        while (!m_todo.isEmpty()) {
            const int address = m_todo.dequeue();
            if (!validAddress(address) || m_done.contains(address))
                continue;
            decodeAddress(address);
        }

        int previousEnd = -1;
        for (auto it = m_lines.cbegin(); it != m_lines.cend(); ++it) {
            if (previousEnd >= 0 && it.key() > previousEnd) {
                EclDisasmLine blank;
                blank.address = it.key();
                result.lines.append(blank);
                result.listing += QLatin1Char('\n');
            }
            EclDisasmLine line;
            line.address = it.key();
            line.text = it.value();
            line.annotation = m_annotations.value(it.key());
            result.lines.append(line);

            result.listing += line.text;
            if (!line.annotation.isEmpty())
                result.listing += QStringLiteral("    ; ") + line.annotation;
            result.listing += QLatin1Char('\n');
            previousEnd = it.key() + m_lengths.value(it.key(), 1);
        }

        result.valid = true;
        return result;
    }

private:
    quint8 byteAt(int address) const
    {
        const int index = (address - m_memStart) & 0xFFFF;
        if (index < 0 || index >= m_data.size())
            return 0xFF;
        return static_cast<quint8>(m_data.at(index));
    }

    bool validAddress(int address) const
    {
        const int index = (address - m_memStart) & 0xFFFF;
        return index >= 0 && index < m_data.size();
    }

    void addAddress(int address, const QString &text)
    {
        if (address == 0 || !validAddress(address))
            return;
        if (m_done.contains(address) || m_todo.contains(address))
            return;
        m_todo.enqueue(address);
        if (!text.isEmpty()) {
            if (m_annotations.contains(address))
                m_annotations[address] += QStringLiteral(", ") + text;
            else
                m_annotations[address] = text;
        }
    }

    void addLine(int address, const QString &text, int length)
    {
        if (m_done.contains(address))
            return;
        m_done.insert(address);
        m_lines.insert(address, text);
        m_lengths.insert(address, qMax(1, length));
    }

    QString area1Name(int address) const
    {
        return QStringLiteral("area1.word_%1")
            .arg(address & 0xFFFF, 3, 16, QLatin1Char('0'));
    }

    QString area2Name(int address) const
    {
        return QStringLiteral("area2.word_%1")
            .arg(address & 0xFFFF, 3, 16, QLatin1Char('0'));
    }

    QString playerValue(int loc) const
    {
        loc -= 0x6B00;
        if (loc == 0x18) return QStringLiteral("<PLAYER.offset_0x14_b>");
        if (loc == 0x9B) return QStringLiteral("<PLAYER.offset_0x6E_b>");
        if (loc == 0xA0) return QStringLiteral("<PLAYER.offset_0x73_b>");
        if (loc >= 0xA5 && loc <= 0xAC)
            return QStringLiteral("<PLAYER.offset_0x76[%1]_b>").arg(loc - 0xA4);
        if (loc == 0xB8) return QStringLiteral("<PLAYER.offset_0x84_b>");
        if (loc == 0xBB) return QStringLiteral("<PLAYER.offset_0x88_w>");
        if (loc == 0xBD) return QStringLiteral("<PLAYER.offset_0x8A_w>");
        if (loc == 0xBF) return QStringLiteral("<PLAYER.offset_0x8C_w>");
        if (loc == 0xC1) return QStringLiteral("<PLAYER.offset_0x8E_w>");
        if (loc == 0xC3) return QStringLiteral("<PLAYER.offset_0x90_w>");
        if (loc == 0xD8) return QStringLiteral("<PLAYER.offset_0xA0_b>");
        if (loc == 0xF7) return QStringLiteral("<PLAYER.offset_0xB8_w>");
        if (loc == 0xF9) return QStringLiteral("<PLAYER.offset_0xBA_b>");
        if (loc == 0x100)
            return QStringLiteral("<selected player in_combat (true 1, false 0x80, player not found 0)>");
        if (loc == 0x10C)
            return QStringLiteral("<selected player (our team && quick fight 0x80, enemy team 0x81, else 0)>");
        if (loc == 0x10D) return QStringLiteral("<0>");
        if (loc == 0x11B) return QStringLiteral("<PLAYER.offset_0x11C_b>");
        if (loc == 0x2B1 || loc == 0x2B4) return QStringLiteral("<PLAYER index?>");
        if (loc == 0x2CF) return QStringLiteral("<selected player charisma (scaled 0-60)>");
        if (loc == 0x312) return QStringLiteral("<byte_1C6D4>");
        if (loc == 0x33E) return QStringLiteral("<dword_1BD37.field_0x67C>");
        return {};
    }

    QString getMemoryValue(int loc) const
    {
        if (loc >= 0x4900 && loc <= 0x4CFF)
            return area1Name(0x6E00 + (loc * 2));
        if (loc >= 0x6B00 && loc <= 0x6FFF) {
            const QString player = playerValue(loc);
            return player.isEmpty() ? area2Name(0x2A00 + (loc * 2)) : player;
        }
        if (loc >= 0x9700 && loc <= 0x98FF)
            return QStringLiteral("<stru_1B2CA.word_%1>")
                .arg(((loc * 2) + 0x0C00) & 0xFFFF, 4, 16, QLatin1Char('0'));
        if (loc >= 0x9900 && loc <= 0xB6FF)
            return QStringLiteral("<ecl byte %1>%2")
                .arg((loc + m_memStart) & 0xFFFF, 4, 16, QLatin1Char('0'))
                .arg(byteAt(loc));
        if (loc < 0xC04B) {
            switch (loc) {
            case 0x00B1: return QStringLiteral("<word_1D918>");
            case 0x00FB: return QStringLiteral("<word_1D914>");
            case 0x00FC: return QStringLiteral("<word_1D916>");
            case 0x033D: return QStringLiteral("<mapDirection>");
            case 0x035F: return QStringLiteral("0");
            default: break;
            }
        } else {
            switch (loc - 0xC04B) {
            case 0: return QStringLiteral("<mapPosX>");
            case 1: return QStringLiteral("<mapPosY>");
            case 2: return QStringLiteral("<gbl.mapDirection / 2>");
            case 3: return QStringLiteral("<mapWallType>");
            case 4: return QStringLiteral("<mapWallRoof>");
            case 0x0E: return QStringLiteral("0");
            default: break;
            }
        }
        return QStringLiteral("<BAD LOCATION 0x%1>").arg(loc, 0, 16);
    }

    QString writeStringToMemory(int loc) const
    {
        if (loc >= 0x4900 && loc <= 0x4CFF)
            return QStringLiteral("<string starting at %1>").arg(area1Name(0x6E00 + (loc * 2)));
        if (loc >= 0x6B00 && loc <= 0x6FFF) {
            if (loc == 0x6B00)
                return QStringLiteral("<slected player Name>");
            return QStringLiteral("<string starting at %1>").arg(area2Name(0x2A00 + (loc * 2)));
        }
        if (loc >= 0x9700 && loc <= 0x98FF)
            return QStringLiteral("<string starting at stru_1B2CA.word_%1>")
                .arg(((loc * 2) + 0x0C00) & 0xFFFF, 4, 16, QLatin1Char('0'));
        if (loc >= 0x9900 && loc <= 0xB6FF)
            return QStringLiteral("<string starting at ecl byte %1>")
                .arg((loc + 0x9900) & 0xFFFF);
        return QStringLiteral("</user/bin>");
    }

    QString alterCharacter(const QString &value, int location) const
    {
        const int loc = location - 0x6B00;
        if (loc >= 0x20 && loc <= 0x70)
            return QStringLiteral("<selected player spell> = %1").arg(value);
        if (loc == 0xB8)
            return QStringLiteral("<selected player Morale> =  %1 [if value > 0xB2 then value -= 0x32]").arg(value);
        if (loc == 0xBB)
            return QStringLiteral("<selected player field_88 (Copper?)> = %1").arg(value);
        if (loc == 0xBD)
            return QStringLiteral("<selected player field_8A (Electrum?)> = %1").arg(value);
        if (loc == 0xBF)
            return QStringLiteral("<selected player field_8C (Silver?)> = %1").arg(value);
        if (loc == 0xC1)
            return QStringLiteral("<selected player field_8E (Gold> = %1").arg(value);
        if (loc == 0xC3)
            return QStringLiteral("<selected player field_90 (Platinum?)> = %1").arg(value);
        if (loc == 0xF7)
            return QStringLiteral("<selected player field_B8> = %1").arg(value);
        if (loc == 0xF9)
            return QStringLiteral("<selected player field_13E> = %1").arg(value);
        if (loc == 0x100)
            return QStringLiteral("<selected player in_combat> = false [if value '%1' >= 0x80, also if value == 0x87 the health_status = stoned]").arg(value);
        if (loc == 0x10C)
            return QStringLiteral("<selected player CombatTeam  & QuickFight> = %1 [0 - Ours & Not Quick, 0x80 - Ours & Quick, 0x81 - Enemy & Quick]").arg(value);
        if (loc == 0x80)
            return QStringLiteral("<selected player CombatTeam = 0 & QuickFight = 1>");
        if (loc == 0x81)
            return QStringLiteral("<selected player CombatTeam = 1 & QuickFight = 1>");
        if (loc == 0x312)
            return QStringLiteral("<game area> = %1").arg(value);
        if (loc == 0x322)
            return QStringLiteral("<if value > 0x80 set WALLDEF 1 value & 0x7F> value: %1").arg(value);
        if (loc == 0x324)
            return QStringLiteral("<if value > 0x80 set WALLDEF 2 value & 0x7F> value: %1").arg(value);
        if (loc == 0x326)
            return QStringLiteral("<if value > 0x80 set WALLDEF 3 value & 0x7F> value: %1").arg(value);
        return {};
    }

    QString setMemoryValue(int loc, const QString &value) const
    {
        if (loc >= 0x4900 && loc <= 0x4CFF) {
            QString extra;
            if ((loc - 0x4900) == 0x0FD || (loc - 0x4B00) == 0x0FE)
                extra = QStringLiteral(", <byte_1EE94> = true");
            return QStringLiteral("<%1> = %2%3").arg(area1Name(0x6E00 + (loc * 2)), value, extra);
        }
        if (loc >= 0x6B00 && loc <= 0x6FFF) {
            const QString text = QStringLiteral("<%1> = %2")
                                     .arg(area2Name(0x2A00 + (loc * 2)), value);
            const QString alter = alterCharacter(value, loc);
            return alter.isEmpty() ? text : text + QStringLiteral(", ") + alter;
        }
        if (loc >= 0x9700 && loc <= 0x98FF)
            return QStringLiteral("<stru_1B2CA.word_%1> = %2")
                .arg(((loc * 2) + 0xD200) & 0xFFFF, 4, 16, QLatin1Char('0'))
                .arg(value);
        if (loc >= 0x9900 && loc <= 0xB6FF)
            return QStringLiteral("<ecl byte %1> = %2")
                .arg((loc + 0x9900) & 0xFFFF)
                .arg(value);
        if (loc < 0xBF68) {
            switch (loc) {
            case 0x00FB:
            case 0x00FC:
            case 0x00B1:
                return QStringLiteral("nop");
            case 0x03DE: return QStringLiteral("<word_1EE76> = %1").arg(value);
            case 0x00B8: return QStringLiteral("<word_1EE78> = %1").arg(value);
            case 0x00B9: return QStringLiteral("<word_1EE7A> = %1").arg(value);
            default: break;
            }
        } else {
            switch (loc - 0xBF68) {
            case 0xE3: return QStringLiteral("<mapPosX> = %1").arg(value);
            case 0xE4: return QStringLiteral("<mapPosY> = %1").arg(value);
            case 0xE5: return QStringLiteral("<mapDirection> = mapped(%1, [0,2,4,6,0,2,4,6])").arg(value);
            case 0xF1:
            case 0xF7: return QStringLiteral("<byte_1EE91> = true");
            default: break;
            }
        }
        return QStringLiteral("<loc 0x%1> = %2")
            .arg(loc, 4, 16, QLatin1Char('0'))
            .arg(value);
    }

    QString operandValue(const Operand &operand) const
    {
        switch (operand.code) {
        case 0x00:
            return QString::number(operand.low);
        case 0x01:
        case 0x03:
        case 0x80:
            return operand.wordSet ? getMemoryValue(operand.word)
                                   : QStringLiteral("<not loaded>");
        case 0x02:
            return QString::number(operand.word);
        default:
            return QStringLiteral("<code 0x%1>")
                .arg(operand.code, 2, 16, QLatin1Char('0'));
        }
    }

    QString decompressString(const QByteArray &data) const
    {
        QString result;
        int state = 1;
        quint32 lastByte = 0;
        auto inflate = [](quint32 value) -> QChar {
            if (value <= 0x1F)
                value += 0x40;
            return QChar(static_cast<ushort>(value));
        };
        for (char c : data) {
            const quint32 thisByte = static_cast<quint8>(c);
            switch (state) {
            case 1: {
                const quint32 value = (thisByte >> 2) & 0x3F;
                if (value != 0)
                    result.append(inflate(value));
                state = 2;
                break;
            }
            case 2: {
                const quint32 value = ((lastByte << 4) | (thisByte >> 4)) & 0x3F;
                if (value != 0)
                    result.append(inflate(value));
                state = 3;
                break;
            }
            case 3: {
                const quint32 value1 = ((lastByte << 2) | (thisByte >> 6)) & 0x3F;
                if (value1 != 0)
                    result.append(inflate(value1));
                const quint32 value2 = thisByte & 0x3F;
                if (value2 != 0)
                    result.append(inflate(value2));
                state = 1;
                break;
            }
            }
            lastByte = thisByte;
        }
        return result;
    }

    QString stringFromMemory(int word) const
    {
        if (word >= 0x9900 && word <= 0xB6FF) {
            QString text;
            for (int i = 0; i < 256; ++i) {
                const quint8 c = byteAt(word + i);
                if (c == 0)
                    break;
                text.append(QChar(c));
            }
            return text;
        }
        return QStringLiteral("<string at 0x%1>")
            .arg(word, 4, 16, QLatin1Char('0'));
    }

    QVector<Operand> loadOperands(int count)
    {
        QVector<Operand> operands;
        m_strings.clear();
        m_strings.append(QString());

        for (int i = 0; i < count; ++i) {
            if (!validAddress(m_offset + 1))
                break;
            Operand operand;
            operand.code = byteAt(m_offset + 1);
            operand.low = byteAt(m_offset + 2);
            operand.word = operand.low;
            m_offset += 2;

            if (operand.code == 1 || operand.code == 2 || operand.code == 3) {
                m_offset++;
                operand.high = byteAt(m_offset);
                operand.word = (operand.high << 8) | operand.low;
                operand.wordSet = true;
            } else if (operand.code == 0x80) {
                const int length = operand.low;
                if (length > 0) {
                    QByteArray compressed;
                    compressed.reserve(length);
                    for (int j = 0; j < length; ++j)
                        compressed.append(static_cast<char>(byteAt(m_offset + 1 + j)));
                    m_offset += length;
                    operand.string = decompressString(compressed);
                }
                m_strings.append(operand.string);
            } else if (operand.code == 0x81) {
                m_offset++;
                operand.high = byteAt(m_offset);
                operand.word = (operand.high << 8) | operand.low;
                operand.wordSet = true;
                operand.string = stringFromMemory(operand.word);
                m_strings.append(operand.string);
            }
            operands.append(operand);
        }
        m_offset++;
        return operands;
    }

    QString handleCommand(int command, bool &stop)
    {
        switch (command) {
        case 0x00:
            loadOperands(0);
            if (!m_skipNext)
                stop = true;
            return {};
        case 0x01:
        case 0x02: {
            const QVector<Operand> operands = loadOperands(1);
            if (operands.isEmpty())
                return {};
            const int target = operands.first().word;
            addAddress(target, command == 0x01 ? QStringLiteral("Goto")
                                               : QStringLiteral("Gosub"));
            if (command == 0x01 && !m_skipNext)
                stop = true;
            return QStringLiteral("0x%1").arg(target, 4, 16, QLatin1Char('0'));
        }
        case 0x03: {
            const QVector<Operand> operands = loadOperands(2);
            if (operands.size() < 2)
                return {};
            if (operands[0].code >= 0x80 || operands[1].code >= 0x80)
                return QStringLiteral("Strings: '%1' == '%2'")
                    .arg(m_strings.value(2), m_strings.value(1));
            return QStringLiteral("Values: '%1' == '%2'")
                .arg(operandValue(operands[1]), operandValue(operands[0]));
        }
        case 0x04: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            return setMemoryValue(operands[2].word,
                                  QStringLiteral("%1 + %2")
                                      .arg(operandValue(operands[0]),
                                           operandValue(operands[1])));
        }
        case 0x05: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            return setMemoryValue(operands[2].word,
                                  QStringLiteral("%1 - %2")
                                      .arg(operandValue(operands[1]),
                                           operandValue(operands[0])));
        }
        case 0x06: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            return setMemoryValue(operands[2].word,
                                  QStringLiteral("%1 / %2")
                                      .arg(operandValue(operands[0]),
                                           operandValue(operands[1])))
                   + QStringLiteral(", area2_ptr.field_67E = %1 %% %2")
                         .arg(operandValue(operands[0]),
                              operandValue(operands[1]));
        }
        case 0x07: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            return setMemoryValue(operands[2].word,
                                  QStringLiteral("%1 * %2")
                                      .arg(operandValue(operands[0]),
                                           operandValue(operands[1])));
        }
        case 0x08: {
            const QVector<Operand> operands = loadOperands(2);
            if (operands.size() < 2)
                return {};
            return setMemoryValue(operands[1].word,
                                  QStringLiteral("1 to %1 (if max < 255 then max = max + 1)")
                                      .arg(operandValue(operands[0])));
        }
        case 0x09: {
            const QVector<Operand> operands = loadOperands(2);
            if (operands.size() < 2)
                return {};
            const int loc = operands[1].word;
            if (operands[0].code < 0x80)
                return setMemoryValue(loc, operandValue(operands[0]));
            return QStringLiteral("%1 = '%2'")
                .arg(writeStringToMemory(loc), m_strings.value(1));
        }
        case 0x0A: {
            const QVector<Operand> operands = loadOperands(1);
            return QStringLiteral("select player_index: %1")
                .arg(operands.isEmpty() ? QString() : operandValue(operands[0]));
        }
        case 0x0B: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            return QStringLiteral("Id: %1 Copies: %2 CPIC id: %3")
                .arg(operandValue(operands[0]), operandValue(operands[1]),
                     operandValue(operands[2]));
        }
        case 0x0C: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            return QStringLiteral("sprite_id: %1 max_distance: %2 pic_id: %3")
                .arg(operandValue(operands[0]), operandValue(operands[1]),
                     operandValue(operands[2]));
        }
        case 0x0D:
        case 0x1C:
        case 0x31:
        case 0x3A:
        case 0x3D:
            loadOperands(0);
            return {};
        case 0x0E: {
            const QVector<Operand> operands = loadOperands(1);
            if (operands.isEmpty())
                return {};
            bool ok = false;
            const int value = operandValue(operands[0]).toInt(&ok);
            if (ok && value != 0xFF)
                return QStringLiteral("blockId: 0x%1 <if area2_ptr.HeadBlockId == 0xFF ? <if blockId >= 0x78 ? <Load BIGPIC> : <Load PIC>> : <Load BODY>")
                    .arg(value, 2, 16, QLatin1Char('0'));
            if (ok)
                return QStringLiteral("<redraw screen>");
            return QStringLiteral("%1 <load BIGPIC, PIC, BODY or redraw based on value>")
                .arg(operandValue(operands[0]));
        }
        case 0x0F: {
            const QVector<Operand> operands = loadOperands(2);
            if (operands.size() < 2)
                return {};
            return setMemoryValue(operands[1].word, QStringLiteral("<user input number>"));
        }
        case 0x10: {
            const QVector<Operand> operands = loadOperands(2);
            if (operands.size() < 2)
                return {};
            return QStringLiteral("%1 = <user input string>")
                .arg(writeStringToMemory(operands[1].word));
        }
        case 0x11:
        case 0x12: {
            const QVector<Operand> operands = loadOperands(1);
            if (operands.isEmpty())
                return {};
            const QString text = operands[0].code >= 0x80
                                     ? m_strings.value(1)
                                     : operandValue(operands[0]);
            return QStringLiteral("'%1'").arg(text);
        }
        case 0x13:
        case 0x33:
            loadOperands(0);
            if (!m_skipNext)
                stop = true;
            return {};
        case 0x14: {
            const QVector<Operand> operands = loadOperands(4);
            if (operands.size() < 4)
                return {};
            return QStringLiteral("Values: '%1' == '%2' && '%3' == '%4'")
                .arg(operandValue(operands[0]), operandValue(operands[1]),
                     operandValue(operands[2]), operandValue(operands[3]));
        }
        case 0x15: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            const int memLoc = operands[0].word;
            const int menuCount = operands[2].word;
            m_offset--;
            const QVector<Operand> menu = loadOperands(menuCount);
            Q_UNUSED(menu);
            QStringList items;
            for (int i = 0; i < menuCount; ++i)
                items.append(QStringLiteral("'%1'").arg(m_strings.value(i + 1)));
            return setMemoryValue(memLoc, QStringLiteral("<selected index from menu>"))
                   + QStringLiteral(" MENU:") + items.join(QStringLiteral(", "));
        }
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B:
            loadOperands(0);
            m_skipNext = true;
            return QStringLiteral("<if compare flag is false, skip next opp>");
        case 0x1D: {
            const QVector<Operand> operands = loadOperands(1);
            if (operands.isEmpty())
                return {};
            return setMemoryValue(operands[0].word, QStringLiteral("<power_value_of_team>"));
        }
        case 0x1E: {
            const QVector<Operand> operands = loadOperands(6);
            if (operands.size() < 6)
                return {};
            int value = (operands[0].code == 1) ? operands[0].word : 0;
            const QString affect = operandValue(operands[1]);
            const int a = operands[2].word;
            const int b = operands[3].word;
            const int c = operands[4].word;
            const int d = operands[5].word;
            value = (value - 0x7FFF) & 0xFFFF;
            if (value == 8001)
                return setMemoryValue(a, QStringLiteral("<team meber has affect: %1>").arg(affect))
                       + QStringLiteral(", ") + setMemoryValue(b, QStringLiteral("0"))
                       + QStringLiteral(", ") + setMemoryValue(c, QStringLiteral("0"))
                       + QStringLiteral(", ") + setMemoryValue(d, QStringLiteral("0"));
            if (value >= 0x00A5 && value <= 0x00AC) {
                const QString index = QStringLiteral("field_EA[%1]>").arg(value - 0xA5);
                return setMemoryValue(a, QStringLiteral("0"))
                       + QStringLiteral(", ") + setMemoryValue(b, QStringLiteral("<avg ") + index)
                       + QStringLiteral(", ") + setMemoryValue(c, QStringLiteral("<max ") + index)
                       + QStringLiteral(", ") + setMemoryValue(d, QStringLiteral("<min ") + index);
            }
            if (value == 0x9F)
                return setMemoryValue(a, QStringLiteral("0"))
                       + QStringLiteral(", ") + setMemoryValue(b, QStringLiteral("<avg movement>"))
                       + QStringLiteral(", ") + setMemoryValue(c, QStringLiteral("<max movement>"))
                       + QStringLiteral(", ") + setMemoryValue(d, QStringLiteral("<min movement>"));
            return {};
        }
        case 0x1F:
            loadOperands(2);
            return QStringLiteral("todo");
        case 0x20: {
            const QVector<Operand> operands = loadOperands(1);
            if (!m_skipNext)
                stop = true;
            return QStringLiteral("<load ECL %1 (exit ecl)>")
                .arg(operands.isEmpty() ? QString() : operandValue(operands[0]));
        }
        case 0x21: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.isEmpty())
                return {};
            return QStringLiteral("Load GEO %1").arg(operandValue(operands[0]));
        }
        case 0x22:
            loadOperands(2);
            return QStringLiteral("todo");
        case 0x23: {
            const QVector<Operand> operands = loadOperands(4);
            if (operands.size() < 4)
                return {};
            return setMemoryValue(0x2CB,
                                  QStringLiteral("%1 %2 %3 %4")
                                      .arg(operandValue(operands[0]),
                                           operandValue(operands[1]),
                                           operandValue(operands[2]),
                                           operandValue(operands[3])));
        }
        case 0x24:
            loadOperands(0);
            return QStringLiteral("<if monstersLoaded == false && combat_type == normal ? [<if area2.EnterShop >? CityShop() : [<if area2.EnterTemple > ? TempleShop() : AfterCombatExpAndTreasure() ]] : DoCombat()>");
        case 0x25:
        case 0x26: {
            const QVector<Operand> head = loadOperands(2);
            if (head.size() < 2)
                return {};
            const QString value = operandValue(head[0]);
            const int count = head[1].word;
            m_offset--;
            const QVector<Operand> targets = loadOperands(count);
            QStringList list;
            for (const Operand &target : targets) {
                list.append(QStringLiteral("0x%1").arg(target.word, 4, 16, QLatin1Char('0')));
                addAddress(target.word, QStringLiteral("OnGoto"));
            }
            return QStringLiteral("%1 of [%2]").arg(value, list.join(QStringLiteral(", ")));
        }
        case 0x27: {
            const QVector<Operand> operands = loadOperands(8);
            if (operands.size() < 8)
                return {};
            const QString coins = QStringLiteral("Cp: %1 Sl: %2 El: %3 Gl: %4 Pl: %5 Gems: %6 Jewel: %7")
                                      .arg(operandValue(operands[0]), operandValue(operands[1]),
                                           operandValue(operands[2]), operandValue(operands[3]),
                                           operandValue(operands[4]), operandValue(operands[5]),
                                           operandValue(operands[6]));
            const QString block = operandValue(operands[7]);
            return QStringLiteral("%1, <item_val>:%2 [if item_val < 0x80 load ITEM block item_val, if item_val != 0xFF load item_val - 0x80 random items, else no items]")
                .arg(block, coins);
        }
        case 0x28: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            return QStringLiteral("all_party: %1 coin: %2% item: %3")
                .arg(operandValue(operands[0]), operandValue(operands[1]),
                     operandValue(operands[2]));
        }
        case 0x29: {
            const QVector<Operand> operands = loadOperands(14);
            if (operands.size() < 14)
                return {};
            const QString txt1 = QStringLiteral("sprite_id: %1 max_distance: <area2.max_encounter_distance> = %2 pic_id: %3 var_43D: %4")
                                     .arg(operandValue(operands[0]), operandValue(operands[1]),
                                          operandValue(operands[2]), QString::number(operands[3].word));
            const QString txt2 = QStringLiteral("[%1 %2 %3 %4 %5]")
                                     .arg(operandValue(operands[4]), operandValue(operands[5]),
                                          operandValue(operands[6]), operandValue(operands[7]),
                                          operandValue(operands[8]));
            const QString txt3 = QStringLiteral("'%1' '%2' '%3'")
                                     .arg(m_strings.value(1), m_strings.value(2), m_strings.value(3));
            const QString txt4 = QStringLiteral("%1 %2")
                                     .arg(operandValue(operands[12]), operandValue(operands[13]));
            return QStringLiteral("%1 %2 %3 %4")
                .arg(txt1, setMemoryValue(operands[3].word, txt2), txt3, txt4);
        }
        case 0x2A: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            const QString locText = QStringLiteral("%1[%2]")
                                        .arg(getMemoryValue(operands[0].word),
                                             operandValue(operands[1]));
            return setMemoryValue(operands[2].word, locText);
        }
        case 0x2B: {
            const QVector<Operand> operands = loadOperands(2);
            if (operands.size() < 2)
                return {};
            const int loc = operands[0].word;
            const int menuCount = operands[1].word;
            m_offset--;
            loadOperands(menuCount);
            QStringList items;
            for (int i = 0; i < menuCount; ++i)
                items.append(QStringLiteral("'%1'").arg(m_strings.value(i + 1)));
            return setMemoryValue(loc, QStringLiteral("<selected index from menu>"))
                   + QStringLiteral(" MENU:") + items.join(QStringLiteral(", "));
        }
        case 0x2C: {
            const QVector<Operand> operands = loadOperands(6);
            if (operands.size() < 6)
                return {};
            QStringList values;
            for (int i = 0; i < 5; ++i)
                values.append(operandValue(operands[i]));
            return QStringLiteral("<set %1 from [%2] mapped by [HAUGHTY, SLY, NICE, MEEK, ABUSIVE]")
                .arg(setMemoryValue(operands[5].word, QStringLiteral("value")),
                     values.join(QStringLiteral(", ")));
        }
        case 0x2D: {
            const QVector<Operand> operands = loadOperands(1);
            if (operands.isEmpty())
                return {};
            const int value = operands[0].word;
            const int mapped = (value - 0x7FFF) & 0xFFFF;
            switch (mapped) {
            case 1: return QStringLiteral("<setup duel vs. ROLF(self)>");
            case 2: return QStringLiteral("<setup duel>");
            case 0x401F: return QStringLiteral("<walk party foward>");
            case 0x4019: return QStringLiteral("<reload mapWallType, if not in dungeon?" "?>");
            default: break;
            }
            return QStringLiteral("?? %1 %2")
                .arg(value, 4, 16, QLatin1Char('0'))
                .arg(mapped, 4, 16, QLatin1Char('0'));
        }
        case 0x2E: {
            const QVector<Operand> operands = loadOperands(5);
            if (operands.size() < 5)
                return {};
            const int var1 = operands[0].word;
            const QString damage = operandValue(operands[1]) + QStringLiteral("D")
                                   + operandValue(operands[2]) + QStringLiteral("+")
                                   + operandValue(operands[3]);
            const int var6 = operands[4].word;
            if ((var1 & 0x80) != 0) {
                if ((var1 & 0x40) != 0) {
                    if (((var1 & 0x20) != 0) || ((var1 & 0x10) != 0))
                        return QStringLiteral("<all team damaged for %1>").arg(damage);
                    return QStringLiteral("<all team damaged for %1 if idv. save failed>").arg(damage);
                }
                if ((var6 & 0x80) != 0) {
                    if ((var6 & 7) == 0 || (var1 & 0x10) != 0)
                        return QStringLiteral("<selected player damaged for %1>").arg(damage);
                    return QStringLiteral("<selected player damaged for %1 if save failed>").arg(damage);
                }
                if ((var1 & 0x10) != 0)
                    return QStringLiteral("<random team member damaged for %1>").arg(damage);
                return QStringLiteral("<random team member damaged for %1 if save failed>").arg(damage);
            }
            return QStringLiteral("<do %1 attacks of %2 on random team member>")
                .arg(var1)
                .arg(damage);
        }
        case 0x2F:
        case 0x30: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            const QString op = (command == 0x2F) ? QStringLiteral("&") : QStringLiteral("|");
            const QString label = (command == 0x2F)
                                      ? QStringLiteral("<bitwise and>")
                                      : QStringLiteral("<bitwise or>");
            return setMemoryValue(operands[2].word,
                                  QStringLiteral("%1 %2 %3 %4")
                                      .arg(operandValue(operands[0]), op,
                                           operandValue(operands[1]), label))
                   + QStringLiteral(", + result compared to zero");
        }
        case 0x32: {
            const QVector<Operand> operands = loadOperands(1);
            return QStringLiteral("<find if any team member has item type %1>")
                .arg(operands.isEmpty() ? QString() : operandValue(operands[0]));
        }
        case 0x34: {
            const QVector<Operand> operands = loadOperands(2);
            if (operands.size() < 2)
                return {};
            return QStringLiteral("slot: %1 step: %2")
                .arg(operandValue(operands[1]), operandValue(operands[0]));
        }
        case 0x35: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            return QStringLiteral("%1[%2] = %3")
                .arg(getMemoryValue(operands[1].word),
                     operandValue(operands[2]), operandValue(operands[0]));
        }
        case 0x36: {
            const QVector<Operand> operands = loadOperands(2);
            if (operands.size() < 2)
                return {};
            return QStringLiteral("npc_id: %1 morale: %2")
                .arg(operandValue(operands[0]), operandValue(operands[1]));
        }
        case 0x37: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            return QStringLiteral("Load WALLDEF %1 %2 %3")
                .arg(operandValue(operands[2]), operandValue(operands[1]),
                     operandValue(operands[0]));
        }
        case 0x38: {
            const QVector<Operand> operands = loadOperands(1);
            if (operands.isEmpty())
                return {};
            const int value = operands[0].word;
            switch (value) {
            case 0: return QStringLiteral("StartGameMenu");
            case 8: return QStringLiteral("GameWon (exit game)");
            case 9:
                if (!m_skipNext)
                    stop = true;
                return QStringLiteral("TryEncamp (exit ecl)");
            case 3:
                if (!m_skipNext)
                    stop = true;
                return QStringLiteral("PartyKilled (exit ecl)");
            default: break;
            }
            return QString::number(value);
        }
        case 0x39: {
            loadOperands(1);
            return QStringLiteral("<Select Player> %1").arg(m_strings.value(1));
        }
        case 0x3B: {
            const QVector<Operand> operands = loadOperands(3);
            if (operands.size() < 3)
                return {};
            return QStringLiteral("<find team player with spell_id %1, and place %2 and %3 into (spell_index 0xFF if not found)>")
                .arg(operandValue(operands[0]),
                     setMemoryValue(operands[1].word, QStringLiteral("<spell_index>")),
                     setMemoryValue(operands[2].word, QStringLiteral("<player_index>")));
        }
        case 0x3C:
            loadOperands(1);
            return {};
        default:
            break;
        }

        // Fallback for unknown/unhandled commands: consume the table size.
        const QVector<Operand> operands = loadOperands(commands().value(command).size);
        QStringList values;
        for (const Operand &operand : operands)
            values.append(operandValue(operand));
        return values.join(QStringLiteral(", "));
    }

    void decodeAddress(int entry)
    {
        m_offset = entry;
        bool stop = false;
        while (!stop) {
            const int address = m_offset;
            if (!validAddress(address))
                break;
            const int command = byteAt(address);
            if (!commands().contains(command)) {
                addLine(address,
                        QStringLiteral("0x%1 0x%2  Unknown command")
                            .arg(address, 4, 16, QLatin1Char('0'))
                            .arg(command, 2, 16, QLatin1Char('0')),
                        1);
                break;
            }

            const bool lastSkip = m_skipNext;
            const QString detail = handleCommand(command, stop);
            if (lastSkip)
                m_skipNext = false;
            QString text = QStringLiteral("0x%1 0x%2  %3")
                               .arg(address, 4, 16, QLatin1Char('0'))
                               .arg(command, 2, 16, QLatin1Char('0'))
                               .arg(QString::fromLatin1(commands().value(command).name));
            if (!detail.isEmpty())
                text += QLatin1Char(' ') + detail;
            addLine(address, text, (m_offset - address) & 0xFFFF);
        }
    }

    QByteArray m_data;
    int m_memStart = 0x8000;
    int m_offset = 0;
    QMap<int, QString> m_lines;
    QMap<int, int> m_lengths;
    QMap<int, QString> m_annotations;
    QSet<int> m_done;
    QQueue<int> m_todo;
    QStringList m_strings;
    bool m_skipNext = false;
};

} // namespace

EclDisasmResult disassembleEclBlock(const QByteArray &blockData, bool poolOfRadiance)
{
    EclDisassembler disassembler(blockData, poolOfRadiance);
    return disassembler.run();
}
