#include "tbl/tlbfile.h"

#include "core/byteutils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

QImage makeImage(int width, int height)
{
    if (width <= 0 || height <= 0)
        return {};
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    return image;
}

QVector<QRgb> defaultPalette()
{
    QVector<QRgb> palette(256, qRgb(0, 0, 0));
    palette[0] = qRgb(0, 0, 0);
    palette[1] = qRgb(0, 0, 171);
    palette[2] = qRgb(0, 171, 0);
    palette[3] = qRgb(0, 171, 171);
    palette[4] = qRgb(171, 0, 0);
    palette[5] = qRgb(171, 0, 171);
    palette[6] = qRgb(172, 87, 0);
    palette[7] = qRgb(171, 171, 171);
    palette[8] = qRgb(87, 87, 87);
    palette[9] = qRgb(87, 87, 255);
    palette[10] = qRgb(87, 255, 87);
    palette[11] = qRgb(87, 255, 255);
    palette[12] = qRgb(255, 87, 87);
    palette[13] = qRgb(255, 87, 255);
    palette[14] = qRgb(255, 255, 87);
    palette[15] = qRgb(255, 255, 255);
    palette[255] = qRgb(103, 247, 159);
    return palette;
}

} // namespace

bool TlbFile::load(const QString &path)
{
    m_fullPath = path;
    m_bytes.clear();
    m_valid = false;
    m_error.clear();
    m_palette = defaultPalette();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }
    m_bytes = file.readAll();
    if (m_bytes.size() < 16) {
        m_error = QStringLiteral("File is too small to be a TLB file.");
        return false;
    }
    m_valid = true;
    return true;
}

void TlbFile::loadBitmaps()
{
    m_bitmaps.clear();
    m_offsets.clear();
    m_imageOffsets.clear();
    m_palette = defaultPalette();
    if (!m_valid)
        return;

    m_combatIcon = false;
    const QString upperPath = m_fullPath.toUpper();
    if (upperPath.contains(QStringLiteral("CPIC"))
        && !upperPath.contains(QStringLiteral("CPIC.TLB")))
        m_combatIcon = true;
    if (upperPath.contains(QStringLiteral("CBODY")))
        m_combatIcon = true;

    ByteReader reader(m_bytes);
    readHeader(reader);
    if (!reader.ok())
        return;

    if (m_header.contentType == QStringLiteral("TILE")) {
        const int imageCount = getImageCount(reader);
        readTileLibrary(reader, imageCount, 0);
    } else {
        readMasterLibrary(reader);
    }
}

void TlbFile::readHeader(ByteReader &reader)
{
    m_header.fileType = QString::fromLatin1(reader.bytes(4));
    m_header.fileSize = reader.u32();
    m_header.entries = reader.u16();
    m_header.unused = reader.u8();
    m_header.hasTable = reader.u8() == 1;
    m_header.contentType = QString::fromLatin1(reader.bytes(4));
}

int TlbFile::getImageCount(ByteReader &reader)
{
    int imageCount = 0;
    if (m_header.hasTable) {
        m_imageIdTableOffset = reader.u32();
        m_colorDataOffset = reader.u32();
        imageCount = static_cast<int>(m_header.entries) - 2;
    } else {
        m_colorDataOffset = reader.u32();
        imageCount = static_cast<int>(m_header.entries) - 1;
    }
    return qMax(0, imageCount);
}

void TlbFile::readMasterLibrary(ByteReader &reader)
{
    reader.u32(); // offset to set id table, unused

    QVector<quint32> imagePointers;
    const int count = qMax(0, static_cast<int>(m_header.entries) - 1);
    for (int i = 0; i < count; ++i)
        imagePointers.append(reader.u32());

    for (quint32 pointer : imagePointers) {
        reader.seek(static_cast<int>(pointer));
        readHeader(reader);
        const int imageCount = getImageCount(reader);
        readTileLibrary(reader, imageCount, pointer);
    }
}

