#include "dax/renderblocks.h"

#include "core/byteutils.h"
#include "dax/palette.h"

#include <QFileInfo>

#include <algorithm>
#include <cstring>

namespace {

QString fileNameUpper(const QString &path)
{
    return QFileInfo(path).fileName().toUpper();
}

bool startsWith(const QString &text, const QString &prefix)
{
    return text.startsWith(prefix, Qt::CaseInsensitive);
}

QImage makeImage(int width, int height)
{
    if (width < 1 || height < 1)
        return {};
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    return image;
}

void putPixel(QImage &image, int x, int y, QRgb color)
{
    if (x >= 0 && x < image.width() && y >= 0 && y < image.height())
        image.setPixel(x, y, color);
}

bool isEgaSprite(const FileBlockParameters &parameters)
{
    const QByteArray &data = parameters.data;
    if (data.isEmpty())
        return false;
    const int frames = byteAt(data, 0);
    int offset = 1;
    if (frames == 0 || frames > 8)
        return false;

    for (int frame = 0; frame < frames; ++frame) {
        if (data.size() < 21 + offset)
            return false;
        offset += 4;
        const int height = readU16(data, offset);
        offset += 2;
        const int width = readU16(data, offset);
        offset += 2;
        const int xPos = readU16(data, offset);
        offset += 2;
        const int yPos = readU16(data, offset);
        offset += 3;
        offset += 8;

        const int widthPx = width * 8;
        const int heightPx = height;
        const int xPosPx = xPos * 8;
        const int yPosPx = yPos * 8;
        Q_UNUSED(xPosPx);
        Q_UNUSED(yPosPx);

        if (widthPx < 1 || heightPx < 1 || widthPx > 320 || heightPx > 200)
            return false;

        const int egaDataSize = height * width * 4;
        if (data.size() < egaDataSize + offset)
            return false;
        offset += egaDataSize;
    }
    return data.size() <= offset;
}

bool isVgaStrata(const FileBlockParameters &parameters)
{
    const QByteArray &data = parameters.data;
    if (data.size() < 20)
        return false;
    const int height = readU16(data, 0);
    const int width = readU16(data, 2);
    const int widthPx = width * 8;
    if (widthPx < 1 || height < 1 || widthPx > 320 || height > 200)
        return false;

    const int picCount = byteAt(data, 8);
    QByteArray palette;
    const int dataOffset = VgaStrataBlock::getDataOffset(data, 9, palette);
    const int expected = (5 * width * height * picCount) + dataOffset;
    return data.size() == expected;
}

bool isVgaSprite(const FileBlockParameters &parameters)
{
    const QByteArray &data = parameters.data;
    if (data.size() < 20)
        return false;
    if (byteAt(data, 1) != 0)
        return false;

    const int height = byteAt(data, 0);
    const int width = byteAt(data, 2);
    const int widthPx = width * 8;
    if (widthPx < 1 || height < 1 || widthPx > 320 || height > 200)
        return false;

    const int frames = byteAt(data, 8);
    const int clrBase = byteAt(data, 9);
    const int clrCount = byteAt(data, 10);
    if (clrBase == 0 || clrCount == 0)
        return false;

    int dataStart = clrCount * 3 + 132 + 3 * frames;
    const QString name = fileNameUpper(parameters.name);
    if (name.startsWith("SPRIT") && clrBase == 176 && clrCount == 80)
        dataStart = 306;

    const QByteArray unpacked = VgaSpriteBlock::unpackSpriteData(data, dataStart);
    if (unpacked.isEmpty())
        return false;

    const int picSize = widthPx * height;
    if (picSize <= 0 || unpacked.size() % picSize != 0)
        return false;
    return !Palette::extract(data, clrCount, clrBase, 11).isEmpty();
}

bool isVgaMixed(const FileBlockParameters &parameters)
{
    const QByteArray &data = parameters.data;
    if (data.size() < 5)
        return false;
    const int frames = byteAt(data, 0);
    if (frames > 8)
        return false;

    const int offset = 5;
    if (data.size() < 20 + offset)
        return false;

    const int height = readU16(data, offset);
    const int width = readU16(data, offset + 2);
    const int widthPx = width * 8;
    if (widthPx < 1 || height < 1 || widthPx > 320 || height > 200)
        return false;

    const bool egaBlock = (byteAt(data, 9 + offset) & 0xCC) == 0;
    const int blockSize = egaBlock ? (4 * width * height) : ((5 * width * height) + 14);
    if (data.size() < blockSize + 9 + offset)
        return false;
    return data.size() >= frames * blockSize;
}

bool isVga(const FileBlockParameters &parameters)
{
    const QByteArray &data = parameters.data;
    if (data.size() < 20)
        return false;
    const int height = byteAt(data, 0);
    const int width = byteAt(data, 1);
    const int widthPx = width * 8;
    if (widthPx < 1 || height < 1 || widthPx > 320 || height > 200)
        return false;

    const int chunkCount = byteAt(data, 6) + (byteAt(data, 7) << 8);
    const int chunkSize = widthPx * height;
    if (chunkCount * chunkSize > data.size())
        return false;

    const int clrBase = byteAt(data, 8);
    const int clrCount = byteAt(data, 9);
    return !Palette::extract(data, clrCount, clrBase, 10).isEmpty();
}

bool isEga(const FileBlockParameters &parameters)
{
    const QByteArray &data = parameters.data;
    if (data.size() < 9)
        return false;

    const int height = readU16(data, 0);
    const int width = readU16(data, 2);
    const int widthPx = width * 8;
    if (widthPx == 0 || height == 0 || widthPx > 320 || height > 200)
        return false;

    int itemCount = byteAt(data, 8);
    constexpr int egaDataOffset = 17;
    const int egaDataSize = height * width * 4;
    if (data.size() == (egaDataSize * (itemCount + 1)) + egaDataOffset)
        itemCount += 1; // Death Knights of Krynn
    return data.size() == ((egaDataSize * itemCount) + egaDataOffset);
}

bool isMono(const FileBlockParameters &parameters)
{
    return (parameters.data.size() % 8) == 0
           && fileNameUpper(parameters.name).startsWith("8X8");
}

QVector<QRgb> paletteBaseLocal()
{
    return Palette::basePalette();
}

} // namespace

