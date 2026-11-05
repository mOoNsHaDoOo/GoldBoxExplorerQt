#include "parse/iff.h"

#include "core/byteutils.h"

#include <QPainter>

namespace {

quint16 be16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return static_cast<quint16>(
        (static_cast<quint8>(data.at(offset)) << 8)
        | static_cast<quint8>(data.at(offset + 1)));
}

quint32 be32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    return (static_cast<quint32>(static_cast<quint8>(data.at(offset))) << 24)
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 16)
           | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 8)
           | static_cast<quint32>(static_cast<quint8>(data.at(offset + 3)));
}

bool isFourCC(const QByteArray &data)
{
    if (data.size() != 4)
        return false;
    for (char c : data) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')
              || (u >= '0' && u <= '9') || u == ' '))
            return false;
    }
    return true;
}

bool parseFormAt(const QByteArray &data, int offset, int limit, IffForm &form, int depth)
{
    if (depth > 8 || offset < 0 || offset + 12 > limit || offset + 12 > data.size())
        return false;
    if (data.mid(offset, 4) != QByteArrayLiteral("FORM"))
        return false;

    const quint32 declared = be32(data, offset + 4);
    int end = offset + 8 + static_cast<int>(declared);
    if (end > data.size() || end < offset + 12)
        end = qMin(limit, data.size());

    form.type = data.mid(offset + 8, 4);
    int pos = offset + 12;
    while (pos + 8 <= end) {
        const QByteArray id = data.mid(pos, 4);
        const quint32 size = be32(data, pos + 4);
        pos += 8;
        const int available = qMin<int>(size, end - pos);
        if (available < 0)
            break;

        IffChunk chunk;
        chunk.id = id;
        chunk.data = data.mid(pos, available);
        form.chunks.append(chunk);

        if (id == QByteArrayLiteral("FORM")) {
            IffForm nested;
            if (parseFormAt(data, pos - 8, end, nested, depth + 1))
                form.chunks += nested.chunks;
        }

        int next = pos + available;
        if (available & 1) {
            // Most IFF files pad odd chunks, but some Gold Box Amiga files do
            // not. Prefer the position that looks like a valid chunk header.
            const bool noPadHeader = next + 8 <= end
                && isFourCC(data.mid(next, 4))
                && be32(data, next + 4) <= static_cast<quint32>(end - next - 8);
            const bool padHeader = next + 9 <= end
                && isFourCC(data.mid(next + 1, 4))
                && be32(data, next + 5) <= static_cast<quint32>(end - next - 9);
            if (!noPadHeader || padHeader)
                ++next;
        }
        pos = next;
    }
    return true;
}

QByteArray findChunk(const IffForm &form, const QByteArray &id)
{
    for (const IffChunk &chunk : form.chunks) {
        if (chunk.id == id)
            return chunk.data;
    }
    return {};
}

QByteArray readByteRun1(const QByteArray &input, int &pos, int expected)
{
    QByteArray output;
    output.reserve(expected);

    while (output.size() < expected && pos < input.size()) {
        const qint8 n = static_cast<qint8>(input.at(pos++));
        if (n >= 0) {
            const int count = n + 1;
            for (int i = 0; i < count && output.size() < expected && pos < input.size(); ++i)
                output.append(input.at(pos++));
        } else if (n != -128) {
            const int count = -n + 1;
            if (pos < input.size()) {
                const char value = input.at(pos++);
                for (int i = 0; i < count && output.size() < expected; ++i)
                    output.append(value);
            }
        }
    }
    return output;
}

} // namespace

bool readIffForm(const QByteArray &data, IffForm &form, QString *error)
{
    form = {};
    if (!parseFormAt(data, 0, data.size(), form, 0)) {
        if (error)
            *error = QStringLiteral("Not an IFF FORM file.");
        return false;
    }
    return true;
}

bool looksLikeIff(const QByteArray &data, const QByteArray &type)
{
    if (data.size() < 12 || data.left(4) != QByteArrayLiteral("FORM"))
        return false;
    return type.isEmpty() || data.mid(8, 4) == type;
}