void TlbFile::readTileLibrary(ByteReader &reader, int imageCount, quint32 startingPoint)
{
    m_offsets.clear();
    for (int i = 0; i < imageCount; ++i)
        m_offsets.append(reader.u32());

    if (m_combatIcon) {
        if (m_fullPath.toUpper().contains(QStringLiteral("CBODY"))) {
            // FRUA CBODY.TLB needs both an external and an internal colour table.
            const quint32 temporary = m_colorDataOffset;
            findAndLoadExternalColorTable();
            m_colorDataOffset = temporary;
            readColorTable(reader, startingPoint);
        } else {
            // Some FRUA combat icons use the colour pointer as an action image.
            reader.seek(16);
            imageCount += 1;
            const quint32 actionImage = reader.u32();
            m_offsets.append(actionImage);
            findAndLoadExternalColorTable();
        }
    } else {
        readColorTable(reader, startingPoint);
    }

    for (int entry = 0; entry < imageCount; ++entry) {
        if (entry >= m_offsets.size())
            break;
        const quint32 offset = m_offsets.at(entry);
        reader.seek(static_cast<int>(offset + startingPoint));
        if (!reader.ok())
            break;

        TlbImageHeader header;
        header.imageHeight = reader.u16();
        header.verticalOffset = reader.i16();
        header.horizontalOffset = reader.i16();
        header.imageWidth = reader.u8() * 4;
        header.drawingMethod = reader.u8();

        QImage image;
        switch (header.drawingMethod) {
        case 0:
            break;
        case 16:
            image = createBitmap(header, reader);
            break;
        case 17: {
            const int pixelsToRead = header.imageHeight * header.imageWidth;
            QByteArray mask = deinterlacePixels(reader.bytes(pixelsToRead));
            QByteArray pixels = deinterlacePixels(reader.bytes(pixelsToRead));
            for (int i = 0; i < mask.size() && i < pixels.size(); ++i) {
                if (static_cast<quint8>(mask.at(i)) == 255)
                    pixels[i] = static_cast<char>(255);
            }
            image = createBitmapFromBuffer(header, pixels);
            break;
        }
        case 18: {
            const int target = header.imageWidth * header.imageHeight;
            const int width = header.imageWidth;
            int plotted = 0;
            int screenOffset = 0;
            int runCount = 0;
            QByteArray pixels(target, '\0');
            while (plotted < target && reader.remaining() > 0) {
                const int x = reader.u8();
                if (x < 128) {
                    const int length = x + 1;
                    for (int i = 0; i < length && plotted < target; ++i) {
                        const quint8 value = reader.u8();
                        const int index = screenOffset + width * (runCount / 4);
                        if (index >= 0 && index < pixels.size())
                            pixels[index] = static_cast<char>(value);
                        screenOffset += 4;
                        if (screenOffset >= width) {
                            ++runCount;
                            screenOffset = runCount % 4;
                        }
                        ++plotted;
                    }
                } else {
                    const quint8 value = reader.u8();
                    const int length = 257 - x;
                    for (int i = 0; i < length && plotted < target; ++i) {
                        const int index = screenOffset + width * (runCount / 4);
                        if (index >= 0 && index < pixels.size())
                            pixels[index] = static_cast<char>(value);
                        screenOffset += 4;
                        if (screenOffset >= width) {
                            ++runCount;
                            screenOffset = runCount % 4;
                        }
                        ++plotted;
                    }
                }
            }
            image = createBitmapFromBuffer(header, pixels);
            break;
        }
        case 21:
            image = createBitmap(header, reader);
            break;
        case 23: {
            QByteArray buffer(4 + header.imageHeight * header.imageWidth, '\0');
            const int rowsToPlot = header.imageHeight;
            int row = 0;
            int pos = 0;
            int sweep = 0;
            while (sweep < 4 && reader.remaining() > 0) {
                const int value = reader.u8();
                if (value == 0) {
                    ++row;
                    pos = 0;
                    if (row >= rowsToPlot) {
                        ++sweep;
                        row = 0;
                    }
                } else if (value >= 128) {
                    pos += (257 - value - 1) * 4;
                } else {
                    for (int i = 0; i < value; ++i) {
                        const quint8 pixel = reader.u8();
                        const int index = (row * header.imageWidth) + pos + sweep;
                        if (index >= 0 && index < buffer.size())
                            buffer[index] = static_cast<char>(pixel);
                        pos += 4;
                    }
                }
            }
            image = createBitmapFromBuffer(header, buffer);
            break;
        }
        case 24:
        case 25:
            break;
        default:
            break;
        }

        if (!image.isNull()) {
            m_bitmaps.append(image);
            m_imageOffsets.append(QPoint(header.verticalOffset, header.verticalOffset));
        }
    }
}