EgaSpriteBlock::EgaSpriteBlock(const FileBlockParameters &parameters)
{
    QByteArray data = parameters.data;
    setBlockId(parameters.id);
    if (data.isEmpty())
        return;
    const int frames = byteAt(data, 0);
    int offset = 1;
    if (frames > 8)
        return;

    QVector<QRgb> colors(16);
    for (int i = 0; i < 16; ++i) {
        static const QRgb ega[16] = {
            Palette::black, Palette::blue, Palette::green, Palette::cyan,
            Palette::red, Palette::magenta, Palette::brown, Palette::lightGray,
            Palette::darkGray, Palette::brightBlue, Palette::brightGreen, Palette::brightCyan,
            Palette::brightRed, Palette::brightMagenta, Palette::brightYellow, Palette::brightWhite
        };
        colors[i] = ega[i];
    }

    const QString filename = fileNameUpper(parameters.name);
    bool xorFrames = startsWith(filename, QStringLiteral("PIC"));
    xorFrames |= startsWith(filename, QStringLiteral("FINAL"));
    if (startsWith(filename, QStringLiteral("SPRI"))) {
        colors[0] = qRgba(0, 0, 0, 0);
        colors[13] = Palette::black;
    }

    QByteArray firstFrame;
    for (int frame = 0; frame < frames; ++frame) {
        if (data.size() < 21 + offset)
            return;

        offset += 4; // delay, ignored
        const int height = readU16(data, offset);
        offset += 2;
        const int width = readU16(data, offset);
        offset += 2;
        const int xPos = readU16(data, offset);
        offset += 2;
        const int yPos = readU16(data, offset);
        offset += 3;
        offset += 8;

        const int widthPx = width * 8;
        const int heightPx = height;
        const int xPosPx = xPos * 8;
        const int yPosPx = yPos * 8;

        if (widthPx < 1 || heightPx < 1 || widthPx > 320 || heightPx > 200)
            return;

        const int egaDataSize = height * width * 4;
        if (data.size() < egaDataSize + offset)
            return;

        if (xorFrames) {
            if (frame == 0) {
                firstFrame = data.mid(offset, egaDataSize);
            } else {
                for (int i = 0; i < egaDataSize && i < firstFrame.size(); ++i)
                    data[offset + i] = static_cast<char>(data.at(offset + i) ^ firstFrame.at(i));
            }
        }

        QImage image = makeImage(widthPx + xPosPx, heightPx + yPosPx);
        for (int y = 0; y < heightPx; ++y) {
            for (int x = 0; x < widthPx; x += 2) {
                const int index = (y * width * 4) + (x / 2) + offset;
                if (index < 0 || index >= data.size())
                    continue;
                const quint8 b = byteAt(data, index);
                putPixel(image, x + xPosPx, y + yPosPx, colors[b >> 4]);
                putPixel(image, x + 1 + xPosPx, y + yPosPx, colors[b & 0x0F]);
            }
        }
        if (!image.isNull())
            m_bitmaps.append(image);
        offset += egaDataSize;
    }
}

