# Champions of Krynn (Amiga) coverage

Folder checked: an installed Amiga *Champions of Krynn* hard-disk directory
(96 files including `Sound/`, `save/`, `c/`, `l/`, `s/`; the game executables
`cok*`, `game`, `game_prot` and the emulator `*.uaem` side files are not
counted). Format specifications come from the readable C port of the Amiga
executable (`h028_files.c` DAX/DAA loader, `h029_gfx.c` graphic records and
pointers, `h042_seq.c` picture sequences, `h066.c` monster/save records,
`h006.c` item types).

Legend: **full** = content decoded and labelled; **partial** = structure shown
but some fields unknown or misread; **summary** = header/strings only;
**hex** = hex dump only. "+list" = the file now opens in the block browser
(block table, per-block viewer, Info/Hex tabs, export).

## Summary

| | Before (0.56) | After |
| --- | --- | --- |
| Game data files fully viewable | 45 / 83 | 82 / 83 |
| Game data files partial / misread | 29 | 1 (`savgamA.dat`: state C unnamed) |
| Game data files summary only | 9 | 0 |
| AmigaOS system files (icons, executables, `.info` lists; 13) | hex | icons decoded, executables listed, 2 `.info` lists still hex |
| Containers with a block list | 0 | 36 (15 `.dax`, 21 `.daa`), 594 blocks, every block opens in a specific viewer |

## Table

| File(s) | Plugin / viewer | Before | After | Still missing |
| --- | --- | --- | --- | --- |
| `ecl1.dax`, `ecl2.dax` | Container → `EclPlugin` | full (all blocks) | full +list; each block opens alone in the ECL viewer | — |
| `geo1.dax`, `geo2.dax` | Container → `GeoDaxPlugin` | full (2D map, first person) | full +list; each map opens alone (companion WALLDEF/ECL found through the folder mirror) | — |
| `walldef1.dax`, `walldef2.dax` | Container → `DaxWallDefPlugin` | full | full +list | walls are textured with the PC-style `8x8d1.dax`; the Amiga game uses the 4-plane `8x8d<n>.daa` sets (`h034.c`) |
| `8x8d1.dax` | Container → `DaxImagePlugin` | full (EGA 8×8 tiles) | full +list | — |
| `8x8d0.daa`, `8x8d1.daa`, `8x8d2.daa` | Container → `DaaPlugin` | misread (tiles decoded as chunky 4bpp: noise) | full +list: 4-bitplane strips of 8×8 tiles, game palette (`set_game_palette`) | block 201 is all zero (shown as "Empty block") |
| `bigpic1/2`, `body1/2`, `head1/2`, `cpic1/2`, `comspr`, `border`, `sky` `.daa` | Container → `DaaPlugin` | full | full +list | transparency colour (mask) not applied |
| `dungcom.daa`, `wildcom.daa`, `randcom.daa` | Container → `DaaPlugin` | summary | full +list: strips of 25/35/6 combat tiles 24×24 (`width8` frames of `count*8` pixels) | — |
| `pic1.daa`, `pic2.daa`, `sprit1.daa`, `sprit2.daa` | Container → `DaaPlugin` | summary (index misread as 6-byte records with a LE header) | full +list: `Load_Sequence` layout, ByteRun1 header and frames, frames XOR-ed with frame 1, durations | random mirroring of the game not shown |
| `pointers.daa` | `DaaPlugin` | summary | full: 12 pointer shapes with hot spots | pointer colours are not in the file (placeholders) |
| `charS.LBM`, `charT.LBM`, `gothic.lbm`, `scrn1b.lbm`, `scrn2.lbm`, `text1.lbm`, `text2.lbm` | `DaaPlugin` (IFF ILBM) | full | full | — |
| `item1.dax`, `item2.dax`, `mon1itm.dax`, `mon2itm.dax` | Container → `DaxRecordPlugin` | partial (Pool of Radiance layout and name table) | full +list: $3F-byte item records converted like `Load_MonsterRecord`, CoK name words (H005+$406E) | — |
| `mon1cha.dax`, `mon2cha.dax` | Container → `DaxRecordPlugin` | misread (PoR character layout) | full +list: $199-byte PC monster record → $1AA character record (moves, sign extension, swaps), 127 named fields | — |
| `mon1spc.dax`, `mon2spc.dax` | Container → `DaxRecordPlugin` | partial | full +list: 9-byte effects (type, duration, parameter, flag) | effect type names not mapped to text |
| `items` | `AmigaItemsPlugin` | misread (256 × 8-byte "templates") | full: 2-byte header + 128 × 16-byte item types (`load_items_table`), named columns, type names | 5 bytes per record unused by the port (shown, marked "?") |
| `save/CHRDATA*.sav` (6) | `SaveFilePlugin` | partial (PoR layout) | full: $1AA in-memory character record, named fields (PoR view kept as 2nd tab) | — |
| `save/CHRDATA*.swg` (6) | `SaveFilePlugin` | partial (raw 72-byte rows) | full: $48-byte item records, names, bonus, weight, value | — |
| `save/CHRDATA*.fx` (5) | `SaveFilePlugin` | partial (PoR effect names) | full fields | effect type names (see SPC) |
| `save/savgamA.dat` | `SaveFilePlugin` | summary (strings) | partial: `save_game` layout (disk, state A/B/C, position, mode, wall sets, party files), named A/B fields | state C ($400 bytes) has no named fields in the port |
| `Sound/*` (18: 8SVX samples, `cast3`/`cast4` INST, `krynn` SMUS, `soundData` bank, `periodTable`) | `AudioPlugin`, `IffPlugin`, `SoundBankPlugin`, `PeriodTablePlugin` | full | full | — |
| `s/startup-sequence`, `s/QUARTEX_RULES!` | `TextPlugin` | full | full | — |
| `Disk.info`, `game.info`, `hdinstall.info`, `emptydirectory.info` | `AmigaSystemPlugin` | hex | full: Workbench DiskObject icon images, type, tool types | real Workbench palette unknown (WB 1.x pens used) |
| `.info`, `EmptyDirectory/.info` | `HexPlugin` | hex | hex | magic $F34C list of icon names; not documented in the port, left undecoded |
| `c/assign`, `c/endcli`, `c/run`, `c/stack`, `l/disk-validator`, `hdinstall`, `s/type` | `AmigaSystemPlugin` | hex | hunk list (CODE/DATA/BSS sizes, relocations) | no 68000 disassembly (system tools, not game data) |

## Container browser

`--list FILE` prints the same block table as the GUI, for example:

```
$ gbexplorer --list pic1.daa
format: Amiga DAA picture sequences (6-byte directory, ByteRun1)
Directory: 164 bytes, 27 sequences
   #    id   offset  stored    size  type
   0    57      164    4522    4522  Sequence 88x88, 1 frame(s)
   1    14     4686   15445   15445  Sequence 88x88, 5 frame(s)
```

`--probe-block FILE ROW` opens one block the way the browser does and fails
if only the hex fallback is available; running it over all 594 blocks of the
36 CoK containers found a specific viewer for every block.
