#!/usr/bin/env python3
"""Generate src/parse/record_layouts_gen.cpp: the field layouts of the
Death Knights of Krynn and Pool of Radiance (Amiga) records, taken from the
C ports of the two executables.

Death Knights of Krynn (repository DeathKnightsRemakeC, directory src):
  records.c       s_character_fields / s_item_fields / s_effect_fields
  character.h     "+$NN: description" comments of the record structs
  state_blocks.c  s_fields_a / s_fields_b / s_fields_c (saved game blocks)
  h004_image.c    s_fields (the globals that globals.daa initialises)
Pool of Radiance (repository PoolOfRadiance, directory src):
  include/game.h  OFS_CH_* / OFS_IT_* / OFS_AF_* and their accessors

Usage: tools/gen_record_layouts.py DKK_SRC POR_SRC > src/parse/record_layouts_gen.cpp
"""

import os
import re
import sys


def read(path):
    with open(path, errors="replace") as handle:
        return handle.read()


def struct_comments(text, struct):
    """member -> description from 'type member[..]; /* +$NN: text */' (the
    comment may continue on the next lines)."""
    match = re.search(r"typedef struct %s \{(.*?)\} %s;" % (struct, struct), text, re.S)
    out = {}
    if not match:
        return out
    lines = match.group(1).split("\n")
    i = 0
    while i < len(lines):
        m = re.match(r"\s*[\w\s\*]+?\b(\w+)(?:\[[^\]]*\])*;\s*/\*\s*\+\$[0-9A-Fa-f]+:?\s*(.*)", lines[i])
        if m:
            member, comment = m.group(1), m.group(2)
            while "*/" not in comment and i + 1 < len(lines):
                i += 1
                comment += " " + lines[i].strip().lstrip("*").strip()
            out[member] = " ".join(comment.split("*/")[0].split())
        i += 1
    return out


def dkk_records(src):
    text = read(os.path.join(src, "records.c"))
    header = read(os.path.join(src, "character.h"))
    layouts = {}
    for table, struct in (("s_character_fields", "Character"), ("s_item_fields", "Item"),
                          ("s_effect_fields", "Effect")):
        body = re.search(r"static const RecField %s\[\] = \{(.*?)\n\};" % table, text, re.S).group(1)
        comments = struct_comments(header, struct)
        fields = []
        for m in re.finditer(r"\{ 0x([0-9A-Fa-f]+), (\d), (\d+), (\d+), offsetof\(\w+, (\w+)\)"
                             r"(?: \+ offsetof\(\w+, (\w+)\))?", body):
            member = m.group(5)
            name = member + ("." + m.group(6) if m.group(6) else "")
            fields.append((int(m.group(1), 16), int(m.group(2)), int(m.group(3)), int(m.group(4)),
                           False, name, comments.get(member, "")))
        layouts[struct] = fields
    return layouts


def dkk_states(src):
    text = read(os.path.join(src, "state_blocks.c"))
    out = {}
    for block in "abc":
        body = re.search(r"static const StateField s_fields_%s\[\] = \{(.*?)\n\};" % block, text, re.S).group(1)
        fields = []
        for m in re.finditer(r"F[AB]\(0x([0-9A-Fa-f]+), (\d), (0x[0-9A-Fa-f]+|\d+), (\w+)\)", body):
            width = int(m.group(2))
            fields.append((int(m.group(1), 16), width, int(m.group(3), 0), width, False, m.group(4), ""))
        for m in re.finditer(r"\{ 0x([0-9A-Fa-f]+), (\d), (0x[0-9A-Fa-f]+|\d+), \(u16\)offsetof\(\w+, (\w+)\) \}", body):
            width = int(m.group(2))
            fields.append((int(m.group(1), 16), width, int(m.group(3), 0), width, False, m.group(4), ""))
        out[block.upper()] = sorted(fields)
    return out


def dkk_globals(src):
    text = read(os.path.join(src, "h004_image.c"))
    body = re.search(r"static const H004Field s_fields\[\] = \{(.*?)\n\};", text, re.S).group(1)
    fields = []
    for m in re.finditer(r"\{ 0x([0-9A-Fa-f]+), (\d), (\d+), 0x([0-9A-Fa-f]+), &([^,]+),", body):
        name = m.group(5).strip().replace("[0]", "[]")
        fields.append((int(m.group(1), 16), int(m.group(2)), int(m.group(3)), int(m.group(4), 16),
                       False, name, ""))
    return sorted(fields)


