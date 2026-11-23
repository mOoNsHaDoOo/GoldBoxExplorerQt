#include "parse/amiga_tile.h"

#include "parse/glib_codec.h"

#include <algorithm>

namespace {

constexpr int kHeaderSize = 16;

quint16 be16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return static_cast<quint16>((static_cast<quint8>(data.at(offset)) << 8)
                                | static_cast<quint8>(data.at(offset + 1)));
}

quint32 be32(const QByteArray &data, int offset)
{
    return (static_cast<quint32>(be16(data, offset)) << 16) | be16(data, offset + 2);
}

quint8 byteAt(const QByteArray &data, int offset)
{
    return offset >= 0 && offset < data.size() ? static_cast<quint8>(data.at(offset)) : 0;
}

QVector<QRgb> greyPalette()
{
    QVector<QRgb> palette(32);
    for (int i = 0; i < 32; ++i) {
        const int v = (i % 16) * 17;
        palette[i] = qRgb(v, v, v);
    }
    return palette;
}

// Decoded bitplanes of one image, kept as the base of the next delta image.
struct Frame
{
    int widthBytes = 0;
    int height = 0;
    QVector<QByteArray> planes;
    QByteArray mask; // empty: opaque
};

QByteArray byteRun1(const QByteArray &data, int pos, int wanted)
{
    QByteArray out;
    out.reserve(wanted);
    while (out.size() < wanted && pos < data.size()) {
        const quint8 c = byteAt(data, pos++);
        if (c < 128) {
            out.append(data.mid(pos, c + 1));
            pos += c + 1;
        } else if (c > 128) {
            out.append(QByteArray(257 - c, static_cast<char>(byteAt(data, pos++))));
        }
    }
    out.truncate(wanted);
    return out;
}

QImage render(const Frame &frame, const QVector<QRgb> &palette)
{
    const int width = frame.widthBytes * 8;
    QImage image(width, frame.height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const int planeSize = frame.widthBytes * frame.height;
    for (int y = 0; y < frame.height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int byte = y * frame.widthBytes + x / 8;
            const int bit = 7 - (x & 7);
            if (!frame.mask.isEmpty()
                && !((static_cast<quint8>(frame.mask.at(byte)) >> bit) & 1))
                continue;
            int index = 0;
            for (int p = 0; p < frame.planes.size(); ++p) {
                if (byte < planeSize && ((static_cast<quint8>(frame.planes.at(p).at(byte)) >> bit) & 1))
                    index |= 1 << p;
            }
            line[x] = index < palette.size() ? palette.at(index) : qRgb(255, 0, 255);
        }
    }
    return image;
}

class Decoder
{
public:
    Decoder(AmigaTileResult &result, const QVector<QRgb> &fallback)
        : m_result(result), m_palette(fallback.isEmpty() ? greyPalette() : fallback)
    {
    }

