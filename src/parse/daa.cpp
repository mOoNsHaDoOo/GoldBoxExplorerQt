#include "parse/daa.h"

#include "core/byteutils.h"
#include "core/daxfile.h"
#include "dax/palette.h"

#include <QFile>
#include <QFileInfo>

#include <cstring>

namespace {

quint16 be16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return static_cast<quint16>((static_cast<quint8>(data.at(offset)) << 8)
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

QVector<QRgb> amigaPalette(const QByteArray &data, int offset)
{
    QVector<QRgb> colors;
    colors.reserve(32);
    for (int i = 0; i < 32; ++i) {
        const int v = be16(data, offset + i * 2);
        colors.append(qRgb(((v >> 8) & 0x0F) * 17,
                           ((v >> 4) & 0x0F) * 17,
                           (v & 0x0F) * 17));
    }
    return colors;
}

QImage renderPlanar(const QByteArray &pixels, int frame, int height, int widthBytes,
                    int rowBytes, const QVector<QRgb> &palette, int planes)
{
    const int width = widthBytes * 8;
    QImage image(width, height, QImage::Format_ARGB32);
    const int frameSize = planes * rowBytes * height;
    const int base = frame * frameSize;

    for (int y = 0; y < height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            int index = 0;
            for (int plane = 0; plane < planes; ++plane) {
                const int offset = base + plane * rowBytes * height + y * rowBytes + (x / 8);
                if (offset < 0 || offset >= pixels.size())
                    continue;
                const quint8 value = static_cast<quint8>(pixels.at(offset));
                if ((value >> (7 - (x & 7))) & 1)
                    index |= 1 << plane;
            }
            line[x] = palette.value(index, qRgb(0, 0, 0));
        }
    }
    return image;
}

QVector<QRgb> gamePalette()
{
    // set_game_palette (H029+$115C): the 16 RGB4 colours loaded at start-up.
    static const quint16 colors[16] = {
        0x000, 0x00A, 0x0A0, 0x0AA, 0xA00, 0xA0A, 0xA50, 0xAAA,
        0x555, 0x55F, 0x5F5, 0x5FF, 0xF55, 0xF5F, 0xFF5, 0xFFF
    };
    QVector<QRgb> palette;
    for (quint16 v : colors)
        palette.append(qRgb(((v >> 8) & 0x0F) * 17, ((v >> 4) & 0x0F) * 17, (v & 0x0F) * 17));
    // Colours 16..31 are not set by the game palette; reuse the first 16.
    for (int i = 0; i < 16; ++i)
        palette.append(palette.at(i));
    return palette;
}

} // namespace

bool DaaFile::load(const QString &path)
{
    m_path = path;
    m_error.clear();
    m_blocks.clear();
    m_valid = false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 2) {
        m_error = QStringLiteral("File is too small to be a DAA container.");
        return false;
    }

    m_dataOffset = be16(bytes, 0);
    if (m_dataOffset < 2 || m_dataOffset > bytes.size()
        || (m_dataOffset - 2) % 9 != 0) {
        m_error = QStringLiteral("Not a DAA container (invalid data offset).");
        return false;
    }

    const int count = (m_dataOffset - 2) / 9;
    if (count <= 0 || count > 4096) {
        m_error = QStringLiteral("Invalid DAA entry count.");
        return false;
    }

    const int payload = bytes.size() - m_dataOffset;
    quint32 maximumEnd = 0;

    for (int i = 0; i < count; ++i) {
        const int entry = 2 + i * 9;
        DaaBlock block;
        block.id = byteAt(bytes, entry);
        block.offset = be32(bytes, entry + 1);
        block.rawSize = be16(bytes, entry + 5);
        block.compressedSize = be16(bytes, entry + 7);

        if (block.offset + block.compressedSize > static_cast<quint32>(payload)
            || block.compressedSize == 0) {
            m_error = QStringLiteral("DAA entry points outside the data area.");
            m_blocks.clear();
            return false;
        }

        const QByteArray compressed = bytes.mid(m_dataOffset + block.offset,
                                                block.compressedSize);
        if (block.rawSize > 0)
            block.data = DaxFile::decode(compressed, block.rawSize);
        else
            block.data = compressed;

        if (block.data.size() < block.rawSize)
            block.data.resize(block.rawSize);

        maximumEnd = qMax(maximumEnd, block.offset + block.compressedSize);
        m_blocks.append(block);
    }

    // The small DAA sprite banks (pic1.daa, SPRIT1.DAA, ...) use another
    // layout. Requiring the index to cover the payload avoids treating their
    // data as a valid DAA index.
    if (maximumEnd != static_cast<quint32>(payload)) {
        m_error = QStringLiteral("DAA index does not cover the file payload.");
        m_blocks.clear();
        return false;
    }

    m_valid = true;
    return true;
}