TYPES = {"char": (1, True), "BYTE": (1, True), "UBYTE": (1, False), "WORD": (2, True),
         "UWORD": (2, False), "LONG": (4, True), "ULONG": (4, False)}


def por_struct(text, prefix, size):
    entries = []
    pattern = re.compile(r"#define OFS_%s_(\w+)\s+0x([0-9A-Fa-f]+)\s*\n#define %s_\w+\((b)(, i)?\)\s+"
                         r"\(\*\((\w+) \*\)" % (prefix, prefix))
    for m in pattern.finditer(text):
        width, signed = TYPES.get(m.group(5), (1, False))
        entries.append([int(m.group(2), 16), width, 1, width, signed, m.group(1).rstrip("_"), bool(m.group(4))])
    entries.sort()
    for i, e in enumerate(entries):
        if e[6]:
            end = entries[i + 1][0] if i + 1 < len(entries) else size
            e[2] = max(1, (end - e[0]) // e[1])
    return [tuple(e[:6]) + ("",) for e in entries]


def cstr(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit(name, fields):
    lines = ["const RecordField %s[] = {" % name]
    for off, width, count, step, signed, field, desc in fields:
        lines.append("    {0x%X, %d, %d, %d, %s, %s, %s}," % (off, width, count, step,
                                                          "true" if signed else "false",
                                                          cstr(field), cstr(desc)))
    lines.append("};")
    return "\n".join(lines)


def main():
    dkk, por = sys.argv[1], sys.argv[2]
    records = dkk_records(dkk)
    states = dkk_states(dkk)
    globals_ = dkk_globals(dkk)
    game_h = read(os.path.join(por, "include", "game.h"))
    layouts = [
        ("dkk.character", "Death Knights of Krynn character (.gam/.pch)", 0xE8, "kDkkCharacter", records["Character"]),
        ("dkk.item", "Death Knights of Krynn item (.jnk)", 0x48, "kDkkItem", records["Item"]),
        ("dkk.effect", "Death Knights of Krynn effect (.eff)", 0x0A, "kDkkEffect", records["Effect"]),
        ("dkk.stateA", "Death Knights of Krynn game state A", 0x400, "kDkkStateA", states["A"]),
        ("dkk.stateB", "Death Knights of Krynn game state B", 0x400, "kDkkStateB", states["B"]),
        ("dkk.stateC", "Death Knights of Krynn game state C", 0x200, "kDkkStateC", states["C"]),
        ("dkk.globals", "Death Knights of Krynn globals (H004 image)", 0x7FEA, "kDkkGlobals", globals_),
        ("por.character", "Pool of Radiance character (.cha/.sav)", 0x120, "kPorCharacter",
         por_struct(game_h, "CH", 0x120)),
        ("por.item", "Pool of Radiance item (.itm)", 0x41, "kPorItem", por_struct(game_h, "IT", 0x41)),
        ("por.affect", "Pool of Radiance affect (.spc)", 0x0A, "kPorAffect", por_struct(game_h, "AF", 0x0A)),
    ]
    out = ["// Generated by tools/gen_record_layouts.py from the Death Knights of Krynn",
           "// and Pool of Radiance C ports. Do not edit.",
           '#include "parse/record_layouts.h"', "", "namespace RecordLayouts {", "", "namespace {", ""]
    for _, _, _, var, fields in layouts:
        out.append(emit(var, fields))
        out.append("")
    out.append("} // namespace")
    out.append("")
    out.append("const RecordLayout layouts[] = {")
    for key, title, size, var, fields in layouts:
        out.append("    {%s, %s, 0x%X, %s, %d}," % (cstr(key), cstr(title), size, var, len(fields)))
    out.append("};")
    out.append("const int layoutCount = %d;" % len(layouts))
    out.append("")
    out.append("} // namespace RecordLayouts")
    print("\n".join(out))


if __name__ == "__main__":
    main()
