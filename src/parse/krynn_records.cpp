#include "parse/krynn_records.h"

#include <QDir>
#include <QFileInfo>

namespace KrynnRecords {

namespace {

quint8 at(const QByteArray &d, int o)
{
    return (o >= 0 && o < d.size()) ? static_cast<quint8>(d.at(o)) : 0;
}

void copy(QByteArray &mem, const QByteArray &src, int from, int to, int count)
{
    for (int i = 0; i < count; ++i)
        if (to + i < mem.size())
            mem[to + i] = static_cast<char>(at(src, from + i));
}

void swapWord(QByteArray &mem, int o)
{
    const char a = mem.at(o);
    mem[o] = mem.at(o + 1);
    mem[o + 1] = a;
}

void putSignedWord(QByteArray &mem, int o, quint8 value)
{
    const qint16 v = static_cast<qint8>(value);
    mem[o] = static_cast<char>((static_cast<quint16>(v) >> 8) & 0xFF);
    mem[o + 1] = static_cast<char>(static_cast<quint16>(v) & 0xFF);
}

const char *lookup(const char *const table[], int count, int index)
{
    return (index >= 0 && index < count) ? table[index] : nullptr;
}

} // namespace

QByteArray monsterToMemory(const QByteArray &d)
{
    // Load_MonsterRecord (H066+$1FDA).
    static const int copies[][3] = {
        {0x010, 0x010, 0x48}, {0x059, 0x05A, 0x07}, {0x060, 0x062, 0x03},
        {0x063, 0x066, 0x6B}, {0x0CE, 0x0D2, 0x07}, {0x0D5, 0x0DA, 0x3C},
        {0x113, 0x11A, 0x03}, {0x116, 0x11E, 0x0B}, {0x121, 0x12A, 0x05},
        {0x126, 0x130, 0x05}, {0x12B, 0x136, 0x05}, {0x130, 0x13C, 0x09},
        {0x139, 0x146, 0x43}, {0x17D, 0x18C, 0x18}, {0x197, 0x1A8, 0x02}};
    static const int signedBytes[][2] = {
        {0x058, 0x058}, {0x111, 0x116}, {0x112, 0x118}, {0x17C, 0x18A},
        {0x195, 0x1A4}, {0x196, 0x1A6}};

    QByteArray mem(characterSize, '\0');
    copy(mem, d, 1, 0, 0x0F);
    mem[0x0F] = 0;
    for (const auto &c : copies)
        copy(mem, d, c[0], c[1], c[2]);
    for (const auto &s : signedBytes)
        putSignedWord(mem, s[1], at(d, s[0]));
    // h031_swap_long: the long at +$11E is reversed.
    std::swap(mem[0x11E], mem[0x121]);
    std::swap(mem[0x11F], mem[0x120]);
    swapWord(mem, 0x62);
    swapWord(mem, 0x13C);
    for (int i = 0; i <= 6; ++i)
        swapWord(mem, 0xF0 + i * 2);
    swapWord(mem, 0x18C);
    for (int o : {0x150, 0xE8, 0x18E, 0x192})
        for (int i = 0; i < 4; ++i)
            mem[o + i] = 0;
    return mem;
}

QByteArray itemToMemory(const QByteArray &d)
{
    // Item part of Load_MonsterRecord; the same $3F-byte records are used by
    // ITEM*.DAX.
    QByteArray mem(itemSize, '\0');
    copy(mem, d, 0x00, 0x00, 0x28);
    mem[0x28] = 0;
    copy(mem, d, 0x29, 0x2A, 1);
    copy(mem, d, 0x2A, 0x2C, 5);
    copy(mem, d, 0x2F, 0x32, 3);
    putSignedWord(mem, 0x36, at(d, 0x32));
    putSignedWord(mem, 0x38, at(d, 0x33));
    copy(mem, d, 0x34, 0x3A, 3);
    copy(mem, d, 0x37, 0x3E, 3);
    copy(mem, d, 0x3A, 0x42, 5);
    swapWord(mem, 0x3E);
    swapWord(mem, 0x42);
    for (int i = 0; i < 4; ++i)
        mem[0x2C + i] = 0;
    return mem;
}

QByteArray effectToMemory(const QByteArray &d)
{
    QByteArray mem(effectSize, '\0');
    copy(mem, d, 0, 0, 1);
    copy(mem, d, 1, 2, 8);
    swapWord(mem, 2);
    for (int i = 6; i < 10; ++i)
        mem[i] = 0;
    return mem;
}

QString itemWordName(int word)
{
    const char *name = lookup(KrynnFields::itemWords, KrynnFields::itemWordsCount, word - 1);
    return name ? QString::fromLatin1(name) : QString();
}

QString itemName(const QByteArray &mem)
{
    QStringList words;
    for (int o = 0x32; o <= 0x34; ++o) {
        const QString w = itemWordName(at(mem, o));
        if (!w.isEmpty())
            words.append(w);
    }
    const qint16 bonus = static_cast<qint16>((at(mem, 0x36) << 8) | at(mem, 0x37));
    QString name = words.join(QLatin1Char(' '));
    if (bonus != 0)
        name += QStringLiteral(" %1%2").arg(bonus > 0 ? QStringLiteral("+") : QString()).arg(bonus);
    return name;
}

QVector<FieldValue> describe(KrynnFields::Kind kind, const QByteArray &mem)
{
    QVector<FieldValue> result;
    for (int i = 0; i < KrynnFields::fieldCount; ++i) {
        const KrynnFields::Field &f = KrynnFields::fields[i];
        if (f.kind != kind || f.offset + qMin(f.size, 4) > mem.size())
            continue;
        FieldValue v;
        v.offset = f.offset;
        v.size = f.size;
        v.name = QString::fromLatin1(f.name);
        v.raw = QString::fromLatin1(mem.mid(f.offset, f.size).toHex(' ').toUpper());
        if (f.size > 4) {
            QByteArray text = mem.mid(f.offset, f.size);
            const int end = text.indexOf('\0');
            if (end >= 0)
                text.truncate(end);
            v.value = QString::fromLatin1(text);
        } else {
            quint32 value = 0;
            for (int b = 0; b < f.size; ++b)
                value = (value << 8) | at(mem, f.offset + b);
            QString text = QString::number(value);
            if (f.size == 2 && v.name.contains(QStringLiteral("signed")))
                text = QString::number(static_cast<qint16>(value));
            if (kind == KrynnFields::Kind::Character) {
                const char *label = nullptr;
                if (f.offset == 0x5B)
                    label = lookup(KrynnFields::races, KrynnFields::racesCount, static_cast<int>(value));
                else if (f.offset == 0x5C)
                    label = lookup(KrynnFields::classes, KrynnFields::classesCount, static_cast<int>(value));
                else if (f.offset == 0x10F)
                    label = lookup(KrynnFields::alignments, KrynnFields::alignmentsCount, static_cast<int>(value));
                if (label)
                    text += QStringLiteral("  (%1)").arg(QString::fromLatin1(label));
            } else if (kind == KrynnFields::Kind::Item && f.offset >= 0x32 && f.offset <= 0x34) {
                const QString word = itemWordName(static_cast<int>(value));
                if (!word.isEmpty())
                    text += QStringLiteral("  (%1)").arg(word);
            }
            v.value = text;
        }
        result.append(v);
    }
    return result;
}

bool looksLikeAmigaKrynnFolder(const QString &filePath)
{
    const QDir dir = QFileInfo(filePath).absoluteDir();
    bool hasItems = false;
    bool hasDaa = false;
    const QStringList entries = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (entry.compare(QStringLiteral("items"), Qt::CaseInsensitive) == 0)
            hasItems = true;
        else if (entry.endsWith(QStringLiteral(".daa"), Qt::CaseInsensitive))
            hasDaa = true;
    }
    return hasItems && hasDaa;
}

} // namespace KrynnRecords