int VgaStrataBlock::getDataOffset(const QByteArray &input, int offset, QByteArray &paletteOut)
{
    QByteArray var6A(0x20, '\0');
    QByteArray var4A(0x20, '\0');
    QByteArray var2A(0x18, '\0');
    QByteArray var12(0x10, '\0');
    paletteOut = QByteArray(0x3B, '\0');

    if (offset < 0 || offset >= input.size())
        return offset;

    if ((byteAt(input, offset) & 0xCC) == 0) {
        paletteOut[0] = 3;
        var12 = input.mid(offset, 8);
        for (int i = 0; i <= 0x0F; ++i) {
            var4A[i] = static_cast<char>(i);
            if ((i & 1) != 0)
                var6A[i] = static_cast<char>(byteAt(var12, i / 2) & 0x0F);
            else
                var6A[i] = static_cast<char>((byteAt(var12, i / 2) >> 4) & 0x0F);
        }
        offset += 8;
    } else {
        paletteOut[0] = input.at(offset);
        offset += 1;

        if ((byteAt(paletteOut, 0) & 1) != 0) {
            var12 = input.mid(offset, 8);
            for (int i = 0; i <= 0x1F; ++i) {
                const int dx = i + i + 6;
                var6A[i] = static_cast<char>((byteAt(var12, i / 4) >> dx) & 3);
            }
            offset += 8;
        }
        if ((byteAt(paletteOut, 0) & 2) != 0) {
            var12 = input.mid(offset, 0x10);
            for (int i = 0; i < 0x10; ++i) {
                var4A[i * 2 + 0] = static_cast<char>(byteAt(var12, i) & 0x0F);
                var4A[i * 2 + 1] = static_cast<char>((byteAt(var12, i) >> 4) & 0x0F);
            }
            offset += 0x10;
        }
        if ((byteAt(paletteOut, 0) & 8) != 0) {
            var2A = input.mid(offset, 0x18);
            offset += 0x18;
        }
    }

    for (int i = 0; i < 0x18; ++i)
        paletteOut[0x20 + i] = var2A.at(i);
    if ((byteAt(paletteOut, 0) & 8) != 0)
        paletteOut[0x39] = static_cast<char>(0xFF);
    return offset;
}

