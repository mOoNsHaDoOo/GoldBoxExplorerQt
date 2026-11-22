# Music of the Amiga Gold Box games

| Game | Music | Player |
| --- | --- | --- |
| Pool of Radiance | inside the executable `program` ("(c)Wally Beben 1988 music on the Amiga") | its own replay routine, not decoded yet |
| Curse of the Azure Bonds | none (`sounds.glb` holds the sound effects) | - |
| Secret of the Silver Blades | `secret.mx` (7 songs) | MX driver |
| Pools of Darkness | `pod.mx` (8 songs) | MX driver |
| Dark Queen of Krynn | `dqk.mx` (8 songs) | MX driver |

The `.SLB` files (`CURSE.SLB`, `SECRET.SLB`, `MUSIC.SLB`) are not loaded by
any Amiga executable (no file name, no `SLBR` check): they are left over from
the PC versions, one-voice note/duration tunes for the PC speaker. The viewer
says so and keeps its square-wave preview.

## MX driver

Reconstructed from the Dark Queen of Krynn executable (code hunk addresses);
Secret of the Silver Blades and Pools of Darkness link the same driver.
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