void TlbFile::readColorTable(ByteReader &reader, quint32 startingPoint)
{
    reader.seek(static_cast<int>(m_colorDataOffset + startingPoint));
    if (!reader.ok())
        return;
    readColorTableHeader(reader);
    readColorTableData(reader);
}

void TlbFile::readColorTableHeader(ByteReader &reader)
{
    reader.u16(); // cycling value
    m_colorFirst = reader.u16();
    m_colorCount = reader.u16();
    reader.u8(); // cycling ranges
    reader.u8(); // magic
}

void TlbFile::readColorTableData(ByteReader &reader)
{
    if (m_fullPath.toLower().contains(QStringLiteral("topview.tlb")))
        return;

    for (int color = 0; color < m_colorCount; ++color) {
        const quint8 red = reader.u8();
        const quint8 green = reader.u8();
        const quint8 blue = reader.u8();
        const int index = color + m_colorFirst;
        if (index >= 0 && index < m_palette.size())
            m_palette[index] = qRgb(red, green, blue);
    }
}

void TlbFile::findAndLoadExternalColorTable()
{
    static const char *locations[] = {
        "DUNGCOM.TLB", "DEFAULT.DSN/DUNGCOM.TLB", "DISK2/DUNGCOM.TLB", "../DISK2/DUNGCOM.TLB"
    };
    const QDir dir = QFileInfo(m_fullPath).absoluteDir();
    for (const char *location : locations) {
        const QString path = dir.filePath(QString::fromLatin1(location));
        if (QFileInfo::exists(path)) {
            loadExternalColorTable(path);
            return;
        }
    }
}

void TlbFile::loadExternalColorTable(const QString &path)
{
    TlbFile other(path);
    other.loadBitmaps();
    if (other.m_palette.size() == 256)
        m_palette = other.m_palette;
}

QImage TlbFile::createBitmap(const TlbImageHeader &header, ByteReader &reader)
{
    const int pixelsToRead = header.imageHeight * header.imageWidth;
    QByteArray buffer = deinterlacePixels(reader.bytes(pixelsToRead));
    return createBitmapFromBuffer(header, buffer);
}

QImage TlbFile::createBitmapFromBuffer(const TlbImageHeader &header, const QByteArray &buffer)
{
    QImage image = makeImage(header.imageWidth, header.imageHeight);
    if (image.isNull())
        return image;

    for (int y = 0; y < header.imageHeight; ++y) {
        for (int x = 0; x < header.imageWidth; ++x) {
            const int index = y * header.imageWidth + x;
            if (index < 0 || index >= buffer.size())
                continue;
            const quint8 pixelIndex = static_cast<quint8>(buffer.at(index));
            if ((header.drawingMethod == 21 || header.drawingMethod == 17) && pixelIndex == 255)
                continue;
            if (pixelIndex < m_palette.size())
                image.setPixel(x, y, m_palette.at(pixelIndex));
        }
    }
    return image;
}

QByteArray TlbFile::deinterlacePixels(const QByteArray &buffer)
{
    const int numberOfPixels = buffer.size();
    QByteArray result(numberOfPixels, '\0');
    int index = 0;
    int pixelOffset = 0;
    int destPixel = 0;

    for (char b : buffer) {
        if (numberOfPixels == 0)
            break;
        const int destOffset = (destPixel % numberOfPixels) + pixelOffset;
        if (destOffset >= 0 && destOffset < result.size())
            result[destOffset] = b;
        ++index;
        destPixel = index * 4;
        if (numberOfPixels > 0 && destPixel % numberOfPixels == 0)
            ++pixelOffset;
    }
    return result;
}

QVector<QPoint> TlbFile::imageOffsets() const
{
    return m_imageOffsets;
}
