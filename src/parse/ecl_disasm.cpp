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

// Operand counts match the original EclDump command table.
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
        {0x15, {"VERTICAL MENU", 0}},
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
        {0x2B, {"HORIZONTAL MENU", 0}},
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

        int previous = -2;
        for (auto it = m_lines.cbegin(); it != m_lines.cend(); ++it) {
            if (it.key() != previous + 1 && previous != -2) {
                EclDisasmLine blank;
                blank.address = it.key();
                blank.text = QString();
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
            previous = it.key();
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
        Q_UNUSED(length);
    }

    QString memoryName(int word) const
    {
        if (word >= 0x4900 && word <= 0x4CFF)
            return QStringLiteral("<area1 field 0x%1>").arg(word, 4, 16, QLatin1Char('0'));
        if (word >= 0x6B00 && word <= 0x6FFF)
            return QStringLiteral("<player field 0x%1>").arg(word, 4, 16, QLatin1Char('0'));
        if (word >= 0x9700 && word <= 0x98FF)
            return QStringLiteral("<stru_1B2CA.word_%1>")
                .arg(((word * 2) + 0x0C00) & 0xFFFF, 4, 16, QLatin1Char('0'));
        if (word >= 0x9900 && word <= 0xB6FF)
            return QStringLiteral("<ecl byte %1>").arg(word, 4, 16, QLatin1Char('0'));
        if (word >= 0xC04B) {
            switch (word - 0xC04B) {
            case 0: return QStringLiteral("<mapPosX>");
            case 1: return QStringLiteral("<mapPosY>");
            case 2: return QStringLiteral("<gbl.mapDirection / 2>");
            case 3: return QStringLiteral("<mapWallType>");
            case 4: return QStringLiteral("<mapWallRoof>");
            case 0x0E: return QStringLiteral("0");
            default: break;
            }
        }
        switch (word) {
        case 0x00B1: return QStringLiteral("<word_1D918>");
        case 0x00FB: return QStringLiteral("<word_1D914>");
        case 0x00FC: return QStringLiteral("<word_1D916>");
        case 0x033D: return QStringLiteral("<mapDirection>");
        case 0x035F: return QStringLiteral("0");
        default: break;
        }
        return QStringLiteral("<loc 0x%1>").arg(word, 4, 16, QLatin1Char('0'));
    }

    QString operandValue(const Operand &operand) const
    {
        switch (operand.code) {
        case 0x00:
            return QString::number(operand.low);
        case 0x01:
        case 0x03:
        case 0x80:
            return operand.wordSet ? memoryName(operand.word)
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

            if (command == 0x25 || command == 0x26) {
                const QVector<Operand> head = loadOperands(2);
                const int count = head.size() >= 2 ? head[1].word : 0;
                m_offset--;
                const QVector<Operand> targets = loadOperands(count);
                QStringList list;
                for (const Operand &target : targets) {
                    list.append(QStringLiteral("0x%1")
                                    .arg(target.word, 4, 16, QLatin1Char('0')));
                    addAddress(target.word, QStringLiteral("OnGoto"));
                }
                const QString value = head.isEmpty() ? QString()
                                                     : operandValue(head[0]);
                const QString name = QString::fromLatin1(
                    commands().value(command).name);
                QString text = QStringLiteral("0x%1 0x%2  %3 %4 of [%5]")
                                   .arg(address, 4, 16, QLatin1Char('0'))
                                   .arg(command, 2, 16, QLatin1Char('0'))
                                   .arg(name, value, list.join(QStringLiteral(", ")));
                addLine(address, text, (m_offset - address) & 0xFFFF);
                stop = true;
                continue;
            }

            const int size = commands().value(command).size;
            const QVector<Operand> operands = loadOperands(size);
            const int length = (m_offset - address) & 0xFFFF;
            QString text = QStringLiteral("0x%1 0x%2  %3")
                               .arg(address, 4, 16, QLatin1Char('0'))
                               .arg(command, 2, 16, QLatin1Char('0'))
                               .arg(QString::fromLatin1(commands().value(command).name));
            QString detail;
            switch (command) {
            case 0x01:
            case 0x02:
                if (!operands.isEmpty()) {
                    const int target = operands.first().word;
                    detail = QStringLiteral("0x%1")
                                 .arg(target, 4, 16, QLatin1Char('0'));
                    addAddress(target, command == 0x01 ? QStringLiteral("Goto")
                                                      : QStringLiteral("Gosub"));
                }
                break;
            case 0x11:
            case 0x12:
                if (!operands.isEmpty()) {
                    const Operand &operand = operands.first();
                    const QString value = (operand.code >= 0x80)
                                              ? m_strings.value(1)
                                              : operandValue(operand);
                    detail = QStringLiteral("'%1'").arg(value);
                }
                break;
            case 0x03:
                if (operands.size() >= 2) {
                    if (operands[0].code >= 0x80 || operands[1].code >= 0x80) {
                        detail = QStringLiteral("Strings: '%1' == '%2'")
                                     .arg(m_strings.value(2), m_strings.value(1));
                    } else {
                        detail = QStringLiteral("Values: '%1' == '%2'")
                                     .arg(operandValue(operands[1]),
                                          operandValue(operands[0]));
                    }
                }
                break;
            case 0x16:
            case 0x17:
            case 0x18:
            case 0x19:
            case 0x1A:
            case 0x1B:
                detail = QStringLiteral("<if compare flag is false, skip next opp>");
                break;
            case 0x24:
                detail = QStringLiteral("<do combat>");
                break;
            default:
                for (int i = 0; i < operands.size(); ++i) {
                    if (i > 0)
                        detail += QStringLiteral(", ");
                    detail += operandValue(operands.at(i));
                }
                break;
            }
            if (!detail.isEmpty())
                text += QLatin1Char(' ') + detail;
            addLine(address, text, length);

            if (command == 0x00 || command == 0x01
                || command == 0x13 || command == 0x33)
                stop = true;
        }
    }

    QByteArray m_data;
    int m_memStart = 0x8000;
    int m_offset = 0;
    QMap<int, QString> m_lines;
    QMap<int, QString> m_annotations;
    QSet<int> m_done;
    QQueue<int> m_todo;
    QStringList m_strings;
};

} // namespace

EclDisasmResult disassembleEclBlock(const QByteArray &blockData, bool poolOfRadiance)
{
    EclDisassembler disassembler(blockData, poolOfRadiance);
    return disassembler.run();
}