QImage decodeIffImage(const QByteArray &data, QString *error)
{
    IffForm form;
    if (!readIffForm(data, form, error))
        return {};

    const QByteArray bmhd = findChunk(form, QByteArrayLiteral("BMHD"));
    const QByteArray body = findChunk(form, QByteArrayLiteral("BODY"));
    if (bmhd.size() < 20 || body.isEmpty()) {
        if (error)
            *error = QStringLiteral("No ILBM/PBM bitmap data found.");
        return {};
    }

    const int width = be16(bmhd, 0);
    const int height = be16(bmhd, 2);
    const int planes = byteAt(bmhd, 8);
    const int masking = byteAt(bmhd, 9);
    const int compression = byteAt(bmhd, 10);
    if (width <= 0 || height <= 0 || planes <= 0 || planes > 8) {
        if (error)
            *error = QStringLiteral("Invalid ILBM dimensions.");
        return {};
    }

    QByteArray cmap = findChunk(form, QByteArrayLiteral("CMAP"));
    const QByteArray camgData = findChunk(form, QByteArrayLiteral("CAMG"));
    const quint32 camg = camgData.size() >= 4 ? be32(camgData, 0) : 0;

    QVector<QRgb> palette;
    for (int i = 0; i + 2 < cmap.size(); i += 3) {
        palette.append(qRgb(static_cast<quint8>(cmap.at(i)),
                            static_cast<quint8>(cmap.at(i + 1)),
                            static_cast<quint8>(cmap.at(i + 2))));
    }
    if (palette.isEmpty()) {
        const int count = 1 << planes;
        for (int i = 0; i < count; ++i) {
            const int v = (i * 255) / qMax(1, count - 1);
            palette.append(qRgb(v, v, v));
        }
    }

    const int rowBytes = ((width + 15) / 16) * 2;
    const int planeCount = planes + (masking == 1 ? 1 : 0);
    QByteArray raw;

    if (compression == 0) {
        raw = body;
    } else if (compression == 1) {
        int pos = 0;
        for (int y = 0; y < height; ++y) {
            for (int plane = 0; plane < planeCount; ++plane)
                raw += readByteRun1(body, pos, rowBytes);
        }
    } else {
        if (error)
            *error = QStringLiteral("Unsupported ILBM compression %1.").arg(compression);
        return {};
    }

    if (raw.size() < rowBytes * planeCount * height) {
        if (error)
            *error = QStringLiteral("Truncated ILBM bitmap data.");
        return {};
    }

    const bool ham = (camg & 0x800) != 0;
    const bool ehb = (camg & 0x80) != 0;
    QImage image(width, height, QImage::Format_ARGB32);

    int currentR = 0;
    int currentG = 0;
    int currentB = 0;

    for (int y = 0; y < height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int byteIndex = x / 8;
            const int bit = 7 - (x & 7);
            int index = 0;
            for (int plane = 0; plane < planes; ++plane) {
                const int offset = (y * planeCount * rowBytes) + (plane * rowBytes) + byteIndex;
                if (offset >= 0 && offset < raw.size()) {
                    const quint8 value = static_cast<quint8>(raw.at(offset));
                    if ((value >> bit) & 1)
                        index |= (1 << plane);
                }
            }

            QRgb color;
            if (ham && planes >= 6) {
                const int control = (index >> (planes - 2)) & 3;
                const int component = index & ((1 << (planes - 2)) - 1);
                if (control == 0) {
                    color = palette.value(index, qRgb(0, 0, 0));
                    currentR = qRed(color);
                    currentG = qGreen(color);
                    currentB = qBlue(color);
                } else {
                    const int value = (component * 255) / ((1 << (planes - 2)) - 1);
                    if (control == 1)
                        currentB = value;
                    else if (control == 2)
                        currentR = value;
                    else
                        currentG = value;
                    color = qRgb(currentR, currentG, currentB);
                }
            } else if (ehb && index >= 32) {
                const QRgb base = palette.value(index - 32, qRgb(0, 0, 0));
                color = qRgb(qRed(base) / 2, qGreen(base) / 2, qBlue(base) / 2);
            } else {
                color = palette.value(index, qRgb(0, 0, 0));
            }
            line[x] = color;
        }
    }

    return image;
}
