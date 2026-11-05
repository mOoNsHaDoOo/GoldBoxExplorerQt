#include "parse/daa.h"

#include "core/byteutils.h"
#include "core/daxfile.h"
#include "dax/palette.h"

#include <QFile>

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

QVector<QImage> renderEightByEight(const QByteArray &pixels, int frameCount)
{
    QVector<QImage> images;
    const QVector<QRgb> ega = Palette::basePalette();
    constexpr int tileBytes = 32; // 8x8 at 4 bits per pixel

    for (int frame = 0; frame < frameCount; ++frame) {
        const int base = frame * tileBytes;
        if (base + tileBytes > pixels.size())
            break;

        QImage image(8, 8, QImage::Format_ARGB32);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const int index = base + y * 4 + x / 2;
                const quint8 byte = static_cast<quint8>(pixels.at(index));
                const int color = (x & 1) ? (byte & 0x0F) : (byte >> 4);
                image.setPixel(x, y, ega.value(color, qRgb(0, 0, 0)));
            }
        }
        images.append(image);
    }
    return images;
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

QVector<QImage> decodeDaaImages(const DaaFile &file, QVector<int> *imageIds, QString *error)
{
    QVector<QImage> images;
    QVector<int> ids;
    bool sawBlock = false;

    for (const DaaBlock &block : file.blocks()) {
        const QByteArray &data = block.data;
        if (data.size() < 9)
            continue;

        const int height = be16(data, 0);
        const int widthBytes = be16(data, 2);
        const int itemCount = byteAt(data, 8);
        if (height <= 0 || height > 512 || widthBytes <= 0 || widthBytes > 128)
            continue;

        const int width = widthBytes * 8;
        const int rowBytes = ((width + 15) / 16) * 2;
        const QByteArray pixels = data.mid(9);

        // The 8x8D tile sets are stored as 8x8 4bpp tiles without a palette.
        if (height == 8 && widthBytes == 1 && itemCount > 0
            && pixels.size() >= itemCount * 32) {
            const QVector<QImage> tiles = renderEightByEight(pixels, itemCount);
            for (const QImage &tile : tiles) {
                images.append(tile);
                ids.append(block.id);
            }
            sawBlock = true;
            continue;
        }

        // Amiga 32-colour planar records: 64-byte palette followed by a
        // single 5-plane image. Multi-frame combat banks use an as-yet
        // undecoded variant; showing them as garbage would be worse than
        // falling back to the summary/hex view.
        const int paletteSize = 64;
        const int frameSize = 5 * rowBytes * height;
        if (itemCount == 1 && pixels.size() >= paletteSize + frameSize) {
            const QVector<QRgb> palette = amigaPalette(data, 9);
            const QImage image = renderPlanar(pixels.mid(paletteSize), 0, height,
                                              widthBytes, rowBytes, palette, 5);
            if (!image.isNull()) {
                images.append(image);
                ids.append(block.id);
            }
            sawBlock = true;
        }
    }

    if (!sawBlock) {
        if (error)
            *error = QStringLiteral("No decodable DAA image records were found.");
        return {};
    }

    if (imageIds)
        *imageIds = ids;
    return images;
}


bool PicDaaFile::load(const QString &path)
{
    m_error.clear();
    m_records.clear();
    m_valid = false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 8) {
        m_error = QStringLiteral("File is too small for the alternate DAA index.");
        return false;
    }

    m_dataOffset = be16(bytes, 0);
    if (m_dataOffset < 2 || m_dataOffset > bytes.size()
        || (m_dataOffset - 2) % 6 != 0) {
        m_error = QStringLiteral("Not an alternate DAA container.");
        return false;
    }

    const int count = (m_dataOffset - 2) / 6;
    if (count <= 0 || count > 4096) {
        m_error = QStringLiteral("Invalid alternate DAA record count.");
        return false;
    }

    const int payload = bytes.size() - m_dataOffset;
    quint32 previousEnd = 0;
    for (int i = 0; i < count; ++i) {
        const int entry = 2 + i * 6;
        PicDaaRecord record;
        record.index = i;
        record.paddedSize = be16(bytes, entry);
        record.offset = be32(bytes, entry + 2);

        const quint32 end = (i + 1 < count)
                                ? be32(bytes, entry + 8)
                                : static_cast<quint32>(payload);
        if (record.offset < previousEnd || end < record.offset
            || end > static_cast<quint32>(payload)) {
            m_error = QStringLiteral("Alternate DAA offsets are not consecutive.");
            m_records.clear();
            return false;
        }
        previousEnd = end;

        const QByteArray compressed = bytes.mid(m_dataOffset + record.offset,
                                                end - record.offset);
        record.decoded = DaxFile::decode(compressed, record.paddedSize);
        if (record.decoded.size() < record.paddedSize)
            record.decoded.resize(record.paddedSize);

        if (record.decoded.size() >= 9) {
            record.height = static_cast<quint8>(record.decoded.at(0))
                            | (static_cast<quint8>(record.decoded.at(1)) << 8);
            record.width = static_cast<quint8>(record.decoded.at(2))
                           | (static_cast<quint8>(record.decoded.at(3)) << 8);
            record.x = static_cast<quint8>(record.decoded.at(4))
                       | (static_cast<quint8>(record.decoded.at(5)) << 8);
            record.y = static_cast<quint8>(record.decoded.at(6))
                       | (static_cast<quint8>(record.decoded.at(7)) << 8);
            record.frameCount = byteAt(record.decoded, 8);
        }
        m_records.append(record);
    }

    m_valid = true;
    return true;
}
