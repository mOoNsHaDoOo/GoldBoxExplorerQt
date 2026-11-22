#include "parse/record_layouts.h"

#include <cstring>

namespace {

quint32 readBe(const QByteArray &data, int offset, int width)
{
    quint32 value = 0;
    for (int i = 0; i < width; ++i)
        value = (value << 8) | static_cast<quint8>(data.at(offset + i));
    return value;
}

qint32 signExtend(quint32 value, int width)
{
    const int bits = width * 8;
    if (bits >= 32)
        return static_cast<qint32>(value);
    const quint32 sign = 1u << (bits - 1);
    return static_cast<qint32>((value ^ sign) - sign);
}

bool isTextField(const RecordLayouts::RecordField &field)
{
    return field.width == 1 && field.count >= 8 && std::strstr(field.name, "name") != nullptr;
}

} // namespace

namespace RecordLayouts {

const RecordLayout *find(const char *id)
{
    for (int i = 0; i < layoutCount; ++i) {
        if (std::strcmp(layouts[i].id, id) == 0)
            return &layouts[i];
    }
    return nullptr;
}

QVector<FieldValue> describe(const RecordLayout &layout, const QByteArray &record)
{
    QVector<FieldValue> out;
    for (int f = 0; f < layout.fieldCount; ++f) {
        const RecordField &field = layout.fields[f];
        FieldValue v;
        v.offset = field.offset;
        v.name = QString::fromLatin1(field.name);
        v.description = QString::fromLatin1(field.description);
        const int last = field.offset + (field.count - 1) * field.step + field.width;
        if (last > record.size()) {
            v.value = QStringLiteral("(outside the data)");
            out.append(v);
            continue;
        }
        if (isTextField(field)) {
            v.value = QString::fromLatin1(record.mid(field.offset, field.count)).section(QChar(0), 0, 0);
        } else {
            QStringList values;
            const int shown = qMin(field.count, 64);
            for (int i = 0; i < shown; ++i) {
                const quint32 raw = readBe(record, field.offset + i * field.step, field.width);
                values.append(field.isSigned ? QString::number(signExtend(raw, field.width))
                                             : QString::number(raw));
            }
            if (shown < field.count)
                values.append(QStringLiteral("... (%1 elements)").arg(field.count));
            v.value = values.join(QLatin1Char(' '));
        }
        QStringList raw;
        const int rawCount = qMin(field.count, 16);
        for (int i = 0; i < rawCount; ++i)
            raw.append(QString::fromLatin1(record.mid(field.offset + i * field.step, field.width).toHex()));
        if (rawCount < field.count)
            raw.append(QStringLiteral("..."));
        v.raw = raw.join(QLatin1Char(' '));
        out.append(v);
    }
    return out;
}

} // namespace RecordLayouts

namespace DkkGlobals {

namespace {

quint32 be32(const QByteArray &d, int o)
{
    return o >= 0 && o + 4 <= d.size() ? readBe(d, o, 4) : 0;
}

} // namespace

bool looksLikeGlobals(const QByteArray &file)
{
    if (file.size() < 0x20 || be32(file, 0) != static_cast<quint32>(imageSize))
        return false;
    const quint32 tree = be32(file, 4);
    const quint32 words = be32(file, 8);
    return tree > 0x0C && words > tree && words < static_cast<quint32>(file.size());
}

// H032: Huffman walk (inner node: word = jump for bit 1; leaf: 0 then a
// negative literal byte or a word-table offset), then run-length pairs
// (n, v) = n words v:v, (0, n) = 2n literal bytes.
QByteArray unpack(const QByteArray &file, QString *error)
{
    if (!looksLikeGlobals(file)) {
        if (error)
            *error = QStringLiteral("Not a globals.daa image.");
        return {};
    }
    const int tree = static_cast<int>(be32(file, 4));
    const int words = static_cast<int>(be32(file, 8));
    int bits = 0x0C;
    quint32 current = be32(file, bits);
    bits += 4;
    int bit = 31;
    QByteArray out;
    out.reserve(imageSize + 0x100);
    bool ok = true;

    auto decode = [&](int count) {
        while (count > 0 && ok) {
            int node = tree;
            for (int guard = 0;; ++guard) {
                if (node + 2 > file.size() || guard > 64) {
                    ok = false;
                    return;
                }
                const qint16 w = static_cast<qint16>(readBe(file, node, 2));
                node += 2;
                if (w == 0)
                    break;
                if ((current >> bit) & 1u)
                    node += w;
                if (--bit < 0) {
                    bit = 31;
                    if (bits + 4 > file.size()) {
                        ok = false;
                        return;
                    }
                    current = be32(file, bits);
                    bits += 4;
                }
            }
            if (node + 2 > file.size()) {
                ok = false;
                return;
            }
            const qint16 v = static_cast<qint16>(readBe(file, node, 2));
            if (v < 0) {
                out.append(static_cast<char>(v & 0xFF));
                count -= 1;
            } else {
                if (words + v + 2 > file.size()) {
                    ok = false;
                    return;
                }
                out.append(file.at(words + v));
                out.append(file.at(words + v + 1));
                count -= 2;
            }
        }
    };

    int left = imageSize;
    while (left > 0 && ok) {
        decode(2);
        if (!ok || out.size() < 2)
            break;
        const quint8 n = static_cast<quint8>(out.at(out.size() - 2));
        const char v = out.at(out.size() - 1);
        out.chop(2);
        if (n != 0) {
            left -= 2 * n;
            if (left < 0)
                break;
            out.append(QByteArray(2 * n, v));
        } else {
            const int count = 2 * static_cast<quint8>(v);
            left -= count;
            if (left < 0)
                break;
            decode(count);
        }
    }
    if (!ok || out.size() < imageSize) {
        if (error)
            *error = QStringLiteral("globals.daa stream ends after %1 of %2 bytes.").arg(out.size()).arg(imageSize);
        return {};
    }
    out.truncate(imageSize);
    return out;
}

} // namespace DkkGlobals