VgaStrataBlock::VgaStrataBlock(const FileBlockParameters &parameters)
{
    setBlockId(parameters.id);
    const QByteArray &data = parameters.data;
    if (data.size() < 20)
        return;

    const int height = readU16(data, 0);
    const int width = readU16(data, 2);
    const int xPos = readU16(data, 4);
    const int yPos = readU16(data, 6);
    const int picCount = byteAt(data, 8);
    const int widthPx = width * 8;
    const int heightPx = height;
    const int xPosPx = xPos * 8;
    const int yPosPx = yPos * 8;

    QByteArray tempPalette;
    int dataOffset = getDataOffset(data, 9, tempPalette);

    int dd[48] = {};
    constexpr int paletteBase = 0x20;
    for (int i = 0; i < 24; ++i) {
        const int b = byteAt(tempPalette, paletteBase + i);
        dd[0 + i] = (b & 0x0F) * 4;
        dd[24 + i] = (b & 0xF0) / 4;
    }

    QVector<QRgb> egaPalette = paletteBaseLocal();
    for (int i = 0; i < 14; ++i) {
        const int r = dd[(i * 3) + 6 + 0];
        const int g = dd[(i * 3) + 6 + 1];
        const int b = dd[(i * 3) + 6 + 2];
        egaPalette[i + 18] = qRgb(qMin(255, r * 4), qMin(255, g * 4), qMin(255, b * 4));
    }

    QVector<int> pixelColors(heightPx * widthPx, 0);

    for (int pic = 0; pic < picCount; ++pic) {
        int inOffset = dataOffset;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < widthPx; ++x) {
                const int mask = 0x80 >> (x & 7);
                const int inputByte = x / 8;
                int px = 0;
                const int rowStart = inOffset + y * width * 5;
                if ((byteAt(data, rowStart + inputByte) & mask) != 0)
                    px = 1;
                if ((byteAt(data, rowStart + inputByte + width) & mask) != 0)
                    px += 2;
                if ((byteAt(data, rowStart + inputByte + width * 2) & mask) != 0)
                    px += 4;
                if ((byteAt(data, rowStart + inputByte + width * 3) & mask) != 0)
                    px += 8;
                if ((byteAt(data, rowStart + inputByte + width * 4) & mask) != 0)
                    px += 0x10;
                pixelColors[y * widthPx + x] = px;
            }
        }
        inOffset += width * 5 * height;
        dataOffset = inOffset;

        QImage image = makeImage(widthPx + xPosPx, heightPx + yPosPx);
        for (int y = 0; y < heightPx; ++y) {
            for (int x = 0; x < widthPx; ++x) {
                const int index = pixelColors[y * widthPx + x];
                if (index >= 0 && index < egaPalette.size())
                    putPixel(image, x + xPosPx, y + yPosPx, egaPalette[index]);
            }
        }
        if (!image.isNull())
            m_bitmaps.append(image);
    }
}

QByteArray VgaSpriteBlock::unpackSpriteData(const QByteArray &data, int offset)
{
    QByteArray result;
    int inputIndex = offset;
    while (inputIndex < data.size()) {
        const qint8 runLength = static_cast<qint8>(data.at(inputIndex));
        if (runLength >= 0) {
            if (inputIndex + runLength + 1 >= data.size())
                return {};
            for (int i = 0; i <= runLength; ++i)
                result.append(data.at(inputIndex + i + 1));
            inputIndex += runLength + 2;
        } else {
            const int count = -runLength;
            if (inputIndex + 1 >= data.size())
                return {};
            for (int i = 0; i <= count; ++i)
                result.append(data.at(inputIndex + 1));
            inputIndex += 2;
        }
    }
    return result;
}

VgaSpriteBlock::VgaSpriteBlock(const FileBlockParameters &parameters)
{
    setBlockId(parameters.id);
    const QByteArray &origData = parameters.data;
    if (origData.size() < 20)
        return;

    const int height = byteAt(origData, 0);
    const int width = byteAt(origData, 2);
    const int widthPx = width * 8;
    const int heightPx = height;
    const int frames = byteAt(origData, 8);
    const int clrBase = byteAt(origData, 9);
    const int clrCount = byteAt(origData, 10);
    const int picSize = widthPx * heightPx;
    if (picSize <= 0)
        return;

    QVector<QRgb> colors = Palette::extract(origData, clrCount, clrBase, 11);
    if (colors.isEmpty())
        return;

    int dataStart = (clrCount * 3) + 132 + (3 * frames);
    const QString filename = fileNameUpper(parameters.name);
    const bool isSpriteFile = filename.startsWith("SPRIT");
    if (isSpriteFile) {
        colors[0] = qRgba(0, 0, 0, 0);
        colors[13] = Palette::black;
        if (clrBase == 176 && clrCount == 80)
            dataStart = 306;
    }

    const QByteArray data = unpackSpriteData(origData, dataStart);
    if (data.isEmpty())
        return;
    const int frameCount = data.size() / picSize;
    if (frameCount < 1)
        return;

    int mframe = byteAt(origData, (clrCount * 3) + 132 - 5);
    if (isSpriteFile)
        mframe = frameCount - 1;
    mframe = qBound(0, mframe, frameCount - 1);

    for (int frame = 0; frame < frameCount; ++frame) {
        QImage image = makeImage(widthPx, heightPx);
        for (int y = 0; y < heightPx; ++y) {
            for (int x = 0; x < widthPx; ++x) {
                const int base = (y * widthPx) + x;
                const int i1 = base + (picSize * frame);
                const int i3 = base + (picSize * mframe);
                if (i1 < 0 || i1 >= data.size())
                    continue;
                const quint8 b1 = byteAt(data, i1);
                const quint8 b3 = byteAt(data, i3);
                const int index = (frame != mframe) ? (b1 ^ b3) : b1;
                if (index >= 0 && index < colors.size())
                    putPixel(image, x, y, colors[index]);
            }
        }
        if (!image.isNull())
            m_bitmaps.append(image);
    }
}