bool isEightByEightDaaName(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    return name.startsWith(QStringLiteral("8x8"), Qt::CaseInsensitive);
}

DaaRecordInfo inspectDaaRecord(const QByteArray &data, bool eightByEight)
{
    DaaRecordInfo info;
    info.eightByEight = eightByEight;
    if (data.size() < 9) {
        info.description = QStringLiteral("Too short for a graphic record");
        return info;
    }
    info.rows = be16(data, 0);
    info.count = be16(data, 2);
    info.width8 = byteAt(data, 8);
    info.planes = eightByEight ? 4 : 5;
    int width = info.width8 * 8 * info.count;
    if (width & 0x0F)
        width += 0x10 - (width & 0x0F);
    info.width = width;

    bool allZero = true;
    for (char c : data) {
        if (c != 0) {
            allZero = false;
            break;
        }
    }
    if (allZero) {
        info.description = QStringLiteral("Empty block (all zero)");
        return info;
    }
    if (info.rows <= 0 || info.rows > 400 || info.count <= 0 || info.width8 <= 0
        || width <= 0 || width > 4096) {
        info.description = QStringLiteral("Not a graphic record (rows %1, count %2, width8 %3)")
                               .arg(info.rows).arg(info.count).arg(info.width8);
        return info;
    }
    const int pixelStart = eightByEight ? 9 : 9 + 64;
    const int needed = pixelStart + info.planes * (width / 8) * info.rows;
    if (data.size() < needed) {
        info.description = QStringLiteral("Graphic record header %1x%2 needs %3 bytes, block has %4")
                               .arg(width).arg(info.rows).arg(needed).arg(data.size());
        return info;
    }
    info.valid = true;
    const int frameWidth = info.count * 8;
    if (eightByEight) {
        info.description = QStringLiteral("8x8 tile set: %1 tiles %2x%3, 4 planes, game palette")
                               .arg(info.width8).arg(frameWidth).arg(info.rows);
    } else if (info.width8 > 1) {
        info.description = QStringLiteral("Image strip: %1 frames %2x%3, 5 planes, 32-colour palette")
                               .arg(info.width8).arg(frameWidth).arg(info.rows);
    } else {
        info.description = QStringLiteral("Image %1x%2, 5 planes, 32-colour palette")
                               .arg(frameWidth).arg(info.rows);
    }
    return info;
}

QVector<QImage> decodeDaaRecord(const QByteArray &data, bool eightByEight, DaaRecordInfo *infoOut)
{
    const DaaRecordInfo info = inspectDaaRecord(data, eightByEight);
    if (infoOut)
        *infoOut = info;
    if (!info.valid)
        return {};

    const QVector<QRgb> palette = eightByEight ? gamePalette() : amigaPalette(data, 9);
    const QByteArray pixels = data.mid(eightByEight ? 9 : 9 + 64);
    const int rowBytes = info.width / 8;
    const QImage strip = renderPlanar(pixels, 0, info.rows, rowBytes, rowBytes, palette,
                                      info.planes);
    // transfer_gfx_planes/remap_gfx_colors: frame f covers columns
    // [count*8*f, count*8*(f+1)).
    QVector<QImage> frames;
    const int frameWidth = info.count * 8;
    for (int f = 0; f < info.width8; ++f)
        frames.append(strip.copy(f * frameWidth, 0, frameWidth, info.rows));
    return frames;
}

