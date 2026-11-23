# Amiga GLIB image libraries

Curse of the Azure Bonds, Secret of the Silver Blades, Pools of Darkness and
Dark Queen of Krynn keep their pictures in big-endian `GLIB` libraries
(`*.TLB`).  The layout below was read from the Secret of the Silver Blades
executable (addresses are offsets in its code hunk); the same reader handles
all four games.

Code: `src/parse/glib_codec.*` (decompression) and `src/parse/amiga_tile.*`
(images), used by `TlbFile` for every big-endian GLIB TILE file.

## Library header (16 bytes)

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 4 | `GLIB` |
| 4 | 4 | library size, header included, **after** decompression |
| 8 | 2 | number of pointers |
| 10 | 1 | compression method (0, 3 or 5) |
| 11 | 1 | 1 = the first pointer is an identifier table |
| 12 | 4 | content type: `TILE`, `GLIB`, `DATA`, `TEXT`, `DIG8` |

A library whose content type is `GLIB` holds other libraries: its first
pointer is the set table, every other pointer is a sub-library, and each
sub-library is compressed on its own.

## Compression (`LBDecode`, $417a4)

The body after the header starts with a 16-bit slack word, then the stream.
The body is rebuilt in $2000-byte output slices: each slice restarts the
stream (a fresh bit buffer for LZW) but keeps the decoder state, and the next
slice is written at its fixed offset.

- **Method 5** ($454f2), byte LZ, until a byte `111xxxxx`:
  - `0ddddddd LLLmmlll`: `mm` literal bytes, then copy `lll+3` bytes from
    distance `(LLL<<7 | ddddddd) + 1`
  - `10dddddd LLLlllll hhhhhhmm`: `mm` literal bytes, then copy `lllll+3` bytes
    from distance `(hhhhhh<<9 | LLL<<6 | dddddd) + 1`
  - `110Snnnn`: literal run of `nnnn` bytes (S=1) or `(nnnn+1)*4` bytes (S=0)
- **Method 3** ($41b3c, $41baa, $422fc, $41abe): LZW with MSB-first 10-bit
  codes and 1024 entries.  New entries take the lowest free code; when none is
  left, the leaves (except the last code) are freed until at least $55 codes
  are free.

## TILE images ($3fbec)

Every pointer of a TILE library is an image (except the identifier table).
Image header: height, vertical offset, horizontal offset (words), width in
bytes, drawing method.  Low nibble of the method:

| Low nibble | Encoding |
| --- | --- |
| 0 | raw bitplanes, plane after plane |
| 1 | mask plane (0 = transparent), then the raw bitplanes |
| 2 | ByteRun1 rows: 4-bit chunky pixels when the high nibble is 0 ($44fca converts them), row-interleaved planes otherwise |
| 3 | delta ($43faa): words *command bytes*, *data bytes per plane*; the same commands run for every plane, each plane with its own data. Command `00n` skip, `01n` n masked bytes (masks in the command stream), `10n` fill n with one data byte, `11n` copy n data bytes, 0 ends the row. With no image of the same size before it, the delta is drawn over an empty frame and the bytes it touches become the mask |
| 8 | palette: 16 RGB triplets (32 with high nibble E), no image; the game keeps 4 bits per gun, `(v + 8) / 17` ($43cca) |

High nibble: 0 = 4 planes, E = 5 planes, any other value = 4 row-interleaved
planes (title pictures).  Images before the first palette of their file use
the first palette of `ALWAYS.TLB`, searched next to the file and in the sibling
disk folders.

## Palettes of the pictures

The game loads the whole palette entry of a picture library (by library and
index, $401ec; method 8 only) through $43cca, which rounds every gun to 4
bits. In Secret of the Silver Blades every palette (`ALWAYS.TLB`, `8X8D.TLB`,
`PIC.TLB`, `BIGPIC.TLB`...) keeps the same ten interface colours and changes
only a few slots per picture, so the big pictures of `BIGPIC.TLB` are drawn
mostly with the interface colours: their strong, dithered look (the cave
with the dragon and the well, the first two) is the game's own, not a wrong
palette. Each picture uses the palette that precedes it in its sub-library.

## Coverage

With the four games' disks, all 95 `.TLB`/`.GLB` files open as containers
(8408 entries).  The synthetic `testdata/AMIGA.TLB` (built by
`tools/make_testdata.py`) covers both compressions and drawing methods 1, 2,
3 and 8.