int VgaMixedBlock::decodePaletteData(QByteArray &target, int offset, const QByteArray &input)
{
    QByteArray var6A(0x20, '\0');
    QByteArray var4A(0x20, '\0');
    QByteArray var2A(0x18, '\0');
    QByteArray var12(0x10, '\0');

    if (offset < 0 || offset >= input.size())
        return offset;

    if ((byteAt(input, offset) & 0xCC) == 0) {
        target[0] = 3;
        var12 = input.mid(offset, 8);
        for (int i = 0; i <= 0x0F; ++i) {
            var4A[i] = static_cast<char>(i);
            if ((i & 1) != 0)
                var6A[i] = static_cast<char>(byteAt(var12, i / 2) & 0x0F);
            else
                var6A[i] = static_cast<char>((byteAt(var12, i / 2) >> 4) & 0x0F);
        }
        offset += 8;
    } else {
        target[0] = input.at(offset);
        offset += 1;
        if ((byteAt(target, 0) & 1) != 0) {
            var12 = input.mid(offset, 8);
            for (int i = 0; i <= 0x1F; ++i) {
                const int dx = i + i + 6;
                var6A[i] = static_cast<char>((byteAt(var12, i / 4) >> dx) & 3);
            }
            offset += 8;
        }
        if ((byteAt(target, 0) & 2) != 0) {
            var12 = input.mid(offset, 0x10);
            for (int i = 0; i < 0x10; ++i) {
                var4A[i * 2 + 0] = static_cast<char>(byteAt(var12, i) & 0x0F);
                var4A[i * 2 + 1] = static_cast<char>((byteAt(var12, i) >> 4) & 0x0F);
            }
            offset += 0x10;
        }
        if ((byteAt(target, 0) & 8) != 0) {
            var2A = input.mid(offset, 0x18);
            offset += 0x18;
        }
    }

    for (int i = 0; i < 0x18; ++i)
        target[0x20 + i] = var2A.at(i);
    if ((byteAt(target, 0) & 8) != 0)
        target[0x39] = static_cast<char>(0xFF);
    return offset;
}