QVector<QImage> decodeDaaImages(const DaaFile &file, QVector<int> *imageIds, QString *error)
{
    QVector<QImage> images;
    QVector<int> ids;
    const bool eightByEight = isEightByEightDaaName(file.path());

    for (const DaaBlock &block : file.blocks()) {
        const QVector<QImage> frames = decodeDaaRecord(block.data, eightByEight);
        for (const QImage &frame : frames) {
            images.append(frame);
            ids.append(block.id);
        }
    }

    if (images.isEmpty()) {
        if (error)
            *error = QStringLiteral("No decodable DAA image records were found.");
        return {};
    }

    if (imageIds)
        *imageIds = ids;
    return images;
}

int unpackByteRun1(const QByteArray &source, int sourceOffset, int sourceSize,
                   QByteArray &output, int outputSize)
{
    output = QByteArray(qMax(0, outputSize), '\0');
    int s = sourceOffset;
    const int end = qMin(source.size(), sourceOffset + sourceSize);
    int d = 0;
    while (d < outputSize) {
        if (s >= end)
            return -1;
        int n = static_cast<quint8>(source.at(s++));
        if (n > 0x7F)
            n -= 0x100;
        if (n >= 0) {
            const int count = n + 1;
            if (s + count > end || d + count > outputSize)
                return -1;
            std::memcpy(output.data() + d, source.constData() + s, static_cast<size_t>(count));
            s += count;
            d += count;
        } else if (n != -128) {
            const int count = 1 - n;
            if (s >= end || d + count > outputSize)
                return -1;
            std::memset(output.data() + d, static_cast<unsigned char>(source.at(s++)),
                        static_cast<size_t>(count));
            d += count;
        }
    }
    return s - sourceOffset;
}

bool DaaSequenceFile::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        m_valid = false;
        return false;
    }
    return parse(file.readAll());
}

