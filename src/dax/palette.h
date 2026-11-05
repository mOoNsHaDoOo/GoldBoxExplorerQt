#pragma once

#include <QByteArray>
#include <QVector>
#include <QRgb>

// The 16 colour EGA palette used by the Gold Box games, plus the VGA palette
// extraction helpers used by the DAX renderers.
namespace Palette {

QVector<QRgb> basePalette();

// Returns an empty vector when a palette component is out of range, which
// mirrors the original C# code returning null.
QVector<QRgb> extract(const QByteArray &data, int colorCount, int colorBase, int offset);

constexpr QRgb black = qRgb(0x00, 0x00, 0x00);
constexpr QRgb blue = qRgb(0x00, 0x00, 0xAA);
constexpr QRgb green = qRgb(0x00, 0xAA, 0x00);
constexpr QRgb cyan = qRgb(0x00, 0xAA, 0xAA);
constexpr QRgb red = qRgb(0xAA, 0x00, 0x00);
constexpr QRgb magenta = qRgb(0xAA, 0x00, 0xAA);
constexpr QRgb brown = qRgb(0xAA, 0x55, 0x00);
constexpr QRgb lightGray = qRgb(0xAA, 0xAA, 0xAA);
constexpr QRgb darkGray = qRgb(0x55, 0x55, 0x55);
constexpr QRgb brightBlue = qRgb(0x55, 0x55, 0xFF);
constexpr QRgb brightGreen = qRgb(0x55, 0xFF, 0x55);
constexpr QRgb brightCyan = qRgb(0x55, 0xFF, 0xFF);
constexpr QRgb brightRed = qRgb(0xFF, 0x55, 0x55);
constexpr QRgb brightMagenta = qRgb(0xFF, 0x55, 0xFF);
constexpr QRgb brightYellow = qRgb(0xFF, 0xFF, 0x55);
constexpr QRgb brightWhite = qRgb(0xFF, 0xFF, 0xFF);
constexpr QRgb transparentCombat = qRgb(0x67, 0xF7, 0x9F);

} // namespace Palette
