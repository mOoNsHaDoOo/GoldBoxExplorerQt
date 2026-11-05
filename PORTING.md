# Porting notes: C# WinForms → C++17/Qt 6

This document maps the original classes to the Qt port and records the
deliberate behaviour changes.

## Architecture mapping

| Original (C#) | Port (C++) |
| --- | --- |
| `Explorer/Program.cs` | `src/main.cpp` |
| `Explorer/MainForm.cs` + `MainForm.Designer.cs` | `src/MainWindow.h/.cpp` |
| `Explorer/MainViewPresenter.cs` | `src/MainWindow.cpp` (the presenter is folded into the window because the Qt widget API already separates selection, viewing, and zoom cleanly) |
| `Explorer/ExportForm.cs` | `src/ExportDialog.h/.cpp` |
| `Explorer/FileDto.cs` | `fileType()` / `formatSize()` / file table population in `MainWindow.cpp` |
| `Common/Plugins/IPlugin.cs` | `src/plugins/plugin.h` |
| `Common/Plugins/PluginFactory.cs` | `src/plugins/pluginfactory.cpp` |
| `Common/Plugins/PluginParameter.cs` | `PluginParameters` in `plugin.h` |
| `Common/GoldBoxFile.cs` | replaced by direct `QByteArray` reads in each parser |
| `Common/Frua/ByteHelper.cs` and `StringExtensions.cs` | `src/core/byteutils.h` |
| `Plugins/FileHelper.cs` | `src/core/game.h/.cpp` (`GameDetector`) |

### Plugins and viewers

| Original plugin | Port class | Viewer |
| --- | --- | --- |
| `DaxImagePlugin` | `DaxImagePlugin` | `ImageSheetView` |
| `DaxWallDefPlugin` | `DaxWallDefPlugin` | tabbed `ImageSheetView`s |
| `FruaTblPlugin` | `TlbPlugin` | `ImageSheetView` |
| `TextFilePlugin` | `TextPlugin` | `QPlainTextEdit` |
| `ImagePlugin` | `ImagePlugin` | `QScrollArea` + `QLabel` |
| `HexPlugin` (default) | `HexPlugin` | `HexView` |
| `FruaCharacterPlugin` | `CharacterPlugin` | `CharacterView` |
| `FruaItemPlugin` | `ItemsPlugin` | `QTableWidget` |
| `FruaCharacterPlugin` (DAX item/effect records) | `DaxRecordPlugin` | `CharacterView` |
| `FruaGameDataPlugin` | `GameDataPlugin` | `QPlainTextEdit` |
| `FruaSavedGamePlugin` | `SavedGamePlugin` | `QPlainTextEdit` |
| `FruaVaultPlugin` (disabled upstream) | `VaultPlugin` | `QPlainTextEdit` |
| `FruaGlbPlugin` | `GlbPlugin` | `HexView` (same fallback as upstream) |
| `FruaGeoPlugin` | `GeoPlugin` | tabbed summary/map text |
| `GeoDaxPlugin` | `GeoDaxPlugin` | 2D `GeoMapWidget` tabs |
| `DaxEclPlugin` | `EclPlugin` | per-block strings and combined text |
| *(new)* Amiga IFF/8SVX | `AudioPlugin`, `DaaPlugin` | `AudioView`, `ImageSheetView`, summary/hex tabs |
| *(new)* IFF instruments/music | `IffPlugin`, `SoundBankPlugin` | summary + referenced audio tabs |
| *(new)* Amiga save files | `SaveFilePlugin` | character/equipment/effect/save views |
| *(new)* Amiga item table | `AmigaItemsPlugin` | byte-template table |

### File-format code

| Original | Port |
| --- | --- |
| `Plugins/Dax/DaxFile.cs` | `src/core/daxfile.cpp` |
| `Plugins/Dax/DaxFileHeaderEntry.cs`, `DaxFileBlock.cs` | `DaxFileBlock` in `daxfile.h` |
| `Plugins/Dax/EgaVgaPalette.cs` | `src/dax/palette.h/.cpp` |
| `Plugins/Dax/*Block*.cs` | `src/dax/renderblocks.h/.cpp` |
| `Plugins/Dax/DaxImageFile.cs` | `src/dax/daximagefile.cpp` |
| `Plugins/Dax/DaxWallDefFile.cs` | `src/dax/walldef.cpp` |
| `Plugins/Tbl/FruaTlbFile.cs` | `src/tbl/tlbfile.cpp` |
| `Plugins/Character/FruaCharacter.cs` | `src/parse/character.cpp` |
| `Plugins/Character` name tables | generated `src/parse/character_tables.*` |
| `Plugins/Items/FruaItem.cs` name parts | generated `src/parse/item_names.*` |
| `Plugins/GeoDax/GeoDaxFile.cs` map names | generated `src/parse/geodax_names.*` |
| `Plugins/Geo/FruaGeoFile.cs` | `src/parse/geo.cpp` |
| `Plugins/GeoDax/GeoDaxFile.cs` (map records) | `src/parse/geodax.cpp` |
| `Plugins/DaxEcl/DaxEclFile.cs` string extraction | `src/parse/ecl.cpp` |
| *(new)* Amiga IFF reader | `src/parse/iff.cpp` |
| *(new)* 8SVX/16SV reader and WAV writer | `src/parse/audio.cpp` |
| *(new)* Amiga DAA container and image decoder | `src/parse/daa.cpp` |
| *(new)* Amiga DAX container and ByteKiller depacker | `src/core/daxfile.cpp` (Amiga branch) |
| *(new)* SMUS parser, sound-bank resolver, wavetable synthesis | `src/parse/smus.cpp` |

## Deliberate behaviour changes

- **Rendering backend.** GDI+ `Bitmap` + `SetPixel` became `QImage`
  (`Format_ARGB32`) + `setPixel`. Images are drawn with nearest-neighbour
  scaling so the pixel art stays crisp.
- **Colour values.** The original's `uint` ARGB constants are represented as
  `QRgb` values. `EGAtransparentColor` (0xFF67F79F) remains an opaque colour,
  exactly as in the original, rather than an alpha-transparent pixel.
- **Unsafe indexing.** Several C# paths index the byte array without bounds
  checks. The port returns zeroes for out-of-range reads and skips
  out-of-range writes so malformed files do not crash the viewer.
- **File paths.** `WALLDEF` → `8x8d` replacement and the external
  `DUNGCOM.TLB` lookup resolve file names case-insensitively. This is required
  on macOS/Linux and harmless on Windows.
- **Text detection.** The original round-trips the file through
  `StreamReader`/`StreamWriter` to decide whether it is text. The port accepts
  UTF-8/UTF-16/UTF-32 with a BOM, rejects files containing NUL bytes, and
  otherwise requires at least 95% printable characters. This also makes
  legacy DOS/ASCII text files display reliably.
- **Character viewer.** The original paints one row per annotation inside a
  panel; the port uses a two-column table for faster scrolling and easier
  copying, while keeping the same annotation order, values, and item-template
  name row.
- **Vault viewer.** `FruaVaultPlugin` is commented out in the upstream plugin
  factory. The parser here is bounds-safe, so the port enables it.
- **Item/effect DAX records.** The upstream character plugin only enables
  `ITEM*.DAX`/`MON*ITM.DAX` handling inside a Pool of Radiance directory. The
  port enables the same 63-byte item records and 9-byte effect records for the
  Krynn games as `DaxRecordPlugin`. `MON*CHA.DAX` (409-byte Amiga character
  blocks) is shown with the Pool of Radiance annotations as a best effort.
- **IFF instrument files.** The original viewer only understood 8SVX/ILBM.
  The port adds a generic IFF inspector that decodes `FORM INST` references
  (PTCH/VLUM/SAMP), resolves referenced samples in the same directory, and
  shows `FORM SMUS` scores and embedded sound banks.
- **Amiga DAX.** The DOS and Amiga `.dax` formats share an extension but not
  a layout. The port auto-detects the Amiga big-endian index and unpacks its
  ByteKiller blocks, then renders the 9-byte-header 4-plane images. An
  8-byte prefix before the image header is handled for `pic.dax`,
  `sprit.dax`, and `final.dax`.
- **SMUS playback.** The port parses SHDR/INS1/TRAK, resolves INS1 register
  names (for example `in1`) to INST forms in `soundData`, resolves their SAMP
  chunks to 8SVX samples, and renders the note events through those samples
  as a looping wavetable with attack/release envelopes. This is a practical
  preview, not a Paula hardware emulation.
- **Curse of the Azure Bonds / Secret of the Silver Blades.** Both Amiga
  conversions package graphics in big-endian `GLIB` libraries. The TLB reader
  detects the byte order from the header file size and decodes the Amiga
  planar methods 0 (4 planes) and 1 (5 planes) using the embedded colour
  table. DOS/little-endian TLB files keep the original drawing-method path.
- **DKK**. The Death Knights of Krynn set uses the same Amiga DAX/DAA and SMUS
  readers. `MON1WIZ.DAX` is exposed as a decoded-block table; `PIC1.DAA`
  (alternate DAA index) and the multi-frame combat DAA files are structurally
  indexed but their pixel packing is still open.
- **DAA/IFF previews.** The original application has no Amiga asset support.
  The native DAA container is read with a big-endian index and the same SSI
  run-length decoder as DAX; image records use a 9-byte header, a 64-byte
  Amiga 12-bit palette, and 5-plane planar data. 8×8D blocks are 4bpp tiles.
  The viewer also tries IFF ILBM/PBM/ANIM and 8SVX, then a DAX image container,
  and finally falls back to a chunk/string summary plus hex. Multi-frame
  combat DAA files and the alternate `pic*.daa`/`SPRIT*.DAA` layouts are not
  decoded yet and remain summary/hex only.
- **Audio playback.** Qt Multimedia is not required. The port writes a
  temporary PCM WAV and plays it with the platform player (`afplay` on macOS,
  `aplay`/`paplay`/`ffplay` on Linux, PowerShell `SoundPlayer` on Windows).
  `Save WAV...` always works without an external player.
- **Error handling.** The original shows a custom unhandled-exception dialog.
  The port avoids throwing for malformed files and shows inline messages or
  empty viewers instead.

## Remaining work

The largest missing subsystem is the ECL disassembler. Upstream splits it
across `DaxEcl/Commands.cs`, `Program.cs`, `Memory.cs`, and `Sys.cs`, and it
produces the annotated per-instruction listing shown by `EclFileViewer`. A
direct port is feasible with the existing `EclFile` block access, but is not
part of this first Qt milestone.

The GeoDAX 3D first-person renderer and its ECL cross-navigation are also
left for a later pass; the 2D map and map naming are implemented.
