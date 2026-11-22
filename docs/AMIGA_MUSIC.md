# Music of the Amiga Gold Box games

| Game | Music | Player |
| --- | --- | --- |
| Pool of Radiance | inside the executable `program` ("(c)Wally Beben 1988 music on the Amiga"), 6 songs | Wally Beben's replay routine, run from the original code |
| Curse of the Azure Bonds | `CURSE.SLB` (3 songs) | SLB sequencer and synth |
| Secret of the Silver Blades | `secret.mx` (7 songs); `SECRET.SLB` (1 song) when the MX music is not loaded | MX driver / SLB |
| Pools of Darkness | `pod.mx` (8 songs) | MX driver (the SLB code is linked, no `.SLB` file on the disks) |
| Dark Queen of Krynn | `dqk.mx` (8 songs); `MUSIC.SLB` (8 songs) when the MX music is not loaded | MX driver / SLB |

Dark Queen of Krynn loads `dqk.mx` and plays it when that worked, else it
loads `music.slb` (code $36456). Secret of the Silver Blades does the same
(code $31d76, $31df4): it loads `secret.mx` when the MX driver starts
($4ac0a) and a 57 KB block is free (AvailMem, $45716), else `secret.slb`.
Pools of Darkness links the same SLB code but its disks have no `.SLB`.
`sounds.glb` holds the sound effects.

## Pool of Radiance: Wally Beben's player

Hunk 27 of `program` (code, 33568 bytes, "(c)Wally Beben" at $7C92) holds the
player, hunk 28 its data; the two known versions of `program` have the same
player. The explorer loads the executable with its relocations into the
memory of a small 68000 interpreter (`src/core/m68k.*`, ported from the
interpreter of the Pool of Radiance C port) and runs the original code:
$78A0 with the song number in d0 (0 = stop), then $78C2 once per vertical
blank (50 Hz), the way the game's interrupt server does. The writes to the
Paula registers (`DMACON`, `AUDxLC/LEN/PER/VOL`) drive an emulated Paula
(`src/core/paula.*`): DMA starts a channel at its latched location, the
channel restarts from the latch at the end of a block (sample loops),
channels 0/3 left and 1/2 right, PAL clock. The register writes are the same
as with the interpreter of the C port.

Songs (game.h of the port): 1 title and camp, 2 combat, 3 character creation,
5 game over; 4 and 6 are not started by the game. Songs 2, 3 and 5 end (the
player clears its playing flag at H28+$17D5), 1, 4 and 6 loop: a track byte
$FF restarts the track (the `clr.b $C(a0)` at $7976); the rendering stops
when every voice has looped once, then fades out.

Implementation: `src/parse/exemusic.*`, Music tab of the executable viewer
(`src/plugins/amigaplugins.cpp`), CLI `--exe-music FILE [SONG OUT.wav]`.

## SLB song banks

Reconstructed from the Curse of the Azure Bonds executable (code hunk
addresses), checked by running the original sequencers of Curse, Secret of
the Silver Blades and Dark Queen of Krynn in the 68000 interpreter: same
Paula output for every song.
Implementation: `src/parse/slb.*`, viewer `src/plugins/slbplugin.cpp`, CLI
`--slb FILE [SONG OUT.wav]`.

### File (loader $3aa54, callback $3a9b6)

```
"SLBR" long file size, byte songs, byte 0
word offsets[songs + 1]          (relative to the end of this table)
per song:
  word tempo, byte volume, byte tracks (up to 5)
  word track offsets[5]          (relative to the end of this 14-byte header)
  tracks of 2-byte events [byte][data]
```

| Byte | Meaning |
| --- | --- |
| < $80 | note (MIDI number, 60 = C4), data = length; data bit 6 ties it to the next note (no new attack) |
| $80 | rest, data = length |
| $81 | voice off; data = instrument (stored by $42496, not used by the synth) |
| $84 | volume = data |
| $FF | end of the track |
| others | $82, $83, $87, $88 are skipped |

Dark Queen of Krynn's sequencer ($45ba4) also skips notes and rests whose
data has bit 7 set (only its `MUSIC.SLB` has some).

Secret of the Silver Blades' song start ($3ec6e) gives voice n the track
n + 1 and starts tracks - 1 voices: the first track of `SECRET.SLB` is not
played (it starts with the same notes as the second, at volume $40). The viewer
marks it "not played"; the variant is chosen by the file name.