    void library(const QByteArray &stored, int depth)
    {
        if (depth > 4 || stored.size() < kHeaderSize || stored.left(4) != QByteArrayLiteral("GLIB"))
            return;
        QString error;
        if (GlibCodec::compressionMethod(stored) > 0)
            ++m_result.compressedLibraries;
        const QByteArray lib = GlibCodec::decompress(stored, &error);
        if (lib.isEmpty()) {
            m_result.errors.append(error);
            return;
        }
        ++m_result.libraries;
        const int entries = be16(lib, 8);
        const bool table = byteAt(lib, 11) == 1;
        const QByteArray content = lib.mid(12, 4);
        QVector<int> pointers;
        for (int i = 0; i < entries; ++i)
            pointers.append(static_cast<int>(be32(lib, kHeaderSize + i * 4)));

        if (content == QByteArrayLiteral("GLIB")) {
            QVector<int> sorted = pointers;
            sorted.append(lib.size());
            std::sort(sorted.begin(), sorted.end());
            for (int i = 1; i < pointers.size(); ++i) {
                const int start = pointers.at(i);
                if (start <= 0 || start + kHeaderSize > lib.size())
                    continue;
                const auto next = std::upper_bound(sorted.begin(), sorted.end(), start);
                const int end = next == sorted.end() ? lib.size() : *next;
                library(lib.mid(start, end - start), depth + 1);
            }
        } else if (content == QByteArrayLiteral("TILE")) {
            tiles(lib, table ? pointers.mid(1) : pointers);
        }
    }

private:
    void tiles(const QByteArray &lib, const QVector<int> &pointers)
    {
        const int libraryIndex = m_result.libraries - 1;
        m_frame = Frame();
        for (int i = 0; i < pointers.size(); ++i) {
            const int at = pointers.at(i);
            if (at < kHeaderSize || at + 8 > lib.size()) {
                m_result.errors.append(QStringLiteral("library %1 image %2: offset %3 out of range")
                                           .arg(libraryIndex).arg(i).arg(at));
                continue;
            }
            const int height = be16(lib, at);
            const int yOffset = static_cast<qint16>(be16(lib, at + 2));
            const int xOffset = static_cast<qint16>(be16(lib, at + 4));
            const int widthBytes = byteAt(lib, at + 6);
            const quint8 method = byteAt(lib, at + 7);
            const int kind = method & 0x0F;
            const int high = method & 0xF0;
            const int data = at + 8;

            if (kind == 8) {
                const int count = high == 0xE0 ? 32 : 16;
                QVector<QRgb> palette = m_palette;
                palette.resize(qMax(palette.size(), 32));
                // The game keeps 4 bits per gun: (v + 8) / 17 ($43cca)
                auto gun = [&lib](int at) {
                    return (byteAt(lib, at) + 8) / 17 * 17;
                };
                for (int c = 0; c < count; ++c)
                    palette[c] = qRgb(gun(data + c * 3), gun(data + c * 3 + 1), gun(data + c * 3 + 2));
                m_palette = palette;
                m_result.palettes.append(palette.mid(0, count));
                m_havePalette = true;
                continue;
            }
            if (height <= 0 || widthBytes <= 0)
                continue;

            const int planes = high == 0xE0 ? 5 : 4;
            const int planeSize = widthBytes * height;
            Frame frame;
            frame.widthBytes = widthBytes;
            frame.height = height;
            bool ok = true;
            switch (kind) {
            case 0:
            case 1: {
                int pos = data;
                if (kind == 1) {
                    frame.mask = lib.mid(pos, planeSize);
                    pos += planeSize;
                }
                for (int p = 0; p < planes; ++p, pos += planeSize)
                    frame.planes.append(lib.mid(pos, planeSize));
                ok = pos <= lib.size();
                break;
            }
            case 2:
                ok = decodeRows(lib, data, high, planes, frame);
                break;
            case 3:
                ok = applyDelta(lib, data, planes, frame);
                break;
            default:
                m_result.errors.append(QStringLiteral("library %1 image %2: drawing method $%3 not supported")
                                           .arg(libraryIndex).arg(i).arg(method, 2, 16, QLatin1Char('0')));
                continue;
            }
            if (!ok) {
                m_result.errors.append(QStringLiteral("library %1 image %2: method $%3 data is damaged")
                                           .arg(libraryIndex).arg(i).arg(method, 2, 16, QLatin1Char('0')));
                continue;
            }
            for (QByteArray &plane : frame.planes)
                plane.resize(planeSize);
            if (!frame.mask.isEmpty())
                frame.mask.resize(planeSize);
            m_frame = frame;

            if (!m_havePalette)
                m_result.usedFallbackPalette = true;
            AmigaTileImage image;
            // every pixel inside the mask has colour 0?
            auto onlyMask = [&frame, planeSize]() {
                if (frame.mask.isEmpty())
                    return false;
                for (int b = 0; b < planeSize; ++b) {
                    const quint8 m = static_cast<quint8>(frame.mask.at(b));
                    for (const QByteArray &p : frame.planes)
                        if (static_cast<quint8>(p.at(b)) & m)
                            return false;
                }
                return true;
            };
            if (kind == 1 && onlyMask()) {
                // only the mask: show the shape instead of black on black
                QVector<QRgb> shape = m_palette;
                if (shape.isEmpty())
                    shape.append(0);
                shape[0] = qRgb(0xD0, 0xD0, 0xD0);
                image.image = render(frame, shape);
                image.maskOnly = true;
            } else {
                image.image = render(frame, m_palette);
            }
            image.library = libraryIndex;
            image.index = i;
            image.method = method;
            image.planes = frame.planes.size();
            image.xOffset = xOffset;
            image.yOffset = yOffset;
            m_result.images.append(image);
        }
    }

    // Method 2: ByteRun1 rows.  High nibble 0: 4-bit chunky pixels, converted
    // to planes by the game ($44fca); otherwise row-interleaved planes.
    static bool decodeRows(const QByteArray &lib, int pos, int high, int planes, Frame &frame)
    {
        const int w = frame.widthBytes;
        const int h = frame.height;
        const QByteArray rows = byteRun1(lib, pos, planes * w * h);
        if (rows.size() < planes * w * h)
            return false;
        frame.planes = QVector<QByteArray>(planes, QByteArray(w * h, '\0'));
        for (int y = 0; y < h; ++y) {
            const char *row = rows.constData() + y * planes * w;
            if (high == 0) {
                for (int x = 0; x < w * 8; ++x) {
                    const quint8 b = static_cast<quint8>(row[x / 2]);
                    const int v = (x & 1) ? (b & 0x0F) : (b >> 4);
                    for (int p = 0; p < planes; ++p) {
                        if ((v >> p) & 1)
                            frame.planes[p][y * w + x / 8] =
                                static_cast<char>(frame.planes[p][y * w + x / 8] | (0x80 >> (x & 7)));
                    }
                }
            } else {
                for (int p = 0; p < planes; ++p)
                    std::copy(row + p * w, row + (p + 1) * w, frame.planes[p].data() + y * w);
            }
        }
        return true;
    }