VgaMixedBlock::VgaMixedBlock(const FileBlockParameters &parameters)
{
    setBlockId(parameters.id);
    const QByteArray &data = parameters.data;
    if (data.size() < 5)
        return;

    const int frames = byteAt(data, 0);
    const int offset = 5;
    if (frames > 8 || data.size() < 20 + offset)
        return;

    const int height = readU16(data, offset);
    const int width = readU16(data, offset + 2);
    const int xPos = readU16(data, offset + 4);
    const int yPos = readU16(data, offset + 6);
    const int widthPx = width * 8;
    const int heightPx = height;
    const int xPosPx = xPos * 8;
    const int yPosPx = yPos * 8;

    if (widthPx < 1 || heightPx < 1 || widthPx > 320 || heightPx > 200)
        return;

    const bool egaBlock = (byteAt(data, 9 + offset) & 0xCC) == 0;
    const int blockSize = egaBlock ? (4 * width * height) : (5 * width * height);
    if (data.size() < blockSize + 9 + offset)
        return;

    QByteArray tempPalette(0x3B, '\0');
    int dataOffset = decodePaletteData(tempPalette, 9 + offset, data);

    int dd[48] = {};
    constexpr int paletteBase = 0x20;
    for (int i = 0; i < 24; ++i) {
        const int b = byteAt(tempPalette, paletteBase + i);
        dd[0 + i] = (b & 0x0F) * 4;
        dd[24 + i] = (b & 0xF0) / 4;
    }

    QVector<QRgb> colors = paletteBaseLocal();
    for (int i = 0; i < 14; ++i) {
        const int r = dd[(i * 3) + 6 + 0];
        const int g = dd[(i * 3) + 6 + 1];
        const int b = dd[(i * 3) + 6 + 2];
        colors[i + 18] = qRgb(qMin(255, r * 4), qMin(255, g * 4), qMin(255, b * 4));
    }

    QVector<int> pixelColors(heightPx * widthPx, 0);

    for (int frame = 0; frame < frames; ++frame) {
        if (egaBlock) {
            int inOffset = dataOffset;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < widthPx; x += 2) {
                    const quint8 b = byteAt(data, inOffset++);
                    pixelColors[y * widthPx + x] = b >> 4;
                    if (x + 1 < widthPx)
                        pixelColors[y * widthPx + x + 1] = b & 0x0F;
                }
            }
            dataOffset = inOffset;
        } else {
            int inOffset = dataOffset;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < widthPx; ++x) {
                    const int mask = 0x80 >> (x & 7);
                    const int inputByte = x / 8;
                    int px = 0;
                    if ((byteAt(data, inOffset + inputByte) & mask) != 0)
                        px = 1;
                    if ((byteAt(data, inOffset + inputByte + width) & mask) != 0)
                        px += 2;
                    if ((byteAt(data, inOffset + inputByte + width * 2) & mask) != 0)
                        px += 4;
                    if ((byteAt(data, inOffset + inputByte + width * 3) & mask) != 0)
                        px += 8;
                    if ((byteAt(data, inOffset + inputByte + width * 4) & mask) != 0)
                        px += 0x10;
                    pixelColors[y * widthPx + x] ^= px;
                }
                inOffset += width * 5;
            }
            dataOffset = inOffset + 14;
        }

        QImage image = makeImage(widthPx + xPosPx, heightPx + yPosPx);
        for (int y = 0; y < heightPx; ++y) {
            for (int x = 0; x < widthPx; ++x) {
                const int index = pixelColors[y * widthPx + x];
                if (index >= 0 && index < colors.size())
                    putPixel(image, x + xPosPx, y + yPosPx, colors[index]);
            }
        }
        if (!image.isNull())
            m_bitmaps.append(image);
    }
}

VgaBlock::VgaBlock(const FileBlockParameters &parameters)
{
    setBlockId(parameters.id);
    const QByteArray &data = parameters.data;
    if (data.size() < 20)
        return;

    const int height = byteAt(data, 0);
    const int width = byteAt(data, 1);
    const int widthPx = width * 8;
    const int heightPx = height;
    const int clrCount = byteAt(data, 9) + 1;
    const int chunkSize = widthPx * heightPx;
    const int chunkCount = readU16(data, 6);
    int dataStart = data.size() - (chunkCount * chunkSize);
    if (chunkSize <= 0 || dataStart < 0)
        return;

    for (int chunk = 0; chunk < chunkCount; ++chunk, dataStart += chunkSize) {
        const QVector<QRgb> colors = Palette::extract(data, clrCount, byteAt(data, 8), 10);
        if (colors.isEmpty())
            return;

        QImage image = makeImage(widthPx, heightPx);
        for (int y = 0; y < heightPx; ++y) {
            for (int x = 0; x < widthPx; ++x) {
                const int index = byteAt(data, dataStart + (y * widthPx) + x);
                if (index >= 0 && index < colors.size())
                    putPixel(image, x, y, colors[index]);
            }
        }
        if (!image.isNull())
            m_bitmaps.append(image);
    }
}

