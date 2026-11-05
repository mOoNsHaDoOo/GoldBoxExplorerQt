#include "dax/daximagefile.h"

#include "core/byteutils.h"
#include "dax/palette.h"
#include "dax/renderblocks.h"

namespace {

quint16 readBe16(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 2 > data.size())
        return 0;
    return static_cast<quint16>((static_cast<quint8>(data.at(offset)) << 8)
                                | static_cast<quint8>(data.at(offset + 1)));
}

QVector<QImage> renderAmigaBlock(const QByteArray &data)
{
    QVector<QImage> images;
    if (data.size() < 12)
        return images;

    // Most Amiga DAX image blocks start with the 9-byte header directly.
    // pic.dax/sprit.dax/final.dax prepend eight bytes before the same header.
    int headerOffset = 0;
    auto heightAt = [&](int offset) { return readBe16(data, offset); };
    auto widthAt = [&](int offset) { return readBe16(data, offset + 2); };
    if (heightAt(0) <= 0 || heightAt(0) > 512
        || widthAt(0) <= 0 || widthAt(0) > 128) {
        headerOffset = 8;
    }

    const int height = heightAt(headerOffset);
    const int widthBytes = widthAt(headerOffset);
    const int xPos = readBe16(data, headerOffset + 4);
    const int yPos = readBe16(data, headerOffset + 6);
    const int itemCount = byteAt(data, headerOffset + 8);
    if (height <= 0 || height > 512 || widthBytes <= 0 || widthBytes > 128)
        return images;

    const int widthPx = widthBytes * 8;
    const int rowBytes = ((widthPx + 15) / 16) * 2;
    const int frameSize = 4 * rowBytes * height;
    if (frameSize <= 0)
        return images;

    const int dataStart = headerOffset + 9;
    const int availableFrames = (data.size() - dataStart) / frameSize;
    const int frames = itemCount > 0 ? itemCount : availableFrames;
    const int count = qMin(frames, availableFrames);
    if (count <= 0)
        return images;

    const QVector<QRgb> palette = Palette::basePalette();
    const int xOffset = xPos * 8;
    const int yOffset = yPos * 8;

    for (int frame = 0; frame < count; ++frame) {
        QImage image(widthPx + xOffset, height + yOffset, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        const int base = dataStart + frame * frameSize;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < widthPx; ++x) {
                int index = 0;
                for (int plane = 0; plane < 4; ++plane) {
                    const int offset = base + plane * rowBytes * height + y * rowBytes + x / 8;
                    if (offset < 0 || offset >= data.size())
                        continue;
                    const quint8 value = byteAt(data, offset);
                    if ((value >> (7 - (x & 7))) & 1)
                        index |= 1 << plane;
                }
                const int px = x + xOffset;
                const int py = y + yOffset;
                if (px >= 0 && px < image.width() && py >= 0 && py < image.height())
                    image.setPixel(px, py, palette.value(index, qRgb(0, 0, 0)));
            }
        }
        images.append(image);
    }
    return images;
}

} // namespace

DaxImageFile::DaxImageFile(const QString &path)
    : m_file(path)
{
    if (!m_file.isValid())
        return;

    if (m_file.isAmiga()) {
        for (const DaxFileBlock &block : m_file.blocks()) {
            for (const QImage &bitmap : renderAmigaBlock(block.data)) {
                m_bitmaps.append(bitmap);
                m_bitmapIds.append(block.id);
            }
        }
        return;
    }

    for (const DaxFileBlock &block : m_file.blocks()) {
        FileBlockParameters parameters;
        parameters.data = block.data;
        parameters.name = block.file;
        parameters.id = block.id;

        auto renderBlock = RenderBlockFactory::create(parameters);
        for (const QImage &bitmap : renderBlock->getBitmaps()) {
            m_bitmaps.append(bitmap);
            m_bitmapIds.append(renderBlock->blockId());
        }
    }
}
