#include "dax/palette.h"

#include "core/byteutils.h"

namespace Palette {

QVector<QRgb> basePalette()
{
    static const QRgb ega[16] = {
        black, blue, green, cyan, red, magenta, brown, lightGray,
        darkGray, brightBlue, brightGreen, brightCyan, brightRed,
        brightMagenta, brightYellow, brightWhite
    };

    QVector<QRgb> colors(256);
    for (int block = 0; block < 16; ++block) {
        for (int i = 0; i < 16; ++i)
            colors[block * 16 + i] = ega[i];
    }
    return colors;
}

QVector<QRgb> extract(const QByteArray &data, int colorCount, int colorBase, int offset)
{
    QVector<QRgb> colors = basePalette();
    if (colorCount < 0 || colorBase < 0 || colorBase + colorCount > colors.size())
        return {};

    for (int i = 0; i < colorCount; ++i) {
        const int p = offset + i * 3;
        if (p + 2 >= data.size())
            return {};

        const int r = byteAt(data, p) * 4;
        const int g = byteAt(data, p + 1) * 4;
        const int b = byteAt(data, p + 2) * 4;
        if (r > 255 || g > 255 || b > 255)
            return {};

        colors[colorBase + i] = qRgb(r, g, b);
    }
    return colors;
}

} // namespace Palette