EgaBlock::EgaBlock(const FileBlockParameters &parameters)
{
    setBlockId(parameters.id);
    const QByteArray &data = parameters.data;
    if (data.size() < 9)
        return;

    const int height = readU16(data, 0);
    const int width = readU16(data, 2);
    const int xPos = readU16(data, 4);
    const int yPos = readU16(data, 6);
    int itemCount = byteAt(data, 8);
    const int widthPx = width * 8;
    const int heightPx = height;
    const int xPosPx = xPos * 8;
    const int yPosPx = yPos * 8;
    constexpr int egaDataOffset = 17;
    const int egaDataSize = height * width * 4;

    static const QRgb ega[16] = {
        Palette::black, Palette::blue, Palette::green, Palette::cyan,
        Palette::red, Palette::magenta, Palette::brown, Palette::lightGray,
        Palette::darkGray, Palette::brightBlue, Palette::brightGreen, Palette::brightCyan,
        Palette::brightRed, Palette::brightMagenta, Palette::brightYellow, Palette::brightWhite
    };
    static const QRgb egaCombat[16] = {
        Palette::transparentCombat, Palette::blue, Palette::green, Palette::cyan,
        Palette::red, Palette::magenta, Palette::brown, Palette::lightGray,
        Palette::black, Palette::brightBlue, Palette::brightGreen, Palette::brightCyan,
        Palette::brightRed, Palette::brightMagenta, Palette::brightYellow, Palette::brightWhite
    };

    const QString filename = fileNameUpper(parameters.name);
    const bool combat = filename.contains("CPIC") || filename.contains("CHEAD")
                        || filename.contains("CBODY") || filename.contains("DUNGCOM")
                        || filename.contains("WILDCOM") || filename.contains("RANDCOM")
                        || filename.contains("COMSPR");
    const QRgb *colors = combat ? egaCombat : ega;

    if (data.size() == (egaDataSize * (itemCount + 1)) + egaDataOffset)
        itemCount += 1;

    if (data.size() != (egaDataSize * itemCount) + egaDataOffset)
        return;

    int offset = 0;
    for (int i = 0; i < itemCount; ++i, offset += egaDataSize) {
        QImage image = makeImage(widthPx + xPosPx, heightPx + yPosPx);
        for (int y = 0; y < heightPx; ++y) {
            for (int x = 0; x < widthPx; x += 2) {
                const int index = egaDataOffset + (y * width * 4) + (x / 2) + offset;
                if (index < 0 || index >= data.size())
                    continue;
                const quint8 b = byteAt(data, index);
                putPixel(image, x + xPosPx, y + yPosPx, colors[b >> 4]);
                putPixel(image, x + 1 + xPosPx, y + yPosPx, colors[b & 0x0F]);
            }
        }
        if (!image.isNull())
            m_bitmaps.append(image);
    }
}

MonoBlock::MonoBlock(const FileBlockParameters &parameters)
{
    setBlockId(parameters.id);
    static const int monoBitMask[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
    const int count = parameters.data.size() / 8;
    for (int ch = 0; ch < count; ++ch) {
        QImage image = makeImage(8, 8);
        for (int y = 0; y < 8; ++y) {
            const quint8 b = byteAt(parameters.data, (ch * 8) + y);
            for (int x = 0; x < 8; ++x)
                putPixel(image, x, y, ((b & monoBitMask[x]) != 0) ? Palette::brightWhite : Palette::black);
        }
        m_bitmaps.append(image);
    }
}

std::unique_ptr<RenderBlock> RenderBlockFactory::create(const FileBlockParameters &parameters)
{
    if (isEgaSprite(parameters))
        return std::make_unique<EgaSpriteBlock>(parameters);
    if (isVgaStrata(parameters))
        return std::make_unique<VgaStrataBlock>(parameters);
    if (isVgaSprite(parameters))
        return std::make_unique<VgaSpriteBlock>(parameters);
    if (isVgaMixed(parameters))
        return std::make_unique<VgaMixedBlock>(parameters);
    if (isVga(parameters))
        return std::make_unique<VgaBlock>(parameters);
    if (isEga(parameters))
        return std::make_unique<EgaBlock>(parameters);
    if (isMono(parameters))
        return std::make_unique<MonoBlock>(parameters);
    return std::make_unique<UnknownBlock>();
}
