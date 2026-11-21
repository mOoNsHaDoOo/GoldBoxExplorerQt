#pragma once

// Field names of the Champions of Krynn (Amiga) character, item and effect
// records, generated from the comments of the readable C port
// (tools/gen_krynn_fields.py). Offsets are those of the in-memory records,
// which the .sav/.swg/.fx save files store verbatim (big-endian).
namespace KrynnFields {

// StateA/B/C: the $800/$800/$400-byte game state blocks of savgam*.dat.
enum class Kind { Character, Item, Effect, StateA, StateB, StateC };

struct Field
{
    Kind kind;
    int offset;
    int size;          // 1, 2 or 4 bytes (big-endian); larger = text
    const char *name;
};

extern const Field fields[];
extern const int fieldCount;

// Name tables copied into H005 by H004 (index 0 = first entry).
extern const char *const itemWords[];   // H005+$406E, item name words 1..n
extern const int itemWordsCount;
extern const char *const races[];       // H005+$4D04
extern const int racesCount;
extern const char *const classes[];     // H005+$4D84
extern const int classesCount;
extern const char *const alignments[];  // H005+$4C62
extern const int alignmentsCount;

} // namespace KrynnFields