### Timing

Length ($3a962): whole note $6900 >> (data & 7); bit 3 dotted (x 1.5); bits
4-5 = n (2, 4, 6 after the shift) take n / (n + 1) of it. The result is divided
by the tempo factor `105 * tempo / 24000` ($3a93a) with the remainder carried
to the next event, giving time units of 5 ms: the "Music.task" (timer.device,
$3ff8e) wakes up every 10 ms and calls the sequencer ($3ac12) with the time
since its start in 5 ms units. A voice plays the events due; a note followed
by the same note ends one unit early (a short gap); when every track has
ended the song starts again.

### Synth ($425f8, $424b8)

Period = table[note % 12] (760, 717, 677, 639, 603, 569, 537, 507, 479, 452,
427, 403 for octave 5), shifted left or right by the octave difference, at
most two octaves. The voice plays a sawtooth cycle: 18 samples below period
375, 54 samples (period / 3) below 750, else 108 samples (period / 6), so the
pitch is 3546895 / (18 * period). Paula volume = volume / 8 + 3. A note off
sets the volume to 0 and stops the channel's DMA.

## MX driver

Reconstructed from the Dark Queen of Krynn executable (code hunk addresses).
Secret of the Silver Blades ($4a1a6-$4c400) and Pools of Darkness link an
earlier build of the same driver. Compared instruction by instruction with
Dark Queen's, the differences that touch the music files are:

* note length: Dark Queen ($52fee) shortens a note by the time its event was
  served late (event time - current time), Secret and Pools start counting
  from the frame of the note on. The player follows the game (by file name:
  `secret.mx`, `pod.mx`);
* voice choice: Dark Queen does not prefer the channel's voice pair for
  channels 8-15, Secret always does (the files use channels 0-4 only);
* controller handling (volume flags, mono/poly), not used by `secret.mx`
  (notes, one tempo and the end only);
* Secret waits a raster line before restarting Paula DMA on Kickstart 1.x,
  Dark Queen checks the beam position in its interrupt: hardware timing only.
Implementation: `src/parse/mxmusic.*`, viewer `src/plugins/mxtxplugin.cpp`,
CLI `--mx-render FILE SONG OUT.wav`.

### File ($53a5e)

```
"MXTX" word flags word flags2 (bit 1: velocity sensitive, bit 15: 256 bytes follow)
word songs, per song: long events, events * 6 bytes
word instruments, per instrument:
  word id, word tune (signed, 1/24 semitone), word sustain level,
  word samples, long one-shot length, long loop length,
  word attack entries, word release entries
  attack entries, release entries: word time (ms), word level (0..$8000)
  samples: one-shot part then loop part; each further sample is twice as
  long (the same sound one octave lower)
```

### Sequencer ($52e84, every vertical blank)

Position += BPM * $C000 / (Hz * 60), in ticks << 8: 192 ticks per quarter
note. Events `[status][data][delta ticks word][word]`:

| Status | Meaning |
| --- | --- |
| < $80 | note: velocity (data >> 1) & $78 (0 = ignored), channel data & $F, length = word ticks; the note off is scheduled per voice |
| $80 | tempo = data BPM, ramp over the word ticks when not 0 |
| $B0 | controller b4 = b5: 6 bend range, 7 volume, 10 pan, $40 sustain, $41 portamento, $78/$79/$7B/$7E/$7F |
| $C0 | program change (channel instrument) |
| $E0 | pitch bend |
| $A0 | 1 signal to the game, 2 repeat start (count), 3 repeat end |
| $FF | end of song (or loop) |

Channel n plays instrument n (the files have no program changes).

### Voices ($51c80-$52130)

* Four Paula voices; a note takes the voice with the lowest state (free,
  then releasing), the channel's pair first (0/3 left, 1/2 right).
* Pitch: `log = $9FD77 - ((note << 8) + (tune << 8) / 24 + bend - $2D00) * 4/3 * 16`,
  period = 2^(log / $10000); while the period is above about 509 and a longer
  sample exists, the next sample is taken and the period halved. A 32-byte
  cycle sounds at the written note.
* Envelope state machine: attack/decay table (time ms, level), sustain,
  release table after the note off, then the voice stops. Levels change
  linearly per frame.
* Volume = 64 * velocity * envelope * channel volume (each /128), at most 64.
* The one-shot part is played once and the loop part repeated
  (audio.device requests with 1 and 0 cycles).
