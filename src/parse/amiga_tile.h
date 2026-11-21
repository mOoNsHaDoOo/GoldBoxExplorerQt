#pragma once

#include <QByteArray>
#include <QImage>
#include <QRgb>
#include <QString>
#include <QStringList>
#include <QVector>

// Image libraries ("GLIB" with content type TILE, or GLIB of GLIBs) of the
// Amiga Gold Box games that use the GLIB format: Curse of the Azure Bonds,
// Secret of the Silver Blades, Pools of Darkness, Dark Queen of Krynn.
//
// Layout, as drawn by the Secret of the Silver Blades executable ($3fbec):
// - a GLIB of GLIBs lists the set table first, then one sub-library per
//   picture set; every library may be compressed (see GlibCodec);
// - in a TILE library every pointer is an image, except the first one when
//   byte 11 of the header is 1 (identifier table);
// - an image starts with an 8-byte header: height, vertical and horizontal
//   offset (signed words), width in bytes, drawing method;
// - the low nibble of the method selects the encoding:
//     0 raw bitplanes, plane after plane
//     1 mask plane followed by the raw bitplanes (mask bit 0 = transparent)
//     2 ByteRun1 rows: 4-bit chunky pixels (high nibble 0) or row-interleaved
//       bitplanes (other high nibbles)
//     3 delta against the previous image of the library: one command stream
//       applied to every plane, with per-plane data
//     8 palette: 16 RGB triplets (32 with high nibble E), no image
// - the high nibble selects the depth: 0 = 4 planes, E = 5 planes, any other
//   value = 4 row-interleaved planes (title pictures).
struct AmigaTileImage
{
    QImage image;
    int library = 0;
    int index = 0;
    quint8 method = 0;
    int planes = 0;
    int xOffset = 0;
    int yOffset = 0;
};

struct AmigaTileResult
{
    QVector<AmigaTileImage> images;
    QVector<QVector<QRgb>> palettes;
    QStringList errors;
    int libraries = 0;
    int compressedLibraries = 0;
    bool usedFallbackPalette = false;
};

namespace AmigaTile {

// True for a big-endian GLIB file whose content is TILE or GLIB.
bool isAmigaTileLibrary(const QByteArray &file);

// Decodes every image.  fallbackPalette is used for the images that come
// before any palette entry of the file (the game keeps the palette of the
// previously loaded library); when it is empty a grey ramp is used.
AmigaTileResult decode(const QByteArray &file, const QVector<QRgb> &fallbackPalette = {});

// First palette entry found in the file (decompressing as needed).
QVector<QRgb> firstPalette(const QByteArray &file);

} // namespace AmigaTile
