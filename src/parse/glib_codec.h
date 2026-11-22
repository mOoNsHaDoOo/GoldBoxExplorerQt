#pragma once

#include <QByteArray>
#include <QString>

// Decompression of the Amiga "GLIB" libraries of Curse of the Azure Bonds,
// Secret of the Silver Blades, Pools of Darkness and Dark Queen of Krynn.
//
// Byte 10 of the 16-byte GLIB header selects how the library body (everything
// after the header) is stored.  The game (LBDecode, Secret of the Silver Blades
// executable at $417a4) rebuilds the body in $2000-byte output slices; each
// slice restarts the bit/byte stream but keeps the decoder state.  The stored
// body starts with a 16-bit slack word that is skipped.
//
//   0  stored as is
//   3  10-bit LZW, 1024 codes, leaf entries recycled when the table is full
//      ($41b3c init, $41baa/$422fc decode, $41abe recycle)
//   5  byte-oriented LZ with two back-reference forms and literal runs ($454f2)
//
// Methods 1, 2 and 4 exist in the decoder switch (plain copy and a table
// driven text coder) but no game file uses them.
namespace GlibCodec {

// Returns the compression byte of a GLIB header, or -1 when the data does not
// start with a GLIB header.
int compressionMethod(const QByteArray &library);

// Returns the library with its body decompressed and byte 10 cleared, the
// input unchanged when it is not compressed, or an empty array (and an error
// message) when the method is unknown or the stream is damaged.
QByteArray decompress(const QByteArray &library, QString *error = nullptr);

} // namespace GlibCodec