    // Method 3 ($43faa): header words = command bytes, data bytes per plane.
    // Command byte: 00nnnnnn skip n, 01nnnnnn n masked bytes (masks in the
    // command stream), 10nnnnnn fill n with one data byte, 11nnnnnn copy n;
    // 0 ends the row.  The same commands are run for every plane.
    bool applyDelta(const QByteArray &lib, int pos, int planes, Frame &frame) const
    {
        // Without a base of the same size (sprite sets, first image of a
        // library) the commands draw over an empty frame; the bytes they
        // touch become the mask.
        const bool hasBase = m_frame.widthBytes == frame.widthBytes && m_frame.height == frame.height
                             && m_frame.planes.size() >= planes;
        const int commandBytes = be16(lib, pos);
        const int dataPerPlane = be16(lib, pos + 2);
        const int commands = pos + 4;
        const int dataStart = commands + commandBytes;
        if (dataStart + planes * dataPerPlane > lib.size())
            return false;
        const int w = frame.widthBytes;
        if (hasBase) {
            frame.planes = m_frame.planes.mid(0, planes);
            frame.mask = m_frame.mask;
        } else {
            frame.planes = QVector<QByteArray>(planes, QByteArray(w * frame.height, '\0'));
            frame.mask = QByteArray(w * frame.height, '\0');
        }
        QByteArray *touched = hasBase ? nullptr : &frame.mask;
        for (int p = 0; p < planes; ++p) {
            QByteArray &plane = frame.planes[p];
            int c = commands;
            int d = dataStart + p * dataPerPlane;
            for (int y = 0; y < frame.height; ++y) {
                int col = 0;
                for (;;) {
                    if (c >= dataStart)
                        return false;
                    const quint8 op = byteAt(lib, c++);
                    if (op == 0)
                        break;
                    const int n = op & 0x3F;
                    const int type = op >> 6;
                    if (col + n > w)
                        return false;
                    char *dst = plane.data() + y * w + col;
                    char *seen = touched && p == 0 ? touched->data() + y * w + col : nullptr;
                    if (type == 1) {
                        for (int k = 0; k < n; ++k) {
                            const quint8 mask = byteAt(lib, c++);
                            if (seen)
                                seen[k] = static_cast<char>(static_cast<quint8>(seen[k]) | mask);
                            const quint8 value = byteAt(lib, d++);
                            dst[k] = static_cast<char>((static_cast<quint8>(dst[k]) & ~mask) | (value & mask));
                        }
                    } else if (type == 2) {
                        const char value = static_cast<char>(byteAt(lib, d++));
                        std::fill(dst, dst + n, value);
                    } else if (type == 3) {
                        for (int k = 0; k < n; ++k)
                            dst[k] = static_cast<char>(byteAt(lib, d++));
                    }
                    if (seen && type >= 2)
                        std::fill(seen, seen + n, static_cast<char>(0xFF));
                    col += n;
                }
            }
        }
        return true;
    }

    AmigaTileResult &m_result;
    QVector<QRgb> m_palette;
    Frame m_frame;
    bool m_havePalette = false;
};

} // namespace

namespace AmigaTile {

bool isAmigaTileLibrary(const QByteArray &file)
{
    if (file.size() < kHeaderSize || file.left(4) != QByteArrayLiteral("GLIB"))
        return false;
    if (be32(file, 4) != static_cast<quint32>(file.size())
        && GlibCodec::compressionMethod(file) == 0)
        return false;
    const QByteArray content = file.mid(12, 4);
    return content == QByteArrayLiteral("TILE") || content == QByteArrayLiteral("GLIB");
}

AmigaTileResult decode(const QByteArray &file, const QVector<QRgb> &fallbackPalette)
{
    AmigaTileResult result;
    Decoder decoder(result, fallbackPalette);
    decoder.library(file, 0);
    return result;
}

QVector<QRgb> firstPalette(const QByteArray &file)
{
    const AmigaTileResult result = decode(file);
    return result.palettes.isEmpty() ? QVector<QRgb>() : result.palettes.first();
}

} // namespace AmigaTile
