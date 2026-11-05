# Gold Box Explorer — Qt port

A cross-platform C++17/Qt 6 port of [Gold Box Explorer](https://github.com/bsimser/Gold-Box-Explorer) done with DeepSeek Flash V4.1,
the Windows/.NET file explorer for SSI's classic *Gold Box* role-playing games
(Pool of Radiance, Curse of the Azure Bonds, Pools of Darkness, the Krynn
games, Unlimited Adventures/FRUA, and the Buck Rogers games).

The original application is C# WinForms targeting .NET Framework 3.5. This
port keeps the same plugin architecture and file-format logic, but renders
through Qt Widgets so it builds and runs on macOS, Linux, and Windows.

![Gold Box Explorer Qt](docs/screenshot.png)

## Status

The macOS build is the primary target and has been exercised with synthetic
DAX/TLB/GEO/ECL files. The same CMake project is portable to Linux and
Windows.

### Implemented

| Area | Notes |
| --- | --- |
| Main window | Directory tree, sortable file list, zoom menu/toolbar, Ctrl+wheel zoom, status bar, remembered root folder |
| Plugin framework | Same detection order and plugin-per-file design as the original |
| DAX images | VGA, EGA, VGA sprites, EGA sprites, VGA strata, VGA mixed (planar and EGA), 8×8 monochrome, unknown blocks |
| DAX wall sets | `WALLDEF*.DAX` assembly from `8x8d*.DAX`, including 8×8 tile loading and multi-wall-set layouts |
| TLB graphics | TILE and master libraries, drawing methods 16/17/18/21/23, internal/external colour tables, CBODY/CPIC combat icons |
| Image export | PNG/BMP/JPEG through the file context menu, same naming scheme as the original exporter |
| Generic viewers | Text, PNG/JPEG/GIF/BMP/TGA/TIFF (as supported by Qt), and a fast hex viewer |
| Amiga audio | 8SVX/16SV IFF audio with waveform preview, playback (afplay/aplay/paplay/ffplay/PowerShell), and WAV export |
| Amiga / DAA assets | `.DAA`, `.DAI`, `.IFF`, `.LBM`, `.ILBM`, `.ANIM`: auto-detects IFF ILBM/PBM images, IFF ANIM base frames, IFF 8SVX audio, and DAX image containers; otherwise shows chunk metadata, strings, and hex |
| Data viewers | FRUA characters (`.SAV`, `.ITM`, `.SPC`, `MON*.DAX`, `ITEM*.DAX`), item tables, `GAME001.DAT`, `SAVGAM*.CSV`, vault files |
| GEO (.DAT) | Module summary and grid dump |
| GEODAX (.DAX) | Per-map 2D renderer with rooms, walls, doors, and events |
| ECL (.DAX) | Per-block extracted strings and combined ECL text |

### Not ported yet

These are the main remaining differences from the original application:

- The full ECL bytecode disassembler/decompiler (`EclDump` in
  `Commands.cs`, `Program.cs`, `Memory.cs`, `Sys.cs`). The current ECL viewer
  exposes block strings and raw text rather than decoded instructions.
- The GeoDAX first-person 3D view, map navigation, and ECL cross-linking.
- The specialised “Export to Wall Template” form for wall/overlay images.
  The generic image export still exports the wall images.

Everything else in the original `src/Common` and `src/Explorer` projects has a
C++/Qt counterpart.

## Build

### macOS

```sh
brew install qt cmake ninja

cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/gbexplorer.app
```

If Homebrew installed only the `qtbase` formula, point CMake at that prefix
instead:

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase)"
```

### Linux

```sh
sudo apt install qt6-base-dev cmake ninja-build   # Debian/Ubuntu
cmake -S . -B build -G Ninja
cmake --build build
./build/gbexplorer
```

### Windows

Install Qt 6 (MSVC kit), CMake, and Visual Studio, then:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
cmake --build build --config Release
```

The target is built as a GUI application (`WIN32_EXECUTABLE`) on Windows and
as a `.app` bundle on macOS.

### Packaging the macOS app

For a standalone bundle that does not depend on Homebrew's Qt installation:

```sh
"$(brew --prefix qt)/bin/macdeployqt" build/gbexplorer.app
```

Use `$(brew --prefix qtbase)/bin/macdeployqt` if Qt was installed as
`qtbase` only.

## Using the application

1. Click the directory field in the toolbar (or use **File → Open Folder**) to
   choose the folder containing the game files.
2. Browse the tree on the left and select a file in the list; its viewer opens
   above the list.
3. Change zoom from **View → Zoom**, with the toolbar buttons, or with
   Ctrl/Cmd + mouse wheel.
4. Right-click a file (or use **File → Export**) to export decoded images as
   PNG, BMP, or JPEG.

The root folder is stored with `QSettings` under the `GoldBoxExplorer` /
`GoldBoxExplorerQt` organization/application names.

## Testing

Generate synthetic files (no copyrighted game data is included):

```sh
python3 tools/make_testdata.py
```

Probe a file without opening a window:

```sh
QT_QPA_PLATFORM=offscreen ./build/gbexplorer.app/Contents/MacOS/gbexplorer \
    --probe testdata/BIG.DAX
```

`--probe` prints the plugin selected and the decoded image sizes.

A window-level smoke test loads a file, processes events, and saves a
screenshot to `/tmp/gbexplorer_smoke.png`:

```sh
QT_QPA_PLATFORM=offscreen ./build/gbexplorer.app/Contents/MacOS/gbexplorer \
    --smoke testdata/BIG.DAX
```

Audio parsing and WAV conversion can be checked without a window:

```sh
QT_QPA_PLATFORM=offscreen ./build/gbexplorer.app/Contents/MacOS/gbexplorer \
    --audio testdata/TEST.8SVX
# writes /tmp/gbexplorer_audio.wav
```

On Linux/Windows use `./build/gbexplorer` (or the `.exe`) instead of the
bundle path.

## Project layout

```
src/
  MainWindow.*          Main browser window (tree, file list, zoom, export)
  ExportDialog.*        Image export dialog
  core/                 ByteReader, DAX container reader, game detection
  dax/                  Palette, render blocks, image file, wall sets
  tbl/                  TLB reader and image decoding
  parse/                Character/item/GEO/GEODAX/ECL parsers, IFF and 8SVX readers, name tables
  widgets/              Image sheet, hex, character, and audio views
  plugins/              Plugin interface, factory, and concrete plugins
tools/make_testdata.py  Synthetic file generator
testdata/               Generated smoke-test files
```

See [PORTING.md](PORTING.md) for the class-by-class mapping from the C#
original to this port and the behaviour differences.

## License

MIT. The original Gold Box Explorer is Copyright (c) 2018 Bil Simser; see
[LICENSE](LICENSE). Game file formats and trademarked game names belong to
their respective owners.
=======
# GoldBoxExplorerQt
Portale version of GoldBoxExplorer converted in C++ and QT with DeepSeek Flash
>>>>>>> c3f3104ed124af511015f491cc95594afb3115c4