bool DaaSequenceFile::parse(const QByteArray &bytes)
{
    m_error.clear();
    m_sequences.clear();
    m_valid = false;

    if (bytes.size() < 8) {
        m_error = QStringLiteral("File is too small for a picture sequence container.");
        return false;
    }
    m_dataOffset = be16(bytes, 0);
    if (m_dataOffset < 8 || m_dataOffset > bytes.size() || (m_dataOffset - 2) % 6 != 0) {
        m_error = QStringLiteral("Not a picture sequence container.");
        return false;
    }
    const int count = (m_dataOffset - 2) / 6;
    const int payload = bytes.size() - m_dataOffset;

    for (int i = 0; i < count; ++i) {
        const int entry = 2 + i * 6;
        DaaSequence seq;
        seq.index = i;
        seq.id = byteAt(bytes, entry);
        seq.flag = byteAt(bytes, entry + 1);
        seq.offset = be32(bytes, entry + 2);
        if (seq.offset + 2 > static_cast<quint32>(payload)) {
            m_error = QStringLiteral("Sequence %1 points outside the file.").arg(i);
            m_sequences.clear();
            return false;
        }

        int at = m_dataOffset + static_cast<int>(seq.offset);
        const int headerSize = be16(bytes, at);
        at += 2;
        const int used = unpackByteRun1(bytes, at, headerSize, seq.header, 0x56);
        if (headerSize <= 0 || headerSize > 0x100 || used < 0) {
            m_error = QStringLiteral("Sequence %1 has an invalid packed header.").arg(i);
            m_sequences.clear();
            return false;
        }
        at += headerSize;

        seq.rows = be16(seq.header, 0);
        seq.units = be16(seq.header, 2);
        seq.w4 = be16(seq.header, 4);
        seq.w6 = be16(seq.header, 6);
        seq.frameCount = be16(seq.header, 8);
        if (seq.rows <= 0 || seq.rows > 400 || seq.units <= 0 || seq.units > 64
            || seq.frameCount <= 0 || seq.frameCount > 6) {
            m_error = QStringLiteral("Sequence %1 has an implausible header.").arg(i);
            m_sequences.clear();
            return false;
        }
        seq.palette = amigaPalette(seq.header, 0x0A);
        seq.palette[0] = qRgb(0, 0, 0); // Load_Sequence clears colour 0

        int width = seq.units * 8;
        if (width & 0x0F)
            width += 0x10 - (width & 0x0F);
        const int rowBytes = width / 8;
        const int planeBytes = rowBytes * seq.rows * 5;

        QByteArray first;
        for (int k = 0; k < seq.frameCount; ++k) {
            const int packed = be16(seq.header, 0x4A + k * 2);
            seq.packedSizes.append(packed);
            QByteArray planes;
            if (at + packed + 2 > bytes.size()
                || unpackByteRun1(bytes, at, packed, planes, planeBytes) < 0) {
                seq.error = QStringLiteral("frame %1 does not unpack").arg(k + 1);
                break;
            }
            at += packed;
            seq.durations.append(be16(bytes, at));
            at += 2;
            if (k == 0) {
                first = planes;
            } else {
                for (int b = 0; b < planeBytes; ++b)
                    planes[b] = static_cast<char>(planes.at(b) ^ first.at(b));
            }
            seq.frames.append(renderPlanar(planes, 0, seq.rows, rowBytes, rowBytes,
                                           seq.palette, 5)
                                  .copy(0, 0, seq.units * 8, seq.rows));
        }
        seq.storedSize = at - (m_dataOffset + static_cast<int>(seq.offset));
        if (i == 0 && seq.frames.isEmpty()) {
            m_error = QStringLiteral("The first sequence does not decode.");
            m_sequences.clear();
            return false;
        }
        m_sequences.append(seq);
    }

    m_valid = !m_sequences.isEmpty();
    return m_valid;
}

bool decodePointerFile(const QByteArray &data, QVector<DaaPointer> &pointers)
{
    pointers.clear();
    constexpr int entries = 12;
    if (data.size() < entries * 10 + 8)
        return false;
    // Placeholder colours: transparent, then sprite colours 17..19, which
    // come from the current screen palette at run time.
    const QRgb colors[4] = {qRgba(0, 0, 0, 0), qRgb(0xEE, 0x44, 0x44), qRgb(0, 0, 0),
                            qRgb(0xEE, 0xEE, 0xCC)};
    for (int i = 0; i < entries; ++i) {
        DaaPointer p;
        p.index = i;
        p.offset = be32(data, i * 10);
        p.hotX = static_cast<qint16>(be16(data, i * 10 + 4));
        p.hotY = static_cast<qint16>(be16(data, i * 10 + 6));
        p.height = be16(data, i * 10 + 8);
        if (p.offset < entries * 10 || p.height <= 0 || p.height > 64
            || p.offset + 8 + p.height * 4 > static_cast<quint32>(data.size())) {
            pointers.clear();
            return false;
        }
        QImage image(16, p.height, QImage::Format_ARGB32);
        for (int y = 0; y < p.height; ++y) {
            const int row = static_cast<int>(p.offset) + 4 + y * 4;
            const int plane0 = be16(data, row);
            const int plane1 = be16(data, row + 2);
            for (int x = 0; x < 16; ++x) {
                const int bit = 15 - x;
                const int index = ((plane0 >> bit) & 1) | (((plane1 >> bit) & 1) << 1);
                image.setPixel(x, y, colors[index]);
            }
        }
        p.image = image;
        pointers.append(p);
    }
    return true;
}
